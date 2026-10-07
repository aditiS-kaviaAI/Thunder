#include <gtest/gtest.h>
#include <core/core.h>
#include <cryptalgo/SecureSocketPort.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <poll.h>
#include <thread>
#include <sys/socket.h>
#include <unistd.h>

namespace {
struct Credentials {
    EVP_PKEY* key = nullptr;
    X509* cert = nullptr;
    explicit Credentials(const char* identity)
    {
        EVP_PKEY_CTX* context = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        if (context == nullptr) return;
        if (EVP_PKEY_keygen_init(context) == 1
            && EVP_PKEY_CTX_set_rsa_keygen_bits(context, 2048) == 1) {
            EVP_PKEY_keygen(context, &key);
        }
        EVP_PKEY_CTX_free(context);
        if (key == nullptr) return;
        cert = X509_new();
        if (cert == nullptr) return;
        X509_set_version(cert, 2);
        ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
        X509_gmtime_adj(X509_getm_notBefore(cert), -60);
        X509_gmtime_adj(X509_getm_notAfter(cert), 3600);
        X509_set_pubkey(cert, key);
        X509_NAME* name = X509_get_subject_name(cert);
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
            reinterpret_cast<const unsigned char*>("P1 fixture"), -1, -1, 0);
        X509_set_issuer_name(cert, name);
        X509_EXTENSION* extension = X509V3_EXT_conf_nid(nullptr, nullptr,
            NID_subject_alt_name, const_cast<char*>(identity));
        if (extension != nullptr) {
            X509_add_ext(cert, extension, -1);
            X509_EXTENSION_free(extension);
        }
        X509_sign(cert, key, EVP_sha256());
    }
    ~Credentials() { X509_free(cert); EVP_PKEY_free(key); }
};

class Port final : public Thunder::Crypto::SecureSocketPort {
public:
    using SecureSocketPort::SecureSocketPort;
    std::atomic<int> verdict{0};
    // PUBLIC_INTERFACE
    uint16_t SendData(uint8_t*, const uint16_t) override
    { /** No application payload is needed for handshake tests. */ return 0; }
    // PUBLIC_INTERFACE
    uint16_t ReceiveData(uint8_t*, const uint16_t count) override
    { /** Consume received test bytes. */ return count; }
    // PUBLIC_INTERFACE
    void StateChange() override
    {
        /** Record only a completed handshake or a terminal transport error. */
        if (HasError()) verdict = -1;
        else if (IsOpen()) verdict = 1;
    }
};

class Accept final : public Thunder::Crypto::SecureSocketPort::IValidate {
public:
    // PUBLIC_INTERFACE
    bool Validate(const Thunder::Crypto::Certificate&) const override
    { /** Accept the presented test certificate under an explicit custom policy. */ return true; }
};

void SetTimeout(int descriptor)
{
    timeval timeout{2, 0};
    setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(descriptor, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

void Wait(Port& port)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (port.verdict == 0 && std::chrono::steady_clock::now() < deadline) {
        port.Trigger();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

TEST(TLSHandshakeP1, AcceptedOrdinaryAndMutualTls)
{
    Credentials credentials("IP:127.0.0.1");
    ASSERT_NE(credentials.key, nullptr);
    ASSERT_NE(credentials.cert, nullptr);
    for (int mode : {0, 1, 2}) {
        int descriptors[2];
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors), 0);
        SetTimeout(descriptors[1]);
        Thunder::Crypto::Certificate cert(credentials.cert);
        Thunder::Crypto::Key key(credentials.key);
        Port server(Thunder::Core::SocketPort::STREAM, descriptors[0],
            Thunder::Core::NodeId("127.0.0.1", uint16_t(443)), 1024, 1024);
        ASSERT_EQ(server.Certificate(cert, key), Thunder::Core::ERROR_NONE);
        Accept validator;
        if (mode != 0) server.Validate(&validator);
        server.Open(0);
        SSL_CTX* context = SSL_CTX_new(TLS_client_method());
        ASSERT_NE(context, nullptr);
        if (mode == 2) {
            ASSERT_EQ(SSL_CTX_use_certificate(context, credentials.cert), 1);
            ASSERT_EQ(SSL_CTX_use_PrivateKey(context, credentials.key), 1);
        }
        SSL* client = SSL_new(context);
        ASSERT_NE(client, nullptr);
        SSL_set_fd(client, descriptors[1]);
        const int connected = SSL_connect(client);
        Wait(server);
        if (mode == 1) EXPECT_EQ(server.verdict.load(), -1);
        else {
            EXPECT_EQ(connected, 1);
            EXPECT_EQ(server.verdict.load(), 1);
        }
        server.Close(1000);
        SSL_free(client);
        SSL_CTX_free(context);
        close(descriptors[1]);
    }
}

void OutgoingIdentity(bool dns, bool explicitRoot = true, bool defaultTrusted = false)
{
    for (bool matching : {true, false}) {
        Credentials credentials(matching ? (dns ? "DNS:localhost" : "IP:127.0.0.1") : "DNS:wrong.example");
        if (defaultTrusted) {
            const char* certificatePath = std::getenv("P1_TLS_CERT");
            const char* keyPath = std::getenv("P1_TLS_KEY");
            ASSERT_NE(certificatePath, nullptr);
            ASSERT_NE(keyPath, nullptr);
            BIO* certificate = BIO_new_file(certificatePath, "rb");
            BIO* key = BIO_new_file(keyPath, "rb");
            X509_free(credentials.cert);
            EVP_PKEY_free(credentials.key);
            credentials.cert = certificate ? PEM_read_bio_X509(certificate, nullptr, nullptr, nullptr) : nullptr;
            credentials.key = key ? PEM_read_bio_PrivateKey(key, nullptr, nullptr, nullptr) : nullptr;
            BIO_free(certificate);
            BIO_free(key);
        }
        ASSERT_NE(credentials.key, nullptr);
        ASSERT_NE(credentials.cert, nullptr);
        const int listener = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(listener, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ASSERT_EQ(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
        ASSERT_EQ(listen(listener, 1), 0);
        socklen_t size = sizeof(address);
        ASSERT_EQ(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size), 0);
        SSL_CTX* context = SSL_CTX_new(TLS_server_method());
        ASSERT_NE(context, nullptr);
        ASSERT_EQ(SSL_CTX_use_certificate(context, credentials.cert), 1);
        ASSERT_EQ(SSL_CTX_use_PrivateKey(context, credentials.key), 1);
        std::atomic<bool> finished{false};
        std::thread peer([&] {
            pollfd ready{listener, POLLIN, 0};
            const int descriptor = poll(&ready, 1, 2500) > 0 ? accept(listener, nullptr, nullptr) : -1;
            if (descriptor >= 0) {
                SetTimeout(descriptor);
                SSL* ssl = SSL_new(context);
                if (ssl != nullptr) {
                    SSL_set_fd(ssl, descriptor);
                    SSL_accept(ssl);
                    while (!finished) std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    SSL_free(ssl);
                }
                close(descriptor);
            }
        });
        {
            Port client(Thunder::Core::SocketPort::STREAM,
                Thunder::Core::NodeId("0.0.0.0", uint16_t(0)),
                Thunder::Core::NodeId((dns && (!defaultTrusted || matching)) ? "localhost" : "127.0.0.1",
                    ntohs(address.sin_port), Thunder::Core::NodeId::TYPE_IPV4), 1024, 1024);
            Thunder::Crypto::Certificate cert(credentials.cert);
            Thunder::Crypto::CertificateStore store;
            store.Add(cert);
            if (explicitRoot) EXPECT_EQ(client.Root(store), Thunder::Core::ERROR_NONE);
            client.Open(1000);
            Wait(client);
            EXPECT_EQ(client.verdict.load(), matching && (explicitRoot || defaultTrusted) ? 1 : -1);
            client.Close(1000);
        }
        finished = true;
        peer.join();
        SSL_CTX_free(context);
        close(listener);
    }
}

TEST(TLSHandshakeP1, TrustedMatchingAndWrongIdentity)
{
    OutgoingIdentity(false);
}

TEST(TLSHandshakeP1, TrustedDnsMatchingAndWrongIdentity)
{
    OutgoingIdentity(true);
}

TEST(TLSHandshakeP1, DefaultStoreRejectsUntrustedPeer)
{
    OutgoingIdentity(true, false);
}

TEST(TLSDefaultTrustP1, TrustedDnsAndWrongIpWithoutExplicitRoot)
{
    // The runner sets trust before CertificateStore's static initialization.
    OutgoingIdentity(true, false, true);
}
}
