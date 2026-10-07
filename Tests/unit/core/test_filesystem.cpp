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
#include <filesystem>
#include <atomic>
#include <thread>
#ifdef __LINUX__
#include <sys/syscall.h>
#include <linux/fs.h>
#endif

namespace Thunder {
namespace Tests {
namespace Core {

    TEST (test_file, file)
    {
        ::Thunder::Core::File file;
        ::Thunder::Core::File fileObj1("Sample.txt");
        fileObj1.Create(true);
        ::Thunder::Core::File fileObj2(fileObj1);
        fileObj2.SetSize(150);

        EXPECT_TRUE(fileObj1.Open());
        EXPECT_TRUE(fileObj1.Append());
        EXPECT_TRUE(fileObj1.Create());
        EXPECT_TRUE(fileObj1.Create(::Thunder::Core::File::USER_WRITE));
        EXPECT_TRUE(fileObj1.Open(true));

        char buffer[] = "New  Line is added to the File.";
        if (fileObj1.IsOpen()) {
          fileObj1.Write(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        }
        if (fileObj1.IsOpen()) {
            fileObj1.Read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        }

        static string fileName = file.FileName("/home/file/datafile.txt");
        EXPECT_EQ(fileName, "datafile");
        static string filenameExt = file.FileNameExtended("/home/file/datafile.txt");
        EXPECT_EQ(filenameExt, "datafile.txt");
        static string pathName = file.PathName("/home/file/datafile.txt");
        EXPECT_EQ(pathName, "/home/file/");
        static string extension = file.Extension("/home/file/datafile.txt");
        EXPECT_EQ(extension, "txt");
        fileObj1.Destroy();
    }

    TEST (test_file, file_functions)
    {
        ::Thunder::Core::File fileObj1("Sample2.txt");
        fileObj1.Create(true);
        char buffer[] = "Sample2.txt is moved to newFile.txt";
        fileObj1.Write(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        EXPECT_TRUE(fileObj1.IsOpen());
        EXPECT_TRUE(fileObj1.Exists());
        EXPECT_FALSE(fileObj1.IsReadOnly());
        EXPECT_FALSE(fileObj1.IsHidden());
        EXPECT_FALSE(fileObj1.IsSystem());
        EXPECT_FALSE(fileObj1.IsArchive());
        EXPECT_FALSE(fileObj1.IsDirectory());
        EXPECT_FALSE(fileObj1.IsLink());
        EXPECT_FALSE(fileObj1.IsCompressed());
        EXPECT_FALSE(fileObj1.IsEncrypted());
        uint64_t size = 0;
        EXPECT_EQ(fileObj1.Size(), size);
        //EXPECT_EQ(fileObj1.DuplicateHandle(), 11); TODO
        EXPECT_TRUE(fileObj1.Move("newFile.txt"));
        fileObj1.Destroy();
    }

    TEST (test_file, directory)
    {
        string path = "home/file";
        ::Thunder::Core::Directory dirOne(path.c_str());
        ::Thunder::Core::Directory dirTwo(path.c_str(), _T("*"));
        ::Thunder::Core::Directory dirThree = dirOne ;

        EXPECT_TRUE(dirOne.CreatePath());
        EXPECT_FALSE(dirOne.Create());
        EXPECT_FALSE(dirThree.IsValid());
        EXPECT_TRUE(dirOne.Next());

        char buffer[15];
        string currenPath = "..";
        snprintf(buffer,(path.size() + currenPath.size() + 2), "%s/%s",path.c_str(), currenPath.c_str());
#ifdef BUILD_ARM
        if ((dirOne.Current(), buffer) == 0) {
#else
        if (strcmp(dirOne.Current().c_str(), buffer) == 0) {
#endif
            EXPECT_EQ(dirOne.Current(), buffer);
            EXPECT_EQ(dirOne.Name(), currenPath.c_str());
        } else {
            currenPath = ".";
            snprintf(buffer,(path.size() + currenPath.size() + 2), "%s/%s",path.c_str(), currenPath.c_str());
            EXPECT_EQ(dirOne.Current(), buffer);
            EXPECT_EQ(dirOne.Name(), currenPath.c_str());
        }

        EXPECT_TRUE(dirOne.IsDirectory());
        dirOne.Reset();
        EXPECT_TRUE(dirOne.Next());
        std::filesystem::remove_all("home");
    }

    TEST (test_file, directory_normalize_path)
    {
        EXPECT_EQ(::Thunder::Core::Directory::Normalize(""), "");

        EXPECT_EQ(::Thunder::Core::Directory::Normalize("/"), "/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("/."), "/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("/././././"), "/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("////"), "/");

        EXPECT_EQ(::Thunder::Core::Directory::Normalize("."), "./");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("./"), "./");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("././././././././"), "./");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("./././././././."), "./");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize(".////"), "./");

        EXPECT_EQ(::Thunder::Core::Directory::Normalize(".."), "../");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("../"), "../");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("../../.."), "../../../");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("../../../"), "../../../");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("./../"), "../");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("././../"), "../");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("././../.."), "../../");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("..///"), "../");

        EXPECT_EQ(::Thunder::Core::Directory::Normalize("/foo/bar"), "/foo/bar/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("foo/bar/"), "foo/bar/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("/foo/bar/."), "/foo/bar/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("foo/bar/.."), "foo/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("/foo/bar/./"), "/foo/bar/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("foo/bar/../"), "foo/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("/foo/bar/."), "/foo/bar/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("foo/bar/.////"), "foo/bar/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("/foo/././././bar"), "/foo/bar/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("foo/bar/quux/../../xyzzy"), "foo/xyzzy/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("../foo/bar/."), "../foo/bar/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("../foo/bar/.."), "../foo/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("/foo/bar/.") , "/foo/bar/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("foo////bar////"), "foo/bar/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("///foo////bar////"), "/foo/bar/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("././././foo/bar"), "foo/bar/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("./foo/bar"), "foo/bar/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("foo////bar////././././"), "foo/bar/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("foo////bar////././././."), "foo/bar/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("/foo////bar////././././."), "/foo/bar/");

        // Cases where navigating upwards compacts completely
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("foo/bar/../.."), "./");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("foo/bar/../.."), "./");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("./foo/../bar/.."), "./");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("./foo/../bar/../.."), "../");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("/foo/../bar/.."), "/");

        // Negative cases navigating past root
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("/.."), "");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("/../.."), "");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("/./.."), "");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("/foo/../bar/../.."), "");

#ifdef __WINDOWS__
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("C:\\foo\\\\bar\\.\\.\\quux\\..\\."), "C:/foo/bar/"); 
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("C:\\foo\\bar\\..\\.."), "C:/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("C:\\foo\\bar\\.."), "C:/foo");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("C:\\foo\\bar\\..\\"), "C:/foo");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("C:\\foo\\bar\\."), "C:/foo/bar");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("C:\\foo\\bar\\.\\"), "C:/foo/bar");

        EXPECT_EQ(::Thunder::Core::Directory::Normalize("C:\\.\\foo\\bar\\..\\.."), "C:/");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("C:\\foo\\bar\\..\\..\\.."), "");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("C:\\.."), "");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("C:\\\\"), "");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("C:\\..\\.."), "");
#endif
    }

    TEST (test_file, file_normalize_path)
    {
        EXPECT_EQ(::Thunder::Core::File::Normalize("./foo"), "foo");
        EXPECT_EQ(::Thunder::Core::File::Normalize("./../foo"), "../foo");
        EXPECT_EQ(::Thunder::Core::File::Normalize("foo///bar"), "foo/bar");
        EXPECT_EQ(::Thunder::Core::File::Normalize("/foo///bar"), "/foo/bar");
        EXPECT_EQ(::Thunder::Core::File::Normalize("foo/../bar"), "bar");
        EXPECT_EQ(::Thunder::Core::File::Normalize("/foo/../bar"), "/bar");
        EXPECT_EQ(::Thunder::Core::File::Normalize("/foo/.././././bar"), "/bar");
        EXPECT_EQ(::Thunder::Core::File::Normalize("foo/../../../.././././././bar"), "../../../bar");

        // Negative test cases, all fail because they point to a directory
        EXPECT_EQ(::Thunder::Core::File::Normalize("/"), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize("."), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize(".."), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize("/././././././"), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize("foo/.."), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize("foo/."), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize("/foo/.."), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize("/foo/."), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize("/foo/bar/"), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize("foo/bar///../.."), "");
    }

    TEST (test_file, file_safe_normalize_path)
    {
        EXPECT_EQ(::Thunder::Core::File::Normalize("./foo", true), "foo");
        EXPECT_EQ(::Thunder::Core::File::Normalize("foo///bar", true), "foo/bar");
        EXPECT_EQ(::Thunder::Core::File::Normalize("/foo///bar", true), "/foo/bar");
        EXPECT_EQ(::Thunder::Core::File::Normalize("foo/../bar", true), "bar");
        EXPECT_EQ(::Thunder::Core::File::Normalize("/foo/../bar", true), "/bar");
        EXPECT_EQ(::Thunder::Core::File::Normalize("/foo/.././././bar", true), "/bar");

        // Negative test cases, all fail because they point to a directory
        EXPECT_EQ(::Thunder::Core::File::Normalize("/", true), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize(".", true), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize("..", true), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize("/././././././", true), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize("foo/..", true), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize("foo/.", true), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize("/foo/..", true), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize("/foo/.", true), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize("/foo/bar/", true), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize("foo/bar///../..", true), "");

        // Negative test cases, all fail because they point past current dir
        EXPECT_EQ(::Thunder::Core::File::Normalize("./../foo", true), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize("../foo", true), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize("../../foo", true), "");
        EXPECT_EQ(::Thunder::Core::File::Normalize("foo/../../bar", true), "");

        // Negative test cases, all fail because they point past root
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("/../foo", true), "");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("/../../foo", true), "");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("/./../foo", true), "");
        EXPECT_EQ(::Thunder::Core::Directory::Normalize("/foo/../bar/../../quux", true), "");
    }


    TEST(test_file_p1, path_boundaries)
    {
        EXPECT_FALSE(::Thunder::Core::Directory("").CreatePath());
        EXPECT_FALSE(::Thunder::Core::Directory(std::string(300, 'x').c_str()).CreatePath());
        EXPECT_FALSE(::Thunder::Core::Directory("").Destroy());
    }

    TEST(test_file_p1, resize_and_exclusive_creation)
    {
        const auto path = std::filesystem::temp_directory_path() / "thunder-p1-exclusive.txt";
        std::filesystem::remove(path);
        ::Thunder::Core::File file(path.string());
        ASSERT_TRUE(file.Create(true));
        const uint8_t bytes[] = { 1, 2, 3, 4 };
        ASSERT_EQ(file.Write(bytes, sizeof(bytes)), sizeof(bytes));
        EXPECT_TRUE(file.SetSize(12));
        EXPECT_EQ(file.Size(), 12u);
        EXPECT_EQ(file.Position(), 4);
        EXPECT_TRUE(file.SetSize(2));
        EXPECT_EQ(file.Size(), 2u);
        file.Close();
        ::Thunder::Core::File second(path.string());
        EXPECT_FALSE(second.Create(true));
        ASSERT_TRUE(file.Open(true));
        uint8_t read[2] = {};
        EXPECT_EQ(file.Read(read, sizeof(read)), 2u);
        EXPECT_EQ(read[0], 1);
        EXPECT_EQ(read[1], 2);
        file.Close();
        std::filesystem::remove(path);
    }

#ifdef __LINUX__
    TEST(test_file_p1, links_are_not_traversed)
    {
        char location[] = "/tmp/thunder-p1-files-XXXXXX";
        ASSERT_NE(mkdtemp(location), nullptr);
        const std::filesystem::path base(location);
        const auto root = base / "root";
        const auto outside = base / "outside";
        std::filesystem::create_directories(root / "nested");
        std::filesystem::create_directories(outside);
        ::Thunder::Core::File secret((outside / "secret").string());
        ASSERT_TRUE(secret.Create());
        const uint8_t outsideValue = 99;
        const uint8_t value = 42;
        ASSERT_EQ(secret.Write(&outsideValue, 1), 1u);
        secret.Close();
        std::filesystem::create_directory_symlink(outside, root / "escape");
        std::filesystem::create_symlink(outside / "secret", root / "link");
        ::Thunder::Core::File checked;
        EXPECT_FALSE(checked.OpenUnderRoot(root.string(), "link"));
        EXPECT_FALSE(checked.OpenUnderRoot(root.string(), "escape/secret"));
        EXPECT_FALSE(checked.OpenUnderRoot(root.string(), "../outside/secret"));
        ::Thunder::Core::File ordinary((root / "index.html").string());
        ASSERT_TRUE(ordinary.Create());
        ASSERT_EQ(ordinary.Write(&value, 1), 1u);
        ordinary.Close();
        ASSERT_TRUE(checked.OpenUnderRoot(root.string(), "index.html"));
        std::filesystem::remove(root / "index.html");
        std::filesystem::create_symlink(outside / "secret", root / "index.html");
        uint8_t output = 0;
        EXPECT_EQ(checked.Read(&output, 1), 1u);
        EXPECT_EQ(output, value);
        checked.Close();
        EXPECT_TRUE(::Thunder::Core::Directory(root.string().c_str()).Destroy());
        EXPECT_TRUE(std::filesystem::exists(outside / "secret"));
        std::filesystem::remove_all(base);
    }

    TEST(test_file_p1, concurrent_directory_replacement_does_not_escape)
    {
        char location[] = "/tmp/thunder-p1-remove-race-XXXXXX";
        ASSERT_NE(mkdtemp(location), nullptr);
        const std::filesystem::path base(location);
        const auto root = base / "root";
        const auto outside = base / "outside";
        std::filesystem::create_directory(outside);
        ::Thunder::Core::File sentinel((outside / "keep").string());
        ASSERT_TRUE(sentinel.Create());
        const uint8_t forbidden = 99;
        ASSERT_EQ(sentinel.Write(&forbidden, 1), 1u);
        sentinel.Close();
        for (unsigned iteration = 0; iteration < 100; ++iteration) {
            std::filesystem::create_directories(root / "victim");
            const auto victim = root / "victim";
            const auto swap = base / "swap";
            std::filesystem::create_directory_symlink(outside, swap);
            std::atomic<bool> done{false};
            std::atomic<unsigned> exchanges{0};
            std::atomic<int> fixtureError{0};
            std::thread attacker([&] {
                while (!done) {
                    if (syscall(SYS_renameat2, AT_FDCWD, victim.c_str(),
                            AT_FDCWD, swap.c_str(), RENAME_EXCHANGE) == 0) {
                        ++exchanges;
                    } else {
                        if (errno != ENOENT) fixtureError = errno;
                        break;
                    }
                }
            });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (exchanges == 0 && fixtureError == 0
                && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
            // Namespace changes may legitimately cause removal to report failure.
            ::Thunder::Core::Directory(root.string().c_str()).Destroy();
            done = true;
            attacker.join();
            EXPECT_EQ(fixtureError.load(), 0);
            EXPECT_GT(exchanges.load(), 0u);
            ::Thunder::Core::File checked((outside / "keep").string());
            ASSERT_TRUE(checked.Open(true)) << "External sentinel lost in iteration " << iteration;
            uint8_t byte = 0;
            EXPECT_EQ(checked.Read(&byte, 1), 1u);
            EXPECT_EQ(byte, forbidden);
            checked.Close();
            std::filesystem::remove_all(root);
            std::filesystem::remove_all(swap);
        }
        std::filesystem::remove_all(base);
    }

    TEST(test_file_p1, permission_failure_is_reported)
    {
        ASSERT_NE(geteuid(), 0u) << "Permission denial requires an unprivileged test process";
        char location[] = "/tmp/thunder-p1-permission-XXXXXX";
        ASSERT_NE(mkdtemp(location), nullptr);
        const std::filesystem::path base(location);
        const auto locked = base / "locked";
        std::filesystem::create_directory(locked);
        ::Thunder::Core::File file((locked / "keep").string());
        ASSERT_TRUE(file.Create());
        file.Close();
        ASSERT_EQ(chmod(locked.c_str(), 0500), 0);
        EXPECT_FALSE(::Thunder::Core::Directory(base.string().c_str()).Destroy());
        EXPECT_TRUE(std::filesystem::exists(locked / "keep"));
        EXPECT_EQ(chmod(locked.c_str(), 0700), 0);
        std::filesystem::remove_all(base);
    }

    TEST(test_file_p1, concurrent_file_replacement_stays_confined)
    {
        char location[] = "/tmp/thunder-p1-race-XXXXXX";
        ASSERT_NE(mkdtemp(location), nullptr);
        const std::filesystem::path base(location);
        const auto root = base / "root";
        std::filesystem::create_directory(root);
        ::Thunder::Core::File safe((root / "safe").string());
        ::Thunder::Core::File outside((base / "secret").string());
        ASSERT_TRUE(safe.Create());
        ASSERT_TRUE(outside.Create());
        const uint8_t allowed = 42, forbidden = 99;
        ASSERT_EQ(safe.Write(&allowed, 1), 1u);
        ASSERT_EQ(outside.Write(&forbidden, 1), 1u);
        safe.Close();
        outside.Close();
        std::filesystem::create_hard_link(root / "safe", root / "index");
        std::atomic<bool> done{false};
        std::atomic<unsigned> replacements{0};
        std::atomic<bool> fixtureError{false};
        std::thread attacker([&] {
            while (!done) {
                std::error_code error;
                std::filesystem::create_symlink(base / "secret", root / "next", error);
                if (!error) std::filesystem::rename(root / "next", root / "index", error);
                if (!error) std::filesystem::create_hard_link(root / "safe", root / "next", error);
                if (!error) std::filesystem::rename(root / "next", root / "index", error);
                if (error) { fixtureError = true; break; }
                ++replacements;
            }
        });
        for (unsigned i = 0; i < 1000; ++i) {
            ::Thunder::Core::File checked;
            if (checked.OpenUnderRoot(root.string(), "index")) {
                uint8_t byte = 0;
                EXPECT_EQ(checked.Read(&byte, 1), 1u);
                EXPECT_EQ(byte, allowed);
            }
            std::this_thread::yield();
        }
        done = true;
        attacker.join();
        EXPECT_FALSE(fixtureError);
        EXPECT_GT(replacements.load(), 0u);
        EXPECT_TRUE(std::filesystem::exists(base / "secret"));
        std::filesystem::remove_all(base);
    }
#endif

} // Core
} // Tests
} // Thunder
