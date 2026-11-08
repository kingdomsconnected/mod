#include "features/player/player_nametags.h"

#include "features/player/player_nametag_policy.h"

#include "core/application.h"
#include "features/social/social_scoreboard.h"
#include "features/ui/ui_hud_elements.h"

#include "sdk/cryengine/action/c_view.h"
#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/action/i_view_system.h"
#include "sdk/cryengine/animation/i_character_instance.h"
#include "sdk/cryengine/animation/i_default_skeleton.h"
#include "sdk/cryengine/animation/i_skeleton_pose.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/renderer/i_renderer.h"
#include "sdk/wh/rpgmodule/c_calendar.h"

#include <core_modules.h>
#include <imgui.h>
#include <integrations/client/ui/nametag_notes.h>
#include <networking/replication/replication_manager.h>
#include <utils/profiler.h>
#include <voice/client/voice_client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace KCDC::Features::Player {

    namespace {
        constexpr float kNeckClearance = 0.5F;

        // Where the widget's size ramp and pixel lift sit, in metres. Absolute
        // rather than fractions of the reach, so a body five metres away draws
        // at the same size at noon and at midnight. The widget's own defaults
        // assume a fifty-metre reach and would hold every tag near maximum
        // size at twenty-five. Only the fade band follows the reach.
        constexpr float kScaleStart        = 3.0F;
        constexpr float kScaleEnd          = NametagLightCurve::kDaylightDistance;
        constexpr float kShiftStart        = 8.0F;
        constexpr float kFadeStartFraction = 0.7F;

        // The name's height as a fraction of the screen, before the distance
        // ramp scales it: 20 px at 1080p. The game font's x-height is small
        // beside ImGui's pixel font, so it cannot draw at the default size.
        constexpr float kNameFontHeight = 0.0185F;

        // 0xAARRGGBB wire colour -> ImU32
        ImU32 ToImColor(std::uint32_t color) {
            return IM_COL32((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, (color >> 24) & 0xFF);
        }

        // Bone geometry has its own collider, separate from IEntity::GetPhysics.
        SDK::IPhysicalEntity *CharacterPhysics(SDK::ICharacterInstance *character) {
            SDK::ISkeletonPose *pose = character != nullptr ? character->GetISkeletonPose() : nullptr;
            return pose != nullptr ? pose->GetCharacterPhysics() : nullptr;
        }

        /**
         * Where the visibility ray should stop: the anchor pulled a quarter of
         * the entity's smallest world bound back towards the camera, the same
         * offset `CPlayerVisTable::DoVisibilityCheck` applies before queueing
         * its linetest. The neck sits inside the body, so without this the
         * last stretch of the segment runs through whatever the target is
         * wearing or carrying. An entity with no geometry answers with the
         * engine's reset box and is left where it is.
         */
        SDK::Vec3 ProbePoint(SDK::IEntity *entity, const SDK::Vec3 &anchor, const SDK::Vec3 &camera) {
            SDK::AABB bounds {};
            entity->GetWorldBounds(bounds);
            if (bounds.min.x > bounds.max.x) {
                return anchor;
            }

            const float clearance = std::min({bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y, bounds.max.z - bounds.min.z}) * 0.25F;
            const SDK::Vec3 toCamera {camera.x - anchor.x, camera.y - anchor.y, camera.z - anchor.z};
            const float distance = std::sqrt(toCamera.x * toCamera.x + toCamera.y * toCamera.y + toCamera.z * toCamera.z);
            if (!std::isfinite(clearance) || clearance <= 0.0F || distance <= clearance) {
                return anchor;
            }

            const float scale = clearance / distance;
            return SDK::Vec3 {anchor.x + toCamera.x * scale, anchor.y + toCamera.y * scale, anchor.z + toCamera.z * scale};
        }

        bool NeckPosition(SDK::IEntity *entity, SDK::ICharacterInstance *character, SDK::Vec3 &out) {
            SDK::ISkeletonPose *pose = character->GetISkeletonPose();
            if (pose == nullptr) {
                return false;
            }

            const auto &skeleton     = character->GetIDefaultSkeleton();
            const std::int16_t joint = skeleton.GetJointIDByBoneID(SDK::BONEID_NECK);
            if (joint < 0 || static_cast<std::uint32_t>(joint) >= skeleton.GetJointCount()) {
                return false;
            }

            // Absolute joints are model-space; the slot supplies world space.
            const SDK::Vec3 local         = pose->GetAbsJointByID(joint).t;
            const SDK::Matrix34 slotWorld = entity->GetSlotWorldTM(0);
            const SDK::Vec3 world         = slotWorld.TransformPoint(local);
            if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z)) {
                return false;
            }

            out = world;
            return true;
        }
    } // namespace

    void NametagPass::Render() {
        FW_PROFILE_SCOPE_N("KCDC Nametags");
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr || !Begin()) {
            return;
        }

        Framework::Integrations::Client::UI::Nametags::Notes().Expire();

        const float deltaTime           = ImGui::GetIO().DeltaTime;
        const float cullDistance        = _layout.drawDistance + NametagRayPolicy::kPreCullSlack;
        const float cullDistanceSquared = cullDistance * cullDistance;

        // The pre-cull runs on the replicated position rather than the
        // entity's, so a body out of range costs no engine call at all.
        _candidates.clear();
        replication->ForEach<Human>([&](Human *human) {
            // The local player is not named to itself.
            if (human->IsOwner()) {
                return;
            }
            Consider(Subject {human->GetEntityId(), human->GetNetworkID(), human->position, &human->nametag, &human->nickname, human->soul.HealthPercent(), &human->nametagVisibility, static_cast<std::uint64_t>(human->ownerGUID), human->input.crouch}, cullDistanceSquared);
        });

        // Server-owned NPCs, on the same terms and out of the same buffer, so
        // one budget and one sort cover both. Only the ones the server asked to
        // be named: an NPC's tag is a decision the gamemode made, where a
        // player's is a given.
        replication->ForEach<KCDC::Features::Npc::Npc>([&](KCDC::Features::Npc::Npc *npc) {
            if (!Shared::Game::HasFlag(npc->flags, Shared::Game::NpcFlags::ShowNametag)) {
                npc->nametagVisibility = NametagVisibility {};
                return;
            }
            const float health = npc->maxHealth > 0.0F ? std::clamp(100.0F * npc->health / npc->maxHealth, 0.0F, 100.0F) : 0.0F;
            Consider(Subject {npc->GetEntityId(), npc->GetNetworkID(), npc->position, &npc->nametag, &npc->name, health, &npc->nametagVisibility}, cullDistanceSquared);
        });

        // Nearest first: that is where occlusion changes fastest, and the
        // segments traced there are the shortest.
        std::sort(_candidates.begin(), _candidates.end(), [](const Candidate &left, const Candidate &right) {
            return left.distance < right.distance;
        });

        int rayBudget = NametagRayPolicy::kBudgetPerFrame;
        for (const Candidate &candidate : _candidates) {
            Draw(candidate.subject, deltaTime, rayBudget);
        }
        _candidates.clear();
    }

    void NametagPass::Consider(const Subject &subject, float cullDistanceSquared) {
        // The pre-cull runs on the replicated position rather than the entity's,
        // so a body out of range costs no engine call at all.
        const float dx              = subject.position.x - _cameraPosition.x;
        const float dy              = subject.position.y - _cameraPosition.y;
        const float dz              = subject.position.z - _cameraPosition.z;
        const float distanceSquared = dx * dx + dy * dy + dz * dz;
        if (!std::isfinite(distanceSquared) || distanceSquared >= cullDistanceSquared) {
            *subject.visibility = NametagVisibility {};
            return;
        }

        // A sneaking body is a candidate only while its tag is still fading
        // out. Once it has gone, the body costs a flag read and nothing more,
        // however many players are crouched in range.
        if (subject.concealed && subject.visibility->fade <= 0.0F) {
            *subject.visibility = NametagVisibility {};
            return;
        }

        _candidates.push_back(Candidate {subject, std::sqrt(distanceSquared)});
    }

    bool NametagPass::Begin() {
        if (!Framework::External::ImGUI::Widgets::NameTagView::showTags) {
            return false;
        }

        // The player list is a translucent Flash panel over the world, and
        // the ImGui pass draws after it, so a tag would show through it. No
        // tag is drawn while the list is on screen at all.
        if (KCDC::Core::gApplication->GetScoreboardService().IsShown()) {
            return false;
        }

        const ImGuiIO &io = ImGui::GetIO();
        if (io.DisplaySize.x <= 0.0F || io.DisplaySize.y <= 0.0F) {
            return false;
        }

        _drawList = ImGui::GetBackgroundDrawList();
        if (_drawList == nullptr) {
            return false;
        }
        _displayWidth  = io.DisplaySize.x;
        _displayHeight = io.DisplaySize.y;
        _font          = KCDC::Core::gApplication->GetImGUI()->GetFont(kFontName);

        _renderer = SDK::IRenderer::GetInstance();
        if (_renderer == nullptr) {
            return false;
        }

        auto *framework  = SDK::IGameFramework::GetInstance();
        auto *viewSystem = framework != nullptr ? framework->GetIViewSystem() : nullptr;
        auto *view       = viewSystem != nullptr ? static_cast<SDK::CView *>(viewSystem->GetActiveView()) : nullptr;
        if (view == nullptr) {
            return false;
        }

        _physics      = SDK::CPhysicalWorld::GetInstance();
        _entitySystem = SDK::IEntitySystem::GetInstance();
        if (_physics == nullptr || _entitySystem == nullptr) {
            return false;
        }

        auto *localActor       = framework->GetClientActor();
        auto *localEntity      = localActor != nullptr ? localActor->GetEntity() : nullptr;
        _localPhysics          = localEntity != nullptr ? localEntity->GetPhysics() : nullptr;
        _localCharacterPhysics = CharacterPhysics(localEntity != nullptr ? localEntity->GetCharacter(0) : nullptr);

        _cameraPosition      = view->GetRenderCamera()->GetPosition();
        _layout.drawDistance = LightDrawDistance(io.DeltaTime);
        _layout.fadeStart    = _layout.drawDistance * kFadeStartFraction;
        _layout.scaleStart   = kScaleStart;
        _layout.scaleEnd     = kScaleEnd;
        _layout.shiftStart   = kShiftStart;
        return true;
    }

    float NametagPass::LightDrawDistance(float deltaTime) {
        auto *calendar = SDK::wh::rpgmodule::C_Calendar::GetInstance();
        if (calendar == nullptr) {
            return NametagLightCurve::kDaylightDistance;
        }

        // The faked hour when one is running: that is the hour the sky is
        // actually drawn at.
        const float hour   = calendar->IsTimeOfDayFaked() ? calendar->GetFakeTimeOfDayHour() : calendar->GetHourOfDay();
        const float target = NametagLightCurve::DrawDistanceFor(hour);

        if (_lightDistance < 0.0F) {
            _lightDistance = target;
            return _lightDistance;
        }

        _lightDistance += (target - _lightDistance) * std::clamp(deltaTime * NametagLightCurve::kAdaptSpeed, 0.0F, 1.0F);
        return _lightDistance;
    }

    bool NametagPass::ResolveAnchor(const Subject &subject, Anchor &anchor) const {
        const SDK::EntityId entityId = subject.entityId;
        if (entityId == 0 || !Framework::External::ImGUI::Widgets::NameTagHasComponent(subject.nametag->components, Framework::External::ImGUI::Widgets::NameTagComponent::Name)) {
            return false;
        }

        anchor.entity = _entitySystem->GetEntity(entityId);
        if (anchor.entity == nullptr || anchor.entity->IsHidden() || anchor.entity->IsInvisible()) {
            return false;
        }

        // The pre-cull measured the replicated position; the fade wants the
        // one the body is actually standing at.
        const SDK::Vec3 origin      = anchor.entity->GetWorldPos();
        const float dx              = origin.x - _cameraPosition.x;
        const float dy              = origin.y - _cameraPosition.y;
        const float dz              = origin.z - _cameraPosition.z;
        const float distanceSquared = dx * dx + dy * dy + dz * dz;
        if (!std::isfinite(distanceSquared) || distanceSquared >= _layout.drawDistance * _layout.drawDistance) {
            return false;
        }

        anchor.distance = std::sqrt(distanceSquared);
        if (Framework::External::ImGUI::Widgets::NameTagAlpha(anchor.distance, subject.nametag->components, _layout) <= 0.0F) {
            return false;
        }

        anchor.character = anchor.entity->GetCharacter(0);
        if (anchor.character == nullptr || !anchor.character->IsCharacterVisible()) {
            return false;
        }

        // A guessed standing height can expose crouching players behind cover.
        if (!NeckPosition(anchor.entity, anchor.character, anchor.neck)) {
            return false;
        }

        // Both the neck and the label position above it have to be on screen.
        float percentX = 0.0F, percentY = 0.0F, depth = 0.0F;
        if (!_renderer->ProjectToScreen(anchor.neck.x, anchor.neck.y, anchor.neck.z, &percentX, &percentY, &depth) || !_renderer->ProjectToScreen(anchor.neck.x, anchor.neck.y, anchor.neck.z + kNeckClearance, &percentX, &percentY, &depth)) {
            return false;
        }

        anchor.screen = ImVec2(percentX * 0.01F * _displayWidth, percentY * 0.01F * _displayHeight);
        return true;
    }

    void NametagPass::Draw(const Subject &subject, float deltaTime, int &rayBudget) {
        FW_PROFILE_SCOPE_N("KCDC Nametag candidate");
        const std::string &label = subject.nametag->text.empty() ? *subject.fallbackLabel : subject.nametag->text;

        Anchor anchor {};
        if (label.empty() || !ResolveAnchor(subject, anchor)) {
            *subject.visibility = NametagVisibility {};
            return;
        }

        // Everything above is a cheap rejection. The ray is the one expensive
        // thing the pass does, and the one thing it rations.
        NametagVisibility &visibility = *subject.visibility;
        const double now              = ImGui::GetTime();
        if (subject.concealed) {
            // No ray for a tag that is only leaving, and no stale clear left
            // behind to show it the moment the body stands up again.
            visibility.clear = false;
        } else if (rayBudget > 0 && NametagRayPolicy::WantsTrace(visibility.checkedAt, now)) {
            --rayBudget;
            visibility.clear     = HasLineOfSight(anchor, ProbePoint(anchor.entity, anchor.neck, _cameraPosition));
            visibility.checkedAt = now;
        }

        const bool visible = NametagRayPolicy::Authorises(visibility.clear, visibility.checkedAt, now);
        visibility.fade    = Framework::External::ImGUI::Widgets::NameTagFadeStep(visibility.fade, visible, deltaTime, NametagRayPolicy::kFadeSpeed);
        if (visibility.fade <= 0.0F) {
            return;
        }

        const ImU32 textColor = ToImColor(subject.nametag->color);
        Framework::External::ImGUI::Widgets::NameTagStyle style;
        style.anchor   = Framework::External::ImGUI::Widgets::NameTagAnchor::BottomCenter;
        style.font     = _font;
        style.fontSize = _displayHeight * kNameFontHeight;

        // Bare text, no plate behind it: ImGui drops a fill whose alpha is
        // zero, so the widget's backdrop is never drawn.
        style.bgColor = IM_COL32(0, 0, 0, 0);

        // A script's Nametags.setLabel line rides the tag, so it fades and
        // hides behind cover exactly as the name does.
        if (const auto *note = Framework::Integrations::Client::UI::Nametags::Notes().Find(subject.networkId)) {
            style.note      = note->text.c_str();
            style.noteColor = note->color != 0 ? ToImColor(note->color) : 0;
        }

        // Keyed as the framework keys speakers, by the avatar's owner, so the
        // icon is on for exactly the talker this client is hearing. The
        // framework already holds a speaker across the pauses between words;
        // the fade only smooths the edges of a sentence. A resource hiding it
        // through Voice.setIndicatorVisible fades it out the same way.
        const Framework::Voice::VoiceClient *voice = Framework::CoreModules::GetVoiceClient();
        const bool talking                         = voice != nullptr && subject.voiceSpeaker != 0 && KCDC::Features::UI::HudElements::Get().IsVoiceIndicatorVisible() && voice->IsSpeakerTalking(subject.voiceSpeaker);
        const float voiceStep                      = deltaTime * (talking ? NametagRayPolicy::kVoiceFadeInSpeed : NametagRayPolicy::kVoiceFadeOutSpeed);
        visibility.voiceFade                       = std::clamp(visibility.voiceFade + (talking ? voiceStep : -voiceStep), 0.0F, 1.0F);
        if (visibility.voiceFade > 0.0F) {
            style.voiceLevel = talking ? voice->GetSpeakerLevel(subject.voiceSpeaker) : 0.0F;
            style.voiceColor = Framework::External::ImGUI::Widgets::WorldTextModulateAlpha(textColor, visibility.voiceFade);
            // One wave for a whisper, two for normal, three for a shout.
            style.voiceWaves = voice != nullptr ? static_cast<int>(voice->GetSpeakerTier(subject.voiceSpeaker)) + 1 : static_cast<int>(Framework::Voice::VoiceTier::Normal) + 1;
        }

        // One alpha drives the whole widget. The bar must never leave on its
        // own: the bottom anchor reserves its height, so dropping it alone
        // would jump the name up by that much in a single frame.
        Framework::External::ImGUI::Widgets::DrawNameTagAt(_drawList, anchor.screen, anchor.distance, label.c_str(), subject.nametag->components, textColor, subject.healthPercent, _layout, style, visibility.fade);
    }

    bool NametagPass::HasLineOfSight(const Anchor &anchor, const SDK::Vec3 &target) const {
        FW_PROFILE_SCOPE_N("KCDC Nametag occlusion ray");
        const SDK::Vec3 direction {target.x - _cameraPosition.x, target.y - _cameraPosition.y, target.z - _cameraPosition.z};

        // The object types already put actors out of reach; naming the two
        // colliders each one owns is what CPlayerVisTable does on top.
        SDK::IPhysicalEntity *colliders[] {_localPhysics, _localCharacterPhysics, anchor.entity->GetPhysics(), CharacterPhysics(anchor.character)};
        SDK::IPhysicalEntity *skip[4] {};
        int skipCount = 0;
        for (SDK::IPhysicalEntity *collider : colliders) {
            if (collider != nullptr && std::find(skip, skip + skipCount, collider) == skip + skipCount) {
                skip[skipCount++] = collider;
            }
        }

        SDK::SRayHit hit {};
        return _physics->RayWorldIntersection(_cameraPosition, direction, SDK::CPhysicalWorld::kVisibilityObjectTypes, SDK::CPhysicalWorld::kVisibilityRayFlags, &hit, 1, skip, skipCount) == 0;
    }
} // namespace KCDC::Features::Player
