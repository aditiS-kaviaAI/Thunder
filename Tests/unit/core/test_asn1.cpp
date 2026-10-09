// Regression coverage for ISS-006, ISS-008 and ISS-009; no fork harness is needed.
#define MODULE_NAME ThunderUnitTests
#include <gtest/gtest.h>
#include <core/ASN1.h>
#include <initializer_list>
#include <vector>

using namespace WPEFramework::Core;

namespace {
    /** Copy literal test bytes into ASN.1 storage and return its shared owner. */
    ASN1::Buffer MakeBuffer(std::initializer_list<uint8_t> bytes)
    {
        ASN1::Buffer buffer(static_cast<uint16_t>(bytes.size()));
        uint16_t index = 0;
        for (const uint8_t byte : bytes) {
            buffer[index++] = byte;
        }
        return buffer;
    }
}

TEST(ASN1Issues, SharedOwnershipBeyondByteCounter)
{
    ASN1::Buffer survivor;
    {
        ASN1::Buffer original(1024);
        ASSERT_EQ(original.Size(), 1024u);
        original[1023] = 0xA5;
        // More than 255 copies formerly wrapped the in-band ownership counter.
        std::vector<ASN1::Buffer> owners(300, original);
        survivor = owners.back();
        owners.erase(owners.begin(), owners.begin() + 256);
        EXPECT_EQ(survivor[1023], 0xA5);
        original.Size(512);
        EXPECT_EQ(survivor.Size(), 512u);
        original.Size(1025);
        EXPECT_EQ(original.Size(), 512u);
        original.Size(1024);
        original = original;
    }
    EXPECT_EQ(survivor[1023], 0xA5);
    ASN1::Buffer empty;
    empty.Size(0);
    empty.Size(1);
    EXPECT_EQ(empty.Size(), 0u);
    // Invalid indexing must not become an out-of-bounds reference in release builds.
    EXPECT_DEATH(static_cast<void>(empty[0]), "");
}

TEST(ASN1Issues, OidProgressPackedArcsAndZero)
{
    const uint8_t bytes[] = {0x88, 0x37, 0x00, 0x81, 0x00, 0x83, 0xFF, 0x7F};
    ASN1::OID oid(bytes, sizeof(bytes));
    EXPECT_EQ(oid.Text(), "2.999.0.128.65535");
    auto iterator = oid.Elements();
    EXPECT_EQ(iterator.Count(), 5u);
    for (uint16_t arc : {2, 999, 0, 128, 65535}) {
        ASSERT_TRUE(iterator.Next());
        EXPECT_EQ(iterator.Number(), arc);
    }
    EXPECT_FALSE(iterator.Next());
    EXPECT_FALSE(iterator.Next());
    iterator.Reset();
    ASSERT_TRUE(iterator.Next());
    EXPECT_EQ(iterator.Number(), 2u);
    ASN1::OID textual("2.999.0.128.65535");
    EXPECT_EQ(textual, oid);
    EXPECT_EQ(ASN1::OID("0.0").Text(), "0.0");
    EXPECT_EQ(ASN1::OID("1.2.0x80").Text(), "1.2.128");
    for (const char* invalid : {"", "1", "3.0", "1.40", "1..2", "2.65536", "2.0.", "0x.0"}) {
        EXPECT_EQ(ASN1::OID(string(invalid)).Length(), 0u) << invalid;
    }
}

TEST(ASN1Issues, OidRejectsTruncatedAndOverflowingArcs)
{
    const uint8_t truncated[] = {0x2A, 0x81};
    ASN1::OID oid(truncated, sizeof(truncated));
    EXPECT_EQ(oid.Text(), "1.2");
    EXPECT_EQ(oid.Elements().Count(), 2u);
    const uint8_t missingFirst[] = {0x80};
    EXPECT_EQ(ASN1::OID(missingFirst, sizeof(missingFirst)).Text(), "");
    const uint8_t oversized[] = {0x2A, 0x84, 0x80, 0x00};
    EXPECT_EQ(ASN1::OID(oversized, sizeof(oversized)).Text(), "1.2");
    ASN1::OID::Iterator empty;
    EXPECT_FALSE(empty.Next());
    EXPECT_EQ(empty.Count(), 0u);
}

TEST(ASN1Issues, SequenceExtentsAndProgress)
{
    for (const auto& bytes : {std::vector<uint8_t>{}, {0x02}, {0x02, 0x02, 0x01},
             {0x02, 0x81, 0x01, 0x01}, {0x02, 0x80}}) {
        ASN1::Buffer buffer(static_cast<uint16_t>(bytes.size()));
        for (size_t index = 0; index < bytes.size(); ++index) {
            buffer[index] = bytes[index];
        }
        ASN1::Sequence sequence(buffer);
        EXPECT_FALSE(sequence.Next());
        EXPECT_FALSE(sequence.Next());
        uint32_t value = 0xA5;
        EXPECT_EQ(sequence.Value(value), ASN1::ASN1_OUT_OF_DATA);
        EXPECT_EQ(value, 0xA5u);
    }
    // Empty values are complete TLVs, but are not valid scalar integer payloads.
    ASN1::Sequence sequence(MakeBuffer({0x02, 0, 0x02, 1, 7, 0x0C, 0}));
    ASSERT_TRUE(sequence.Next());
    uint32_t number = 42;
    EXPECT_EQ(sequence.Value(number), ASN1::ASN1_INVALID_LENGTH);
    EXPECT_EQ(number, 42u);
    ASSERT_TRUE(sequence.Next());
    EXPECT_EQ(sequence.Value(number), ASN1::ASN1_OK);
    EXPECT_EQ(number, 7u);
    bool wrongTag = false;
    EXPECT_EQ(sequence.Value(wrongTag), ASN1::ASN1_UNEXPECTED_TAG);
    ASSERT_TRUE(sequence.Next());
    string text = "unchanged";
    EXPECT_EQ(sequence.Value(text), ASN1::ASN1_OK);
    EXPECT_TRUE(text.empty());
    EXPECT_EQ(sequence.Data(), nullptr);
    EXPECT_FALSE(sequence.Next());
}

TEST(ASN1Issues, SequenceNestedSlicesAndSharedResize)
{
    auto buffer = MakeBuffer({0x30, 3, 0x02, 1, 7, 0x02, 1, 9});
    ASN1::Sequence outer(buffer);
    ASSERT_TRUE(outer.Next());
    ASN1::Sequence child;
    ASSERT_EQ(outer.Value(child), ASN1::ASN1_OK);
    ASSERT_TRUE(child.Next());
    uint32_t value = 0;
    EXPECT_EQ(child.Value(value), ASN1::ASN1_OK);
    EXPECT_EQ(value, 7u);
    EXPECT_FALSE(child.Next());
    child.Reset();
    ASSERT_TRUE(child.Next());
    ASSERT_TRUE(outer.Next());
    EXPECT_EQ(outer.Value(value), ASN1::ASN1_OK);
    EXPECT_EQ(value, 9u);
    EXPECT_FALSE(outer.Next());
    ASN1::Sequence invalidSlice(buffer, 7, 3);
    EXPECT_FALSE(invalidSlice.Next());
    // Shared size changes cannot leave an iterator exposing a now-truncated TLV.
    buffer.Size(3);
    EXPECT_FALSE(child.IsValid());
    EXPECT_EQ(child.Value(value), ASN1::ASN1_OUT_OF_DATA);
    EXPECT_EQ(value, 9u);
}
