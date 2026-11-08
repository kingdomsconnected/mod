#pragma once

#include <cstddef>
#include <cstdint>

/**
 * The slice of the FMOD Core and Studio C ABI that KCD2 exposes.
 *
 * KCD2 ships FMOD 2.02.21 -- `CWh_FmodStudioWrapper::InitializeFmodStudio`
 * builds against header version `0x20221` and refuses older binaries.
 *
 * The C ABI rather than the C++ one, because `WHGame.dll` imports only the
 * mangled methods it happens to use, and `ChannelControl::set3DAttributes`
 * -- the one call that places a voice in the world -- is not among them. The
 * C entry points are exported from the same DLLs and cover the whole API.
 */
namespace SDK::Fmod {
    // Opaque handles: the C API never defines these, so this is where they are
    // declared.
    struct FMOD_SYSTEM;
    struct FMOD_SOUND;
    struct FMOD_CHANNEL;
    struct FMOD_CHANNELGROUP;
    struct FMOD_SOUNDGROUP;
    struct FMOD_DSP;
    struct FMOD_STUDIO_SYSTEM;
    struct FMOD_STUDIO_BANK;
    struct FMOD_STUDIO_BUS;
    struct FMOD_STUDIO_EVENTINSTANCE;
    struct FMOD_STUDIO_EVENTDESCRIPTION;

    // Only FMOD_OK is ever compared against; the rest is an error code the
    // caller logs and gives up on.
    enum FMOD_RESULT : std::int32_t {
        FMOD_OK = 0,
    };

    struct FMOD_VECTOR {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    static_assert(sizeof(FMOD_VECTOR) == 12, "FMOD_VECTOR size mismatch");

    /**
     * Position, velocity, forward and up, in FMOD's left-handed Y-up space.
     *
     * CryEngine is right-handed Z-up, so every vector crossing this boundary
     * swaps y and z. `CAudioListener181148EA0::PushListenerTransformation` is
     * where the game does it for the listener.
     */
    struct FMOD_3D_ATTRIBUTES {
        FMOD_VECTOR position;
        FMOD_VECTOR velocity;
        FMOD_VECTOR forward;
        FMOD_VECTOR up;
    };

    static_assert(sizeof(FMOD_3D_ATTRIBUTES) == 48, "FMOD_3D_ATTRIBUTES size mismatch");

    // FMOD 2.02 FMOD_MODE bits.
    constexpr std::uint32_t FMOD_LOOP_NORMAL            = 0x00000002;
    constexpr std::uint32_t FMOD_3D                     = 0x00000010;
    constexpr std::uint32_t FMOD_CREATESTREAM           = 0x00000080;
    constexpr std::uint32_t FMOD_CREATESAMPLE           = 0x00000100;
    constexpr std::uint32_t FMOD_OPENUSER               = 0x00000400;
    constexpr std::uint32_t FMOD_3D_WORLDRELATIVE       = 0x00080000;
    constexpr std::uint32_t FMOD_3D_LINEARSQUAREROLLOFF = 0x00400000;

    constexpr std::int32_t FMOD_SOUND_FORMAT_PCM16   = 2;
    constexpr std::int32_t FMOD_CHANNELORDER_DEFAULT = 0;

    // FMOD_DRIVER_STATE, the `state` bits System::getRecordDriverInfo reports.
    constexpr std::uint32_t FMOD_DRIVER_STATE_CONNECTED = 0x00000001;

    // The version InitializeFmodStudio builds against, and the ABI every
    // layout here describes.
    constexpr std::uint32_t kFmodVersion = 0x00020221;

    struct FMOD_GUID {
        std::uint32_t Data1 = 0;
        std::uint16_t Data2 = 0;
        std::uint16_t Data3 = 0;
        std::uint8_t Data4[8] {};
    };

    // Fills `datalen` bytes for a sound opened with FMOD_OPENUSER. For a stream
    // it runs on FMOD's *stream* thread, which refills a double buffer of
    // `decodebuffersize` ahead of the mixer -- not on the mixer itself -- so a
    // late stream thread makes the mixer replay the half it already played.
    using FMOD_SOUND_PCMREAD_CALLBACK = FMOD_RESULT(__stdcall *)(FMOD_SOUND *sound, void *data, unsigned int datalen);

    // FMOD_CHANNELCONTROL_DSP_INDEX: positions on a channel's DSP chain. The
    // tail is the input end, ahead of the fader that applies volume and 3D
    // panning.
    constexpr int FMOD_CHANNELCONTROL_DSP_HEAD  = -1;
    constexpr int FMOD_CHANNELCONTROL_DSP_FADER = -2;
    constexpr int FMOD_CHANNELCONTROL_DSP_TAIL  = -3;

    // FMOD_PLUGIN_SDK_VERSION of the 2.02 headers, which FMOD_DSP_DESCRIPTION
    // carries as its version handshake.
    constexpr std::uint32_t FMOD_PLUGIN_SDK_VERSION = 110;

    /**
     * FMOD 2.02's FMOD_DSP_STATE: what a DSP callback is handed. `instance` is
     * the FMOD_DSP the callback is running for.
     */
    struct FMOD_DSP_STATE {
        FMOD_DSP *instance               = nullptr; // 0x00
        void *plugindata                 = nullptr; // 0x08
        std::uint32_t channelmask        = 0;       // 0x10, FMOD_CHANNELMASK
        std::int32_t source_speakermode  = 0;       // 0x14, FMOD_SPEAKERMODE
        float *sidechaindata             = nullptr; // 0x18
        std::int32_t sidechainchannels   = 0;       // 0x20
        std::int32_t pad0024             = 0;       // 0x24
        void *functions                  = nullptr; // 0x28, FMOD_DSP_STATE_FUNCTIONS *
        std::int32_t systemobject        = 0;       // 0x30
        std::int32_t pad0034             = 0;       // 0x34

      private:
        static void AssertLayout() {
            static_assert(offsetof(FMOD_DSP_STATE, instance) == 0x00, "instance offset mismatch");
            static_assert(offsetof(FMOD_DSP_STATE, channelmask) == 0x10, "channelmask offset mismatch");
            static_assert(offsetof(FMOD_DSP_STATE, functions) == 0x28, "functions offset mismatch");
            static_assert(offsetof(FMOD_DSP_STATE, systemobject) == 0x30, "systemobject offset mismatch");
        }
    };

    static_assert(sizeof(FMOD_DSP_STATE) == 0x38, "FMOD_DSP_STATE size mismatch");

    // Called on FMOD's mixer thread for every block the DSP takes part in. It
    // must write `length * *outchannels` samples, and must not block, allocate,
    // or call back into FMOD.
    using FMOD_DSP_READ_CALLBACK = FMOD_RESULT(__stdcall *)(FMOD_DSP_STATE *state, float *inbuffer, float *outbuffer, unsigned int length, int inchannels, int *outchannels);

    // Mixer thread, before each block. FMOD_OK runs the read callback for it
    // whether or not the DSP's inputs are idle.
    using FMOD_DSP_SHOULDIPROCESS_CALLBACK = FMOD_RESULT(__stdcall *)(FMOD_DSP_STATE *state, int inputsidle, unsigned int length, std::uint32_t inmask, int inchannels, std::int32_t speakermode);

    /**
     * FMOD 2.02's FMOD_DSP_DESCRIPTION, the layout `System::createDSP` reads.
     *
     * FMOD keeps referencing it for the life of every DSP created from it, so
     * it must outlive them. The field order is FMOD's published plug-in API;
     * the callbacks KCDC never sets are still named, so nothing can drift.
     */
    struct FMOD_DSP_DESCRIPTION {
        std::uint32_t pluginsdkversion                  = FMOD_PLUGIN_SDK_VERSION; // 0x00
        char name[32]                                   = {};                      // 0x04
        std::uint32_t version                           = 0;                       // 0x24
        std::int32_t numinputbuffers                    = 0;                       // 0x28
        std::int32_t numoutputbuffers                   = 0;                       // 0x2C
        void *create                                    = nullptr;                 // 0x30
        void *release                                   = nullptr;                 // 0x38
        void *reset                                     = nullptr;                 // 0x40
        FMOD_DSP_READ_CALLBACK read                     = nullptr;                 // 0x48
        void *process                                   = nullptr;                 // 0x50
        void *setposition                               = nullptr;                 // 0x58
        std::int32_t numparameters                      = 0;                       // 0x60
        std::int32_t pad0064                            = 0;                       // 0x64
        void *paramdesc                                 = nullptr;                 // 0x68
        void *setparameterfloat                         = nullptr;                 // 0x70
        void *setparameterint                           = nullptr;                 // 0x78
        void *setparameterbool                          = nullptr;                 // 0x80
        void *setparameterdata                          = nullptr;                 // 0x88
        void *getparameterfloat                         = nullptr;                 // 0x90
        void *getparameterint                           = nullptr;                 // 0x98
        void *getparameterbool                          = nullptr;                 // 0xA0
        void *getparameterdata                          = nullptr;                 // 0xA8
        FMOD_DSP_SHOULDIPROCESS_CALLBACK shouldiprocess = nullptr;                 // 0xB0
        void *userdata                                  = nullptr;                 // 0xB8
        void *sys_register                              = nullptr;                 // 0xC0
        void *sys_deregister                            = nullptr;                 // 0xC8
        void *sys_mix                                   = nullptr;                 // 0xD0

      private:
        static void AssertLayout() {
            static_assert(offsetof(FMOD_DSP_DESCRIPTION, name) == 0x04, "name offset mismatch");
            static_assert(offsetof(FMOD_DSP_DESCRIPTION, numinputbuffers) == 0x28, "numinputbuffers offset mismatch");
            static_assert(offsetof(FMOD_DSP_DESCRIPTION, read) == 0x48, "read offset mismatch");
            static_assert(offsetof(FMOD_DSP_DESCRIPTION, numparameters) == 0x60, "numparameters offset mismatch");
            static_assert(offsetof(FMOD_DSP_DESCRIPTION, shouldiprocess) == 0xB0, "shouldiprocess offset mismatch");
            static_assert(offsetof(FMOD_DSP_DESCRIPTION, sys_mix) == 0xD0, "sys_mix offset mismatch");
        }
    };

    static_assert(sizeof(FMOD_DSP_DESCRIPTION) == 0xD8, "FMOD_DSP_DESCRIPTION size mismatch");

    /**
     * FMOD 2.02's FMOD_CREATESOUNDEXINFO.
     *
     * `cbsize` is the version handshake -- FMOD reads exactly the 224 bytes
     * this declares -- so the assertions below are load-bearing. The field
     * order is FMOD's published 2.x struct; every offset the assertions check
     * is one `CFmodMusicWrapper182570AE0::StartOutputStream` writes into its
     * own instance, which is the game stating its build layout.
     *
     * Unused fields are still named: a run of anonymous padding is what lets
     * the next field drift silently.
     */
    struct FMOD_CREATESOUNDEXINFO {
        std::int32_t cbsize                         = 0;       // 0x00
        std::uint32_t length                        = 0;       // 0x04, bytes
        std::uint32_t fileoffset                    = 0;       // 0x08
        std::int32_t numchannels                    = 0;       // 0x0C
        std::int32_t defaultfrequency               = 0;       // 0x10
        std::int32_t format                         = 0;       // 0x14, FMOD_SOUND_FORMAT
        std::uint32_t decodebuffersize              = 0;       // 0x18, PCM samples
        std::int32_t initialsubsound                = 0;       // 0x1C
        std::int32_t numsubsounds                   = 0;       // 0x20
        std::int32_t pad0024                        = 0;       // 0x24
        std::int32_t *inclusionlist                 = nullptr; // 0x28
        std::int32_t inclusionlistnum               = 0;       // 0x30
        std::int32_t pad0034                        = 0;       // 0x34
        FMOD_SOUND_PCMREAD_CALLBACK pcmreadcallback = nullptr; // 0x38
        void *pcmsetposcallback                     = nullptr; // 0x40
        void *nonblockcallback                      = nullptr; // 0x48
        const char *dlsname                         = nullptr; // 0x50
        const char *encryptionkey                   = nullptr; // 0x58
        std::int32_t maxpolyphony                   = 0;       // 0x60
        std::int32_t pad0064                        = 0;       // 0x64
        void *userdata                              = nullptr; // 0x68
        std::int32_t suggestedsoundtype             = 0;       // 0x70
        std::int32_t pad0074                        = 0;       // 0x74
        void *fileuseropen                          = nullptr; // 0x78
        void *fileuserclose                         = nullptr; // 0x80
        void *fileuserread                          = nullptr; // 0x88
        void *fileuserseek                          = nullptr; // 0x90
        void *fileuserasyncread                     = nullptr; // 0x98
        void *fileuserasynccancel                   = nullptr; // 0xA0
        void *fileuserdata                          = nullptr; // 0xA8
        std::int32_t filebuffersize                 = 0;       // 0xB0
        std::int32_t channelorder                   = 0;       // 0xB4, FMOD_CHANNELORDER
        FMOD_SOUNDGROUP *initialsoundgroup          = nullptr; // 0xB8
        std::uint32_t initialseekposition           = 0;       // 0xC0
        std::uint32_t initialseekpostype            = 0;       // 0xC4, FMOD_TIMEUNIT
        std::int32_t ignoresetfilesystem            = 0;       // 0xC8
        std::uint32_t audioqueuepolicy              = 0;       // 0xCC
        std::uint32_t minmidigranularity            = 0;       // 0xD0
        std::int32_t nonblockthreadid               = 0;       // 0xD4
        FMOD_GUID *fsbguid                          = nullptr; // 0xD8

      private:
        static void AssertLayout() {
            static_assert(offsetof(FMOD_CREATESOUNDEXINFO, cbsize) == 0x00, "cbsize offset mismatch");
            static_assert(offsetof(FMOD_CREATESOUNDEXINFO, length) == 0x04, "length offset mismatch");
            static_assert(offsetof(FMOD_CREATESOUNDEXINFO, numchannels) == 0x0C, "numchannels offset mismatch");
            static_assert(offsetof(FMOD_CREATESOUNDEXINFO, defaultfrequency) == 0x10, "defaultfrequency offset mismatch");
            static_assert(offsetof(FMOD_CREATESOUNDEXINFO, format) == 0x14, "format offset mismatch");
            static_assert(offsetof(FMOD_CREATESOUNDEXINFO, decodebuffersize) == 0x18, "decodebuffersize offset mismatch");
            static_assert(offsetof(FMOD_CREATESOUNDEXINFO, pcmreadcallback) == 0x38, "pcmreadcallback offset mismatch");
            static_assert(offsetof(FMOD_CREATESOUNDEXINFO, channelorder) == 0xB4, "channelorder offset mismatch");
            static_assert(offsetof(FMOD_CREATESOUNDEXINFO, fsbguid) == 0xD8, "fsbguid offset mismatch");
        }
    };

    static_assert(sizeof(FMOD_CREATESOUNDEXINFO) == 224, "FMOD_CREATESOUNDEXINFO size mismatch -- FMOD 2.02 declares 224 bytes");
} // namespace SDK::Fmod
