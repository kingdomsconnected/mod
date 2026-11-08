#include "audio_range.h"

#include "sdk/cryengine/system/i_xml_node.h"
#include "sdk/fmod/fmod_api.h"
#include "sdk/wh/c_wh_fmod_studio_wrapper.h"
#include "sdk/wh/fsw_event_description.h"

#include <logging/logger.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace KCDC::Features::Audio {
    namespace {
        constexpr const char *kLogger = "Audio";

        constexpr double kNearFraction  = 0.05;
        constexpr double kTaperFraction = 0.25;

        struct RangedEvent {
            SDK::XmlNodeRef event;
            float range;
        };

        /** The `<FmodEvent>` of every trigger in an ATL control document that `ranges` names, with its range. */
        std::vector<RangedEvent> FindRangedEvents(const SDK::XmlNodeRef &document, std::span<const AudioRange::TriggerRange> ranges) {
            std::vector<RangedEvent> found;
            SDK::XmlNodeRef triggers = document->findChild("AudioTriggers");
            if (!triggers.IsValid()) {
                return found;
            }
            for (int i = 0; i < triggers->getChildCount(); ++i) {
                SDK::XmlNodeRef trigger = triggers->getChild(i);
                if (!trigger.IsValid() || !trigger->isTag("ATLTrigger")) {
                    continue;
                }
                // The ATL keys a control by the CRC of its lowercased name.
                const char *name = trigger->getAttr("atl_name");
                for (const AudioRange::TriggerRange &range : ranges) {
                    if (_stricmp(name, range.trigger) != 0) {
                        continue;
                    }
                    SDK::XmlNodeRef event = trigger->findChild("FmodEvent");
                    if (event.IsValid() && event->haveAttr("fmod_name")) {
                        found.push_back(RangedEvent {event, range.range});
                    }
                }
            }
            return found;
        }
    } // namespace

    AudioRange &AudioRange::Get() {
        static AudioRange instance;
        return instance;
    }

    bool AudioRange::Limit(SDK::XmlNodeRef &document, std::span<const TriggerRange> ranges) {
        if (ranges.empty() || !document->isTag("ATLConfig") || FindRangedEvents(document, ranges).empty()) {
            return false;
        }

        // Cloned because a document out of a pak can be the read-only node,
        // on which every write is silently dropped.
        SDK::XmlNodeRef edited = document->clone(true);
        if (!edited.IsValid()) {
            return false;
        }

        std::scoped_lock lock(_mutex);
        for (const RangedEvent &ranged : FindRangedEvents(edited, ranges)) {
            const long metres = std::lround(ranged.range);
            ranged.event->setAttr("distance_culling", std::to_string(metres).c_str());

            const std::string path = ranged.event->getAttr("fmod_name");
            if (std::find(_eventPaths.begin(), _eventPaths.end(), path) == _eventPaths.end()) {
                _eventPaths.push_back(path);
            }
            const double range = static_cast<double>(metres);
            for (std::atomic<double> &slot : _ranges) {
                const double known = slot.load(std::memory_order_relaxed);
                if (known == range) {
                    break;
                }
                if (known == 0.0) {
                    slot.store(range, std::memory_order_release);
                    break;
                }
            }
            Framework::Logging::GetLogger(kLogger)->info("{} carries {} m", path, metres);
        }
        document = edited;
        return true;
    }

    bool AudioRange::IsRangeInUse(double maxDistance) const {
        for (const std::atomic<double> &slot : _ranges) {
            const double range = slot.load(std::memory_order_acquire);
            if (range == maxDistance) {
                return true;
            }
            if (range == 0.0) {
                return false;
            }
        }
        return false;
    }

    bool AudioRange::IsRanged(const char *eventPath) const {
        std::scoped_lock lock(_mutex);
        return std::any_of(_eventPaths.begin(), _eventPaths.end(), [eventPath](const std::string &path) {
            return path == eventPath;
        });
    }

    bool AudioRange::IsRangedInstance(const SDK::wh::FSW_B_EVENT_INSTANCE &instance) const {
        // Null between a failed createInstance and the instance's teardown.
        const SDK::wh::FSW_EVENT_DESCRIPTION *description = instance.GetDescription();
        return instance.GetFmodInstance() != nullptr && IsRangeInUse(description->GetMaxDistance()) && IsRanged(description->GetPath()) && SDK::Fmod::Api::Get().Resolve();
    }

    double AudioRange::DistanceToListener(const SDK::wh::FSW_B_EVENT_INSTANCE &instance) {
        // Both positions are already in FMOD's space, so no axis swap.
        const SDK::Fmod::FMOD_VECTOR &listener = SDK::wh::CWh_FmodStudioWrapper::GetInstance()->GetListenerAttributes().position;
        const SDK::Fmod::FMOD_VECTOR &source   = instance.GetAttributes().position;
        const double dx                        = static_cast<double>(source.x) - listener.x;
        const double dy                        = static_cast<double>(source.y) - listener.y;
        const double dz                        = static_cast<double>(source.z) - listener.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    void AudioRange::Attenuate(const SDK::wh::FSW_B_EVENT_INSTANCE &instance) {
        if (!IsRangedInstance(instance)) {
            return;
        }
        {
            std::scoped_lock lock(_mutex);
            _silenced.erase(std::remove(_silenced.begin(), _silenced.end(), &instance), _silenced.end());
        }
        SDK::Fmod::Api::Get().Studio_EventInstance_SetVolume(instance.GetFmodInstance(), Gain(DistanceToListener(instance), instance.GetDescription()->GetMaxDistance()));
    }

    void AudioRange::Follow(const SDK::wh::FSW_B_EVENT_INSTANCE &instance, bool wasWaiting) {
        if (instance.GetState() != SDK::wh::FSW_B_EVENT_INSTANCE::EState::Playing || !IsRangedInstance(instance)) {
            return;
        }
        float gain = Gain(DistanceToListener(instance), instance.GetDescription()->GetMaxDistance());
        {
            std::scoped_lock lock(_mutex);
            const bool silenced = std::find(_silenced.begin(), _silenced.end(), &instance) != _silenced.end();
            if (wasWaiting && !silenced) {
                _silenced.push_back(&instance);
            }
            if (wasWaiting || silenced) {
                gain = 0.0F;
            }
        }
        SDK::Fmod::Api::Get().Studio_EventInstance_SetVolume(instance.GetFmodInstance(), gain);
    }

    float AudioRange::Gain(double distance, double range) {
        if (range <= 0.0) {
            return 1.0F;
        }
        const double nearDistance = range * kNearFraction;
        double gain               = distance <= nearDistance ? 1.0 : nearDistance / distance;
        const double taperStart   = range * (1.0 - kTaperFraction);
        if (distance > taperStart) {
            gain *= std::max(0.0, (range - distance) / (range - taperStart));
        }
        return static_cast<float>(gain);
    }
} // namespace KCDC::Features::Audio
