#pragma once

#include "i_action_script_function.h"
#include "i_flash_player_handlers.h"
#include "i_flash_variable_object.h"
#include "s_flash_input_events.h"
#include "s_flash_var_value.h"

namespace SDK {
    enum EFlashVariableArrayType : int {
        FVAT_Int          = 0,
        FVAT_Double       = 1,
        FVAT_Float        = 2,
        FVAT_ConstStrPtr  = 3,
        FVAT_ConstWstrPtr = 4
    };

    /**
     * Scaleform movie interface. Invoke and variable access resolve AS paths.
     * Keep virtual overloads distinctly named to preserve MSVC slot order.
     */
    class IFlashPlayer {
      public:
        enum EScaleModeType : int {
            eSM_NoScale  = 0,
            eSM_ShowAll  = 1,
            eSM_ExactFit = 2,
            eSM_NoBorder = 3
        };
        enum EAlignType : int {
            eAT_Center       = 0,
            eAT_TopCenter    = 1,
            eAT_BottomCenter = 2,
            eAT_CenterLeft   = 3,
            eAT_CenterRight  = 4,
            eAT_TopLeft      = 5,
            eAT_TopRight     = 6,
            eAT_BottomLeft   = 7,
            eAT_BottomRight  = 8
        };

        virtual bool Load(const char *filePath, unsigned int options, unsigned int category)        = 0;
        virtual void unk_fn0001() = 0;
        virtual void SetBackgroundAlpha(float alpha)                                                = 0;
        virtual float GetBackgroundAlpha() const                                                    = 0;
        virtual void SetViewport(int x, int y, int width, int height, float aspectRatio = 1.0f)     = 0;
        virtual void GetViewport(int &x, int &y, int &width, int &height, float &aspectRatio) const = 0;
        virtual void SetViewScaleMode(EScaleModeType scaleMode)                                     = 0;
        virtual EScaleModeType GetViewScaleMode() const                                             = 0;
        virtual void SetViewAlignment(EAlignType alignment)                                         = 0;
        virtual EAlignType GetViewAlignment() const                                                 = 0;
        virtual void SetScissorRect(int x, int y, int width, int height)                            = 0;
        virtual void GetScissorRect(int &x, int &y, int &width, int &height) const                  = 0;
        /** Slot 12. Registered FlashUI elements already advance their movie. */
        virtual void Advance(float deltaTime) = 0;

        /** Slot 13. Uses native render scheduling; not an immediate draw API. */
        virtual void Render()                                              = 0;
        virtual void unk_fn0014() = 0;
        virtual void SetCompositingDepth(float depth)                      = 0;
        virtual void StereoEnforceFixedProjectionDepth(bool enforce)       = 0;
        virtual void StereoSetCustomMaxParallax(float maxParallax = -1.0f) = 0;
        virtual void AvoidStencilClear(bool avoid)                         = 0;
        virtual void EnableMaskedRendering(bool enable)                    = 0;
        virtual void ExtendCanvasToViewport(bool extend)                   = 0;
        virtual void unk_fn0021() = 0;
        virtual void Restart()                                             = 0;
        virtual bool IsPaused() const                                      = 0;
        virtual void Pause(bool pause)                                     = 0;
        /** Player frame indices are zero based; display-object goto is one based. */
        virtual void GotoFrame(unsigned int frame)                       = 0;
        virtual bool GotoLabeledFrame(const char *label, int offset = 0) = 0;
        virtual unsigned int GetCurrentFrame() const                     = 0;
        virtual bool HasLooped() const                                   = 0;
        /** Registered FlashUI elements own these handlers; replacing them bypasses it. */
        virtual void SetFSCommandHandler(IFSCommandHandler *handler, void *userData = nullptr)                 = 0;
        virtual void SetExternalInterfaceHandler(IExternalInterfaceHandler *handler, void *userData = nullptr) = 0;
        virtual void SendCursorEvent(const SFlashCursorEvent &event)                                           = 0;
        virtual void SendKeyEvent(const SFlashKeyEvent &event)                                                 = 0;
        virtual void SendCharEvent(const SFlashCharEvent &event)                                               = 0;
        virtual bool HitTest(float x, float y) const                                                           = 0;
        /** Movie visibility alone does not update FlashUI input/cursor flags. */
        virtual void SetVisible(bool visible) = 0;
        virtual bool GetVisible() const       = 0;
        /** This binary only locks/unlocks; no IME backend operation survives. */
        virtual void SetImeFocus() = 0;

        virtual bool SetVariableObject(const char *path, const IFlashVariableObject *pVarObj) = 0;

        virtual bool SetVariable(const char *path, const SFlashVarValue &value) = 0;

        virtual bool GetVariableObject(const char *path, IFlashVariableObject *&pVarObj) const = 0;

        virtual bool GetVariable(const char *path, SFlashVarValue &value) const = 0;

        virtual bool IsAvailable(const char *path) const = 0;

        virtual bool SetVariableArray(EFlashVariableArrayType type, const char *path, unsigned int index, const void *data, unsigned int count) = 0;
        virtual unsigned int GetVariableArraySize(const char *path) const                                                                       = 0;
        /** String pointers returned in the caller's array remain engine-owned. */
        virtual bool GetVariableArray(EFlashVariableArrayType type, const char *path, unsigned int index, void *data, unsigned int count) const = 0;

        virtual bool Invoke(const char *path, const SFlashVarValue *pArgs, unsigned int numArgs, SFlashVarValue *pResult) = 0;

        /** Native allocations: release each returned handle through slot 0. */
        virtual bool CreateString(const char *text, IFlashVariableObject *&object)                                                           = 0; // 47
        virtual bool CreateStringW(const wchar_t *text, IFlashVariableObject *&object)                                                       = 0;
        virtual bool CreateObject(const char *className, const SFlashVarValue *arguments, unsigned int count, IFlashVariableObject *&object) = 0;
        virtual bool CreateArray(IFlashVariableObject *&object)                                                                              = 0; // 50
        /** Callback is borrowed; it must outlive all AS references to the function. */
        virtual bool CreateFunction(IFlashVariableObject *&object, IActionScriptFunction *callback, void *userData = nullptr) = 0; // 51
        virtual unsigned int GetFrameCount() const                                                                            = 0;
        virtual float GetFrameRate() const                                                                                    = 0;

        /** Stage dimensions, independent of the viewport size. */
        virtual int GetWidth() const = 0;

        virtual int GetHeight() const = 0;

        virtual std::size_t GetMetadata(char *buffer, unsigned int size) const = 0; // 56
        virtual bool HasMetadata(const char *tag) const                        = 0;
        virtual const char *GetFilePath() const                                = 0;
        virtual void ResetDirtyFlags()                                         = 0;
        /** These only translate the viewport origin; they do not undo stage scaling. */
        virtual void ScreenToClient(int &x, int &y) const = 0; // 60
        virtual void ClientToScreen(int &x, int &y) const = 0;

        bool Call(const char *path) {
            return Invoke(path, nullptr, 0, nullptr);
        }

        /** Compare resolved addresses and native/virtual dispatch results. */
        bool VerifySlots() const;
    };
} // namespace SDK
