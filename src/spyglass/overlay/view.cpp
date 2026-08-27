#include "spyglass/overlay/view.h"

#include <imgui.h>

namespace spyglass::overlay {

void View::draw()
{
    if (!visible_) {
        return;
    }

    ImGui::Begin("spyglass", &visible_);
    ImGui::End();
}

}  // namespace spyglass::overlay
