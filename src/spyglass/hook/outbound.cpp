#include "spyglass/hook/outbound.h"

#ifdef _WIN32
#include <stdexcept>

namespace spyglass::hook {

// Windows injects into the client directly and has no launcher between the two, so the send
// path there wants a different approach than rewriting the client's vtables.
void install_outbound_hook()
{
    throw std::runtime_error{"watching sends is only built for the Linux launcher"};
}

bool outbound_installed()
{
    return false;
}

}  // namespace spyglass::hook

#else

#include <cstdint>
#include <format>
#include <stdexcept>
#include <unordered_map>

#include <sys/mman.h>
#include <unistd.h>

#include "bedrock/network/packet.h"
#include "spyglass/core/log.h"
#include "spyglass/hook/packet.h"
#include "spyglass/hook/vtables.h"

namespace spyglass::hook {
namespace {

/**
 * Packet's virtuals run: the two destructors, getId, getName, the size ceiling, checkSize, then
 * write and its no-argument forwarder, and the read that readNoHeader dispatches through. Only the
 * read slot is known for certain, from the call readNoHeader makes, so it is what the numbering is
 * checked against before anything is written.
 */
constexpr std::size_t kWriteSlot = 7;
constexpr std::size_t kReadSlot = 9;
constexpr std::size_t kSlotsNeeded = 10;

std::unordered_map<void **, void *> g_originals;
bool g_installed = false;

void restore_all();

/** Small integers arrive in the same registers as pointers, so they are ruled out before any
 *  argument is followed. */
bool plausible(const void *pointer)
{
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    return address > 0x10000 && (address & (alignof(void *) - 1)) == 0;
}

bool in_client(const void *pointer)
{
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    return address >= client_base() && address < client_limit();
}

/**
 * Every integer argument is carried across untouched and whatever the original returned is handed
 * back, so being wrong about the slot costs a useless hook rather than a corrupted call.
 */
void *dispatch(void *a0, void *a1, void *a2, void *a3, void *a4, void *a5)
{
    void **vtable = nullptr;
    const Packet *packet = nullptr;

    // Which argument is the packet depends on whether the slot returns something large enough to
    // need a hidden pointer, so it is identified by its vtable rather than by position.
    for (auto *candidate : {a0, a1}) {
        if (!plausible(candidate) || !in_client(*reinterpret_cast<void **>(candidate))) {
            continue;
        }
        auto **table = *reinterpret_cast<void ***>(candidate);
        if (const auto it = g_originals.find(table); it != g_originals.end()) {
            vtable = table;
            packet = reinterpret_cast<const Packet *>(candidate);
            break;
        }
    }

    if (vtable == nullptr) {
        // Nothing here is the packet, so the slot is not what it was taken to be and there is no
        // original to hand this call on to. One send is lost, and the patch comes straight back
        // out rather than spoiling every send after it.
        log::error("outbound: slot {} is not the write function, undoing the patch", kWriteSlot);
        restore_all();
        return nullptr;
    }

    note_outbound(*packet);

    auto *original = reinterpret_cast<void *(*)(void *, void *, void *, void *, void *, void *)>(g_originals[vtable]);
    return original(a0, a1, a2, a3, a4, a5);
}

void restore_all()
{
    for (auto &[vtable, original] : g_originals) {
        vtable[kWriteSlot] = original;
    }
    g_originals.clear();
    g_installed = false;
}

void make_writable(void *address)
{
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    auto start = reinterpret_cast<std::uintptr_t>(address) & ~(page - 1);
    // A vtable entry can straddle nothing, but the two pages are cheap to cover.
    if (mprotect(reinterpret_cast<void *>(start), page * 2, PROT_READ | PROT_WRITE) != 0) {
        throw std::runtime_error{"cannot make the client's vtables writable"};
    }
}

}  // namespace

void install_outbound_hook()
{
    if (g_installed) {
        return;
    }

    const auto &classes = packet_classes();
    if (classes.empty()) {
        throw std::runtime_error{"found no packet classes in the client"};
    }

    // The read slot has to hold a real function everywhere before the numbering is trusted, since
    // that is the one slot readNoHeader proves the position of.
    std::size_t usable = 0;
    for (const auto &entry : classes) {
        bool complete = true;
        for (std::size_t slot = 0; slot < kSlotsNeeded; ++slot) {
            if (!in_client(entry.functions[slot])) {
                complete = false;
                break;
            }
        }
        if (complete && in_client(entry.functions[kReadSlot])) {
            ++usable;
        }
    }

    if (usable * 2 < classes.size()) {
        throw std::runtime_error{
            std::format("only {} of {} packet vtables look the expected shape, refusing to patch", usable,
                        classes.size())};
    }

    std::size_t patched = 0;
    for (const auto &entry : classes) {
        bool complete = true;
        for (std::size_t slot = 0; slot < kSlotsNeeded; ++slot) {
            if (!in_client(entry.functions[slot])) {
                complete = false;
                break;
            }
        }
        if (!complete || g_originals.contains(entry.functions)) {
            continue;
        }

        make_writable(&entry.functions[kWriteSlot]);
        g_originals.emplace(entry.functions, entry.functions[kWriteSlot]);
        entry.functions[kWriteSlot] = reinterpret_cast<void *>(&dispatch);
        ++patched;
    }

    g_installed = true;
    log::info("watching outbound packets, {} of {} vtables patched", patched, classes.size());
}

bool outbound_installed()
{
    return g_installed;
}

}  // namespace spyglass::hook

#endif
