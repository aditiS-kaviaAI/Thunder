// Regression coverage for ISS-011 and ISS-020 uses current production entrypoints.
#define MODULE_NAME ThunderUnitTests
#include <gtest/gtest.h>
#include <websocket/URL.h>
#include <cryptalgo/AES.h>
#include <algorithm>
#include <array>
#include <cstring>

using namespace WPEFramework;

TEST(URLIssues, LengthDelimitedInput)
{
    char output[32] = {};
    EXPECT_EQ(Core::URL::Encode(nullptr, 0, output, sizeof(output)), 0u);
    EXPECT_EQ(Core::URL::Decode(nullptr, 0, output, sizeof(output)), 0u);
    EXPECT_EQ(Core::URL::Encode(nullptr, 0, nullptr, 0), 0u);
    EXPECT_EQ(Core::URL::Decode(nullptr, 0, nullptr, 0), 0u);
    // These arrays intentionally have no trailing NUL; ASan detects any lookahead past them.
    const char plain[] = {'A', ' ', 'B'};
    EXPECT_EQ(Core::URL::Encode(plain, sizeof(plain), output, sizeof(output)), 3u);
    EXPECT_STREQ(output, "A+B");
    const char encoded[] = {'%', '4', '1', '+'};
    EXPECT_EQ(Core::URL::Decode(encoded, sizeof(encoded), output, sizeof(output)), 2u);
    EXPECT_STREQ(output, "A ");
    const char percent[] = {'%'};
    EXPECT_EQ(Core::URL::Decode(percent, sizeof(percent), output, sizeof(output)), 1u);
    EXPECT_STREQ(output, "%");
    const char partial[] = {'%', '4'};
    EXPECT_EQ(Core::URL::Decode(partial, sizeof(partial), output, sizeof(output)), 2u);
    EXPECT_STREQ(output, "%4");
    const char nonhex[] = {'%', 'G', '0'};
    EXPECT_EQ(Core::URL::Decode(nonhex, sizeof(nonhex), output, sizeof(output)), 3u);
    EXPECT_STREQ(output, "%G0");
    std::array<char, 3> exact = {{'?', '?', '!'}};
    EXPECT_EQ(Core::URL::Decode(encoded, sizeof(encoded), exact.data(), 1), 1u);
    EXPECT_EQ(exact[0], 'A');
    EXPECT_EQ(exact[1], '?');
    EXPECT_EQ(exact[2], '!');
    const char colon[] = {'h', ':'};
    const char slash[] = {'h', ':', '/'};
    EXPECT_FALSE(Core::URL(Core::TextFragment(colon, sizeof(colon))).IsValid());
    EXPECT_FALSE(Core::URL(Core::TextFragment(slash, sizeof(slash))).IsValid());
    const char url[] = {'h', 't', 't', 'p', ':', '/', '/', 'x'};
    EXPECT_EQ(Core::URL(Core::TextFragment(url, sizeof(url))).Host().Value(), "x");
}

TEST(AESIssues, RejectPartialBlocksWithoutMutation)
{
    const uint8_t key[16] = {};
    const uint8_t iv[16] = {1, 2, 3, 4};
    const uint8_t input[32] = {};
    for (const auto mode : {Crypto::AES_ECB, Crypto::AES_CBC}) {
        Crypto::AESEncryption encrypt(mode);
        Crypto::AESDecryption decrypt(mode);
        ASSERT_EQ(encrypt.Key(sizeof(key), key), 0u);
        ASSERT_EQ(decrypt.Key(sizeof(key), key), 0u);
        encrypt.InitialVector(iv);
        decrypt.InitialVector(iv);
        for (const uint32_t length : {1u, 15u, 17u, 31u}) {
            std::array<uint8_t, 32> output;
            output.fill(0xA5);
            EXPECT_EQ(encrypt.Encrypt(length, input, output.data()), Core::ERROR_BAD_REQUEST);
            EXPECT_TRUE(std::all_of(output.begin(), output.end(), [](uint8_t byte) { return byte == 0xA5; }));
            EXPECT_EQ(std::memcmp(encrypt.InitialVector(), iv, sizeof(iv)), 0);
            EXPECT_EQ(decrypt.Decrypt(length, input, output.data()), Core::ERROR_BAD_REQUEST);
            EXPECT_TRUE(std::all_of(output.begin(), output.end(), [](uint8_t byte) { return byte == 0xA5; }));
            EXPECT_EQ(std::memcmp(decrypt.InitialVector(), iv, sizeof(iv)), 0);
        }
    }
}

TEST(AESIssues, AlignedNistVectors)
{
    // NIST SP 800-38A AES-128 ECB/CBC first block, independent of implementation round trips.
    const uint8_t key[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
    const uint8_t iv[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const uint8_t plain[16] = {0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a};
    const uint8_t expected[2][16] = {
        {0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a, 0x36, 0x60, 0xa8, 0x9e, 0xca, 0xf3, 0x24, 0x66, 0xef, 0x97},
        {0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46, 0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d}};
    for (const auto mode : {Crypto::AES_ECB, Crypto::AES_CBC}) {
        Crypto::AESEncryption encrypt(mode);
        Crypto::AESDecryption decrypt(mode);
        ASSERT_EQ(encrypt.Key(sizeof(key), key), 0u);
        ASSERT_EQ(decrypt.Key(sizeof(key), key), 0u);
        encrypt.InitialVector(iv);
        decrypt.InitialVector(iv);
        uint8_t cipher[16];
        uint8_t decoded[16];
        ASSERT_EQ(encrypt.Encrypt(sizeof(plain), plain, cipher), 0u);
        EXPECT_EQ(std::memcmp(cipher, expected[mode == Crypto::AES_CBC ? 1 : 0], sizeof(cipher)), 0);
        ASSERT_EQ(decrypt.Decrypt(sizeof(cipher), cipher, decoded), 0u);
        EXPECT_EQ(std::memcmp(decoded, plain, sizeof(plain)), 0);
    }
}
