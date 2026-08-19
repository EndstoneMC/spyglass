#pragma once

#include <string>
#include <string_view>

namespace spyglass::overlay {

/**
 * Hands `text` to the user under the name `filename`, and says where it went. The clipboard is
 * the whole story on Windows. Under the Linux launcher it owns an X11 selection that a Wayland
 * session does not necessarily see, so the text is written out as well and that path is what
 * gets reported.
 */
std::string offer(std::string_view filename, const std::string &text);

}  // namespace spyglass::overlay
