#ifndef MODULE_NAME
#include "../Module.h"
#endif
#include <gtest/gtest.h>
#include <core/core.h>
#include <processcontainers/IProcessContainers.h>
#include <fcntl.h>
#include <unistd.h>

namespace {
std::string metric;
ssize_t readResult = 0;
static int MetricOpen(const char*, int) { return 123; }
static int MetricClose(int) { return 0; }
static ssize_t MetricRead(int, void* output, size_t capacity)
{
    if (readResult <= 0) return readResult;
    const size_t length = std::min(capacity, metric.size());
    memcpy(output, metric.data(), length);
    return static_cast<ssize_t>(length);
}
}

// Intercept only this header's collector calls; production syscalls stay unchanged.
#define open MetricOpen
#define read MetricRead
#define close MetricClose
#include <processcontainers/common/CGroupContainerInfo.h>
#undef close
#undef read
#undef open

TEST(CGroupP1, FailedAndEmptyReads)
{
    Thunder::ProcessContainers::CGroupMetrics metrics("fixture");
    for (ssize_t result : { ssize_t(-1), ssize_t(0) }) {
        readResult = result;
        auto* memory = metrics.Memory();
        EXPECT_EQ(memory->Allocated(), UINT64_MAX);
        EXPECT_EQ(memory->Resident(), UINT64_MAX);
        memory->Release();
        auto* cpu = metrics.ProcessorInfo();
        EXPECT_EQ(cpu->NumberOfCores(), 0u);
        cpu->Release();
    }
}

TEST(CGroupP1, FullWidthAndMalformedCounters)
{
    Thunder::ProcessContainers::CGroupMetrics metrics("fixture");
    readResult = 1;
    metric = "2147483648 4294967296";
    auto* cpu = metrics.ProcessorInfo();
    ASSERT_EQ(cpu->NumberOfCores(), 2u);
    EXPECT_EQ(cpu->CoreUsage(0), UINT64_C(2147483648));
    EXPECT_EQ(cpu->CoreUsage(1), UINT64_C(4294967296));
    cpu->Release();
    metric = "18446744073709551616";
    auto* memory = metrics.Memory();
    EXPECT_EQ(memory->Allocated(), UINT64_MAX);
    memory->Release();
    metric.assign(2048, '9');
    cpu = metrics.ProcessorInfo();
    EXPECT_EQ(cpu->NumberOfCores(), 0u);
    cpu->Release();
}
