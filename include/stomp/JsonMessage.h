// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 freedy79

#pragma once

#ifdef STOMP_WITH_JSON

#include "stomp/StompClient.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace Stomp::Json
{

/// @brief Parses an untrusted STOMP payload without throwing.
/// @param payload Raw message body received from the broker.
/// @return Parsed document, or std::nullopt if the payload is not valid JSON.
inline std::optional<nlohmann::json> tryParse(std::string_view payload)
{
    nlohmann::json parsed = nlohmann::json::parse(payload, nullptr, false, true);
    if (parsed.is_discarded())
    {
        return std::nullopt;
    }
    return parsed;
}

/// @brief Reads a field and converts it, falling back to @p fallback on type mismatch.
template<typename T>
T get(const nlohmann::json& doc, std::string_view key, T fallback = {})
{
    const auto it = doc.find(key);
    if (it == doc.end())
    {
        return fallback;
    }

    try
    {
        return it->get<T>();
    }
    catch (const nlohmann::json::exception&)
    {
        return fallback;
    }
}

/// @brief Sends a JSON document and sets the matching content-type header.
inline bool send(StompClient& client, std::string_view destination, const nlohmann::json& body, Headers extraHeaders = {})
{
    extraHeaders.emplace_back(std::string(Header::CONTENT_TYPE), "application/json");
    return client.send(destination, body.dump(), extraHeaders);
}

} // namespace Stomp::Json

#endif // STOMP_WITH_JSON