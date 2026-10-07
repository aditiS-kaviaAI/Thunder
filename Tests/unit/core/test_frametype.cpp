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

#include <gtest/gtest.h>

#ifndef MODULE_NAME
#include "../Module.h"
#endif

#include <core/core.h>

namespace Thunder {
namespace Tests {
namespace Core {

    TEST(test_frame, simple_set)
    {
        uint8_t arr[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        uint8_t arr1[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
        const uint16_t BLOCKSIZE = 20;
        ::Thunder::Core::FrameType<BLOCKSIZE> obj1;
        ::Thunder::Core::FrameType<BLOCKSIZE> obj_copy(obj1);
        ::Thunder::Core::FrameType<0> obj2(arr, 15);
        uint16_t len = 15;
        uint16_t len1 = 13;
        uint16_t offset = 0;
        EXPECT_EQ(obj1.SetBuffer<uint16_t>(offset, len, arr), 17u);
        EXPECT_EQ(obj1.GetBuffer<uint16_t>(offset, len1, arr1), 17u);

        ::Thunder::Core::FrameType<BLOCKSIZE>::Writer obj3(obj1, offset);
        obj3.Buffer<uint16_t>(15, arr);
        obj3.Copy(13, arr1);
        obj3.Number<uint16_t>(4);
        obj3.Boolean(TRUE);
        obj3.Text("Frametype");
        obj3.NullTerminatedText("Frametype");

        obj1.Size(5000);
        ::Thunder::Core::FrameType<BLOCKSIZE>::Reader obj4(obj1, offset);
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

    TEST(test_frame, varint_boundaries)
    {
        ::Thunder::Core::FrameType<1, true> big;
        ::Thunder::Core::FrameType<1, false> little;
        for (uint64_t value : { uint64_t(127), uint64_t(128), uint64_t(255), UINT64_MAX }) {
            big.Clear();
            little.Clear();
            EXPECT_GT(big.SetVariableNumber<uint64_t>(0, value), 0u);
            EXPECT_GT(little.SetVariableNumber<uint64_t>(0, value), 0u);
            ::Thunder::Core::FrameType<1, true>::Reader bigReader(big, 0);
            ::Thunder::Core::FrameType<1, false>::Reader littleReader(little, 0);
            EXPECT_EQ(bigReader.VariableNumber<uint64_t>(), value);
            EXPECT_EQ(littleReader.VariableNumber<uint64_t>(), value);
            EXPECT_EQ(bigReader.Length(), 0u);
        }
        big.Clear();
        EXPECT_EQ(big.SetVariableNumber<uint8_t>(0, 255), 2u);
        EXPECT_EQ(big.Size(), 2u);
        EXPECT_EQ(big[0], 0x81);
        EXPECT_EQ(big[1], 0x7F);
    }

    TEST(test_frame, malformed_input_is_not_consumed)
    {
        uint8_t bytes[256];
        memset(bytes, 0x80, sizeof(bytes));
        ::Thunder::Core::FrameType<0> frame(bytes, sizeof(bytes), sizeof(bytes));
        uint64_t value = 99;
        EXPECT_EQ(frame.GetVariableNumber<uint64_t>(0, value), 0u);
        EXPECT_EQ(value, 0u);
        EXPECT_EQ(frame.GetVariableNumberLength(0), 0u);
        string text = "old";
        EXPECT_EQ(frame.GetNullTerminatedText(0, text), 0u);
        EXPECT_TRUE(text.empty());
        frame.Size(1);
        uint16_t number = 99;
        EXPECT_EQ(frame.GetNumber<uint16_t>(0, number), 0u);
        EXPECT_EQ(number, 0u);
        EXPECT_EQ(frame.GetText<uint16_t>(0, text), 0u);
        EXPECT_EQ(frame.GetBuffer<uint16_t>(0, 2, bytes), 0u);
    }

    TEST(test_frame, capacity_boundaries)
    {
        ::Thunder::Core::FrameType<20> frame;
        frame.Size(65520);
        ASSERT_EQ(frame.Size(), 65520u);
        frame[65519] = 42;
        EXPECT_EQ(frame[65519], 42);
        EXPECT_EQ(frame.SetNumber<uint16_t>(65535, 123), 0u);
        EXPECT_EQ(frame.Size(), 65520u);
        uint8_t fixed[2] = { 1, 2 };
        ::Thunder::Core::FrameType<0> bounded(fixed, 2, 2);
        bounded.Size(3);
        EXPECT_EQ(bounded.Size(), 2u);
        EXPECT_EQ(bounded.SetVariableNumber<uint8_t>(1, 255), 0u);
        EXPECT_EQ(fixed[1], 2);
        ::Thunder::Core::FrameType<1> inserted;
        inserted.SetNumber<uint8_t>(0, 7);
        inserted.SetNumber<uint8_t>(1, 8);
        inserted.Expand(1, 2);
        EXPECT_EQ(inserted.Size(), 4u);
        EXPECT_EQ(inserted[3], 8);
    }

    TEST(test_asn1_p1, shared_ownership)
    {
        ::Thunder::Core::ASN1::Buffer original(3);
        original[0] = 2;
        original[1] = 1;
        original[2] = 42;
        std::vector<::Thunder::Core::ASN1::Buffer> owners(300, original);
        original = ::Thunder::Core::ASN1::Buffer();
        owners.erase(owners.begin(), owners.begin() + 299);
        EXPECT_EQ(owners[0][2], 42);
    }

    TEST(test_asn1_p1, oid_termination)
    {
        const uint8_t bytes[] = { 0x2A, 3, 0 };
        ::Thunder::Core::ASN1::OID oid(bytes, sizeof(bytes));
        EXPECT_EQ(oid.Text(), "1.2.3.0");
        auto iterator = oid.Elements();
        EXPECT_EQ(iterator.Count(), 4u);
        for (unsigned count = 0; count < 4; ++count) {
            ASSERT_TRUE(iterator.Next());
        }
        EXPECT_FALSE(iterator.Next());
        const uint8_t truncated[] = { 0x80 };
        ::Thunder::Core::ASN1::OID invalid(truncated, sizeof(truncated));
        EXPECT_FALSE(invalid.Elements().Next());
    }

    TEST(test_asn1_p1, sequence_extents_and_progress)
    {
        namespace ASN1 = ::Thunder::Core::ASN1;
        ASN1::Buffer truncated(2);
        truncated[0] = 2;
        truncated[1] = 1;
        ASN1::Sequence invalid(truncated);
        EXPECT_FALSE(invalid.Next());
        uint32_t value = 99;
        EXPECT_EQ(invalid.Value(value), ASN1::ASN1_OUT_OF_DATA);
        EXPECT_EQ(value, 99u);
        truncated[1] = 0x81;
        ASN1::Sequence longForm(truncated);
        EXPECT_FALSE(longForm.Next());
        truncated[1] = 0;
        ASN1::Sequence empty(truncated);
        ASSERT_TRUE(empty.Next());
        EXPECT_EQ(empty.Value(value), ASN1::ASN1_INVALID_LENGTH);
        ASN1::Buffer pair(6);
        const uint8_t bytes[] = { 2, 1, 7, 2, 1, 8 };
        for (unsigned index = 0; index < sizeof(bytes); ++index) {
            pair[index] = bytes[index];
        }
        ASN1::Sequence sequence(pair);
        ASSERT_TRUE(sequence.Next());
        EXPECT_EQ(sequence.Value(value), ASN1::ASN1_OK);
        EXPECT_EQ(value, 7u);
        ASSERT_TRUE(sequence.Next());
        EXPECT_EQ(sequence.Value(value), ASN1::ASN1_OK);
        EXPECT_EQ(value, 8u);
        EXPECT_FALSE(sequence.Next());
    }

} // Core
} // Tests
} // Thunder
