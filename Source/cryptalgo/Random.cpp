/*
 * Copyright 2020 Metrological
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "Random.h"
#include <cstdlib>

#ifdef __WINDOWS__
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Thunder {
namespace Crypto {
    namespace {
        void Fill(void* destination, size_t length)
        {
#ifdef __WINDOWS__
            // Resolve the system provider without introducing a product dependency.
            using Provider = BOOLEAN (WINAPI*)(PVOID, ULONG);
            HMODULE library = LoadLibraryW(L"advapi32.dll");
            Provider provider = library == nullptr ? nullptr
                : reinterpret_cast<Provider>(GetProcAddress(library, "SystemFunction036"));
            const bool success = (provider != nullptr)
                && provider(destination, static_cast<ULONG>(length));
            if (library != nullptr) {
                FreeLibrary(library);
            }
            if (!success) {
                std::abort();
            }
#else
            int descriptor;
            do {
                descriptor = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
            } while ((descriptor < 0) && (errno == EINTR));
            if (descriptor < 0) {
                std::abort();
            }
            auto* bytes = static_cast<uint8_t*>(destination);
            size_t offset = 0;
            while (offset < length) {
                const ssize_t count = read(descriptor, bytes + offset, length - offset);
                if (count > 0) {
                    offset += static_cast<size_t>(count);
                } else if ((count < 0) && (errno == EINTR)) {
                    continue;
                } else {
                    close(descriptor);
                    // This legacy void API cannot convey failure: never return weak output.
                    std::abort();
                }
            }
            close(descriptor);
#endif
        }
    }

    // PUBLIC_INTERFACE
    void Reseed()
    {
        /** OS entropy requires no application seed; abort if its provider is unavailable. */
        uint8_t probe;
        Fill(&probe, sizeof(probe));
    }

    // PUBLIC_INTERFACE
    void Random(uint8_t& value)
    {
        /** Fill all output bits from OS entropy, aborting on provider failure. */
        Fill(&value, sizeof(value));
    }

    // PUBLIC_INTERFACE
    void Random(uint16_t& value)
    {
        /** Fill all output bits from OS entropy, aborting on provider failure. */
        Fill(&value, sizeof(value));
    }

    // PUBLIC_INTERFACE
    void Random(uint32_t& value)
    {
        /** Fill all output bits from OS entropy, aborting on provider failure. */
        Fill(&value, sizeof(value));
    }

    // PUBLIC_INTERFACE
    void Random(uint64_t& value)
    {
        /** Fill all output bits from OS entropy, aborting on provider failure. */
        Fill(&value, sizeof(value));
    }
}
}
