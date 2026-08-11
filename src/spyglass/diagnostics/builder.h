#pragma once

#include <cstddef>
#include <system_error>

#include "bedrock/core/utility/binary_stream.h"
#include "bedrock/network/packet.h"
#include "bedrock/platform/check.h"
#include "spyglass/diagnostics/diagnostic.h"

namespace spyglass {

/**
 * Snapshots a finished packet read. `error` is null when the read succeeded but
 * left bytes behind. Every string is copied out of the client's heap, never
 * retained, because the packet and its stream die with the caller's frame.
 */
Diagnostic build(const Packet &packet, const ReadOnlyBinaryStream &stream, std::size_t body_begin,
                 const Bedrock::ErrorInfo<std::error_code> *error);

}  // namespace spyglass
