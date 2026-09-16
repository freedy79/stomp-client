// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 freedy79

#include "stomp/StompClient.h"

#include <iostream>

namespace Stomp
{

StompClient::StompClient(std::shared_ptr<ITransport> transport)
    : m_transport(std::move(transport))
{
    if (m_transport)
    {
        m_transport->setOnData([this](std::span<const uint8_t> data) {
            m_parser.feed(data);
        });

        m_transport->setOnError([this](std::string_view err) {
            handleTransportError(err);
        });

        m_transport->setOnClose([this]() {
            handleTransportClosed();
        });
    }

    m_parser.setOnFrame([this](StompFrame frame) {
        handleIncomingFrame(std::move(frame));
    });

    m_parser.setOnHeartbeat([this]() {
        if (m_onHeartbeat)
        {
            m_onHeartbeat();
        }
    });
}

StompClient::~StompClient()
{
    disconnect();
}

bool StompClient::connect(const ClientConfig& config)
{
    if (!m_transport)
    {
        return false;
    }

    m_config = config;
    m_parser.reset();
    m_connected.store(false);

    if (!m_transport->open(m_config.url))
    {
        return false;
    }

    // Send CONNECT / STOMP frame
    StompFrame connectFrame(std::string(Command::CONNECT));
    connectFrame.setHeader(std::string(Header::ACCEPT_VERSION), m_config.acceptVersion);
    connectFrame.setHeader(std::string(Header::HOST), m_config.host);

    if (!m_config.login.empty())
    {
        connectFrame.setHeader(std::string(Header::LOGIN), m_config.login);
    }
    if (!m_config.passcode.empty())
    {
        connectFrame.setHeader(std::string(Header::PASSCODE), m_config.passcode);
    }

    // Heartbeat: cx,cy
    std::string hb = std::to_string(m_config.heartbeatOutgoing.count()) + "," + std::to_string(m_config.heartbeatIncoming.count());
    connectFrame.setHeader(std::string(Header::HEARTBEAT), hb);

    std::string serialized = connectFrame.serialize();
    return m_transport->send(serialized);
}

void StompClient::disconnect()
{
    if (m_connected.load() && m_transport && m_transport->isConnected())
    {
        StompFrame disconnectFrame(std::string(Command::DISCONNECT));
        disconnectFrame.setHeader(std::string(Header::RECEIPT), "disconnect-receipt");
        m_transport->send(disconnectFrame.serialize());
    }

    m_connected.store(false);
    if (m_transport)
    {
        m_transport->close();
    }
}

std::string StompClient::subscribe(std::string_view destination, MessageHandler callback, const Headers& extraHeaders)
{
    std::string subId;
    {
        std::lock_guard<std::mutex> lock(m_subMutex);
        subId = "sub-" + std::to_string(m_nextSubId++);
        m_subscriptions[subId] = std::move(callback);
    }

    if (m_transport && m_transport->isConnected())
    {
        StompFrame subFrame(std::string(Command::SUBSCRIBE));
        subFrame.setHeader(std::string(Header::ID), subId);
        subFrame.setHeader(std::string(Header::DESTINATION), std::string(destination));

        for (const auto& [k, v] : extraHeaders)
        {
            subFrame.setHeader(k, v);
        }

        m_transport->send(subFrame.serialize());
    }

    return subId;
}

void StompClient::unsubscribe(std::string_view subscriptionId)
{
    {
        std::lock_guard<std::mutex> lock(m_subMutex);
        m_subscriptions.erase(std::string(subscriptionId));
    }

    if (m_transport && m_transport->isConnected())
    {
        StompFrame unsubFrame(std::string(Command::UNSUBSCRIBE));
        unsubFrame.setHeader(std::string(Header::ID), std::string(subscriptionId));
        m_transport->send(unsubFrame.serialize());
    }
}

bool StompClient::send(std::string_view destination, std::string_view body, const Headers& extraHeaders)
{
    if (!m_transport || !m_transport->isConnected())
    {
        return false;
    }

    StompFrame sendFrame(std::string(Command::SEND));
    sendFrame.setHeader(std::string(Header::DESTINATION), std::string(destination));
    sendFrame.setHeader(std::string(Header::CONTENT_LENGTH), std::to_string(body.size()));

    for (const auto& [k, v] : extraHeaders)
    {
        sendFrame.setHeader(k, v);
    }

    sendFrame.setBody(std::string(body));
    return m_transport->send(sendFrame.serialize());
}

void StompClient::sendHeartbeat()
{
    if (m_transport && m_transport->isConnected())
    {
        // STOMP heartbeat is a single EOL character '\n'
        std::string_view hb = "\n";
        m_transport->send(hb);
    }
}

void StompClient::handleIncomingFrame(StompFrame frame)
{
    const std::string& cmd = frame.getCommand();

    if (cmd == Command::CONNECTED)
    {
        m_connected.store(true);
        if (m_onConnected)
        {
            m_onConnected(frame);
        }
    }
    else if (cmd == Command::MESSAGE)
    {
        Message msg;
        msg.destination = std::string(frame.getHeader(Header::DESTINATION).value_or(""));
        msg.subscriptionId = std::string(frame.getHeader(Header::SUBSCRIPTION).value_or(""));
        msg.messageId = std::string(frame.getHeader(Header::MESSAGE_ID).value_or(""));
        msg.headers = frame.getHeaders();
        msg.body = frame.getBody();

        MessageHandler handler = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_subMutex);
            auto it = m_subscriptions.find(msg.subscriptionId);
            if (it != m_subscriptions.end())
            {
                handler = it->second;
            }
        }

        if (handler)
        {
            handler(msg);
        }
    }
    else if (cmd == Command::ERROR)
    {
        if (m_onError)
        {
            m_onError(frame);
        }
    }
}

void StompClient::handleTransportClosed()
{
    bool wasConnected = m_connected.exchange(false);
    if (wasConnected && m_onDisconnected)
    {
        m_onDisconnected("Connection closed by transport");
    }
}

void StompClient::handleTransportError(std::string_view error)
{
    m_connected.store(false);
    if (m_onDisconnected)
    {
        m_onDisconnected(error);
    }
}

} // namespace Stomp