#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    struct ITimer;

    /**
     * Shuffled ITimeOfDay interface implemented by CTimeOfDay.
     *
     * The abstract table is at 0x1840328D0 and CTimeOfDay's complete 55-slot
     * table is at 0x183AA5758. Only entries independently proven from the
     * concrete implementation are named; the shuffled remainder stays opaque.
     *
     * CTimeOfDay is not the authority on the time in KCD2. The world clock
     * lives on wh::rpgmodule::C_Calendar and republishes an hour into slot 7
     * whenever it moves, so a write made here alone survives at most a frame.
     */
    class ITimeOfDay {
      public:
        enum ENetSerializeFlags : std::uint32_t {
            NETSER_FORCESET = 1u << 0,
            NETSER_COMPENSATELAG = 1u << 1,
            NETSER_STATICPROPS = 1u << 2,
        };

        /** KCD2's calendar-extended advanced clock state. */
        struct SAdvancedInfo {
            float startTime; // 0x00
            float endTime; // 0x04
            float animationSpeed; // 0x08
            std::uint32_t year; // 0x0C
            std::uint32_t month; // 0x10
            std::uint32_t day; // 0x14
            std::uint32_t state0018;
            std::uint32_t state001C;
            std::uint32_t state0020;
        };

        struct SEnvironmentInfo {
            bool sunLinkedToTimeOfDay; // 0x00
            std::byte padding0001[0x3];
            float sunRotationLatitude; // 0x04
            float sunRotationLongitude; // 0x08
        };

        virtual ~ITimeOfDay() = default;
        virtual void unk_fn0001() = 0;

        /** Copies the 72-byte record at `index` out of the variable vector. */
        virtual bool GetVariableInfo(int index, void *info72) = 0;             // +0x010
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void SetTimeRaw(float hour,
                                bool forceUpdate,
                                bool deferredUpdate) = 0;                      // +0x038
        virtual float GetTime() = 0;                                          // +0x040

        /** The unfaked hour, kept beside the published one. */
        virtual void SetRealHour(float hour) = 0;                             // +0x048
        virtual float GetRealHour() = 0;                                      // +0x050
        virtual void SetSunPos(float longitude, float latitude) = 0;           // +0x058
        virtual float GetSunLatitude() = 0;
        virtual float GetSunLongitude() = 0;
        virtual void SetTime(float hour, bool forceUpdate = false) = 0;        // +0x070
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0;
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0;
        virtual void unk_fn0020() = 0;
        virtual void Tick() = 0;                                               // +0x0A8
        virtual void SetPaused(bool paused) = 0;
        virtual void SetAdvancedInfo(const SAdvancedInfo &info) = 0;
        virtual void GetAdvancedInfo(SAdvancedInfo &info) = 0;
        virtual void unk_fn0025() = 0;
        virtual void unk_fn0026() = 0;

        /** Interpolates the variable set and pushes it at the renderer. */
        virtual void Update(bool interpolate, bool forceUpdate) = 0;           // +0x0D8
        virtual void unk_fn0028() = 0;
        virtual void BeginEditMode() = 0;
        virtual void EndEditMode() = 0;
        virtual void Serialize(TSerialize serialize) = 0;
        virtual void Serialize(XmlNodeRef &node, bool loading) = 0;
        virtual void SetTimer(ITimer *timer) = 0;
        virtual void SetEnvironmentSettings(const SEnvironmentInfo &info) = 0;
        virtual void NetSerialize(TSerialize serialize,
                                  float lag,
                                  std::uint32_t flags) = 0;
        virtual void unk_fn0036() = 0;
        virtual void unk_fn0037() = 0;

        // The preset table. Names live in the 0x40-byte info records at
        // +0x0B8; the 0x18-byte value records the blend runs on are at +0x0A0
        // and share the index.
        virtual bool GetPresetName(std::uint32_t index, CryStringT<char> &name) = 0; // +0x130

        /**
         * The preset's selection weight, not a flag mask.
         *
         * `C_TimeOfDayBlender::MakeRandomEntry` (`0x1813478B4`) reads one of
         * these per preset, sums them as floats and rolls a weighted choice
         * over the total, so this is what decides which sky the engine picks
         * for itself when its queue runs out.
         */
        virtual bool GetPresetWeight(std::uint32_t index, float &weight) = 0; // +0x138
        virtual void *GetPresetEntry(std::uint32_t index) = 0;                 // +0x140
        virtual bool FindPresetByName(const char *name, std::uint32_t &index) = 0;   // +0x148
        virtual std::size_t GetPresetCount() = 0;                              // +0x150
        virtual void unk_fn0043() = 0;
        virtual bool RemovePreset(std::uint32_t index) = 0;                    // +0x160

        /** Loads `index` into the live variable set and records it at +0x0D0. */
        virtual void SetCurrentPreset(std::uint32_t index) = 0;                // +0x168
        virtual std::uint32_t GetCurrentPresetIndex() = 0;                     // +0x170

        /** Blend target and factor at +0x0D4/+0x0D8; the blender rewrites both every frame. */
        virtual bool SetPresetBlend(std::uint32_t index, float factor) = 0;    // +0x178
        virtual void unk_fn0048() = 0;
        virtual void unk_fn0049() = 0;
        virtual void unk_fn0050() = 0;
        virtual void unk_fn0051() = 0;
        virtual void unk_fn0052() = 0;
        virtual void unk_fn0053() = 0;
        virtual void unk_fn0054() = 0;                                        // +0x1B0

        /**
         * Publishes a new clock time. Slot 7, not the symbol-named SetTime at
         * +0x070, which only pushes a sun direction to I3DEngine. Tick proves
         * the ABI: it dispatches +0x038 with the hour in xmm1 and both bytes
         * cleared, the second selecting the immediate-update path. Cosmetic on
         * its own: C_Calendar::PublishTimeOfDay overwrites it on the next tick.
         */
        void InvokeSetTimeRaw(float hour) {
            SetTimeRaw(hour, false, false);
        }
    };

    static_assert(sizeof(ITimeOfDay::SAdvancedInfo) == 0x24,
                  "ITimeOfDay::SAdvancedInfo size mismatch");
    static_assert(sizeof(ITimeOfDay::SEnvironmentInfo) == 0xC,
                  "ITimeOfDay::SEnvironmentInfo size mismatch");
    static_assert(sizeof(ITimeOfDay) == 0x8, "ITimeOfDay size mismatch");
} // namespace SDK
