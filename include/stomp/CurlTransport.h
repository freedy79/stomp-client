// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 freedy79

#pragma once

#include "stomp/ITransport.h"
#include "stomp/TransportConfig.h"

#include <array>
#include <atomic>
#include <curl/curl.h>
#include <functional>
#include <mutex>
#include <random>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace Stomp
{

/// @brief WebSocket transport built on libcurl.
///
/// libcurl handles DNS, TLS, proxies and timeouts. When the linked libcurl provides the
/// native WebSocket API (>= 7.86) it also performs framing and masking; otherwise the
/// connection is used as a plain TLS tunnel and this class frames the messages itself.
class CurlTransport : public ITransport
{
public:
    CurlTransport();
    explicit CurlTransport(TransportConfig config);
    ~CurlTransport() override;

    bool open(std::string_view url) override;
    void close() override;
    bool isConnected() const override { return m_connected.load(); }

    bool send(std::span<const uint8_t> data) override;

    void setOnData(std::function<void(std::span<const uint8_t>)> onData) override { m_onData = std::move(onData); }
    void setOnError(std::function<void(std::string_view errorMsg)> onError) override { m_onError = std::move(onError); }
    void setOnClose(std::function<void()> onClose) override { m_onClose = std::move(onClose); }

    void setConfig(TransportConfig config) { m_config = std::move(config); }
    const TransportConfig& getConfig() const noexcept { return m_config; }

    void setSslVerification(bool verifyPeer, bool verifyHost);

    /// @brief True when the linked libcurl performs WebSocket framing itself.
    static bool hasNativeWebSocketSupport() noexcept;

private:
    bool applyCurlOptions(const std::string& effectiveUrl);
    void reportError(std::string_view message);
    void teardown();
    bool waitReadable(int timeoutMs);
    void sendCloseFrame();
    void workerLoop();

    // Only used when libcurl has no native WebSocket support.
    bool tunnelWrite(const void* data, size_t len);
    bool performWsHandshake(const std::string& host, int port, const std::string& path);
    void dispatchFramedBuffer(std::vector<uint8_t>& buffer);
    std::array<uint8_t, 4> nextMaskKey();

    TransportConfig m_config;
    CURL* m_curl{nullptr};
    std::string m_url;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connected{false};
    std::thread m_workerThread;
    std::mutex m_curlMutex;
    std::mt19937 m_rng{std::random_device{}()};

    std::function<void(std::span<const uint8_t>)> m_onData;
    std::function<void(std::string_view)> m_onError;
    std::function<void()> m_onClose;
};

} // namespace Stomp