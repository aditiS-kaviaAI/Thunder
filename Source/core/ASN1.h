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

#ifndef __ASN1_H
#define __ASN1_H

// ---- Include system wide include files ----

// ---- Include local include files ----
#include "Module.h"
#include "Portability.h"
// ASN.1 must compile when included directly, not only after the core umbrella header.
#include "Trace.h"
#include <memory>
#include <vector>
#include <cstdlib>

// ---- Referenced classes and types ----

// ---- Helper types and constants ----

// ---- Helper functions ----
namespace WPEFramework {
namespace Core {
    namespace ASN1 {

        // Copies share bytes and logical size; concurrent mutation still requires caller synchronization.
        // PUBLIC_INTERFACE
        /** Shared ASN.1 byte storage with uint16_t capacity and checked logical size. */
        class Buffer {
        private:
            /** Own the allocation and shared size without an in-band, wrapping reference counter. */
            struct Storage {
                explicit Storage(const uint16_t length)
                    : bytes(length), size(length) {}
                std::vector<uint8_t> bytes;
                uint16_t size;
            };

        public:
            // PUBLIC_INTERFACE
            /** Create empty shared storage. */
            Buffer() = default;
            // PUBLIC_INTERFACE
            /** Allocate length bytes using the standard allocator's failure policy. */
            Buffer(const uint16_t length)
                : _buffer(length == 0 ? nullptr : std::make_shared<Storage>(length)) {}
            // PUBLIC_INTERFACE
            /** Share the source's allocation and logical size. */
            Buffer(const Buffer& copy) = default;
            // PUBLIC_INTERFACE
            /** Release this owner's reference; the last owner frees the allocation. */
            ~Buffer() = default;
            // PUBLIC_INTERFACE
            /** Share RHS storage and return this buffer; self-assignment is safe. */
            Buffer& operator=(const Buffer& RHS) = default;

        public:
            // PUBLIC_INTERFACE
            /** Set shared logical length; requests beyond allocated capacity leave it unchanged. */
            inline void Size(const uint16_t length)
            {
                // Reject without mutation; the framework normally disables exception handling.
                if (length > (_buffer ? _buffer->bytes.size() : 0)) {
                    return;
                }
                if (_buffer != nullptr) {
                    _buffer->size = length;
                }
            }
            // PUBLIC_INTERFACE
            /** Return the shared logical byte count, or zero for empty storage. */
            inline uint16_t Size() const
            {
                return (_buffer != nullptr ? _buffer->size : 0);
            }
            // PUBLIC_INTERFACE
            /** Return a mutable byte at index; invalid indexing terminates the process. */
            inline uint8_t& operator[](const uint32_t index)
            {
                // A reference cannot report failure; enforce bounds even when assertions are disabled.
                if (index >= Size()) {
                    std::abort();
                }
                return (_buffer->bytes[index]);
            }
            // PUBLIC_INTERFACE
            /** Return a const byte at index; invalid indexing terminates the process. */
            inline const uint8_t& operator[](const uint32_t index) const
            {
                // Match mutable indexing without requiring exceptions or returning a dummy reference.
                if (index >= Size()) {
                    std::abort();
                }
                return (_buffer->bytes[index]);
            }

        private:
            std::shared_ptr<Storage> _buffer;
        };

        class OID {
        public:
            // PUBLIC_INTERFACE
            /** Bounded iterator over uint16_t OID arcs; malformed or oversized arcs end iteration. */
            class Iterator {
            public:
                // PUBLIC_INTERFACE
                /** Create an empty iterator. */
                Iterator()
                    : Iterator(nullptr, 0) {}
                // PUBLIC_INTERFACE
                /** Borrow length encoded bytes; caller retains their lifetime. */
                Iterator(const uint8_t* buffer, const uint16_t length)
                    : _length(length)
                    , _index(0)
                    , _buffer(buffer)
                    , _number(0)
                    , _second(0)
                    , _state(0)
                    , _valid(false) {}
                // PUBLIC_INTERFACE
                /** Copy iterator position without taking ownership of bytes. */
                Iterator(const Iterator& copy) = default;
                // PUBLIC_INTERFACE
                /** Destroy the iterator without releasing borrowed bytes. */
                ~Iterator() = default;
                // PUBLIC_INTERFACE
                /** Copy RHS position and return this iterator. */
                Iterator& operator=(const Iterator& RHS) = default;

            public:
                // PUBLIC_INTERFACE
                /** Return whether the last Next call produced an arc. */
                inline bool IsValid() const
                {
                    return (_valid);
                }
                // PUBLIC_INTERFACE
                /** Rewind to before the first arc. */
                inline void Reset()
                {
                    _index = 0;
                    _state = 0;
                    _valid = false;
                }
                // PUBLIC_INTERFACE
                /** Advance one complete arc; return false at end or on malformed input. */
                inline bool Next()
                {
                    _valid = false;
                    if (_state == 3) {
                        return (false);
                    }
                    if (_state == 1) {
                        _number = _second;
                        _state = 2;
                        _valid = true;
                    } else {
                        uint32_t value = 0;
                        // The packed first subidentifier may be 80 greater than a uint16_t arc.
                        const uint32_t limit = (_state == 0 ? 65535u + 80u : 65535u);
                        if (Decode(value, limit)) {
                            if (_state == 0) {
                                _number = (value < 40 ? 0 : (value < 80 ? 1 : 2));
                                _second = static_cast<uint16_t>(value - _number * 40);
                                _state = 1;
                            } else {
                                _number = static_cast<uint16_t>(value);
                            }
                            _valid = true;
                        } else {
                            _state = 3;
                        }
                    }
                    return (_valid);
                }
                // PUBLIC_INTERFACE
                /** Return the current uint16_t arc, or zero when invalid. */
                inline uint16_t Number() const
                {
                    return (_valid ? _number : 0);
                }
                // PUBLIC_INTERFACE
                /** Count complete arcs from the beginning without changing this position. */
                inline uint16_t Count() const
                {
                    uint16_t result = 0;
                    Iterator cursor(_buffer, _length);
                    while (cursor.Next()) {
                        ++result;
                    }
                    return (result);
                }

            private:
                /** Consume a bounded base-128 subidentifier; false means missing terminator or overflow. */
                bool Decode(uint32_t& value, const uint32_t limit)
                {
                    while ((_buffer != nullptr) && (_index < _length)) {
                        const uint8_t byte = _buffer[_index++];
                        const uint32_t payload = byte & 0x7F;
                        if (value > (limit - payload) / 128) {
                            return (false);
                        }
                        value = value * 128 + payload;
                        if ((byte & 0x80) == 0) {
                            return (true);
                        }
                    }
                    return (false);
                }

                uint16_t _length;
                uint16_t _index;
                const uint8_t* _buffer;
                uint16_t _number;
                uint16_t _second;
                uint8_t _state;
                bool _valid;
            };

        public:
            OID(const uint8_t identifier[], const uint16_t length)
                : _length(length > sizeof(_buffer) ? sizeof(_buffer) : length)
            {
                ::memcpy(_buffer, identifier, _length);
            }
            // PUBLIC_INTERFACE
            /** Encode dotted decimal or 0x-prefixed uint16_t arcs; invalid input yields an empty OID. */
            OID(const string& identifier)
                : _length(0)
            {
                size_t position = 0;
                uint32_t first = 0;
                uint32_t arcs = 0;
                // Parse the final component too, and never publish a partially encoded invalid OID.
                while (position < identifier.length()) {
                    uint32_t value = 0;
                    uint32_t base = 10;
                    if ((identifier[position] == '0') && (position + 1 < identifier.length())
                        && ((identifier[position + 1] == 'x') || (identifier[position + 1] == 'X'))) {
                        base = 16;
                        position += 2;
                    }
                    const size_t begin = position;
                    while ((position < identifier.length()) && (identifier[position] != '.')) {
                        const TCHAR character = identifier[position++];
                        const uint32_t digit = (character >= '0' && character <= '9') ? character - '0'
                            : (character >= 'a' && character <= 'f') ? character - 'a' + 10
                            : (character >= 'A' && character <= 'F') ? character - 'A' + 10 : base;
                        if ((digit >= base) || (value > (65535u - digit) / base)) {
                            _length = 0;
                            return;
                        }
                        value = value * base + digit;
                    }
                    if ((position == begin) || ((arcs == 0) && (value > 2))
                        || ((arcs == 1) && (first < 2) && (value >= 40))) {
                        _length = 0;
                        return;
                    }
                    if (arcs == 0) {
                        first = value;
                    } else if (!Append(arcs == 1 ? first * 40 + value : value)) {
                        _length = 0;
                        return;
                    }
                    ++arcs;
                    if (position < identifier.length() && ++position == identifier.length()) {
                        _length = 0;
                        return;
                    }
                }
                if (arcs < 2) {
                    _length = 0;
                }
            }
            OID(const OID& copy)
            {
                ::memcpy(_buffer, copy._buffer, copy._length);
                _length = copy._length;
            }
            ~OID()
            {
            }

            OID& operator=(const OID& RHS)
            {
                ::memcpy(_buffer, RHS._buffer, RHS._length);
                _length = RHS._length;

                return (*this);
            }

        public:
            inline Iterator Elements() const
            {
                return (Iterator(_buffer, _length));
            }
            inline uint16_t Length() const
            {
                return (_length);
            }
            inline const uint8_t* Buffer() const
            {
                return (&(_buffer[0]));
            }
            // PUBLIC_INTERFACE
            /** Render complete decoded arcs as dotted text, including zero-valued arcs. */
            inline string Text() const
            {
                string result;
                Iterator index(_buffer, _length);

                while (index.Next() == true) {
                    string textValue;
                    uint16_t value = index.Number();

                    // Zero is a real arc, not an empty textual component.
                    do {
                        textValue = static_cast<char>((value % 10) + '0') + textValue;
                        value /= 10;
                    } while (value > 0);

                    if (result.empty() == true) {
                        result = textValue;
                    } else {
                        result = result + '.' + textValue;
                    }
                }
                return (result);
            }
            inline bool operator==(const OID& RHS) const
            {
                return (RHS._length == _length ? (memcmp(&(_buffer[0]), &(RHS._buffer[0]), _length) == 0) : false);
            }
            inline bool operator!=(const OID& RHS) const
            {
                return (!operator==(RHS));
            }

        private:
            /** Append one base-128 value, returning false without mutation if capacity is insufficient. */
            bool Append(uint32_t value)
            {
                uint8_t bytes[3];
                uint8_t count = 0;
                do {
                    bytes[count++] = value & 0x7F;
                    value >>= 7;
                } while (value != 0);
                if (_length + count > sizeof(_buffer)) {
                    return (false);
                }
                while (count != 0) {
                    --count;
                    _buffer[_length++] = bytes[count] | (count != 0 ? 0x80 : 0);
                }
                return (true);
            }

            uint8_t _buffer[255];
            uint16_t _length;
        };
        /**
     * name DER constants
     * These constants comply with DER encoded the ANS1 type tags.
     * DER encoding uses hexadecimal representation.
     * An example DER sequence is:\n
     * - 0x02 -- tag indicating INTEGER
     * - 0x01 -- length in octets
     * - 0x05 -- value
     * Such sequences are typically read into \c ::x509_buf.
     */
        enum enumType {
            TYPE_BOOLEAN = 0x01,
            TYPE_INTEGER = 0x02,
            TYPE_BIT_STRING = 0x03,
            TYPE_OCTET_STRING = 0x04,
            TYPE_NULL = 0x05,
            TYPE_OID = 0x06,
            TYPE_UTF8_STRING = 0x0C,
            TYPE_SEQUENCE = 0x10,
            TYPE_SET = 0x11,
            TYPE_PRINTABLE_STRING = 0x13,
            TYPE_T61_STRING = 0x14,
            TYPE_IA5_STRING = 0x16,
            TYPE_UTC_TIME = 0x17,
            TYPE_GENERALIZED_TIME = 0x18,
            TYPE_UNIVERSAL_STRING = 0x1C,
            TYPE_BMP_STRING = 0x1E,
            TYPE_PRIMITIVE = 0x00,
            TYPE_CONSTRUCTED = 0x20,
            TYPE_CONTEXT_SPECIFIC = 0x80
        };
        enum enumError {
            ASN1_OK = 0x00,
            ASN1_OUT_OF_DATA = 0x60, /**< Out of data when parsing an ASN1 data structure. */
            ASN1_UNEXPECTED_TAG = 0x62, /**< ASN1 tag was of an unexpected value. */
            ASN1_INVALID_LENGTH = 0x64, /**< Error when trying to determine the length or invalid length. */
            ASN1_LENGTH_MISMATCH = 0x66, /**< Actual length differs from expected length. */
            ASN1_INVALID_DATA = 0x68, /**< Data is invalid. (not used) */
            ASN1_MALLOC_FAILED = 0x6A, /**< Memory allocation failed */
            ASN1_BUF_TOO_SMALL = 0x6C /**< Buffer too small when writing ASN.1 data structure. */
        };

        class Sequence {
        public:
            /**
         * name ASN1 Error codes
         * These error codes are OR-ed to X509 error codes for
         * higher error granularity.
         * ASN1 is a standard to specify data structures.
         */
        public:
            Sequence()
                : _buffer(0)
                , _index(0)
                , _start(0)
                , _length(0)
            {
            }
            // PUBLIC_INTERFACE
            /** Iterate short-form TLVs in a bounded slice; invalid slices are empty. */
            Sequence(const Buffer& buffer, const uint16_t index = 0, const uint16_t length = ~0)
                : _buffer(buffer)
                , _index(0)
                , _start(index)
                , _length(index <= buffer.Size()
                    && (length == static_cast<uint16_t>(~0) || length <= buffer.Size() - index)
                    ? (length == static_cast<uint16_t>(~0) ? buffer.Size() : index + length) : index)
            {
            }
            Sequence(const Sequence& copy)
                : _buffer(copy._buffer)
                , _index(copy._index)
                , _start(copy._start)
                , _length(copy._length)
            {
            }
            ~Sequence()
            {
            }

            Sequence& operator=(const Sequence& RHS)
            {

                _buffer = RHS._buffer;
                _index = RHS._index;
                _start = RHS._start;
                _length = RHS._length;

                return (*this);
            }

        public:
            // PUBLIC_INTERFACE
            /** Rewind to before the first TLV of the original slice. */
            inline void Reset()
            {
                _index = 0;
            }
            // PUBLIC_INTERFACE
            /** Return true only for a complete short-form TLV in the slice and backing storage. */
            inline bool IsValid() const
            {
                // Validate before reading length, including after another owner shrinks the buffer.
                const uint32_t end = std::min<uint32_t>(_length, _buffer.Size());
                return ((_index > _start) && (_index < end)
                    && ((_buffer[_index] & 0x80) == 0)
                    && (_buffer[_index] <= end - _index - 1));
            }
            // PUBLIC_INTERFACE
            /** Advance across header and payload; return false on exhaustion or malformed input. */
            inline bool Next()
            {
                if (_index == 0) {
                    _index = _start + 1;
                } else if (IsValid()) {
                    // _index points at length, so the next length follows two header bytes.
                    _index += (Length() + 2);
                } else {
                    return (false);
                }
                if (!IsValid()) {
                    _index = _length + 1;
                    return (false);
                }
                return (true);
            }
            inline enumType Tag() const
            {
                ASSERT(IsValid() == true);

                return (static_cast<enumType>(_buffer[_index - 1]));
            }
            inline uint8_t Length() const
            {
                ASSERT(IsValid() == true);

                return (static_cast<enumType>(_buffer[_index]));
            }
            // PUBLIC_INTERFACE
            /** Return borrowed payload bytes, or nullptr for empty/invalid values. */
            inline const uint8_t* Data() const
            {
                return (IsValid() && Length() != 0 ? &(_buffer[_index + 1]) : nullptr);
            }

            //-----------------------------------------------------
            // BOOLEAN to BOOL VALUE (8Bits)
            //-----------------------------------------------------
            // PUBLIC_INTERFACE
            /** Extract a one-byte boolean; return an ASN.1 error without changing value on failure. */
            inline enumError Value(bool& value) const
            {
                // Check extent, tag and scalar width before reading or mutating output.
                const enumError error = Validate(TYPE_BOOLEAN, 1, 1);
                if (error != ASN1_OK) { return (error); }
                ASSERT(Tag() == TYPE_BOOLEAN);
                ASSERT(Length() == 1);

                value = (_buffer[_index + 1] != 0);

                return (Length() <= 1 ? ASN1_OK : ASN1_BUF_TOO_SMALL);
            }
            //-----------------------------------------------------
            // INTEGER to SCALAR VALUE (8Bits)
            //-----------------------------------------------------
            // PUBLIC_INTERFACE
            /** Extract a one-byte integer; preserve value if extent, tag or width validation fails. */
            inline enumError Value(signed char& value) const
            {
                // Reject empty and oversized integers before accessing payload bytes.
                const enumError error = Validate(TYPE_INTEGER, 1, 1);
                if (error != ASN1_OK) { return (error); }
                ASSERT(Tag() == TYPE_INTEGER);
                value = _buffer[_index + 1];
                return (Length() <= 1 ? ASN1_OK : ASN1_BUF_TOO_SMALL);
            }
            // PUBLIC_INTERFACE
            /** Extract a one-byte unsigned integer; preserve value on validation failure. */
            inline enumError Value(unsigned char& value) const
            {
                // Reject empty and oversized integers before accessing payload bytes.
                const enumError error = Validate(TYPE_INTEGER, 1, 1);
                if (error != ASN1_OK) { return (error); }
                ASSERT(Tag() == TYPE_INTEGER);
                value = _buffer[_index + 1];
                return (Length() <= 1 ? ASN1_OK : ASN1_BUF_TOO_SMALL);
            }
            //-----------------------------------------------------
            // INTEGER to SCALAR VALUE (16Bits)
            //-----------------------------------------------------
            // PUBLIC_INTERFACE
            /** Extract an integer of at most two bytes; preserve value on validation failure. */
            inline enumError Value(int16_t& value) const
            {
                // Validate the full scalar extent before reading it.
                const enumError error = Validate(TYPE_INTEGER, 1, 2);
                if (error != ASN1_OK) { return (error); }
                ASSERT(Tag() == TYPE_INTEGER);
                value = _buffer[_index + 1];
                if (Length() > 1) {
                    value = (value << 8) | _buffer[_index + 2];
                }
                return (Length() <= 2 ? ASN1_OK : ASN1_BUF_TOO_SMALL);
            }
            // PUBLIC_INTERFACE
            /** Extract an unsigned integer of at most two bytes; preserve value on validation failure. */
            inline enumError Value(uint16_t& value) const
            {
                // Validate the full scalar extent before reading it.
                const enumError error = Validate(TYPE_INTEGER, 1, 2);
                if (error != ASN1_OK) { return (error); }
                ASSERT(Tag() == TYPE_INTEGER);
                value = _buffer[_index + 1];
                if (Length() > 1) {
                    value = (value << 8) | _buffer[_index + 2];
                }
                return (Length() <= 2 ? ASN1_OK : ASN1_BUF_TOO_SMALL);
            }
            //-----------------------------------------------------
            // INTEGER to SCALAR VALUE (32Bits)
            //-----------------------------------------------------
            // PUBLIC_INTERFACE
            /** Extract an unsigned integer of at most four bytes; preserve value on validation failure. */
            inline enumError Value(uint32_t& value) const
            {
                // Validate the full scalar extent before reading it.
                const enumError error = Validate(TYPE_INTEGER, 1, 4);
                if (error != ASN1_OK) { return (error); }
                ASSERT(Tag() == TYPE_INTEGER);
                value = _buffer[_index + 1];
                if (Length() > 1) {
                    value = (value << 8) | _buffer[_index + 2];
                }
                if (Length() > 2) {
                    value = (value << 8) | _buffer[_index + 3];
                }
                if (Length() > 3) {
                    value = (value << 8) | _buffer[_index + 4];
                }
                return (Length() <= 4 ? ASN1_OK : ASN1_BUF_TOO_SMALL);
            }
            // PUBLIC_INTERFACE
            /** Extract an integer of at most four bytes; preserve value on validation failure. */
            inline enumError Value(int32_t& value) const
            {
                // Validate the full scalar extent before reading it.
                const enumError error = Validate(TYPE_INTEGER, 1, 4);
                if (error != ASN1_OK) { return (error); }
                ASSERT(Tag() == TYPE_INTEGER);
                value = _buffer[_index + 1];
                if (Length() > 1) {
                    value = (value << 8) | _buffer[_index + 2];
                }
                if (Length() > 2) {
                    value = (value << 8) | _buffer[_index + 3];
                }
                if (Length() > 3) {
                    value = (value << 8) | _buffer[_index + 4];
                }
                return (Length() <= 4 ? ASN1_OK : ASN1_BUF_TOO_SMALL);
            }
            //-----------------------------------------------------
            // INTEGER to SCALAR VALUE (64Bits)
            //-----------------------------------------------------
            // PUBLIC_INTERFACE
            /** Extract an unsigned integer of at most eight bytes; preserve value on validation failure. */
            inline enumError Value(uint64_t& value) const
            {
                // Validate the full scalar extent before reading it.
                const enumError error = Validate(TYPE_INTEGER, 1, 8);
                if (error != ASN1_OK) { return (error); }
                ASSERT(Tag() == TYPE_INTEGER);
                value = _buffer[_index + 1];
                if (Length() > 1) {
                    value = (value << 8) | _buffer[_index + 2];
                }
                if (Length() > 2) {
                    value = (value << 8) | _buffer[_index + 3];
                }
                if (Length() > 3) {
                    value = (value << 8) | _buffer[_index + 4];
                }
                if (Length() > 4) {
                    value = (value << 8) | _buffer[_index + 5];
                }
                if (Length() > 5) {
                    value = (value << 8) | _buffer[_index + 6];
                }
                if (Length() > 6) {
                    value = (value << 8) | _buffer[_index + 7];
                }
                if (Length() > 7) {
                    value = (value << 8) | _buffer[_index + 8];
                }
                return (Length() <= 8 ? ASN1_OK : ASN1_BUF_TOO_SMALL);
            }
            // PUBLIC_INTERFACE
            /** Extract an integer of at most eight bytes; preserve value on validation failure. */
            inline enumError Value(int64_t& value) const
            {
                // Validate the full scalar extent before reading it.
                const enumError error = Validate(TYPE_INTEGER, 1, 8);
                if (error != ASN1_OK) { return (error); }
                ASSERT(Tag() == TYPE_INTEGER);
                value = _buffer[_index + 1];
                if (Length() > 1) {
                    value = (value << 8) | _buffer[_index + 2];
                }
                if (Length() > 2) {
                    value = (value << 8) | _buffer[_index + 3];
                }
                if (Length() > 3) {
                    value = (value << 8) | _buffer[_index + 4];
                }
                if (Length() > 4) {
                    value = (value << 8) | _buffer[_index + 5];
                }
                if (Length() > 5) {
                    value = (value << 8) | _buffer[_index + 6];
                }
                if (Length() > 6) {
                    value = (value << 8) | _buffer[_index + 7];
                }
                if (Length() > 7) {
                    value = (value << 8) | _buffer[_index + 8];
                }
                // The signed 64-bit overload accepts eight bytes, just like the unsigned one.
                return (ASN1_OK);
            }
            //-----------------------------------------------------
            // OID to OID class
            //-----------------------------------------------------
            // PUBLIC_INTERFACE
            /** Copy a nonempty OID payload; preserve value if TLV validation fails. */
            inline enumError Value(OID& value) const
            {
                // An OID needs at least one encoded subidentifier.
                const enumError error = Validate(TYPE_OID, 1, 127);
                if (error != ASN1_OK) { return (error); }
                ASSERT(Tag() == TYPE_OID);
                value = OID(&_buffer[_index + 1], Length());

                return (ASN1_OK);
            }
            //-----------------------------------------------------
            // UTF8String to string class
            //-----------------------------------------------------
            // PUBLIC_INTERFACE
            /** Copy a UTF8String payload, including empty strings; preserve value on validation failure. */
            inline enumError Value(string& value) const
            {
                // Empty terminal strings have no addressable payload byte.
                const enumError error = Validate(TYPE_UTF8_STRING, 0, 127);
                if (error != ASN1_OK) { return (error); }
                ASSERT(Tag() == TYPE_UTF8_STRING);
                value = (Length() == 0 ? string() : string(reinterpret_cast<const char*>(Data()), Length()));

                return (ASN1_OK);
            }
            //-----------------------------------------------------
            // SEQUENCE to ASN1Sequence class
            //-----------------------------------------------------
            // PUBLIC_INTERFACE
            /** Share a nested sequence's bounded payload slice; preserve value on validation failure. */
            inline enumError Value(Sequence& value) const
            {
                // DER sequences use the constructed bit; retain support for the legacy enum tag.
                if (!IsValid()) { return (ASN1_OUT_OF_DATA); }
                if ((Tag() != TYPE_SEQUENCE) && (Tag() != (TYPE_SEQUENCE | TYPE_CONSTRUCTED))) {
                    return (ASN1_UNEXPECTED_TAG);
                }
                value = Sequence(_buffer, static_cast<uint16_t>(_index + 1), Length());

                return (ASN1_OK);
            }

        private:
            /** Validate typed extraction without changing the caller's output. */
            enumError Validate(const enumType tag, const uint8_t minimum, const uint8_t maximum) const
            {
                if (!IsValid()) { return (ASN1_OUT_OF_DATA); }
                if (Tag() != tag) { return (ASN1_UNEXPECTED_TAG); }
                if (Length() < minimum) { return (ASN1_INVALID_LENGTH); }
                if (Length() > maximum) { return (ASN1_BUF_TOO_SMALL); }
                return (ASN1_OK);
            }

            Buffer _buffer;
            uint32_t _index;
            uint32_t _start;
            uint32_t _length;
        };
    }
}
}

#endif // __ASN1_H
