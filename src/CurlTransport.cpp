#include "stomp/CurlTransport.h"

#include <array>
#include <iostream>

namespace Stomp
{

CurlTransport::CurlTransport()
{
    // Ensure curl is globally initialized
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

CurlTransport::~CurlTransport()
{
    close();
}

void CurlTransport::setSslVerification(bool verifyPeer, bool verifyHost)
{
    m_verifyPeer = verifyPeer;
    m_verifyHost = verifyHost;
}

bool CurlTransport::open(std::string_view url)
{
    close();

    m_url = std::string(url);
    m_curl = curl_easy_init();
    if (!m_curl)
    {
        if (m_onError)
        {
            m_onError("curl_easy_init() failed");
        }
        return false;
    }

    curl_easy_setopt(m_curl, CURLOPT_URL, m_url.c_str());
    curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYPEER, m_verifyPeer ? 1L : 0L);
    curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYHOST, m_verifyHost ? 2L : 0L);

#if defined(CURLOPT_WS_OPTIONS)
    // If libcurl WebSocket support is available
    curl_easy_setopt(m_curl, CURLOPT_WS_OPTIONS, CURLWS_RAW_MODE);
#endif

    m_running.store(true);
    m_workerThread = std::thread(&CurlTransport::workerLoop, this);

    return true;
}

void CurlTransport::close()
{
    m_running.store(false);
    if (m_workerThread.joinable())
    {
        m_workerThread.join();
    }

    if (m_curl)
    {
        curl_easy_cleanup(m_curl);
        m_curl = nullptr;
    }
}

bool CurlTransport::send(std::span<const uint8_t> data)
{
    if (!m_curl || !m_running.load())
    {
        return false;
    }

#if defined(CURLOPT_WS_OPTIONS)
    size_t sent = 0;
    CURLcode res = curl_ws_send(m_curl, data.data(), data.size(), &sent, 0, CURLWS_TEXT);
    return (res == CURLE_OK);
#else
    // Fallback: raw write
    size_t sent = 0;
    CURLcode res = curl_easy_send(m_curl, data.data(), data.size(), &sent);
    return (res == CURLE_OK);
#endif
}

void CurlTransport::workerLoop()
{
#if defined(CURLOPT_WS_OPTIONS)
    // Perform initial handshake connect
    CURLcode connRes = curl_easy_perform(m_curl);
    if (connRes != CURLE_OK && connRes != CURLE_AGAIN)
    {
        if (m_onError)
        {
            m_onError(curl_easy_strerror(connRes));
        }
        m_running.store(false);
        if (m_onClose)
        {
            m_onClose();
        }
        return;
    }

    std::array<uint8_t, 4096> buffer;
    while (m_running.load())
    {
        size_t rlen = 0;
        const struct curl_ws_frame* meta = nullptr;
        CURLcode res = curl_ws_recv(m_curl, buffer.data(), buffer.size(), &rlen, &meta);

        if (res == CURLE_OK && rlen > 0)
        {
            if (m_onData)
            {
                m_onData(std::span<const uint8_t>(buffer.data(), rlen));
            }
        }
        else if (res == CURLE_AGAIN)
        {
            // Socket waiting for data
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        else
        {
            if (res != CURLE_OK && m_onError)
            {
                m_onError(curl_easy_strerror(res));
            }
            break;
        }
    }
#else
    // Generic socket/read loop fallback
    std::array<uint8_t, 4096> buffer;
    while (m_running.load())
    {
        size_t rlen = 0;
        CURLcode res = curl_easy_recv(m_curl, buffer.data(), buffer.size(), &rlen);
        if (res == CURLE_OK && rlen > 0)
        {
            if (m_onData)
            {
                m_onData(std::span<const uint8_t>(buffer.data(), rlen));
            }
        }
        else if (res == CURLE_AGAIN)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        else
        {
            break;
        }
    }
#endif

    m_running.store(false);
    if (m_onClose)
    {
        m_onClose();
    }
}

} // namespace Stomp
