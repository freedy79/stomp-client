// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 freedy79

#pragma once

#include <chrono>
#include <string>

namespace Stomp
{

/// @brief Connection settings forwarded to the underlying libcurl handle.
struct TransportConfig
{
    /// Verify the server certificate chain against the CA store.
    bool verifyPeer{true};

    /// Verify that the certificate matches the requested host name.
    bool verifyHost{true};

    /// Custom CA bundle file. Empty means the system default store.
    std::string caFile;

    /// Custom CA directory. Empty means the system default store.
    std::string caPath;

    /// Client certificate and key for mutual TLS. Empty disables mTLS.
    std::string clientCertFile;
    std::string clientKeyFile;
    std::string clientKeyPassword;

    /// Expected server public key, e.g. "sha256//base64==" for certificate pinning.
    std::string pinnedPublicKey;

    /// Proxy URL, e.g. "http://proxy.example.com:3128". Empty means no proxy.
    std::string proxy;

    std::chrono::milliseconds connectTimeout{10000};

    /// TCP keep-alive probes, needed to survive NAT and load balancer idle timeouts.
    bool tcpKeepAlive{true};
    std::chrono::seconds keepAliveIdle{30};
    std::chrono::seconds keepAliveInterval{15};

    /// Emit libcurl protocol traces on stderr.
    bool verbose{false};
};

} // namespace Stomp