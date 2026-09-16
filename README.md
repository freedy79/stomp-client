# Modern C++20 STOMP Client Library (with libcurl WebSocket Transport)

An event-driven, open, and modular STOMP 1.0 / 1.1 / 1.2 client for embedded and host C++ applications.

## Architecture

- **`ITransport`**: Abstract interface for network transmission.
- **`CurlTransport`**: Transport implementation over `libcurl` WebSockets (`ws://` and `wss://`).
- **`StompParser` / `StompFrame`**: Zero-dependency wire-protocol parser and serializer.
- **`StompClient`**: High-level event-driven client providing hooks for connections, subscriptions, and message handling.

## Quick Python Test Server

A mock Spring Boot STOMP-over-WebSocket server is included in `examples/server/stomp_server.py`.

### 1. Start Server
```bash
python3 -m pip install websockets
python3 examples/server/stomp_server.py 8080
```

### 2. Run C++ Example Client
```bash
mkdir -p build && cd build
cmake ..
cmake --build .
./examples/stomp_example_client ws://localhost:8080/stomp/websocket
```

## API Usage Example

```cpp
#include "stomp/StompClient.h"
#include "stomp/CurlTransport.h"

auto transport = std::make_shared<Stomp::CurlTransport>();
Stomp::StompClient client(transport);

// Event Hook: Connected
client.onConnected([&](const Stomp::StompFrame& frame) {
    // Subscribe to destination
    client.subscribe("/topic/commands", [](const Stomp::Message& msg) {
        std::cout << "Received command: " << msg.body << std::endl;
    });

    // Send message to server
    client.send("/app/status", R"({"status":"READY"})");
});

// Event Hook: Errors / Disconnections
client.onError([](const Stomp::StompFrame& frame) {
    std::cerr << "STOMP Error: " << frame.getBody() << std::endl;
});

// Connect
Stomp::ClientConfig config;
config.url = "ws://localhost:8080/stomp/websocket";
client.connect(config);
```
