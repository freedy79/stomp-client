// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 freedy79

#include "stomp/CurlTransport.h"

#include <cstring>
#include <poll.h>
#include <string_view>

#if defined(CURLOPT_WS_OPTIONS) && LIBCURL_VERSION_NUM >= 0x075600
#define STOMP_CURL_NATIVE_WS 1
#endif

namespace Stomp
{

namespace
{
struct ParsedUrl
{
    std::string host;
    std::string path{"/"};
    int port{80};
    bool secure{false};
};

bool parseWsUrl(std::string_view url, ParsedUrl& out)
{
    std::string_view s = url;

    if (s.starts_with("wss://") || s.starts_with("https://"))
    {
        s.remove_prefix(s.starts_with("wss://") ? 6 : 8);
        out.secure = true;
        out.port = 443;
    }
    else if (s.starts_with("ws://") || s.starts_with("http://"))
    {
        s.remove_prefix(s.starts_with("ws://") ? 5 : 7);
        out.secure = false;
        out.port = 80;
    }
    else
    {
        return false;
    }

    const size_t slashPos = s.find('/');
    const std::string_view hostPort = (slashPos != std::string_view::npos) ? s.substr(0, slashPos) : s;
    out.path = (slashPos != std::string_view::npos) ? std::string(s.substr(slashPos)) : "/";

    const size_t colonPos = hostPort.rfind(':');
    if (colonPos != std::string_view::npos && hostPort.find(']') == std::string_view::npos)
    {
        out.host = std::string(hostPort.substr(0, colonPos));
        try
        {
            out.port = std::stoi(std::string(hostPort.substr(colonPos + 1)));
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
    else
    {
        out.host = std::string(hostPort);
    }

    return !out.host.empty();
}

std::string base64Encode(const uint8_t* data, size_t len)
{
    static constexpr char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string res;
    res.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3)
    {
        uint32_t val = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) val |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) val |= static_cast<uint32_t>(data[i + 2]);

        res.push_back(tbl[(val >> 18) & 0x3F]);
        res.push_back(tbl[(val >> 12) & 0x3F]);
        res.push_back((i + 1 < len) ? tbl[(val >> 6) & 0x3F] : '=');
        res.push_back((i + 2 < len) ? tbl[val & 0x3F] : '=');
    }
    return res;
}
} // namespace

// -----------------------------------------------------------------------------

CurlTransport::CurlTransport()
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

CurlTransport::CurlTransport(TransportConfig config)
    : m_config(std::move(config))
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

CurlTransport::~CurlTransport()
{
    close();
}

bool CurlTransport::hasNativeWebSocketSupport() noexcept
{
#ifdef STOMP_CURL_NATIVE_WS
    return true;
#else
    return false;
#endif
}

void CurlTransport::setSslVerification(bool verifyPeer, bool verifyHost)
{
    m_config.verifyPeer = verifyPeer;
    m_config.verifyHost = verifyHost;
}

void CurlTransport::reportError(std::string_view message)
{
    if (m_onError)
    {
        m_onError(message);
    }
}

std::array<uint8_t, 4> CurlTransport::nextMaskKey()
{
    std::uniform_int_distribution<int> dist(0, 255);
    return {static_cast<uint8_t>(dist(m_rng)),
            static_cast<uint8_t>(dist(m_rng)),
            static_cast<uint8_t>(dist(m_rng)),
            static_cast<uint8_t>(dist(m_rng))};
}

bool CurlTransport::applyCurlOptions(const std::string& effectiveUrl)
{
    curl_easy_setopt(m_curl, CURLOPT_URL, effectiveUrl.c_str());

    curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYPEER, m_config.verifyPeer ? 1L : 0L);
    curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYHOST, m_config.verifyHost ? 2L : 0L);

    if (!m_config.caFile.empty())
    {
        curl_easy_setopt(m_curl, CURLOPT_CAINFO, m_config.caFile.c_str());
    }
    if (!m_config.caPath.empty())
    {
        curl_easy_setopt(m_curl, CURLOPT_CAPATH, m_config.caPath.c_str());
    }
    if (!m_config.clientCertFile.empty())
    {
        curl_easy_setopt(m_curl, CURLOPT_SSLCERT, m_config.clientCertFile.c_str());
    }
    if (!m_config.clientKeyFile.empty())
    {
        curl_easy_setopt(m_curl, CURLOPT_SSLKEY, m_config.clientKeyFile.c_str());
    }
    if (!m_config.clientKeyPassword.empty())
    {
        curl_easy_setopt(m_curl, CURLOPT_KEYPASSWD, m_config.clientKeyPassword.c_str());
    }
    if (!m_config.pinnedPublicKey.empty())
    {
        curl_easy_setopt(m_curl, CURLOPT_PINNEDPUBLICKEY, m_config.pinnedPublicKey.c_str());
    }
    if (!m_config.proxy.empty())
    {
        curl_easy_setopt(m_curl, CURLOPT_PROXY, m_config.proxy.c_str());
    }

    curl_easy_setopt(m_curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(m_config.connectTimeout.count()));

    if (m_config.tcpKeepAlive)
    {
        curl_easy_setopt(m_curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(m_curl, CURLOPT_TCP_KEEPIDLE, static_cast<long>(m_config.keepAliveIdle.count()));
        curl_easy_setopt(m_curl, CURLOPT_TCP_KEEPINTVL, static_cast<long>(m_config.keepAliveInterval.count()));
    }

    if (m_config.verbose)
    {
        curl_easy_setopt(m_curl, CURLOPT_VERBOSE, 1L);
    }

#ifdef STOMP_CURL_NATIVE_WS
    // libcurl performs the upgrade handshake, framing, masking and PING/PONG replies.
    curl_easy_setopt(m_curl, CURLOPT_CONNECT_ONLY, 2L);
#else
    // Older libcurl: use the established (TLS) connection as a raw tunnel.
    curl_easy_setopt(m_curl, CURLOPT_CONNECT_ONLY, 1L);
#endif

    return true;
}

bool CurlTransport::open(std::string_view url)
{
    close();

    m_url = std::string(url);

    ParsedUrl parsed;
    if (!parseWsUrl(m_url, parsed))
    {
        reportError("Invalid WebSocket URL: " + m_url);
        return false;
    }

#ifdef STOMP_CURL_NATIVE_WS
    const std::string effectiveUrl = m_url;
#else
    // curl < 7.86 does not know the ws/wss schemes.
    const std::string effectiveUrl = (parsed.secure ? "https://" : "http://") + parsed.host + ":" +
                                     std::to_string(parsed.port) + parsed.path;
#endif

    m_curl = curl_easy_init();
    if (!m_curl)
    {
        reportError("curl_easy_init() failed");
        return false;
    }

    applyCurlOptions(effectiveUrl);

    if (const CURLcode res = curl_easy_perform(m_curl); res != CURLE_OK)
    {
        reportError(std::string("Connection failed: ") + curl_easy_strerror(res));
        teardown();
        return false;
    }

#ifndef STOMP_CURL_NATIVE_WS
    if (!performWsHandshake(parsed.host, parsed.port, parsed.path))
    {
        teardown();
        return false;
    }
#endif

    m_running.store(true);
    m_connected.store(true);
    m_workerThread = std::thread(&CurlTransport::workerLoop, this);
    return true;
}

void CurlTransport::teardown()
{
    if (m_curl)
    {
        curl_easy_cleanup(m_curl);
        m_curl = nullptr;
    }
}

void CurlTransport::sendCloseFrame()
{
    // Status code 1000 (normal closure), big endian.
    static constexpr uint8_t reason[2] = {0x03, 0xE8};

#ifdef STOMP_CURL_NATIVE_WS
    std::lock_guard<std::mutex> lock(m_curlMutex);
    if (!m_curl)
    {
        return;
    }
    size_t sent = 0;
    curl_ws_send(m_curl, reason, sizeof(reason), &sent, 0, CURLWS_CLOSE);
#else
    std::array<uint8_t, 4> mask{};
    {
        std::lock_guard<std::mutex> lock(m_curlMutex);
        if (!m_curl)
        {
            return;
        }
        mask = nextMaskKey();
    }

    std::vector<uint8_t> frame;
    frame.push_back(0x88);                                 // FIN + close
    frame.push_back(static_cast<uint8_t>(0x80 | sizeof(reason)));
    frame.insert(frame.end(), mask.begin(), mask.end());
    for (size_t i = 0; i < sizeof(reason); ++i)
    {
        frame.push_back(reason[i] ^ mask[i % 4]);
    }
    tunnelWrite(frame.data(), frame.size());
#endif
}

void CurlTransport::close()
{
    if (m_connected.exchange(false))
    {
        sendCloseFrame();

        // Give the peer a moment to acknowledge with its own close frame.
        for (int i = 0; i < 20 && m_running.load(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    m_running.store(false);

    if (m_workerThread.joinable())
    {
        if (std::this_thread::get_id() == m_workerThread.get_id())
        {
            m_workerThread.detach();
        }
        else
        {
            m_workerThread.join();
        }
    }

    std::lock_guard<std::mutex> lock(m_curlMutex);
    teardown();
}

bool CurlTransport::waitReadable(int timeoutMs)
{
    curl_socket_t sockfd = CURL_SOCKET_BAD;
    {
        std::lock_guard<std::mutex> lock(m_curlMutex);
        if (!m_curl || curl_easy_getinfo(m_curl, CURLINFO_ACTIVESOCKET, &sockfd) != CURLE_OK)
        {
            return false;
        }
    }

    if (sockfd == CURL_SOCKET_BAD)
    {
        return false;
    }

    struct pollfd pfd{};
    pfd.fd = static_cast<int>(sockfd);
    pfd.events = POLLIN;
    return ::poll(&pfd, 1, timeoutMs) > 0;
}

bool CurlTransport::tunnelWrite(const void* data, size_t len)
{
    const char* ptr = static_cast<const char*>(data);
    size_t total = 0;

    while (total < len)
    {
        size_t sent = 0;
        CURLcode res = CURLE_OK;
        {
            std::lock_guard<std::mutex> lock(m_curlMutex);
            if (!m_curl)
            {
                return false;
            }
            res = curl_easy_send(m_curl, ptr + total, len - total, &sent);
        }

        if (res == CURLE_AGAIN)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (res != CURLE_OK)
        {
            return false;
        }
        total += sent;
    }
    return true;
}

bool CurlTransport::send(std::span<const uint8_t> payload)
{
    if (!m_connected.load())
    {
        return false;
    }

#ifdef STOMP_CURL_NATIVE_WS
    size_t sent = 0;
    std::lock_guard<std::mutex> lock(m_curlMutex);
    if (!m_curl)
    {
        return false;
    }
    const CURLcode res = curl_ws_send(m_curl, payload.data(), payload.size(), &sent, 0, CURLWS_TEXT);
    return res == CURLE_OK;
#else
    std::vector<uint8_t> frame;
    frame.reserve(payload.size() + 14);
    frame.push_back(0x81); // FIN + text frame

    const size_t len = payload.size();
    if (len <= 125)
    {
        frame.push_back(static_cast<uint8_t>(0x80 | len));
    }
    else if (len <= 0xFFFF)
    {
        frame.push_back(0x80 | 126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    }
    else
    {
        frame.push_back(0x80 | 127);
        for (int i = 7; i >= 0; --i)
        {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }

    std::array<uint8_t, 4> mask{};
    {
        std::lock_guard<std::mutex> lock(m_curlMutex);
        mask = nextMaskKey();
    }
    frame.insert(frame.end(), mask.begin(), mask.end());

    for (size_t i = 0; i < len; ++i)
    {
        frame.push_back(payload[i] ^ mask[i % 4]);
    }

    return tunnelWrite(frame.data(), frame.size());
#endif
}

#ifndef STOMP_CURL_NATIVE_WS
bool CurlTransport::performWsHandshake(const std::string& host, int port, const std::string& path)
{
    uint8_t keyBytes[16];
    {
        std::uniform_int_distribution<int> dist(0, 255);
        for (auto& b : keyBytes)
        {
            b = static_cast<uint8_t>(dist(m_rng));
        }
    }

    const std::string request =
        "GET " + path + " HTTP/1.1\r\n"
        "Host: " + host + ":" + std::to_string(port) + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + base64Encode(keyBytes, sizeof(keyBytes)) + "\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    if (!tunnelWrite(request.data(), request.size()))
    {
        reportError("Failed to send WebSocket upgrade request");
        return false;
    }

    std::string response;
    char buf[512];
    while (response.find("\r\n\r\n") == std::string::npos)
    {
        if (!waitReadable(static_cast<int>(m_config.connectTimeout.count())))
        {
            reportError("Timeout while waiting for WebSocket upgrade response");
            return false;
        }

        size_t got = 0;
        CURLcode res = CURLE_OK;
        {
            std::lock_guard<std::mutex> lock(m_curlMutex);
            res = curl_easy_recv(m_curl, buf, sizeof(buf), &got);
        }

        if (res == CURLE_AGAIN)
        {
            continue;
        }
        if (res != CURLE_OK || got == 0)
        {
            reportError("Connection closed during WebSocket upgrade");
            return false;
        }
        response.append(buf, got);
    }

    if (response.find(" 101") == std::string::npos)
    {
        reportError("WebSocket handshake rejected: " + response.substr(0, response.find("\r\n")));
        return false;
    }

    return true;
}

void CurlTransport::dispatchFramedBuffer(std::vector<uint8_t>& buffer)
{
    while (buffer.size() >= 2)
    {
        const uint8_t b1 = buffer[0];
        const uint8_t b2 = buffer[1];

        const uint8_t opcode = b1 & 0x0F;
        const bool masked = (b2 & 0x80) != 0;
        uint64_t payloadLen = b2 & 0x7F;
        size_t headerSize = 2;

        if (payloadLen == 126)
        {
            if (buffer.size() < 4) return;
            payloadLen = (static_cast<uint64_t>(buffer[2]) << 8) | buffer[3];
            headerSize = 4;
        }
        else if (payloadLen == 127)
        {
            if (buffer.size() < 10) return;
            payloadLen = 0;
            for (int i = 0; i < 8; ++i)
            {
                payloadLen = (payloadLen << 8) | buffer[2 + i];
            }
            headerSize = 10;
        }

        if (masked) headerSize += 4;
        if (buffer.size() < headerSize + payloadLen) return;

        std::vector<uint8_t> payload(buffer.begin() + headerSize, buffer.begin() + headerSize + payloadLen);
        if (masked)
        {
            const uint8_t* mask = &buffer[headerSize - 4];
            for (size_t i = 0; i < payload.size(); ++i)
            {
                payload[i] ^= mask[i % 4];
            }
        }
        buffer.erase(buffer.begin(), buffer.begin() + headerSize + payloadLen);

        if (opcode == 0x01 || opcode == 0x02)
        {
            if (m_onData)
            {
                m_onData(std::span<const uint8_t>(payload.data(), payload.size()));
            }
        }
        else if (opcode == 0x08)
        {
            m_running.store(false);
            return;
        }
        else if (opcode == 0x09)
        {
            // A pong must echo the ping payload and, coming from a client, be masked.
            std::vector<uint8_t> pong;
            pong.push_back(0x8A);
            pong.push_back(static_cast<uint8_t>(0x80 | payload.size()));

            std::array<uint8_t, 4> mask{};
            {
                std::lock_guard<std::mutex> lock(m_curlMutex);
                mask = nextMaskKey();
            }
            pong.insert(pong.end(), mask.begin(), mask.end());
            for (size_t i = 0; i < payload.size(); ++i)
            {
                pong.push_back(payload[i] ^ mask[i % 4]);
            }
            tunnelWrite(pong.data(), pong.size());
        }
    }
}
#else
bool CurlTransport::performWsHandshake(const std::string&, int, const std::string&)
{
    return true;
}

void CurlTransport::dispatchFramedBuffer(std::vector<uint8_t>&)
{
}
#endif

void CurlTransport::workerLoop()
{
    std::vector<uint8_t> assembled;
    std::array<uint8_t, 4096> buffer{};

    while (m_running.load())
    {
        if (!waitReadable(200))
        {
            continue;
        }

#ifdef STOMP_CURL_NATIVE_WS
        size_t received = 0;
        const struct curl_ws_frame* meta = nullptr;
        CURLcode res = CURLE_OK;
        {
            std::lock_guard<std::mutex> lock(m_curlMutex);
            if (!m_curl)
            {
                break;
            }
            res = curl_ws_recv(m_curl, buffer.data(), buffer.size(), &received, &meta);
        }

        if (res == CURLE_AGAIN)
        {
            continue;
        }
        if (res != CURLE_OK)
        {
            if (res != CURLE_GOT_NOTHING && m_running.load())
            {
                reportError(curl_easy_strerror(res));
            }
            break;
        }

        if (meta && (meta->flags & CURLWS_CLOSE))
        {
            break;
        }

        assembled.insert(assembled.end(), buffer.begin(), buffer.begin() + received);
        if (meta && meta->bytesleft == 0)
        {
            if (m_onData && !assembled.empty())
            {
                m_onData(std::span<const uint8_t>(assembled.data(), assembled.size()));
            }
            assembled.clear();
        }
#else
        size_t received = 0;
        CURLcode res = CURLE_OK;
        {
            std::lock_guard<std::mutex> lock(m_curlMutex);
            if (!m_curl)
            {
                break;
            }
            res = curl_easy_recv(m_curl, buffer.data(), buffer.size(), &received);
        }

        if (res == CURLE_AGAIN)
        {
            continue;
        }
        if (res != CURLE_OK || received == 0)
        {
            if (res != CURLE_OK && m_running.load())
            {
                reportError(curl_easy_strerror(res));
            }
            break;
        }

        assembled.insert(assembled.end(), buffer.begin(), buffer.begin() + received);
        dispatchFramedBuffer(assembled);
#endif
    }

    m_connected.store(false);
    m_running.store(false);
    if (m_onClose)
    {
        m_onClose();
    }
}

} // namespace Stomp