#pragma once

#include <cstdint>
#include <map>

namespace Thunder {
namespace PluginHost {
namespace Detail {

// PUBLIC_INTERFACE
template <typename PID, typename OPERATION>
uint32_t WithProcessStorage(std::map<PID, void*>& storage, const PID pid,
    OPERATION operation, const bool releaseEmpty)
{
    /** Invoke a backend with its PID-specific storage slot.
     * The caller must serialize access. Failed operations retain their slot
     * for recovery; successful wakeup removes only an emptied slot.
     */
    void*& slot = storage[pid];
    const uint32_t result = operation(&slot);
    if (releaseEmpty && (result == 0) && (slot == nullptr)) {
        storage.erase(pid);
    }
    return result;
}

}
}
}
