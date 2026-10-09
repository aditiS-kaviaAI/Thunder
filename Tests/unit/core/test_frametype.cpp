/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2020 Metrological
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Frame tests use no fork harness; include only their actual dependencies.
// Core headers require the test binary's module identity before they are included.
#define MODULE_NAME ThunderUnitTests
#include <gtest/gtest.h>
// The frame API is header-only; the umbrella header introduces unrelated core linkage.
#include <core/Frame.h>
#include <limits>

using namespace WPEFramework;
using namespace WPEFramework::Core;

namespace {
    /**
     * Exercise unsigned TYPENAME serialization in ORDER (true for big endian).
     * Returns nothing; GoogleTest records wire, value, size and offset failures.
     */
    template <bool ORDER, typename TYPENAME>
    void CheckVariableNumberBoundaries()
    {
        const uint64_t values[] = {0, 1, 127, 128, 255, 16383, 16384,
            65535, 65536, std::numeric_limits<TYPENAME>::max()};

        for (const uint64_t value : values) {
            if (value > std::numeric_limits<TYPENAME>::max()) {
                continue;
            }
            SCOPED_TRACE(value);
            SCOPED_TRACE(ORDER);
            SCOPED_TRACE(sizeof(TYPENAME));

            // A one-byte block forces allocation to follow encoded size, not native width.
            FrameType<1, ORDER> frame;
            typename FrameType<1, ORDER>::Writer writer(frame, 0);
            writer.template Number<uint8_t>(0x5A);
            writer.template VariableNumber<TYPENAME>(static_cast<TYPENAME>(value));

            uint8_t length = 1;
            for (uint64_t remaining = value >> 7; remaining != 0; remaining >>= 7) {
                ++length;
            }
            ASSERT_EQ(frame.Size(), 1u + length);
            EXPECT_EQ(writer.Offset(), frame.Size());
            EXPECT_EQ(frame[0], 0x5A);

            // Derive each expected wire group from its bit position, independently of round trips.
            for (uint8_t index = 0; index < length; ++index) {
                const uint8_t shift = 7 * (ORDER ? length - 1 - index : index);
                const uint8_t expected = static_cast<uint8_t>(((value >> shift) & 0x7F)
                    | (index + 1 < length ? 0x80 : 0));
                EXPECT_EQ(frame[1 + index], expected);
            }
            EXPECT_EQ(frame.GetVariableNumberLength(1), length);
            TYPENAME decoded = 0;
            EXPECT_EQ(frame.template GetVariableNumber<TYPENAME>(1, decoded), length);
            EXPECT_EQ(decoded, value);

            // A following field proves both writer and reader consume precisely the encoded extent.
            writer.template Number<uint8_t>(0xA5);
            typename FrameType<1, ORDER>::Reader reader(frame, 1);
            EXPECT_EQ(reader.template VariableNumber<TYPENAME>(), value);
            EXPECT_EQ(reader.Length(), 1u);
            EXPECT_EQ(reader.template Number<uint8_t>(), 0xA5);
            EXPECT_FALSE(reader.HasData());

            // Rewriting an existing field must not shrink a frame containing subsequent data.
            const uint16_t size = frame.Size();
            EXPECT_EQ(frame.template SetVariableNumber<TYPENAME>(1, static_cast<TYPENAME>(value)), length);
            EXPECT_EQ(frame.Size(), size);
            EXPECT_EQ(frame[1 + length], 0xA5);
        }
    }
}

// Literal wire input isolates the reader return defect from the writer implementation.
TEST(test_frame, variable_number_reader_returns_value)
{
    uint8_t bigEndian[] = {0x82, 0x2C, 0xA5};
    FrameType<0, true> bigFrame(bigEndian, sizeof(bigEndian), sizeof(bigEndian));
    FrameType<0, true>::Reader bigReader(bigFrame, 0);
    EXPECT_EQ(bigReader.VariableNumber<uint16_t>(), 300u);
    EXPECT_EQ(bigReader.Length(), 1u);
    EXPECT_EQ(bigReader.Number<uint8_t>(), 0xA5);
    EXPECT_FALSE(bigReader.HasData());

    uint8_t littleEndian[] = {0xAC, 0x02, 0xA5};
    FrameType<0, false> littleFrame(littleEndian, sizeof(littleEndian), sizeof(littleEndian));
    FrameType<0, false>::Reader littleReader(littleFrame, 0);
    EXPECT_EQ(littleReader.VariableNumber<uint16_t>(), 300u);
    EXPECT_EQ(littleReader.Length(), 1u);
    EXPECT_EQ(littleReader.Number<uint8_t>(), 0xA5);
    EXPECT_FALSE(littleReader.HasData());
}

// Cover the extra encoded byte needed at unsigned integer maxima in both wire orders.
TEST(test_frame, varint_boundaries)
{
    CheckVariableNumberBoundaries<true, uint8_t>();
    CheckVariableNumberBoundaries<false, uint8_t>();
    CheckVariableNumberBoundaries<true, uint16_t>();
    CheckVariableNumberBoundaries<false, uint16_t>();
    CheckVariableNumberBoundaries<true, uint32_t>();
    CheckVariableNumberBoundaries<false, uint32_t>();
    CheckVariableNumberBoundaries<true, uint64_t>();
    CheckVariableNumberBoundaries<false, uint64_t>();
}

TEST(test_frame, simple_set)
{
    uint8_t arr[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    uint8_t arr1[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    const uint16_t BLOCKSIZE = 20;
    FrameType<BLOCKSIZE> obj1;
    FrameType<BLOCKSIZE> obj_copy(obj1);
    FrameType<0> obj2(arr, 15);
    uint16_t len = 15;
    uint16_t len1 = 13;
    uint16_t offset = 0;
    EXPECT_EQ(obj1.SetBuffer<uint16_t>(offset, len, arr), 17u);
    EXPECT_EQ(obj1.GetBuffer<uint16_t>(offset, len1, arr1), 17u);

    FrameType<BLOCKSIZE>::Writer obj3(obj1, offset);
    obj3.Buffer<uint16_t>(15, arr);
    obj3.Copy(13, arr1);
    obj3.Number<uint16_t>(4);
    obj3.Boolean(TRUE);
    obj3.Text("Frametype");
    obj3.NullTerminatedText("Frametype");

    obj1.Size(5000);
    FrameType<BLOCKSIZE>::Reader obj4(obj1, offset);
    obj4.Buffer<uint16_t>(15, arr);
    obj4.Copy(13, arr1);
    obj4.Number<uint16_t>();
    obj4.Boolean();
    obj4.Text();
    obj4.NullTerminatedText();
    obj4.UnlockBuffer(15);
    // TODO: why doesn't this work when inlining is disabled?
    //obj4.Dump();
    uint32_t Size = 5000;
    EXPECT_EQ(obj1.Size(), Size);
    obj1.Clear();
}
