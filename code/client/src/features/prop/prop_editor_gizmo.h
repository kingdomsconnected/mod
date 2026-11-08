#pragma once

// imgui.h first: ImGuizmo.h uses its types without including it.
#include <imgui.h>

#include <ImGuizmo.h>

namespace KCDC::Features::Prop {
    /**
     * Ends a gizmo drag whose mouse-up no Manipulate call will see: the
     * object it was moving streamed out, or the editor closed, so the gizmo
     * is no longer drawn. ImGuizmo has no call to end a drag, and disabling
     * it is the one that clears its drag state.
     */
    inline void FinishReleasedGizmoDrag(bool closed = false) {
        if (ImGuizmo::IsUsingAny() && (closed || !ImGui::GetIO().MouseDown[ImGuiMouseButton_Left])) {
            ImGuizmo::Enable(false);
            ImGuizmo::Enable(true);
        }
    }
} // namespace KCDC::Features::Prop
