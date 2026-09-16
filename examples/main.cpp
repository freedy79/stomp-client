#include "stomp/StompClient.h"
#include "stomp/CurlTransport.h"

#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char* argv[])
{
    std::string url = "ws://localhost:8080/stomp/websocket";
    if (argc > 1)
    {
        url = argv[1];
    }

    std::cout << "Starting STOMP client connecting to " << url << std::endl;

    auto transport = std::make_shared<Stomp::CurlTransport>();
    // transport->setSslVerification(false, false); // If self-signed in development

    Stomp::StompClient client(transport);

    // 1. Setup Event Hooks
    client.onConnected([&](const Stomp::StompFrame& frame) {
        std::cout << "[Event] Connected to STOMP Broker!" << std::endl;

        // 2. Subscribe to topics
        client.subscribe("/topic/commands", [&](const Stomp::Message& msg) {
            std::cout << "[Event] Received Cloud Command on " << msg.destination << ": " << msg.body << std::endl;
        });

        // 3. Send initial balance status/login event
        client.send("/app/balance/status", R"({"serial":"Cubis9-001","status":"READY","user":"operator"})");
    });

    client.onError([](const Stomp::StompFrame& frame) {
        std::cerr << "[Event] STOMP Error: " << frame.getBody() << std::endl;
    });

    client.onDisconnected([](std::string_view reason) {
        std::cout << "[Event] Disconnected: " << reason << std::endl;
    });

    // Connect
    Stomp::ClientConfig config;
    config.url = url;
    config.login = "guest";
    config.passcode = "guest";

    if (!client.connect(config))
    {
        std::cerr << "Failed to start connection." << std::endl;
        return 1;
    }

    // Main loop: keep alive, periodic status updates & heartbeats
    for (int i = 0; i < 30; ++i)
    {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (client.isConnected())
        {
            client.sendHeartbeat();
            if (i % 5 == 0)
            {
                client.send("/app/balance/status", R"({"status":"MEASURING","weight":123.456})");
            }
        }
    }

    std::cout << "Shutting down..." << std::endl;
    client.disconnect();
    return 0;
}
