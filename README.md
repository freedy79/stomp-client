# Modern C++20 STOMP Client Library (with libcurl WebSocket Transport)

An event-driven, open, and modular STOMP 1.0 / 1.1 / 1.2 client for embedded and host C++ applications.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Repository: https://github.com/freedy79/stomp-client

## Architecture

- **`ITransport`**: Abstract interface for network transmission.
- **`CurlTransport`**: Transport implementation over `libcurl` WebSockets (`ws://` and `wss://`).
- **`StompParser` / `StompFrame`**: Zero-dependency wire-protocol parser and serializer.
- **`StompClient`**: High-level event-driven client providing hooks for connections, subscriptions, and message handling.

### Transport modes

`CurlTransport` always delegates DNS resolution, TLS, proxy handling and timeouts to libcurl.
Framing depends on the linked libcurl version and is selected at compile time:

| libcurl | Mode | Framing / masking |
| :--- | :--- | :--- |
| >= 7.86 with WebSocket support | `CURLOPT_CONNECT_ONLY = 2` + `curl_ws_send/recv` | done by libcurl |
| older | `CURLOPT_CONNECT_ONLY = 1` (raw TLS tunnel) | done by this library |

Use `CurlTransport::hasNativeWebSocketSupport()` to query which mode is active.

## Secure connections (`wss://`)

TLS parameters are set through `Stomp::TransportConfig`:

```cpp
Stomp::TransportConfig cfg;
cfg.verifyPeer = true;                     // verify the certificate chain
cfg.verifyHost = true;                     // verify the host name
cfg.caFile = "/etc/ssl/certs/company-ca.pem";
cfg.clientCertFile = "/etc/ssl/device.pem"; // optional mTLS
cfg.clientKeyFile  = "/etc/ssl/device.key";
cfg.pinnedPublicKey = "sha256//BASE64HASH=";
cfg.proxy = "http://proxy.example.com:3128";
cfg.connectTimeout = std::chrono::seconds(10);
cfg.tcpKeepAlive = true;                   // survive NAT / load balancer idle timeouts

auto transport = std::make_shared<Stomp::CurlTransport>(cfg);
Stomp::StompClient client(transport);
```

## Prerequisites

To build the library and example client on your host system, install the `libcurl` development package:

### Ubuntu / Debian:
```bash
sudo apt update
sudo apt install libcurl4-openssl-dev
```

### Fedora / RHEL / CentOS:
```bash
sudo dnf install libcurl-devel
# or on older RHEL/CentOS:
sudo yum install libcurl-devel
```

### Arch Linux:
```bash
sudo pacman -S curl
```

### macOS (Homebrew):
```bash
brew install curl
```

### JSON helpers (optional)

The `Stomp::Json` helpers require [nlohmann/json](https://github.com/nlohmann/json) 3.2 or newer.
If no package is installed, point CMake at an existing header tree:

```bash
cmake -DSTOMP_JSON_INCLUDE_DIR=/path/to/nlohmann/single_include ..
```

Disable them entirely with `-DSTOMP_WITH_JSON=OFF`; the STOMP core does not depend on JSON.

## Ping-Pong Game Example

A fully bidirectional game demonstrating STOMP events over WebSocket:
1. **Match to 11 points**: First to reach 11 points wins.
2. **Server serves a `PING`** towards a random direction (`LEFT`, `MIDDLE`, or `RIGHT`) over `/topic/game`.
3. **C++ Client returns a `PONG`** to `/app/pong` with its predicted direction.
4. **Server counter-defends**:
   - If server blocks the return $\rightarrow$ Point for Server (`SERVER_BLOCK`).
   - If server misses $\rightarrow$ Point for Client (`CLIENT_POINT`).
   - If client missed the serve $\rightarrow$ Point for Server (`CLIENT_MISS`).
5. **No consecutive repeat directions**: Neither player may choose the same direction twice in a row.
6. **Rematch Prompt**: When a match concludes at 11 points, the client prompts the user (`Play a rematch? (y/n)`) to start a new match. Heartbeats keep the STOMP connection alive during user interaction.

### How to Run:

#### 1. Start the Game Server
```bash
python3 examples/server/stomp_server.py 8080
```

#### 2. Start the C++ Player Client
```bash
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

## License

Released under the [MIT License](LICENSE).

### Third-party components

This library links against, but does not bundle:

| Component | License |
| :--- | :--- |
| [libcurl](https://curl.se/libcurl/) | curl license (MIT/X derivate) |
| [nlohmann/json](https://github.com/nlohmann/json) (optional) | MIT |

See [NOTICE](NOTICE) for the full attribution.

