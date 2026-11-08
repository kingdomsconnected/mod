#include "c_flash_ui_element.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <array>
#include <string>
#include <string_view>

namespace SDK {
    void CFlashUIElement::AddEventListener(IUIElementEventListener *listener, const char *name) {
        hook::this_call<void>(gPatterns.CFlashUIElement__AddEventListener, this, listener, name);
    }

    void CFlashUIElement::RemoveEventListener(IUIElementEventListener *listener) {
        hook::this_call<void>(gPatterns.CFlashUIElement__RemoveEventListener, this, listener);
    }

    bool CFlashUIElement::GetLoadedMovieClip(const char *name, FlashVariableObjectRef &clip) {
        clip.Reset();
        IFlashPlayer *movie = GetLoadedFlashPlayer();
        if (movie == nullptr) {
            return false;
        }

        // XML descriptors survive movie reloads; display objects do not.
        // Resolve the descriptor path in this movie, never FlashUI's cache.
        std::array<const char *, 8> names {};
        std::size_t depth = 0;
        for (const SUIParameterDesc *descriptor = GetMovieClipDesc(name); descriptor != nullptr; descriptor = descriptor->GetParent()) {
            if (depth == names.size() || descriptor->GetName() == nullptr || descriptor->GetName()[0] == '\0') {
                return false;
            }
            names[depth++] = descriptor->GetName();
        }
        if (depth == 0) {
            return false;
        }
        std::string path = std::string_view(names[depth - 1]) == "_root" ? "" : "_root";
        while (depth != 0) {
            if (!path.empty()) {
                path.push_back('.');
            }
            path += names[--depth];
        }
        return movie->GetVariableObject(path.c_str(), clip.Receive()) && clip && clip->IsDisplayObject();
    }
} // namespace SDK
