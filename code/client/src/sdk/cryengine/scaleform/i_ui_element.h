#pragma once

#include "i_flash_player.h"
#include "s_ui_event_desc.h"

#include <memory>

namespace SDK {
    /**
     * KCD2 FlashUI interface, proven against CFlashUIElement's primary table
     * at 0x184049BD0. Unknown slots are deliberately not callable APIs.
     * Element handles are intrusive; do not own them with std::shared_ptr.
     * GetFlashPlayer returns a separate, game-owned shared movie reference.
     *
     * Slots 44 to 99 are anchored by the names the binary carries --
     * UpdateViewPort (62), Serialize (65), AddEventListener (66),
     * AddTexture (95), SendCursorEvent (99) -- and by CScriptBind_UIAction,
     * which reaches GetVariableDesc through +0x160, GetFunctionDesc through
     * +0x1D8, CallFunction through +0x220, SetVariable through +0x268 and
     * GetMovieClip through +0x238. Warhorse carries one slot stock CryEngine
     * does not, at 64, so nothing above 65 counts up from below it. Same-name
     * overloads are emitted in reverse declaration order.
     */
    struct IUIElement {
        enum EFlashUIFlags : int {
            eFUI_HARDWARECURSOR   = 0x1,
            eFUI_MOUSEEVENTS      = 0x2,
            eFUI_KEYEVENTS        = 0x4,
            eFUI_CONSOLE_MOUSE    = 0x8,
            eFUI_CONSOLE_CURSOR   = 0x10,
            eFUI_CONTROLLER_INPUT = 0x20,
            eFUI_EVENTS_EXCLUSIVE = 0x40,
        };

        virtual ~IUIElement()                      = default; // 0
        virtual void AddRef()                      = 0;
        virtual void Release()                     = 0;
        virtual unsigned int GetInstanceID() const = 0; // 3
        /**
         * Slot 4 (0x180555EE4). Writes an **owning** handle into `out` and
         * returns it; `Release` what comes back. Instance zero answers with
         * the element itself, and a missing instance is created from the
         * same XML rather than reported absent.
         */
        virtual IUIElement **GetInstance(IUIElement **out, unsigned int instanceId) = 0;
        virtual bool DestroyInstance(unsigned int instanceId)                       = 0; // 5
        virtual void unk_fn0006()                                                   = 0;
        virtual void unk_fn0007()                                                   = 0; // Returns a native vector of instances by sret; never call it.
        virtual void unk_fn0008()                                                   = 0;
        virtual void unk_fn0009()                                                   = 0;
        virtual const char *GetName() const                                         = 0; // 10
        virtual void unk_fn0011()                                                   = 0;
        virtual void unk_fn0012()                                                   = 0;
        virtual void unk_fn0013()                                                   = 0;
        virtual const char *GetFlashFile() const                                    = 0; // 14
        virtual bool Init(bool loadAsset = true)                                    = 0;
        virtual void Unload(bool allInstances = false)                              = 0;
        virtual void Reload(bool allInstances = false)                              = 0; // 17, immediate
        virtual bool IsInit() const                                                 = 0; // 18
        /** Consumed after a later Advance; does not wake an idle lazy UI. */
        virtual void RequestUnload(bool allInstances = false) = 0; // 19
        virtual void unk_fn0020()                             = 0;
        virtual void unk_fn0021()                             = 0;
        virtual void unk_fn0022()                             = 0;
        virtual void Update(float deltaTime)                  = 0; // 23
        virtual void Render()                                 = 0;
        virtual void RenderLockless()                         = 0;
        /** Slot 26. Lets the movie play its own hide transition. */
        virtual void RequestHide()            = 0;
        virtual bool IsHiding() const         = 0; // 27
        virtual void SetVisible(bool visible) = 0;
        virtual bool IsVisible() const        = 0;
        /** Pass one flag per call: the native change test is any-bit, not all-bit. */
        virtual void SetFlag(EFlashUIFlags flag, bool set) = 0; // 30
        virtual bool HasFlag(EFlashUIFlags flag) const     = 0;
        virtual void UpdateFlags()                         = 0; // Reconciles the element-owned cursor reference.
        virtual float GetAlpha() const                     = 0; // 33
        virtual void SetAlpha(float alpha)                 = 0;
        virtual int GetLayer() const                       = 0; // 35
        /** Slot 36. Higher layers compose over lower ones; `hud` sits at 25. */
        virtual void SetLayer(int layer) = 0;
        virtual void unk_fn0037()        = 0;
        virtual void unk_fn0038()        = 0;
        virtual void unk_fn0039()        = 0;
        virtual void unk_fn0040()        = 0;
        virtual void unk_fn0041()        = 0;

        /**
         * Slot 42. Lazily calls Init(true) if no movie exists, then shares it.
         * MSVC member dispatch supplies the native hidden return buffer.
         */
        virtual std::shared_ptr<IFlashPlayer> GetFlashPlayer() = 0;

        virtual void unk_fn0043() = 0;
        /** Slot 44. Borrowed XML descriptor, or null for an undeclared name. */
        virtual const SUIParameterDesc *GetVariableDesc(const char *name) const = 0;
        virtual void unk_fn0045()                                               = 0;
        virtual void unk_fn0046()                                               = 0;
        /** Slot 47. Borrowed XML descriptor, or null for an undeclared name. */
        virtual const SUIParameterDesc *GetArrayDesc(const char *name) const = 0;
        virtual void unk_fn0048()                                            = 0;
        virtual void unk_fn0049()                                            = 0;
        /** Slot 50. Borrowed XML descriptor, or null for an undeclared name. */
        virtual const SUIParameterDesc *GetMovieClipDesc(const char *name) const = 0;
        virtual void unk_fn0051()                                                = 0;
        virtual void unk_fn0052()                                                = 0;
        virtual void unk_fn0053()                                                = 0;
        virtual void unk_fn0054()                                                = 0;
        virtual void unk_fn0055()                                                = 0;
        virtual void unk_fn0056()                                                = 0;
        virtual void unk_fn0057()                                                = 0;
        virtual void unk_fn0058()                                                = 0;
        /** Slot 59. Matches the XML display name or the ActionScript name. */
        virtual const SUIEventDesc *GetFunctionDesc(const char *name) const = 0;
        virtual void unk_fn0060()                                           = 0;
        virtual void unk_fn0061()                                           = 0;
        virtual void UpdateViewPort()                                       = 0; // 62
        virtual void unk_fn0063()                                           = 0;
        virtual void unk_fn0064()                                           = 0; // Not in stock CryEngine.
        virtual void unk_fn0065()                                           = 0; // Serialize
        // 66 and 67 are AddEventListener/RemoveEventListener. They stay
        // unnamed here because CFlashUIElement already calls both through a
        // resolved address: naming them would inherit `virtual` into that
        // class and silently move those calls onto the vtable instead.
        virtual void unk_fn0066() = 0;
        virtual void unk_fn0067() = 0;
        virtual void unk_fn0068() = 0; // CallFunction by descriptor; needs a native SUIArguments.
        virtual void unk_fn0069() = 0; // CallFunction by name; likewise.
        virtual void unk_fn0070() = 0;
        virtual void unk_fn0071() = 0; // GetMovieClip by name; borrowed, cached by the element.
        virtual void unk_fn0072() = 0;
        virtual void unk_fn0073() = 0;
        virtual void unk_fn0074() = 0;
        virtual void unk_fn0075() = 0;
        virtual void unk_fn0076() = 0;
        virtual void unk_fn0077() = 0; // SetVariable; needs a native TUIData.
        virtual void unk_fn0078() = 0;
        virtual void unk_fn0079() = 0;
        virtual void unk_fn0080() = 0;
        virtual void unk_fn0081() = 0;
        virtual void unk_fn0082() = 0;
        virtual void unk_fn0083() = 0;
        virtual void unk_fn0084() = 0;
        virtual void unk_fn0085() = 0;
        virtual void unk_fn0086() = 0;
        virtual void unk_fn0087() = 0;
        virtual void unk_fn0088() = 0;
        virtual void unk_fn0089() = 0;
        virtual void unk_fn0090() = 0;
        virtual void unk_fn0091() = 0;
        virtual void unk_fn0092() = 0;
        /**
         * Slot 93. `px`/`py` are the viewport fraction, not pixels; the result
         * is in stage units. Passing the same storage for input and output is
         * what the engine's own WorldToFlash does.
         */
        virtual void ScreenToFlash(const float &px, const float &py, float &rx, float &ry, bool stageScaleMode) const = 0;
        virtual void unk_fn0094()                                                                                     = 0; // WorldToFlash; its scale argument is not stock.
        virtual void unk_fn0095()                                                                                     = 0; // AddTexture
        virtual void unk_fn0096()                                                                                     = 0; // RemoveTexture
        virtual void unk_fn0097()                                                                                     = 0;
        virtual void unk_fn0098()                                                                                     = 0;
        /** Slot 99. Coordinates are backbuffer pixels; the element maps them. */
        virtual void SendCursorEvent(SFlashCursorEvent::ECursorState state, int x, int y, int button = 0, float wheel = 0.0f) = 0;
    };

    static_assert(sizeof(IUIElement) == 0x8, "IUIElement size mismatch");
} // namespace SDK
