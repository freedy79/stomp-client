#pragma once

#include "stomp/ITransport.h"

#include <atomic>
#include <curl/curl.h>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace Stomp
{

class CurlTransport : public ITransport
{
public:
    CurlTransport();
    ~CurlTransport() override;

    bool open(std::string_view url) override;
    void close() override;
    bool isConnected() const override { return m_running.load() && m_curl != nullptr; }

    bool send(std::span<const uint8_t> data) override;

    void setOnData(std::function<void(std::span<const uint8_t>)> onData) override { m_onData = std::move(onData); }
    void setOnError(std::function<void(std::string_view errorMsg)> onError) override { m_onError = std::move(onError); }
    void setOnClose(std::function<void()> onClose) override { m_onClose = std::move(onClose); }

    /// @brief Set SSL CA path / verification options
    void setSslVerification(bool verifyPeer, bool verifyHost);

private:
    void workerLoop();

    CURL* m_curl{nullptr};
    std::string m_url;
    bool m_verifyPeer{true};
    bool m_verifyHost{true};

    std::atomic<bool> m_running{false};
    std::thread m_workerThread;

    std::function<void(std::span<const uint8_t>)> m_onData;
    std::function<void(std::string_view)> m_onError;
    std::function<void()> m_onClose;
};

} // namespace Stomp
