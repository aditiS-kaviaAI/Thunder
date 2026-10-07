#include <gtest/gtest.h>
#include <ThunderTestRuntime.h>
#include <PluginServer.h>
#include <Controller.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

using namespace Thunder;

#ifdef HIBERNATE_SUPPORT_ENABLED
namespace Thunder {
namespace PluginHost {
// PUBLIC_INTERFACE
struct P1HostAccess {
    /** Test-only attachment and storage inspection for the production Service. */
    // PUBLIC_INTERFACE
    static Server::Service* Create(Server& server, const Plugin::Config& config)
    {
        /** Create a fixture-owned service using the initialized host configuration. */
        return Core::ServiceType<Server::Service>::Create<Server::Service>(
            server.Configuration(), config, server.Services(),
            Server::Service::CONFIGURED, Core::ProxyType<RPC::InvokeServer>());
    }
    // PUBLIC_INTERFACE
    static void Attach(Server::Service& service, RPC::IRemoteConnection* connection)
    {
        /** Borrow a connection for the duration of this synchronous fixture. */
        service._connection = connection;
        service.State(connection ? IShell::ACTIVATED : IShell::DEACTIVATED);
    }
    // PUBLIC_INTERFACE
    static size_t Slots(const Server::Service& service)
    {
        /** Return the number of retained PID-specific backend slots. */
        return service._hibernateStorage.size();
    }
    // PUBLIC_INTERFACE
    static void Cleanup(Server::Service& service)
    {
        /** Free fixture-owned metadata even after a failed assertion. */
        for (auto& slot : service._hibernateStorage) {
            delete static_cast<pid_t*>(slot.second);
        }
        service._hibernateStorage.clear();
        Attach(service, nullptr);
    }
    // PUBLIC_INTERFACE
    static void Release(Server::Service* service)
    {
        /** Detach the borrowed connection and release the fixture-owned service. */
        if (service != nullptr) {
            Cleanup(*service);
            service->Release();
        }
    }
};
}
}

namespace {
std::vector<pid_t> checkpointed;
std::vector<pid_t> awakened;
bool failSecondChild = true;

class Connection : public RPC::IRemoteConnection, public RPC::IMonitorableProcess {
public:
    // PUBLIC_INTERFACE
    uint32_t Id() const override { /** Fixture connection identifier. */ return 1; }
    // PUBLIC_INTERFACE
    uint32_t RemoteId() const override { /** Fixture parent process. */ return getpid(); }
    // PUBLIC_INTERFACE
    void* Acquire(uint32_t, const string&, uint32_t, uint32_t) override
    { /** No remote interfaces are needed for checkpointing. */ return nullptr; }
    // PUBLIC_INTERFACE
    void Terminate() override { /** Never terminate the test process. */ }
    // PUBLIC_INTERFACE
    uint32_t Launch() override { /** The disposable process tree already exists. */ return Core::ERROR_NONE; }
    // PUBLIC_INTERFACE
    void PostMortem() override { /** No remote crash collection is needed. */ }
    // PUBLIC_INTERFACE
    string Callsign() const override { /** Name of the synthetic remote service. */ return "P1Rollback"; }
    // PUBLIC_INTERFACE
    Core::instance_id ParentPID() const override { /** Real parent for production child discovery. */ return getpid(); }

    BEGIN_INTERFACE_MAP(Connection)
        INTERFACE_ENTRY(RPC::IRemoteConnection)
        INTERFACE_ENTRY(RPC::IMonitorableProcess)
    END_INTERFACE_MAP
};

struct Children {
    std::vector<pid_t> pids;
    Children()
    {
        for (int index = 0; index != 2; ++index) {
            const pid_t pid = fork();
            if (pid == 0) {
                // Only async-signal-safe calls after fork; no inherited host locks.
                for (;;) pause();
            }
            if (pid > 0) pids.push_back(pid);
        }
    }
    ~Children()
    {
        for (pid_t pid : pids) {
            kill(pid, SIGKILL);
            while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) { }
        }
    }
};
}

// PUBLIC_INTERFACE
extern "C" uint32_t __wrap_HibernateProcess(uint32_t, pid_t pid, const char*, const char*, void** storage)
{
    /** Fault at the real backend boundary after retaining PID-specific metadata. */
    EXPECT_NE(storage, nullptr);
    if (storage == nullptr) return 1;
    EXPECT_EQ(*storage, nullptr);
    if (*storage != nullptr) return 1;
    *storage = new pid_t(pid);
    checkpointed.push_back(pid);
    return failSecondChild && checkpointed.size() == 3 ? 1 : 0;
}

// PUBLIC_INTERFACE
extern "C" uint32_t __wrap_WakeupProcess(uint32_t, pid_t pid, const char*, const char*, void** storage)
{
    /** Require matching metadata and release only that process's slot. */
    EXPECT_NE(storage, nullptr);
    if (storage == nullptr || *storage == nullptr) {
        ADD_FAILURE() << "Missing checkpoint metadata for " << pid;
        return 1;
    }
    EXPECT_EQ(*static_cast<pid_t*>(*storage), pid);
    awakened.push_back(pid);
    delete static_cast<pid_t*>(*storage);
    *storage = nullptr;
    return 0;
}

TEST(HostP1, PartialHibernateRollbackAndRetry)
{
    Children children;
    ASSERT_EQ(children.pids.size(), 2u);
    TestCore::ThunderTestRuntime runtime;
    ASSERT_EQ(runtime.Initialize({}), Core::ERROR_NONE);
    Plugin::Config config;
    config.Callsign = "P1Rollback";
    auto* service = PluginHost::P1HostAccess::Create(runtime.Server(), config);
    auto* connection = Core::ServiceType<Connection>::Create<RPC::IRemoteConnection>();
    struct Cleanup {
        decltype(service) shell;
        RPC::IRemoteConnection* connection;
        ~Cleanup()
        {
            PluginHost::P1HostAccess::Release(shell);
            if (connection != nullptr) connection->Release();
        }
    } cleanup{service, connection};
    ASSERT_NE(service, nullptr);
    ASSERT_NE(connection, nullptr);
    PluginHost::P1HostAccess::Attach(*service, connection);
    checkpointed.clear();
    awakened.clear();
    failSecondChild = true;
    EXPECT_NE(service->Hibernate(1000), Core::ERROR_NONE);
    ASSERT_EQ(checkpointed.size(), 3u);
    EXPECT_EQ(checkpointed.front(), getpid());
    auto actualChildren = std::vector<pid_t>(checkpointed.begin() + 1, checkpointed.end());
    auto expectedChildren = children.pids;
    std::sort(actualChildren.begin(), actualChildren.end());
    std::sort(expectedChildren.begin(), expectedChildren.end());
    EXPECT_EQ(actualChildren, expectedChildren);
    auto reverse = checkpointed;
    std::reverse(reverse.begin(), reverse.end());
    EXPECT_EQ(awakened, reverse);
    EXPECT_EQ(service->State(), PluginHost::IShell::ACTIVATED);
    EXPECT_EQ(PluginHost::P1HostAccess::Slots(*service), 0u);

    checkpointed.clear();
    awakened.clear();
    failSecondChild = false;
    ASSERT_EQ(service->Hibernate(1000), Core::ERROR_NONE);
    EXPECT_EQ(service->State(), PluginHost::IShell::HIBERNATED);
    EXPECT_EQ(PluginHost::P1HostAccess::Slots(*service), 3u);
    EXPECT_EQ(service->Activate(PluginHost::IShell::REQUESTED), Core::ERROR_NONE);
    reverse = checkpointed;
    std::reverse(reverse.begin(), reverse.end());
    EXPECT_EQ(awakened, reverse);
    EXPECT_EQ(service->State(), PluginHost::IShell::ACTIVATED);
    EXPECT_EQ(PluginHost::P1HostAccess::Slots(*service), 0u);
}
#endif

namespace {
std::set<int> ListeningSockets()
{
    std::set<int> descriptors;
    DIR* directory = opendir("/proc/self/fd");
    if (directory == nullptr) return descriptors;
    while (dirent* entry = readdir(directory)) {
        char* end = nullptr;
        const long candidate = std::strtol(entry->d_name, &end, 10);
        if (end == entry->d_name || *end != '\0' || candidate < 0) continue;
        const int fd = static_cast<int>(candidate);
        int listening = 0;
        socklen_t size = sizeof(listening);
        sockaddr_in address{};
        socklen_t addressSize = sizeof(address);
        if (getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &listening, &size) == 0
            && listening && getsockname(fd, reinterpret_cast<sockaddr*>(&address), &addressSize) == 0
            && address.sin_family == AF_INET && address.sin_addr.s_addr == htonl(INADDR_LOOPBACK)) {
            descriptors.insert(fd);
        }
    }
    closedir(directory);
    return descriptors;
}

struct Response {
    int status = 0;
    string body;
};

Response Get(uint16_t port, const string& path)
{
    Response response;
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return response;
    struct Close { int fd; ~Close() { close(fd); } } cleanup{fd};
    timeval timeout{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) return response;
    const string request = "GET " + path + " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    size_t sent = 0;
    while (sent < request.size()) {
        const ssize_t count = send(fd, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return response;
        sent += static_cast<size_t>(count);
    }
    string bytes;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (bytes.size() < 65536 && std::chrono::steady_clock::now() < deadline) {
        char buffer[4096];
        const ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        bytes.append(buffer, static_cast<size_t>(count));
    }
    const size_t header = bytes.find("\r\n\r\n");
    if (header != string::npos) {
        std::istringstream firstLine(bytes);
        string version;
        firstLine >> version >> response.status;
        response.body = bytes.substr(header + 4);
    }
    return response;
}

struct Tree {
    string path;
    Tree()
    {
        char name[] = "/tmp/thunder-p1-http-XXXXXX";
        const char* created = mkdtemp(name);
        if (created != nullptr) path = created;
    }
    ~Tree()
    {
        if (!path.empty()) {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    }
};

uint16_t BoundPort(const std::set<int>& before)
{
    auto after = ListeningSockets();
    for (int fd : before) after.erase(fd);
    EXPECT_EQ(after.size(), 1u) << "Expected one new loopback HTTP listener";
    if (after.size() != 1) return 0;
    sockaddr_in address{};
    socklen_t size = sizeof(address);
    if (getsockname(*after.begin(), reinterpret_cast<sockaddr*>(&address), &size) != 0) return 0;
    return ntohs(address.sin_port);
}

std::vector<Plugin::Config> SecurityEnabledConfig()
{
    // Serialize the repository enum rather than guessing its configuration spelling.
    Plugin::Controller::Config settings;
    settings.SubSystems.Add() = Plugin::Configuration::subsystem::SECURITY;
    settings.Ui = false;
    Plugin::Config controller;
    controller.Callsign = "Controller";
    string json;
    settings.ToString(json);
    controller.Configuration = json;
    Plugin::Config similarlyPrefixed;
    similarlyPrefixed.Callsign = "ControllerEvil";
    similarlyPrefixed.ClassName = "P1UnloadedService";
    similarlyPrefixed.StartMode = Plugin::Configuration::startmode::DEACTIVATED;
    return {controller, similarlyPrefixed};
}

TEST(HostP1, SecurityEnabledControllerPrefixHttp)
{
    const auto before = ListeningSockets();
    TestCore::ThunderTestRuntime runtime;
    ASSERT_EQ(runtime.Initialize(SecurityEnabledConfig()), Core::ERROR_NONE);
    auto shell = runtime.GetShell("Controller");
    ASSERT_TRUE(shell.IsValid());
    ASSERT_TRUE(runtime.GetShell("ControllerEvil").IsValid());
    const uint16_t port = BoundPort(before);
    ASSERT_NE(port, 0);
    const string prefix = shell->WebPrefix();
    auto* security = runtime.Server().Configuration().Security();
    ASSERT_NE(security, nullptr);
    Web::Request denied;
    denied.Verb = Web::Request::HTTP_GET;
    denied.Path = prefix + "Evil";
    ASSERT_FALSE(security->Allowed(denied)) << "Fixture must retain enabled default security";
    for (const string& path : {prefix + "Evil", prefix + "2", string("/S")}) {
        EXPECT_EQ(Get(port, path).status, 401) << path;
    }
    for (const string& path : {prefix, prefix + "/Plugins"}) {
        const auto response = Get(port, path);
        EXPECT_NE(response.status, 0) << path;
        EXPECT_NE(response.status, 401) << path;
    }
}

TEST(HostP1, StaticHttpDeliveryRejectsEscapeAndReplacement)
{
    Tree tree;
    ASSERT_FALSE(tree.path.empty());
    const string root = tree.path + "/root/";
    ASSERT_TRUE(std::filesystem::create_directory(root));
    const string external = tree.path + "/external.html";
    const string allowed = root + "allowed.html";
    { std::ofstream file(external); file << "EXTERNAL-P1-SENTINEL"; ASSERT_TRUE(file.good()); }
    { std::ofstream file(allowed); file << "ALLOWED-P1-BYTES"; ASSERT_TRUE(file.good()); }
    ASSERT_EQ(symlink(external.c_str(), (root + "escape.html").c_str()), 0);
    const auto before = ListeningSockets();
    TestCore::ThunderTestRuntime runtime;
    ASSERT_EQ(runtime.Initialize(SecurityEnabledConfig()), Core::ERROR_NONE);
    auto shell = runtime.GetShell("Controller");
    ASSERT_TRUE(shell.IsValid());
    shell->EnableWebServer("P1", root);
    const uint16_t port = BoundPort(before);
    ASSERT_NE(port, 0);
    const string prefix = shell->WebPrefix() + "/P1/";
    const auto good = Get(port, prefix + "allowed.html");
    ASSERT_EQ(good.status, 200);
    EXPECT_EQ(good.body, "ALLOWED-P1-BYTES");
    for (const string& relative : {string("escape.html"), string("../external.html")}) {
        const auto rejected = Get(port, prefix + relative);
        EXPECT_EQ(rejected.status, 400) << relative;
        EXPECT_EQ(rejected.body.find("EXTERNAL-P1-SENTINEL"), string::npos);
    }
    ASSERT_EQ(unlink(allowed.c_str()), 0);
    ASSERT_EQ(symlink(external.c_str(), allowed.c_str()), 0);
    const auto replaced = Get(port, prefix + "allowed.html");
    EXPECT_EQ(replaced.status, 400);
    EXPECT_EQ(replaced.body.find("EXTERNAL-P1-SENTINEL"), string::npos);
    shell->DisableWebServer();
}
}
