// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 freedy79

#pragma once

#include "stomp/ITransport.h"
#include "stomp/StompFrame.h"
#include "stomp/StompParser.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Stomp
{

struct ClientConfig
{
    std::string url;                             // e.g. "ws://localhost:8080/stomp/websocket" or "wss://..."
    std::string host = "/";                      // STOMP virtual host
    std::string login;                           // optional STOMP login
    std::string passcode;                        // optional STOMP passcode
    std::string acceptVersion = "1.0,1.1,1.2";   // supported versions
    std::chrono::milliseconds heartbeatOutgoing{10000};
    std::chrono::milliseconds heartbeatIncoming{10000};
};

struct Message
{
    std::string destination;
    std::string subscriptionId;
    std::string messageId;
    Headers headers;
    std::string body;
};

class StompClient
{
public:
    using MessageHandler = std::function<void(const Message&)>;
    using ConnectedHandler = std::function<void(const StompFrame&)>;
    using ErrorHandler = std::function<void(const StompFrame&)>;
    using DisconnectedHandler = std::function<void(std::string_view reason)>;
    using HeartbeatHandler = std::function<void()>;

    explicit StompClient(std::shared_ptr<ITransport> transport);
    ~StompClient();

    // Lifecycle
    bool connect(const ClientConfig& config);
    void disconnect();
    bool isConnected() const noexcept { return m_connected.load(); }

    // STOMP Operations
    std::string subscribe(std::string_view destination, MessageHandler callback, const Headers& extraHeaders = {});
    void unsubscribe(std::string_view subscriptionId);
    bool send(std::string_view destination, std::string_view body, const Headers& extraHeaders = {});

    // Event Hooks / Listeners
    void onConnected(ConnectedHandler handler) { m_onConnected = std::move(handler); }
    void onError(ErrorHandler handler) { m_onError = std::move(handler); }
    void onDisconnected(DisconnectedHandler handler) { m_onDisconnected = std::move(handler); }
    void onHeartbeat(HeartbeatHandler handler) { m_onHeartbeat = std::move(handler); }

    // Heartbeat sender (invoked periodically by timer/loop)
    void sendHeartbeat();

private:
    void handleIncomingFrame(StompFrame frame);
    void handleTransportClosed();
    void handleTransportError(std::string_view error);

    std::shared_ptr<ITransport> m_transport;
    StompParser m_parser;

    std::atomic<bool> m_connected{false};
    ClientConfig m_config;

    std::mutex m_subMutex;
    uint64_t m_nextSubId{1};
    std::unordered_map<std::string, MessageHandler> m_subscriptions;

    ConnectedHandler m_onConnected;
    ErrorHandler m_onError;
    DisconnectedHandler m_onDisconnected;
    HeartbeatHandler m_onHeartbeat;
};

} // namespace Stomp