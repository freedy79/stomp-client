// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 freedy79

#include "stomp/StompParser.h"

#include <charconv>

namespace Stomp
{

void StompParser::reset()
{
    m_state = State::WAIT_COMMAND;
    m_buffer.clear();
    m_currentFrame = StompFrame{};
    m_contentLength = 0;
}

void StompParser::feed(std::span<const uint8_t> data)
{
    if (data.empty())
    {
        return;
    }

    m_buffer.append(reinterpret_cast<const char*>(data.data()), data.size());
    processBuffer();
}

void StompParser::processBuffer()
{
    while (!m_buffer.empty())
    {
        if (m_state == State::WAIT_COMMAND)
        {
            // Skip leading CR/LF (heartbeats or whitespace between frames)
            size_t nonWs = 0;
            while (nonWs < m_buffer.size() && (m_buffer[nonWs] == '\r' || m_buffer[nonWs] == '\n'))
            {
                if (m_buffer[nonWs] == '\n' && m_onHeartbeat)
                {
                    m_onHeartbeat();
                }
                ++nonWs;
            }

            if (nonWs > 0)
            {
                m_buffer.erase(0, nonWs);
            }

            if (m_buffer.empty())
            {
                return;
            }

            size_t lineEnd = m_buffer.find('\n');
            if (lineEnd == std::string::npos)
            {
                return; // Need more data
            }

            std::string command = m_buffer.substr(0, lineEnd);
            if (!command.empty() && command.back() == '\r')
            {
                command.pop_back();
            }

            m_currentFrame = StompFrame(std::move(command));
            m_buffer.erase(0, lineEnd + 1);
            m_state = State::READ_HEADERS;
            m_contentLength = 0;
        }

        if (m_state == State::READ_HEADERS)
        {
            bool headersDone = false;
            while (!m_buffer.empty())
            {
                size_t lineEnd = m_buffer.find('\n');
                if (lineEnd == std::string::npos)
                {
                    return; // Need more data for full header line
                }

                std::string headerLine = m_buffer.substr(0, lineEnd);
                if (!headerLine.empty() && headerLine.back() == '\r')
                {
                    headerLine.pop_back();
                }
                m_buffer.erase(0, lineEnd + 1);

                if (headerLine.empty())
                {
                    // Empty line marks end of headers
                    headersDone = true;
                    break;
                }

                parseHeaderLine(headerLine);
            }

            if (!headersDone)
            {
                return;
            }

            if (auto lenStr = m_currentFrame.getHeader(Header::CONTENT_LENGTH))
            {
                size_t len = 0;
                auto [ptr, ec] = std::from_chars(lenStr->data(), lenStr->data() + lenStr->size(), len);
                if (ec == std::errc{})
                {
                    m_contentLength = len;
                    m_state = State::READ_BODY_BY_LENGTH;
                }
                else
                {
                    m_state = State::READ_BODY_UNTIL_NULL;
                }
            }
            else
            {
                m_state = State::READ_BODY_UNTIL_NULL;
            }
        }

        if (m_state == State::READ_BODY_BY_LENGTH)
        {
            // We need m_contentLength bytes + 1 byte for NULL
            if (m_buffer.size() < m_contentLength + 1)
            {
                return; // Wait for complete body
            }

            std::string body = m_buffer.substr(0, m_contentLength);
            m_currentFrame.setBody(std::move(body));

            // Verify trailing NULL
            if (m_buffer[m_contentLength] == '\0')
            {
                m_buffer.erase(0, m_contentLength + 1);
            }
            else
            {
                m_buffer.erase(0, m_contentLength);
            }

            if (m_onFrame)
            {
                m_onFrame(std::move(m_currentFrame));
            }

            m_state = State::WAIT_COMMAND;
            m_currentFrame = StompFrame{};
            m_contentLength = 0;
        }
        else if (m_state == State::READ_BODY_UNTIL_NULL)
        {
            size_t nullPos = m_buffer.find('\0');
            if (nullPos == std::string::npos)
            {
                return; // Wait for NULL byte
            }

            std::string body = m_buffer.substr(0, nullPos);
            m_currentFrame.setBody(std::move(body));
            m_buffer.erase(0, nullPos + 1);

            if (m_onFrame)
            {
                m_onFrame(std::move(m_currentFrame));
            }

            m_state = State::WAIT_COMMAND;
            m_currentFrame = StompFrame{};
        }
    }
}

bool StompParser::parseHeaderLine(std::string_view line)
{
    size_t colon = line.find(':');
    if (colon == std::string_view::npos)
    {
        return false;
    }

    std::string key = std::string(line.substr(0, colon));
    std::string value = std::string(line.substr(colon + 1));
    m_currentFrame.addHeader(std::move(key), std::move(value));
    return true;
}

} // namespace Stomp