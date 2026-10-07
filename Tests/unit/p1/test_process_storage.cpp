#include <gtest/gtest.h>
#include "../../../Source/Thunder/ProcessStorage.h"

TEST(ProcessStorageP1, IndependentSlotsAndFailureRecovery)
{
    std::map<int, void*> storage;
    int parent = 1, first = 2, second = 3;
    const auto checkpoint = [&](int pid, int* state) {
        return Thunder::PluginHost::Detail::WithProcessStorage(storage, pid,
            [&](void** slot) -> uint32_t {
                EXPECT_EQ(*slot, nullptr);
                *slot = state;
                return 0;
            }, false);
    };
    EXPECT_EQ(checkpoint(1, &parent), 0u);
    EXPECT_EQ(checkpoint(2, &first), 0u);
    // A failed child operation must not overwrite the parent or sibling.
    EXPECT_EQ(Thunder::PluginHost::Detail::WithProcessStorage(storage, 3,
        [&](void** slot) -> uint32_t { *slot = &second; return 1; }, false), 1u);
    EXPECT_EQ(storage[1], &parent);
    EXPECT_EQ(storage[2], &first);
    EXPECT_EQ(storage[3], &second);
    EXPECT_EQ(Thunder::PluginHost::Detail::WithProcessStorage(storage, 2,
        [&](void** slot) -> uint32_t {
            EXPECT_EQ(*slot, &first);
            return 1;
        }, true), 1u);
    EXPECT_EQ(storage[2], &first);
    for (int pid : {3, 2, 1}) {
        EXPECT_EQ(Thunder::PluginHost::Detail::WithProcessStorage(storage, pid,
            [&](void** slot) -> uint32_t {
                EXPECT_NE(*slot, nullptr);
                *slot = nullptr;
                return 0;
            }, true), 0u);
    }
    EXPECT_TRUE(storage.empty());
}
