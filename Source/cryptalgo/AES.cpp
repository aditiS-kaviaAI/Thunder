/*
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

#include "AES.h"

namespace Thunder {
namespace Crypto {
    namespace {
        uint32_t Transform(mbedtls_aes_context& context, const aesType type,
            const int mode, const uint32_t length, const uint8_t* input,
            uint8_t* output, uint8_t* iv, size_t& offset)
        {
            // This API has no extra output capacity or padding-length result.
            if (((type == AES_ECB || type == AES_CBC) && (length % 16 != 0))
                || (length != 0 && (input == nullptr || output == nullptr))) {
                return Core::ERROR_BAD_REQUEST;
            }
            if (length == 0) {
                return Core::ERROR_NONE;
            }
            switch (type) {
            case AES_ECB:
                for (uint32_t index = 0; index < length; index += 16) {
                    const int result = mbedtls_aes_crypt_ecb(&context, mode, input + index, output + index);
                    if (result != 0) {
                        return result;
                    }
                }
                return Core::ERROR_NONE;
#if defined(MBEDTLS_CIPHER_MODE_CBC)
            case AES_CBC:
                return mbedtls_aes_crypt_cbc(&context, mode, length, iv, input, output);
#endif
#if defined(MBEDTLS_CIPHER_MODE_CFB)
            case AES_CFB8:
                return mbedtls_aes_crypt_cfb8(&context, mode, length, iv, input, output);
            case AES_CFB128:
                return mbedtls_aes_crypt_cfb128(&context, mode, length, &offset, iv, input, output);
#endif
#if defined(MBEDTLS_CIPHER_MODE_OFB)
            case AES_OFB:
                return mbedtls_aes_crypt_ofb(&context, length, &offset, iv, input, output);
#endif
            default:
                return Core::ERROR_UNAVAILABLE;
            }
        }
    }

    AESEncryption::AESEncryption(const aesType type)
        : _type(type), _context(), _offset(0)
    {
        mbedtls_aes_init(&_context);
        ::memset(_iv, 0, sizeof(_iv));
    }

    AESEncryption::~AESEncryption()
    {
        mbedtls_aes_free(&_context);
    }

    // PUBLIC_INTERFACE
    uint32_t AESEncryption::Key(const uint8_t length, const uint8_t key[])
    {
        /** Configure a 16, 24, or 32-byte key; return an error for invalid input. */
        if (key == nullptr || (length != 16 && length != 24 && length != 32)) {
            return Core::ERROR_BAD_REQUEST;
        }
        _offset = 0;
        return mbedtls_aes_setkey_enc(&_context, key, length << 3);
    }

    // PUBLIC_INTERFACE
    uint32_t AESEncryption::Encrypt(const uint32_t length, const uint8_t input[], uint8_t output[])
    {
        /** Encrypt length bytes into equally sized output; ECB/CBC require whole blocks. */
        return Transform(_context, _type, MBEDTLS_AES_ENCRYPT, length, input, output, _iv, _offset);
    }

    AESDecryption::AESDecryption(const aesType type)
        : _type(type), _context(), _offset(0)
    {
        mbedtls_aes_init(&_context);
        ::memset(_iv, 0, sizeof(_iv));
    }

    AESDecryption::~AESDecryption()
    {
        mbedtls_aes_free(&_context);
    }

    // PUBLIC_INTERFACE
    uint32_t AESDecryption::Key(const uint8_t length, const uint8_t key[])
    {
        /** Configure a 16, 24, or 32-byte key; stream modes use the encryption schedule. */
        if (key == nullptr || (length != 16 && length != 24 && length != 32)) {
            return Core::ERROR_BAD_REQUEST;
        }
        _offset = 0;
        if (_type == AES_CBC || _type == AES_ECB) {
            return mbedtls_aes_setkey_dec(&_context, key, length << 3);
        }
        return mbedtls_aes_setkey_enc(&_context, key, length << 3);
    }

    // PUBLIC_INTERFACE
    uint32_t AESDecryption::Decrypt(const uint32_t length, const uint8_t input[], uint8_t output[])
    {
        /** Decrypt length bytes without implicit padding; preserve stream IV/offset across calls. */
        return Transform(_context, _type, MBEDTLS_AES_DECRYPT, length, input, output, _iv, _offset);
    }
}
}
