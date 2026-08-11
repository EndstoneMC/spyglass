#include "spyglass/hook/fault.h"

namespace spyglass {

FaultInjection &faults()
{
    static FaultInjection faults;
    return faults;
}

}  // namespace spyglass
