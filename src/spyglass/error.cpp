#include "spyglass/error.h"

#include <mutex>
#include <utility>

namespace spyglass {
namespace {

std::mutex g_mutex;
std::vector<std::string> g_errors;

}  // namespace

void report_error(std::string message)
{
    const std::lock_guard lock{g_mutex};
    g_errors.push_back(std::move(message));
}

std::vector<std::string> errors()
{
    const std::lock_guard lock{g_mutex};
    return g_errors;
}

}  // namespace spyglass
