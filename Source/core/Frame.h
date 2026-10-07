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

#pragma once

#include "Module.h"
#include "Serialization.h"
#include <limits>

namespace Thunder {
namespace Core {

    namespace Frame {

        template <typename NEW_TYPE, typename ORIGINAL_TYPE>
        NEW_TYPE buffer_length_cast(const ORIGINAL_TYPE& input)
        {
            // in release in case the length does not fit we do not want to send data at all, then it is more obvious to the recipient something is wrong instead of only sending partial data
            NEW_TYPE length = 0;

            if (Core::check_and_cast<NEW_TYPE, ORIGINAL_TYPE>(input, length) == false) {
                length = 0;            
            }

            return (length);
        }

    }

    template <const uint32_t BLOCKSIZE, const bool BIG_ENDIAN_ORDERING = true, typename SIZE_CONTEXT = uint16_t>
    class FrameType {
    private:
        template <const uint32_t STARTSIZE, typename SIZETYPE>
        class AllocatorType {
        public:
            AllocatorType<STARTSIZE, SIZETYPE>& operator=(const AllocatorType<STARTSIZE, SIZETYPE>& copy) = delete;

            AllocatorType()
                : _bufferSize(static_cast<SIZETYPE>(STARTSIZE))
                , _data(static_cast<uint8_t*>(::malloc(_bufferSize)))
            {
                // It looks like there is a bug in the windows compiler. It prepares a default/copy constructor
                // if if the template being instantiated is not really utilizing it!
                #ifndef __WINDOWS__
                static_assert((STARTSIZE != 0) && (STARTSIZE != static_cast<uint32_t>(~0)), "This method can only be called if you specify an initial blocksize different than 0 or ~0");
                #endif
            }
            AllocatorType(const SIZETYPE bufferSize)
                : _bufferSize(bufferSize)
                , _data(static_cast<uint8_t*>(::malloc(_bufferSize)))
            {
                // It looks like there is a bug in the windows compiler. It prepares a default/copy constructor
                // if if the template being instantiated is not really utilizing it!
                #ifndef __WINDOWS__
                static_assert(STARTSIZE == static_cast<uint32_t>(~0), "This method can only be called if you specify an initial blocksize of ~0");
                #endif
            }
            AllocatorType(const AllocatorType<STARTSIZE, SIZETYPE>& copy)
                : _bufferSize(copy._bufferSize)
                , _data(static_cast<uint8_t*>(::malloc(_bufferSize)))
            {
                // It looks like there is a bug in the windows compiler. It prepares a default/copy constructor
                // if if the template being instantiated is not really utilizing it!
                #ifndef __WINDOWS__
                static_assert((STARTSIZE != 0) && (STARTSIZE != static_cast<uint32_t>(~0)), "This method can only be called if you specify an initial blocksize different than 0 or ~0");
                #endif
                if (_data != nullptr && copy._data != nullptr && _bufferSize != 0) {
                    ::memcpy(_data, copy._data, _bufferSize);
                } else {
                    _bufferSize = 0;
                }
            }
            AllocatorType(AllocatorType<STARTSIZE, SIZETYPE>&& move)
                : _bufferSize(move._bufferSize)
                , _data(move._data)
            {
                // It looks like there is a bug in the windows compiler. It prepares a default/copy constructor
                // if if the template being instantiated is not really utilizing it!
                #ifndef __WINDOWS__
                static_assert(STARTSIZE != 0, "This method can only be called if you specify an initial blocksize");
                #endif
                move._bufferSize = 0;
                move._data = nullptr;
            }
            AllocatorType(uint8_t buffer[], const SIZETYPE length)
                : _bufferSize(length)
                , _data(buffer)
            {
                // It looks like there is a bug in the windows compiler. It prepares a default/copy constructor
                // if if the template being instantiated is not really utilizing it!
                #ifndef __WINDOWS__
                static_assert(STARTSIZE == 0, "This method can only be called if you specify an initial blocksize of 0");
                #endif
            }
            ~AllocatorType()
            {
                if ((STARTSIZE != 0) && (_data != nullptr)) {
                    ::free(_data);
                }
            }

            AllocatorType<STARTSIZE, SIZETYPE>& operator=(AllocatorType<STARTSIZE, SIZETYPE>&& move) {
                if (STARTSIZE != 0) {
                    if (_data != nullptr) {
                        ::free(_data);
                    }
                    _data =  move._data;
                    move._data = nullptr;
                    _bufferSize = move._bufferSize;
                }
                else {
                    ::memcpy(_data, move._data, _bufferSize);
                }
                return (*this);
            }

        public:
            inline uint8_t& operator[](const SIZETYPE index)
            {
                ASSERT(_data != nullptr);
                ASSERT(index < _bufferSize);
                return (_data[index]);
            }
            inline const uint8_t& operator[](const SIZETYPE index) const
            {
                ASSERT(_data != nullptr);
                ASSERT(index < _bufferSize);
                return (_data[index]);
            }
            inline bool Allocate(SIZETYPE requiredSize)
            {
                return RealAllocate(requiredSize, TemplateIntToType<STARTSIZE == 0 ? false : true>());
            }
            const uint8_t* Data() const { return _data; }

        private:
            inline bool RealAllocate(const SIZETYPE requiredSize, const TemplateIntToType<false>&)
            {
                return requiredSize <= _bufferSize && (requiredSize == 0 || _data != nullptr);
            }
            inline bool RealAllocate(const SIZETYPE requiredSize, const TemplateIntToType<true>&)
            {
                if (requiredSize == 0) {
                    return true;
                }
                if (requiredSize > _bufferSize || _data == nullptr) {
                    // Round in a wide type and cap at the representable capacity.
                    const uint64_t maximum = static_cast<SIZETYPE>(~0);
                    const uint64_t rounded = ((uint64_t(requiredSize) / STARTSIZE) + 1) * STARTSIZE;
                    const SIZETYPE bufferSize = static_cast<SIZETYPE>(rounded > maximum ? maximum : rounded);
                    uint8_t* data = static_cast<uint8_t*>(::realloc(_data, bufferSize));
                    if (data == nullptr) {
                        return false;
                    }
                    _data = data;
                    _bufferSize = bufferSize;
                }
                return true;
            }

        private:
            SIZETYPE _bufferSize;
            uint8_t* _data;
        };

    public:
        class Reader {
        public:
            Reader& operator=(const Reader&) = delete;

            Reader()
                : _offset(0)
                , _container(nullptr)
            {
            }
            Reader(const FrameType& data, const SIZE_CONTEXT offset)
                : _offset(offset)
                , _container(&data)
            {
            }
            Reader(const Reader& copy)
                : _offset(copy._offset)
                , _container(copy._container)
            {
            }
            Reader(Reader&& move)
                : _offset(move._offset)
                , _container(move._container)
            {
                move._offset = 0;
                move._container = nullptr;
            }
            ~Reader() = default;

        public:
            inline bool HasData() const
            {
                return ((_container != nullptr) && (_offset < _container->Size()));
            }
            inline uint32_t Length() const
            {
                return (_container == nullptr || _offset > _container->Size() ? 0 : _container->Size() - _offset);
            }
            SIZE_CONTEXT LockFixedBuffer(const uint8_t*& buffer, const uint32_t length) const
            {
                buffer = nullptr;
                if (_container == nullptr || length > Length() || length == 0) {
                    return 0;
                }
                buffer = &(_container->operator[](_offset));
                return static_cast<SIZE_CONTEXT>(length);
            }
            template <typename TYPENAME>
            TYPENAME LockBuffer(const uint8_t*& buffer) const
            {
                TYPENAME result = 0;
                buffer = nullptr;
                if (_container == nullptr) {
                    return 0;
                }
                const SIZE_CONTEXT prefix = _container->GetNumber<TYPENAME>(_offset, result);
                if (prefix == 0 || uint64_t(result) > Length() - prefix) {
                    return 0;
                }
                _offset += prefix;
                buffer = result == 0 ? nullptr : &(_container->operator[](_offset));
                return result;
            }
            template <typename TYPENAME>
            void UnlockBuffer(TYPENAME length) const
            {
                ASSERT(_container != nullptr);
                ASSERT((length >= 0) && (static_cast<uint32_t>(length) <= Length()));

                _offset += length;
            }
            template <typename TYPENAME>
            TYPENAME Buffer(const TYPENAME maxLength, uint8_t buffer[]) const
            {
                uint32_t result;

                ASSERT(_container != nullptr);

                result = _container->GetBuffer<TYPENAME>(_offset, maxLength, buffer);
                _offset += result;

                return (result < Core::RealSize<TYPENAME>() ? 0 : static_cast<TYPENAME>(result - Core::RealSize<TYPENAME>()));
            }
            void Copy(const SIZE_CONTEXT length, uint8_t buffer[]) const
            {
                ASSERT(_container != nullptr);

                _offset += _container->Copy(_offset, length, buffer);
            }
            template <typename TYPENAME>
            TYPENAME Number() const
            {
                TYPENAME result;

                ASSERT(_container != nullptr);

                _offset += _container->GetNumber<TYPENAME>(_offset, result);

                return (result);
            }
            template <typename TYPENAME>
            TYPENAME VariableNumber() const
            {
                TYPENAME result = 0;
                if (_container != nullptr) {
                    _offset += _container->GetVariableNumber<TYPENAME>(_offset, result);
                }
                return result;
            }
            bool Boolean() const
            {
                bool result;

                ASSERT(_container != nullptr);

                _offset += _container->GetBoolean(_offset, result);

                return (result);
            }
            template <typename TYPENAME = uint16_t>
            string Text() const
            {
                string result;

                ASSERT(_container != nullptr);

                _offset += _container->GetText<TYPENAME>(_offset, result);

                return (result);
            }
            string NullTerminatedText() const
            {
                string result;

                ASSERT(_container != nullptr);

                _offset += _container->GetNullTerminatedText(_offset, result);

                return (result);
            }
            const uint8_t* Data() const
            {
                ASSERT(_container != nullptr);

                return (&(_container->Data()[_offset]));
            }
            void Forward(const SIZE_CONTEXT skip)
            {
                ASSERT(skip <= Length());

                _offset += skip;
            }
            template <typename TYPENAME>
            TYPENAME PeekNumber() const
            {
                TYPENAME result;

                ASSERT(_container != nullptr);

                _container->GetNumber<TYPENAME>(_offset, result);

                return (result);
            }

#ifdef __DEBUG__
            void Dump() const
            {
                _container->Dump(_offset);
            }
#endif

        private:
            mutable SIZE_CONTEXT _offset;
            const FrameType* _container;
        };
        class Writer {
        public:
            Writer& operator=(const Writer&) = delete;

            Writer()
                : _offset(0)
                , _container(nullptr)
            {
            }
            // TODO: should we make offset 0 by default?
            Writer(FrameType& data, const uint32_t offset)
                : _offset(offset)
                , _container(&data)
            {
            }
            Writer(const Writer& copy)
                : _offset(copy._offset)
                , _container(copy._container)
            {
            }
            Writer(Writer&& move)
                : _offset(move._offset)
                , _container(move._container)
            {
                move._offset = 0;
                move._container = nullptr;
            }
            ~Writer() = default;

        public:
            inline SIZE_CONTEXT Offset() const
            {
                return (_offset);
            }
            template <typename TYPENAME>
            void Buffer(const TYPENAME length, const uint8_t buffer[])
            {
                ASSERT(_container != nullptr);

                _offset += _container->SetBuffer<TYPENAME>(_offset, length, buffer);
            }
            void Copy(const SIZE_CONTEXT length, const uint8_t buffer[])
            {
                ASSERT(_container != nullptr);

                _offset += _container->Copy(_offset, length, buffer);
            }
            template <typename TYPENAME>
            void Number(const TYPENAME value)
            {
                ASSERT(_container != nullptr);

                _offset += _container->SetNumber<TYPENAME>(_offset, value);
            }
            template <typename TYPENAME>
            void VariableNumber(const TYPENAME value)
            {
                ASSERT(_container != nullptr);

                _offset += _container->SetVariableNumber<TYPENAME>(_offset, value);
            }
            void Boolean(const bool value)
            {
                ASSERT(_container != nullptr);

                _offset += _container->SetBoolean(_offset, value);
            }
            template <typename TYPENAME = uint16_t>
            void Text(const string& text)
            {
                ASSERT(_container != nullptr);

                _offset += _container->SetText<TYPENAME>(_offset, text);
            }
            void NullTerminatedText(const string& text, const SIZE_CONTEXT maxLength = ~0)
            {
                ASSERT(_container != nullptr);

                _offset += _container->SetNullTerminatedText(_offset, text, maxLength);
            }

        private:
            SIZE_CONTEXT _offset;
            FrameType* _container;
        };

    public:
        FrameType()
            : _size(0)
            , _data() {
            // It looks like there is a bug in the windows compiler. It prepares a default/copy constructor
            // if the template being instantiated is not really utilizing it!
            #ifndef __WINDOWS__
            static_assert(BLOCKSIZE != 0, "This method can only be called if you specify an initial blocksize");
            #endif
        }
        FrameType(const SIZE_CONTEXT length)
            : _size(0)
            , _data(length) {
            // It looks like there is a bug in the windows compiler. It prepares a default/copy constructor
            // if the template being instantiated is not really utilizing it!
            #ifndef __WINDOWS__
            static_assert(BLOCKSIZE == static_cast<uint32_t>(~0), "This method can only be called if you specify a runtime-defined blocksize");
            #endif
        }
        FrameType(const FrameType<BLOCKSIZE, BIG_ENDIAN_ORDERING, SIZE_CONTEXT>& copy)
            : _size(0)
            , _data(copy._data)
        {
            if (_data.Data() != nullptr) {
                _size = copy._size;
            }
            // It looks like there is a bug in the windows compiler. It prepares a default/copy constructor
            // if the template being instantiated is not really utilizing it!
            #ifndef __WINDOWS__
            static_assert(BLOCKSIZE != 0, "This method can only be called if you allocate a new buffer");
            #endif
        }
        FrameType(FrameType<BLOCKSIZE, BIG_ENDIAN_ORDERING, SIZE_CONTEXT>& move)
            : _size(move._size)
            , _data(std::move(move._data))
        {
            // It looks like there is a bug in the windows compiler. It prepares a default/copy constructor
            // if if the template being instantiated is not really utilizing it!
            #ifndef __WINDOWS__
            static_assert(BLOCKSIZE != 0, "This method can only be called if you allocate a new buffer");
            #endif
            move._size = 0;
        }

        FrameType(uint8_t* buffer, const SIZE_CONTEXT length, const SIZE_CONTEXT loadedSize = 0)
            : _size(buffer != nullptr && loadedSize <= length ? loadedSize : 0)
            , _data(buffer, length)
        {
            // It looks like there is a bug in the windows compiler. It prepares a default/copy constructor
            // if the template being instantiated is not really utilizing it!
            #ifndef __WINDOWS__
            static_assert(BLOCKSIZE == 0, "This method can only be called if you pass a buffer that can not be extended");
            #endif
        }
        ~FrameType() = default;

        FrameType<BLOCKSIZE, BIG_ENDIAN_ORDERING, SIZE_CONTEXT>& operator=(const FrameType<BLOCKSIZE, BIG_ENDIAN_ORDERING, SIZE_CONTEXT>& rhs) {
            if (this != &rhs) {
                if (_data.Allocate(rhs.Size())) {
                    _size = rhs.Size();
                    if (_size > 0) {
                        ::memcpy(&(_data[0]), rhs.Data(), _size);
                    }
                }
            }
            return(*this);
        }
        FrameType<BLOCKSIZE, BIG_ENDIAN_ORDERING, SIZE_CONTEXT>& operator=(FrameType<BLOCKSIZE, BIG_ENDIAN_ORDERING, SIZE_CONTEXT>&& move) {
            if (this != &move) {
                _size = move._size;
                _data = std::move(move._data);

                move._size = 0;
            }
            return(*this);
        }

    public:
        inline void Clear()
        {
            _size = 0;
        }
        inline SIZE_CONTEXT Size() const
        {
            return (_size);
        }
        inline const uint8_t* Data() const {
            return _data.Data();
        }
        inline uint8_t& operator[](const SIZE_CONTEXT index)
        {
            return _data[index];
        }
        inline const uint8_t& operator[](const SIZE_CONTEXT index) const
        {
            return _data[index];
        }
        void Size(SIZE_CONTEXT size)
        {
            if (_data.Allocate(size)) {
                _size = size;
            }
        }
        void Shrink(const SIZE_CONTEXT offset, const SIZE_CONTEXT size) {
            if (offset > _size || size > _size - offset) {
                return;
            }

            if (size > 0) {
                if ((offset + size) == _size) {
                    _size -= size;
                }
                else {
                    ::memmove(&(_data[offset]), &(_data[offset + size]), _size - offset - size);
                    _size -= size;
                }
            }
        }
        void Expand(const SIZE_CONTEXT offset, const SIZE_CONTEXT size) {
            const SIZE_CONTEXT oldSize = _size;
            if (offset <= oldSize && size > 0 && Ensure(uint64_t(oldSize) + size)) {
                if (offset < oldSize) {
                    ::memmove(&(_data[offset + size]), &(_data[offset]), oldSize - offset);
                }
            }
        }
        template <typename TYPENAME>
        uint32_t SetBuffer(const SIZE_CONTEXT offset, const TYPENAME length, const uint8_t buffer[])
        {
            const uint64_t requiredLength = uint64_t(Core::RealSize<TYPENAME>()) + length;

            static_assert(Core::RealSize<TYPENAME>() <= sizeof(SIZE_CONTEXT), "Make sure the logic can handle the size (enlarge the SIZE_CONTEXT)");

            if ((length != 0 && buffer == nullptr) || !Ensure(uint64_t(offset) + requiredLength)) {
                return 0;
            }

            SetNumber<TYPENAME>(offset, length);

            if (length != 0) {
                ASSERT(buffer != nullptr);
                ::memcpy(&(_data[offset + Core::RealSize<TYPENAME>()]), buffer, length);
            }

            return (requiredLength);
        }

        SIZE_CONTEXT Copy(const SIZE_CONTEXT offset, const SIZE_CONTEXT length, uint8_t buffer[]) const
        {
            if (offset > _size || length > _size - offset || (length != 0 && buffer == nullptr)) {
                return 0;
            }
            if (length != 0) {
                ::memcpy(buffer, &(_data[offset]), length);
            }

            return (length);
        }
        SIZE_CONTEXT Copy(const SIZE_CONTEXT offset, const SIZE_CONTEXT length, const uint8_t buffer[])
        {
            if ((length != 0 && buffer == nullptr) || !Ensure(uint64_t(offset) + length)) {
                return 0;
            }
            if (length != 0) {
                ::memcpy(&(_data[offset]), buffer, length);
            }

            return (length);
        }
        template <typename TYPENAME = uint16_t>
        SIZE_CONTEXT SetText(const SIZE_CONTEXT offset, const string& value)
        {
            std::string convertedText(Core::ToString(value));
            return (SetBuffer<TYPENAME>(offset, Frame::buffer_length_cast<TYPENAME>(convertedText.length()), reinterpret_cast<const uint8_t*>(convertedText.c_str())));
        }

        SIZE_CONTEXT SetNullTerminatedText(const SIZE_CONTEXT offset, const string& value, const SIZE_CONTEXT maxLength)
        {
            if (maxLength == 0) {
                return 0;
            }
            std::string convertedText(Core::ToString(value));
            if (maxLength != static_cast<SIZE_CONTEXT>(~0) && convertedText.size() >= maxLength) {
                convertedText.resize(maxLength - 1);
            }
            const uint64_t required = convertedText.size() + 1;
            if (!Ensure(uint64_t(offset) + required)) {
                return 0;
            }
            const SIZE_CONTEXT requiredLength = static_cast<SIZE_CONTEXT>(required);
            ::memcpy(&(_data[offset]), convertedText.c_str(), requiredLength);

            return (requiredLength);
        }

        template <typename TYPENAME>
        SIZE_CONTEXT GetBuffer(const SIZE_CONTEXT offset, const TYPENAME length, uint8_t buffer[]) const
        {
            TYPENAME textLength = 0;
            static_assert(Core::RealSize<TYPENAME>() <= sizeof(SIZE_CONTEXT), "Make sure the logic can handle the size (enlarge the SIZE_CONTEXT)");
            const SIZE_CONTEXT prefix = GetNumber<TYPENAME>(offset, textLength);
            if (prefix == 0 || uint64_t(textLength) > uint64_t(_size - offset - prefix)) {
                return 0;
            }
            const TYPENAME copied = textLength > length ? length : textLength;
            if (copied != 0) {
                if (buffer == nullptr) {
                    return 0;
                }
                memcpy(buffer, &(_data[offset + prefix]), copied);
            }
            return static_cast<SIZE_CONTEXT>(prefix + textLength);
        }

        template <typename TYPENAME = uint16_t>
        SIZE_CONTEXT GetText(const SIZE_CONTEXT offset, string& result) const
        {
            TYPENAME textLength = 0;
            result.clear();
            static_assert(Core::RealSize<TYPENAME>() <= sizeof(SIZE_CONTEXT), "Make sure the logic can handle the size (enlarge the SIZE_CONTEXT)");
            const SIZE_CONTEXT prefix = GetNumber<TYPENAME>(offset, textLength);
            if (prefix == 0 || uint64_t(textLength) > uint64_t(_size - offset - prefix)) {
                return 0;
            }
            if (textLength != 0) {
                result = Core::ToString(std::string(reinterpret_cast<const char*>(&(_data[offset + prefix])), textLength));
            }
            return static_cast<SIZE_CONTEXT>(prefix + textLength);
        }

        SIZE_CONTEXT GetNullTerminatedText(const SIZE_CONTEXT offset, string& result) const
        {
            result.clear();
            if (offset >= _size) {
                return 0;
            }
            const char* text = reinterpret_cast<const char*>(&(_data[offset]));
            const char* end = static_cast<const char*>(::memchr(text, 0, _size - offset));
            if (end == nullptr) {
                return 0;
            }
            result = Core::ToString(std::string(text, end - text));
            return static_cast<SIZE_CONTEXT>(end - text + 1);
        }

        SIZE_CONTEXT SetBoolean(const SIZE_CONTEXT offset, const bool value)
        {
            if (!Ensure(uint64_t(offset) + 1)) {
                return 0;
            }

            _data[offset] = (value ? 1 : 0);

            return (1);
        }

        SIZE_CONTEXT GetBoolean(const SIZE_CONTEXT offset, bool& value) const
        {
            value = false;
            if (offset >= _size) {
                return 0;
            }
            value = (_data[offset] != 0);

            return (1);
        }
        template <typename TYPENAME>
        inline SIZE_CONTEXT SetVariableNumber(const SIZE_CONTEXT offset, const TYPENAME number)
        {
            uint8_t bytes[10]; // This equals 2^(10*7) => 2^70 >= uint64_t
            uint8_t index = 0;
            TYPENAME value = number;

            static_assert(Core::RealSize<TYPENAME>() <= ((sizeof(bytes) * 7) / 8), "Make sure the size is not too large (not much bigger than uint64_t)");

            do {
                bytes[index++] = static_cast<uint8_t>(value % 128);
                value /= 128;

            } while (value > 0);

            if (!Ensure(uint64_t(offset) + index)) {
                return 0;
            }

            if ( (BIG_ENDIAN_ORDERING == true) && (index > 1) ) {
                // We need to swap, it is currently LITTLE_ENDIAN
                for (uint8_t step = 0; step < (index / 2); step++) {
                    std::swap(bytes[step], bytes[index - 1 - step]);
                }
            }
            for (uint8_t step = 0; step + 1 < index; ++step) {
                bytes[step] |= 0x80;
            }
            ::memcpy(&(_data[offset]), bytes, index);

            return (index);
        }

        uint8_t GetVariableNumberLength(const SIZE_CONTEXT offset) const {
            for (uint8_t index = 0; index < 10 && uint64_t(offset) + index < _size; ++index) {
                if ((_data[offset + index] & 0x80) == 0) {
                    return index + 1;
                }
            }
            return 0;
        }

        static uint8_t VariableNumberLength(const uint64_t value) {
            uint8_t byteCount = 0;
            uint64_t testValue = value;

            do {
                byteCount++;
                testValue = testValue >> 7;
            } while (testValue != 0);

            return (byteCount);
        }

        template <typename TYPENAME>
        inline SIZE_CONTEXT GetVariableNumber(const SIZE_CONTEXT offset, TYPENAME& number) const
        {
            number = 0;
            const uint8_t length = GetVariableNumberLength(offset);
            const uint64_t maximum = static_cast<uint64_t>(std::numeric_limits<TYPENAME>::max());
            uint64_t value = 0;
            if (length == 0 || length > (sizeof(TYPENAME) * 8 + 6) / 7) {
                return 0;
            }
            for (uint8_t index = 0; index < length; ++index) {
                const uint8_t position = BIG_ENDIAN_ORDERING ? index : length - 1 - index;
                const uint8_t digit = _data[offset + position] & 0x7F;
                if (value > (maximum >> 7) || (value == (maximum >> 7) && digit > (maximum & 0x7F))) {
                    return 0;
                }
                value = (value << 7) | digit;
            }
            number = static_cast<TYPENAME>(value);
            return length;
        }

        template <typename TYPENAME>
        inline SIZE_CONTEXT SetNumber(const SIZE_CONTEXT offset, const TYPENAME number)
        {
            return (SetNumber(offset, number, TemplateIntToType<Core::RealSize<TYPENAME>() == 1>()));
        }

        template <typename TYPENAME>
        inline SIZE_CONTEXT GetNumber(const SIZE_CONTEXT offset, TYPENAME& number) const
        {
            return (GetNumber(offset, number, TemplateIntToType<Core::RealSize<TYPENAME>() == 1>()));
        }

#ifdef __DEBUG__
        void Dump(const SIZE_CONTEXT offset) const
        {
            static const TCHAR character[] = "0123456789ABCDEF";
            string info;
            SIZE_CONTEXT index = offset;

            while (index < _size) {
                if (info.empty() == false) {
                    info += ':';
                }
                info += string("0x") + character[(_data[index] & 0xF0) >> 4] + character[(_data[index] & 0x0F)];
                index++;
            }

            TRACE_L1("MetaData: %s", info.c_str());
        }
#endif

    private:
        bool Ensure(const uint64_t end)
        {
            if (end > static_cast<SIZE_CONTEXT>(~0) || !_data.Allocate(static_cast<SIZE_CONTEXT>(end))) {
                return false;
            }
            if (end > _size) {
                _size = static_cast<SIZE_CONTEXT>(end);
            }
            return true;
        }

        template <typename TYPENAME>
        SIZE_CONTEXT SetNumber(const SIZE_CONTEXT offset, const TYPENAME number, const TemplateIntToType<true>&)
        {
            if (!Ensure(uint64_t(offset) + 1)) {
                return 0;
            }

            _data[offset] = static_cast<uint8_t>(number);

            return (1);
        }

        template <typename TYPENAME>
        void SetNumberLittleEndianPlatform(const SIZE_CONTEXT offset, const TYPENAME number) {
            const uint8_t* source = reinterpret_cast<const uint8_t*>(&number);
            uint8_t* destination = &(_data[offset + Core::RealSize<TYPENAME>() - 1]);

            for (uint8_t index = 0; index < Core::RealSize<TYPENAME>(); index++) {
                *destination-- = *source++;
            }
        }

        template <typename TYPENAME>
        void SetNumberBigEndianPlatform(const SIZE_CONTEXT offset, const TYPENAME number) {
            const uint8_t* source = reinterpret_cast<const uint8_t*>(&number);
            uint8_t* destination = &(_data[offset]);

            for (uint8_t index = 0; index < Core::RealSize<TYPENAME>(); index++) {
                *destination++ = *source++;
            }
        }


        template <typename TYPENAME>
        SIZE_CONTEXT SetNumber(const SIZE_CONTEXT offset, const TYPENAME number, const TemplateIntToType<false>&)
        {
            if (!Ensure(uint64_t(offset) + Core::RealSize<TYPENAME>())) {
                return 0;
            }

            if (BIG_ENDIAN_ORDERING == true) {
#ifdef LITTLE_ENDIAN_PLATFORM
                SetNumberLittleEndianPlatform(offset, number);
#else
                SetNumberBigEndianPlatform(offset, number);
#endif
            }
            else {
#ifdef LITTLE_ENDIAN_PLATFORM
                SetNumberBigEndianPlatform(offset, number);
#else
                SetNumberLittleEndianPlatform(offset, number);
#endif
            }

            return (Core::RealSize<TYPENAME>());
        }

        template <typename TYPENAME>
        SIZE_CONTEXT GetNumber(const SIZE_CONTEXT offset, TYPENAME& number, const TemplateIntToType<true>&) const
        {
            number = static_cast<TYPENAME>(0);
            if (offset >= _size) {
                return 0;
            }
            number = static_cast<TYPENAME>(_data[offset]);

            return (1);
        }

        template <typename TYPENAME>
        inline TYPENAME GetNumberLittleEndianPlatform(const SIZE_CONTEXT offset) const
        {
            TYPENAME result = static_cast<TYPENAME>(0);
            const uint8_t* source = &(_data[offset]);
            uint8_t* destination = &(reinterpret_cast<uint8_t*>(&result)[Core::RealSize<TYPENAME>() - 1]);

            for (uint8_t index = 0; index < Core::RealSize<TYPENAME>(); index++) {
                *destination-- = *source++;
            }

            return (result);
        }

        template <typename TYPENAME>
        inline TYPENAME GetNumberBigEndianPlatform(const SIZE_CONTEXT offset) const
        {
            TYPENAME result = static_cast<TYPENAME>(0);

            // If the sizeof > 1, the alignment could be wrong. Assume the worst, always copy !!!
            const uint8_t* source = &(_data[offset]);
            uint8_t* destination = reinterpret_cast<uint8_t*>(&result);

            for (uint8_t index = 0; index < Core::RealSize<TYPENAME>(); index++) {
                *destination++ = *source++;
            }

            return (result);
        }

        template <typename TYPENAME>
        inline SIZE_CONTEXT GetNumber(const SIZE_CONTEXT offset, TYPENAME& value, const TemplateIntToType<false>&) const
        {
            if (uint64_t(offset) + Core::RealSize<TYPENAME>() > _size) {
                value = static_cast<TYPENAME>(0);
                return 0;
            }
            else if (BIG_ENDIAN_ORDERING == true) {
#ifdef LITTLE_ENDIAN_PLATFORM
                value = GetNumberLittleEndianPlatform<TYPENAME>(offset);
#else
                value = GetNumberBigEndianPlatform<TYPENAME>(offset);
#endif
            }
            else {
#ifdef LITTLE_ENDIAN_PLATFORM
                value = GetNumberBigEndianPlatform<TYPENAME>(offset);
#else
                value = GetNumberLittleEndianPlatform<TYPENAME>(offset);
#endif
            }

            return (Core::RealSize<TYPENAME>());
        }

    private:
        mutable SIZE_CONTEXT _size;
        AllocatorType<BLOCKSIZE,SIZE_CONTEXT> _data;
    };
}
}
