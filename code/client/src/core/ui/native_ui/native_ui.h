#pragma once

#include "sdk/cryengine/scaleform/flash_variable_object_ref.h"
#include "sdk/cryengine/scaleform/i_flash_player.h"
#include "sdk/cryengine/scaleform/s_flash_cxform.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace KCDC::Core {
    class NativeUIHost;

    /**
     * Game-thread composition owner for one subtree in a native movie.
     * Find handles are borrowed until key replacement or page invalidation.
     * Imported classes and linkage exports must already exist in the movie.
     */
    class NativeUI final {
      public:
        NativeUI(KCDC::Core::NativeUIHost &host, std::shared_ptr<SDK::IFlashPlayer> movie): _host(&host), _movie(std::move(movie)) {}
        ~NativeUI();
        NativeUI(const NativeUI &)            = delete;
        NativeUI &operator=(const NativeUI &) = delete;

        bool Mount(const char *parentPath, const std::string &instanceName);
        bool Unmount();
        /** Unload notification: release handles without re-entering ActionScript. */
        void Invalidate();
        /** The host is going: invalidate, and stop referring to it. */
        void Detach();
        SDK::IFlashVariableObject *Find(const std::string &key) const;
        SDK::IFlashVariableObject *Root() const {
            return _root.Get();
        }
        bool CreateClip(const std::string &key, const std::string &parentKey, const char *instanceName, const char *exportName = nullptr, const std::string &initKey = {});
        /** How a text field is set; anything left alone keeps the field's own. */
        struct TextStyle {
            /**
             * A name out of `Libs/UI/fontconfig.xml` -- DefaultFont,
             * DefaultFontBold, LightFont, DisplayFont, Manuscript -- not a
             * face. The engine installs that map and its glyph library on the
             * shared loader, so every movie resolves them, ours included. A
             * field with no font falls back to a device font with no glyphs
             * and draws empty boxes.
             */
            std::string font = "DefaultFont";
            double size      = 20.0;
            /** 0xRRGGBB. */
            int color   = 0xFFFFFF;
            bool bold   = false;
            bool italic = false;
            /** left, center, right or justify; empty leaves the field's own. */
            std::string align;
        };

        /** Create a native text field; coordinates are in its parent's space. */
        bool CreateTextField(const std::string &key, const std::string &parentKey, const char *instanceName, double x, double y, double width, double height);
        /** Applies a format to what the field holds and what it is given next. */
        bool SetTextStyle(const std::string &receiverKey, const TextStyle &style);
        /**
         * Binds `maskKey` as `receiverKey`'s mask, or clears it when empty.
         * Composition hosts only: the host's own KCDC_SetMask does the work,
         * because the native setter takes no self-binding rejection and one
         * mask may own only one target.
         */
        bool SetMask(const std::string &receiverKey, const std::string &maskKey);
        /** Binds `areaKey` as `receiverKey`'s hit area, or clears it when empty. */
        bool SetHitArea(const std::string &receiverKey, const std::string &areaKey);
        /**
         * Loads an external movie or image into a clip, replacing its content.
         * `img://` and `imgps://` reach the engine's image loader; a missing
         * asset installs a placeholder rather than failing.
         */
        bool LoadClip(const std::string &receiverKey, const char *url);
        bool CreateObject(const std::string &key, const char *className, const SDK::SFlashVarValue *arguments = nullptr, unsigned int count = 0);
        bool Construct(const std::string &key, const char *className, const std::vector<std::string> &argumentKeys);
        bool CallFactory(const std::string &key, const std::string &receiverKey, const char *method, const std::vector<std::string> &argumentKeys);
        bool CreateArray(const std::string &key);
        /** Box scalars in native wrappers for mixed object/scalar InvokeObjects. */
        bool CreateValue(const std::string &key, const SDK::SFlashVarValue &value);
        bool CaptureMember(const std::string &key, const std::string &parentKey, const char *member);
        /** Timeline frame changes can replace a named child with a new object. */
        bool RefreshMember(const std::string &key, const std::string &parentKey, const char *member);
        bool CaptureGlobal(const std::string &key, const char *path);
        /** Retain a callback value from this movie, optionally replacing a key. */
        bool CaptureArgument(const std::string &key, const SDK::IActionScriptFunction::Params &params, unsigned int index, bool replace = false);
        bool CreateCallback(const std::string &key, std::function<void(const SDK::IActionScriptFunction::Params &, SDK::IActionScriptFunction::IReturnValue *)> callback);
        bool AcceptsCallback(void *playerIdentity) const {
            return !_closing && _root && _movie.get() == playerIdentity;
        }
        bool SetObject(const std::string &receiverKey, const char *member, const std::string &valueKey);
        bool SetValue(const std::string &receiverKey, const char *member, const SDK::SFlashVarValue &value);
        bool DeleteMember(const std::string &receiverKey, const char *member);
        /** Apply only flagged native display fields; scale/alpha are percentages. */
        bool SetDisplayInfo(const std::string &receiverKey, const SDK::SFlashDisplayInfo &info);
        /** `SetDisplayInfo` with only the visibility flagged. */
        bool SetVisible(const std::string &receiverKey, bool visible);
        /** Returns a cleared value on failure; presence bits describe valid fields. */
        bool GetDisplayInfo(const std::string &receiverKey, SDK::SFlashDisplayInfo &info) const;
        /** RGBA multipliers and unsigned additive bytes; affects timeline updates. */
        bool SetColorTransform(const std::string &receiverKey, const SDK::SFlashCxform &transform);
        /** Local transform only; returns identity on failure, offsets narrow to bytes. */
        bool GetColorTransform(const std::string &receiverKey, SDK::SFlashCxform &transform) const;
        /** Native text field path, or text/htmlText property fallback on clips. */
        bool SetText(const std::string &receiverKey, const char *text, bool html = false);
        /** Copies native narrow text into mod-owned storage; clears on failure. */
        bool GetText(const std::string &receiverKey, std::string &text, bool html = false) const;
        /** One-based frame; native clamping applies. Child handles may change. */
        bool GotoFrame(const std::string &receiverKey, unsigned int frame, bool play = false);
        bool GotoLabel(const std::string &receiverKey, const char *label, bool play = false);
        /** Result strings are borrowed until the next Invoke or invalidation. */
        bool Invoke(const std::string &receiverKey, const char *method, const std::vector<std::string> &argumentKeys, SDK::SFlashVarValue *result = nullptr);
        bool InvokeScalars(const std::string &receiverKey, const char *method, const std::vector<SDK::SFlashVarValue> &arguments, SDK::SFlashVarValue *result = nullptr);
        /** Register in acquisition order; teardown calls run in reverse order. */
        bool AddCleanup(const std::string &receiverKey, const char *method, const std::vector<std::string> &argumentKeys = {});

        bool HasObject(const std::string &key) const {
            return key == "$root" ? static_cast<bool>(_root) : _objects.find(key) != _objects.end();
        }
        /** Drops one handle, and with it the display child it was created as. */
        bool RemoveObject(const std::string &key);
        /** Reads one scalar member; a string result is copied into `text`. */
        bool GetValue(const std::string &receiverKey, const char *member, SDK::SFlashVarValue &value, std::string &text) const;

      private:
        /** What a created display child needs to be taken back out again. */
        struct Child {
            std::string parentKey;
            std::string instanceName;
            bool textField = false;
        };

        struct Cleanup {
            SDK::FlashVariableObjectRef receiver;
            std::string method;
            std::vector<SDK::FlashVariableObjectRef> arguments;
        };

        bool CanCreate(const std::string &key) const;
        /** The movie's own `_root`, captured once, for the host's helpers. */
        SDK::IFlashVariableObject *MovieRoot();
        bool CallHostHelper(const char *helper, const std::string &receiverKey, const std::string &argumentKey);
        bool CallBridge(const std::string &key, SDK::IFlashVariableObject *receiver, const char *name, const std::vector<std::string> &argumentKeys);
        KCDC::Core::NativeUIHost *_host = nullptr;
        std::shared_ptr<SDK::IFlashPlayer> _movie;
        SDK::FlashVariableObjectRef _parent;
        SDK::FlashVariableObjectRef _root;
        SDK::FlashVariableObjectRef _lastResultReceiver;
        std::string _instanceName;
        std::unordered_map<std::string, SDK::FlashVariableObjectRef> _objects;
        std::unordered_map<std::string, Child> _children;
        std::vector<Cleanup> _cleanup;
        std::vector<std::uintptr_t> _callbackIds;
        bool _closing = false;
    };
} // namespace KCDC::Core
