// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 freedy79

#include "stomp/StompClient.h"
#include "stomp/StompFrame.h"
#include "stomp/StompParser.h"
#include "stomp/ITransport.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace std::string_literals;

// Mock transport for isolated unit testing
class MockTransport : public Stomp::ITransport
{
public:
    bool open(std::string_view url) override
    {
        m_connected = true;
        m_openedUrl = std::string(url);
        return true;
    }

    void close() override
    {
        m_connected = false;
        if (m_onClose)
        {
            m_onClose();
        }
    }

    bool isConnected() const override { return m_connected; }

    bool send(std::span<const uint8_t> data) override
    {
        m_sentData.emplace_back(reinterpret_cast<const char*>(data.data()), data.size());
        return true;
    }

    void setOnData(std::function<void(std::span<const uint8_t>)> onData) override { m_onData = std::move(onData); }
    void setOnError(std::function<void(std::string_view errorMsg)> onError) override { m_onError = std::move(onError); }
    void setOnClose(std::function<void()> onClose) override { m_onClose = std::move(onClose); }

    // Simulation helpers
    void injectIncoming(std::string_view raw)
    {
        if (m_onData)
        {
            m_onData(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(raw.data()), raw.size()));
        }
    }

    bool m_connected{false};
    std::string m_openedUrl;
    std::vector<std::string> m_sentData;
    std::function<void(std::span<const uint8_t>)> m_onData;
    std::function<void(std::string_view)> m_onError;
    std::function<void()> m_onClose;
};

void testFrameSerialization()
{
    Stomp::StompFrame frame("SEND");
    frame.setHeader("destination", "/topic/test");
    frame.setHeader("content-length", "11");
    frame.setBody("Hello World");

    std::string wire = frame.serialize();
    assert(wire.find("SEND\n") == 0);
    assert(wire.find("destination:/topic/test\n") != std::string::npos);
    assert(wire.find("content-length:11\n") != std::string::npos);
    assert(wire.find("\n\nHello World") != std::string::npos);
    assert(wire.back() == '\0');
    std::cout << "[PASS] testFrameSerialization" << std::endl;
}

void testParserSimpleFrame()
{
    Stomp::StompParser parser;
    bool received = false;
    parser.setOnFrame([&](Stomp::StompFrame frame) {
        received = true;
        assert(frame.getCommand() == "CONNECTED");
        assert(frame.getHeader("version").value_or("") == "1.2");
        assert(frame.getHeader("session").value_or("") == "123");
    });

    std::string raw = "CONNECTED\nversion:1.2\nsession:123\n\n\0"s;
    parser.feed(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(raw.data()), raw.size()));
    assert(received);
    std::cout << "[PASS] testParserSimpleFrame" << std::endl;
}

void testClientEventHooks()
{
    auto mock = std::make_shared<MockTransport>();
    Stomp::StompClient client(mock);

    bool connectedEvent = false;
    client.onConnected([&](const Stomp::StompFrame& frame) {
        connectedEvent = true;
    });

    Stomp::ClientConfig config;
    config.url = "ws://localhost:8080/stomp/websocket";
    assert(client.connect(config));

    // Verify CONNECT frame sent
    assert(!mock->m_sentData.empty());
    assert(mock->m_sentData[0].find("CONNECT\n") == 0);

    // Simulate Server CONNECTED response
    std::string serverConnected = "CONNECTED\nversion:1.2\nheart-beat:10000,10000\n\n\0"s;
    mock->injectIncoming(serverConnected);
    assert(connectedEvent);
    assert(client.isConnected());

    // Test Subscription & Message hook
    bool messageReceived = false;
    std::string subId = client.subscribe("/topic/commands", [&](const Stomp::Message& msg) {
        messageReceived = true;
        assert(msg.destination == "/topic/commands");
        assert(msg.body == "{\"command\": \"start\"}");
    });

    // Simulate incoming MESSAGE frame from the broker
    std::string incomingMsg = "MESSAGE\ndestination:/topic/commands\nsubscription:" + subId + "\nmessage-id:001\n\n{\"command\": \"start\"}\0"s;
    mock->injectIncoming(incomingMsg);
    assert(messageReceived);

    // Test sending message
    client.send("/app/status", "{\"user\":\"admin\"}");
    assert(mock->m_sentData.back().find("destination:/app/status\n") != std::string::npos);
    assert(mock->m_sentData.back().find("{\"user\":\"admin\"}") != std::string::npos);

    std::cout << "[PASS] testClientEventHooks" << std::endl;
}

int main()
{
    testFrameSerialization();
    testParserSimpleFrame();
    testClientEventHooks();
    std::cout << "All STOMP tests passed successfully!" << std::endl;
    return 0;
}