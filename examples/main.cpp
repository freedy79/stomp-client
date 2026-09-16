// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 freedy79

#include "stomp/StompClient.h"
#include "stomp/CurlTransport.h"
#include "stomp/JsonMessage.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <random>
#include <thread>

namespace
{
std::atomic<bool> g_running{true};

void signalHandler(int /*signal*/)
{
    g_running.store(false);
}

int directionIndex(std::string_view direction)
{
    if (direction == "LEFT")
    {
        return 0;
    }
    if (direction == "MIDDLE")
    {
        return 1;
    }
    return 2;
}

/// @brief Chance to reach the ball, depending on how far the player has to move.
double reachProbability(int distance)
{
    switch (distance)
    {
    case 0:
        return 0.90;
    case 1:
        return 0.60;
    default:
        return 0.30;
    }
}
} // namespace

int main(int argc, char* argv[])
{
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Clear terminal screen on startup
    std::cout << "\033[2J\033[H" << std::flush;

    std::string url = "ws://localhost:8080/stomp/websocket";
    if (argc > 1)
    {
        url = argv[1];
    }

    std::cout << "==========================================" << std::endl;
    std::cout << "🏓 STOMP Ping-Pong Player Client" << std::endl;
    std::cout << "Connecting to: " << url << std::endl;
    std::cout << "Press Ctrl+C anytime to stop." << std::endl;
    std::cout << "==========================================" << std::endl;

    auto transport = std::make_shared<Stomp::CurlTransport>();
    Stomp::StompClient client(transport);

    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> chance(0.0, 1.0);

    const std::array<std::string, 3> allDirections = {"LEFT", "MIDDLE", "RIGHT"};
    std::string playerPosition = "MIDDLE";
    std::string lastReturnTarget;

    // 1. Setup Event Hooks
    client.onConnected([&](const Stomp::StompFrame& /*frame*/) {
        std::cout << "✅ Connected to Game Host! Joining game channel..." << std::endl;

        // 2. Subscribe to the game topic
        client.subscribe("/topic/game", [&](const Stomp::Message& msg) {
            const auto parsed = Stomp::Json::tryParse(msg.body);
            if (!parsed)
            {
                std::cerr << "⚠️  Ignoring malformed JSON message: " << msg.body << std::endl;
                return;
            }
            const nlohmann::json& doc = *parsed;
            const auto type = Stomp::Json::get<std::string>(doc, "type");

            if (type == "GAME_START")
            {
                std::cout << "\n🎮 [GAME HOST]: " << Stomp::Json::get<std::string>(doc, "message")
                          << " (Target: " << Stomp::Json::get<int>(doc, "targetScore", 11) << " points)\n"
                          << std::endl;
            }
            else if (type == "PING")
            {
                const auto incomingDirection = Stomp::Json::get<std::string>(doc, "direction");
                const auto round = Stomp::Json::get<int>(doc, "round", 0);
                const auto rallyHits = Stomp::Json::get<int>(doc, "rallyHits", 0);
                const auto shot = Stomp::Json::get<std::string>(doc, "shot", "SERVE");

                if (incomingDirection.empty())
                {
                    std::cerr << "⚠️  PING without direction - skipping round" << std::endl;
                    return;
                }

                std::cout << "🎾 [Round " << round << " | rally " << rallyHits << "] Server "
                          << (shot == "SERVE" ? "serves" : "returns") << " towards -> " << incomingDirection
                          << "!" << std::endl;

                // The player stands somewhere on the table; the further the ball is away,
                // the less likely it is reached in time.
                const int distance = std::abs(directionIndex(incomingDirection) - directionIndex(playerPosition));
                const bool reached = chance(rng) < reachProbability(distance);
                const std::string fromPosition = playerPosition;
                playerPosition = incomingDirection;

                std::this_thread::sleep_for(std::chrono::milliseconds(250));

                if (!reached)
                {
                    std::cout << "💨 [CLIENT] Too far from " << fromPosition << " to " << incomingDirection
                              << " - out of reach!" << std::endl;
                    Stomp::Json::send(client, "/app/pong", {{"status", "MISS"}, {"direction", ""}});
                    return;
                }

                // Aim into a lane the server has to run for, never the same one twice in a row.
                std::vector<std::string> targets;
                for (const auto& d : allDirections)
                {
                    if (d != lastReturnTarget)
                    {
                        targets.push_back(d);
                    }
                }
                std::uniform_int_distribution<size_t> pick(0, targets.size() - 1);
                const std::string target = targets[pick(rng)];
                lastReturnTarget = target;

                std::cout << "🏓 [CLIENT] Reached it (" << fromPosition << " -> " << incomingDirection
                          << ") and plays it back to " << target << "!" << std::endl;
                Stomp::Json::send(client, "/app/pong", {{"status", "PONG"}, {"direction", target}});
            }
            else if (type == "SCORE")
            {
                std::cout << "📢 [SCORE] Round " << Stomp::Json::get<int>(doc, "round", 0) << " -> "
                          << Stomp::Json::get<std::string>(doc, "outcome") << " after "
                          << Stomp::Json::get<int>(doc, "rallyHits", 0) << " rally hits | Client "
                          << Stomp::Json::get<int>(doc, "clientScore", 0) << " : "
                          << Stomp::Json::get<int>(doc, "serverScore", 0) << " Server\n   "
                          << Stomp::Json::get<std::string>(doc, "detail") << "\n"
                          << std::endl;
                playerPosition = "MIDDLE";
                lastReturnTarget.clear();
            }
            else if (type == "MATCH_FINISH")
            {
                std::cout << "\n==========================================" << std::endl;
                std::cout << "🏆 MATCH FINISHED!" << std::endl;
                std::cout << "Winner: " << Stomp::Json::get<std::string>(doc, "winner", "N/A") << std::endl;
                std::cout << "Final Result -> Client: " << Stomp::Json::get<int>(doc, "finalClientScore", 0)
                          << " | Server: " << Stomp::Json::get<int>(doc, "finalServerScore", 0) << std::endl;
                std::cout << "==========================================" << std::endl;

                // Prompt user for rematch in a dedicated interactive thread
                std::thread([&client]() {
                    std::cout << "\n👉 Play a rematch? (y/n): " << std::flush;
                    std::string answer;
                    if (std::cin >> answer)
                    {
                        const bool rematch = (answer == "y" || answer == "Y" || answer == "yes");
                        if (rematch)
                        {
                            std::cout << "🚀 Rematch requested! Server is starting a new game...\n" << std::endl;
                        }
                        else
                        {
                            std::cout << "👋 Game closed. Goodbye!\n" << std::endl;
                        }
                        Stomp::Json::send(client, "/app/rematch", {{"rematch", rematch}});
                        if (!rematch)
                        {
                            g_running.store(false);
                        }
                    }
                }).detach();
            }
            else if (type == "GAME_OVER")
            {
                std::cout << "👋 [SERVER]: " << Stomp::Json::get<std::string>(doc, "message") << std::endl;
                g_running.store(false);
            }
        });
    });

    client.onError([](const Stomp::StompFrame& frame) {
        std::cerr << "❌ [STOMP Error]: " << frame.getBody() << std::endl;
    });

    client.onDisconnected([](std::string_view reason) {
        std::cout << "🔌 [Disconnected]: " << reason << std::endl;
        g_running.store(false);
    });

    // Connect
    Stomp::ClientConfig config;
    config.url = url;

    if (!client.connect(config))
    {
        std::cerr << "Failed to connect to " << url << std::endl;
        return 1;
    }

    // Keep client running continuously until Ctrl+C or the transport closes.
    // NOTE: client.isConnected() only becomes true once the server's CONNECTED frame
    // has been processed, so the loop condition uses the transport state instead.
    int heartbeatCounter = 0;
    while (g_running.load() && transport->isConnected())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (++heartbeatCounter % 10 == 0 && client.isConnected())
        {
            client.sendHeartbeat();
        }
    }

    std::cout << "\nStopping game client..." << std::endl;
    client.disconnect();
    return 0;
}