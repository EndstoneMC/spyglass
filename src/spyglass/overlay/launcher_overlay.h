#pragma once

namespace spyglass::overlay {

/** Draws the overlay inside the launcher's own ImGui frame. Throws if the client cannot host it. */
void install_launcher_overlay();

}  // namespace spyglass::overlay
