#include <gtest/gtest.h>
#include <core/core.h>
#include <cryptalgo/Random.h>
#include <cerrno>
#include <cstdarg>
#include <fcntl.h>
#include <signal.h>

namespace {
// The compiler cannot see the linker's malloc/realloc interposition.
volatile bool failAllocation = false;
volatile bool failEntropy = false;
}

extern "C" {
void* __real_malloc(size_t);
void* __real_realloc(void*, size_t);
int __real_open(const char*, int, ...);
ssize_t __real_read(int, void*, size_t);

// PUBLIC_INTERFACE
/** Link-time test shim: fail only allocations explicitly armed by a test. */
void* __wrap_malloc(size_t size)
{
    return failAllocation ? nullptr : __real_malloc(size);
}
// PUBLIC_INTERFACE
/** Link-time test shim preserving the original allocation on forced failure. */
void* __wrap_realloc(void* pointer, size_t size)
{
    return failAllocation ? nullptr : __real_realloc(pointer, size);
}
// PUBLIC_INTERFACE
/** Link-time test shim rejecting only the entropy device when armed. */
int __wrap_open(const char* path, int flags, ...)
{
    if (failEntropy && strcmp(path, "/dev/urandom") == 0) {
        errno = EIO;
        return -1;
    }
    if ((flags & O_CREAT) != 0) {
        va_list arguments;
        va_start(arguments, flags);
        const mode_t mode = static_cast<mode_t>(va_arg(arguments, int));
        va_end(arguments);
        return __real_open(path, flags, mode);
    }
    return __real_open(path, flags);
}
// PUBLIC_INTERFACE
/** Forward normal reads; entropy-open failure is the injected boundary. */
ssize_t __wrap_read(int descriptor, void* buffer, size_t size)
{
    return __real_read(descriptor, buffer, size);
}
}

TEST(FrameAllocationP1, FailedGrowthPreservesState)
{
    Thunder::Core::FrameType<1> frame;
    ASSERT_EQ(frame.SetNumber<uint8_t>(0, 42), 1u);
    const auto* original = frame.Data();
    failAllocation = true;
    frame.Size(5000);
    const auto size = frame.Size();
    const auto* buffer = frame.Data();
    const auto byte = frame[0];
    failAllocation = false;
    EXPECT_EQ(size, 1u);
    EXPECT_EQ(buffer, original);
    EXPECT_EQ(byte, 42);
}

TEST(FrameAllocationP1, FailedCopyRemainsEmpty)
{
    Thunder::Core::FrameType<1> frame;
    frame.SetNumber<uint8_t>(0, 42);
    const auto& source = frame;
    failAllocation = true;
    Thunder::Core::FrameType<1> copy(source);
    failAllocation = false;
    EXPECT_EQ(copy.Size(), 0u);
    EXPECT_EQ(frame[0], 42);
}

TEST(EntropyP1, ProviderFailureCannotFallBack)
{
    EXPECT_EXIT({
        failEntropy = true;
        uint64_t value = 0;
        Thunder::Crypto::Random(value);
        _exit(0);
    }, ::testing::KilledBySignal(SIGABRT), "");
}
