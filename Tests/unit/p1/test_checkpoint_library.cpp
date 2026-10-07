#include <gtest/gtest.h>
#include "../../../Source/addons/hibernate/hibernate.h"

TEST(CheckpointLibraryP1, UnsupportedOperationsNeverReportSuccess)
{
    int sentinel = 42;
    void* storage = &sentinel;
    EXPECT_EQ(HibernateProcess(50, 123, "", "", &storage), HIBERNATE_ERROR_GENERAL);
    EXPECT_EQ(storage, &sentinel);
    EXPECT_EQ(WakeupProcess(50, 123, "", "", &storage), HIBERNATE_ERROR_GENERAL);
    EXPECT_EQ(storage, &sentinel);
}
