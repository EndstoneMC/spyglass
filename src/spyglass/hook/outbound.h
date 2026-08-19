#pragma once

namespace spyglass::hook {

/**
 * Starts reporting the packets the client sends as well as the ones it reads. Off until asked for:
 * it rewrites a vtable entry in every packet class, which is worth doing only while you are looking.
 * Throws when the client does not look the way this expects, rather than patching it anyway.
 */
void install_outbound_hook();
bool outbound_installed();

}  // namespace spyglass::hook
