#include "stomp/StompFrame.h"

#include <algorithm>
#include <sstream>

namespace Stomp
{

StompFrame::StompFrame(std::string command)
    : m_command(std::move(command))
{
}

std::optional<std::string_view> StompFrame::getHeader(std::string_view key) const noexcept
{
    for (const auto& [k, v] : m_headers)
    {
        if (k == key)
        {
            return v;
        }
    }
    return std::nullopt;
}

void StompFrame::setHeader(std::string key, std::string value)
{
    for (auto& [k, v] : m_headers)
    {
        if (k == key)
        {
            v = std::move(value);
            return;
        }
    }
    m_headers.emplace_back(std::move(key), std::move(value));
}

void StompFrame::addHeader(std::string key, std::string value)
{
    m_headers.emplace_back(std::move(key), std::move(value));
}

bool StompFrame::hasHeader(std::string_view key) const noexcept
{
    return getHeader(key).has_value();
}

void StompFrame::setBody(std::string body)
{
    m_body = std::move(body);
}

std::string StompFrame::serialize() const
{
    std::string out;
    out.reserve(m_command.size() + 64 + m_body.size());

    out.append(m_command);
    out.push_back('\n');

    for (const auto& [k, v] : m_headers)
    {
        out.append(k);
        out.push_back(':');
        out.append(v);
        out.push_back('\n');
    }

    out.push_back('\n'); // Header-Body separator
    out.append(m_body);
    out.push_back('\0'); // STOMP end-of-frame NULL byte

    return out;
}

} // namespace Stomp
