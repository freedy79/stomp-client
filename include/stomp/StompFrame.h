// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 freedy79

#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Stomp
{

namespace Command
{
    inline constexpr std::string_view CONNECT = "CONNECT";
    inline constexpr std::string_view STOMP = "STOMP";
    inline constexpr std::string_view CONNECTED = "CONNECTED";
    inline constexpr std::string_view SEND = "SEND";
    inline constexpr std::string_view SUBSCRIBE = "SUBSCRIBE";
    inline constexpr std::string_view UNSUBSCRIBE = "UNSUBSCRIBE";
    inline constexpr std::string_view BEGIN = "BEGIN";
    inline constexpr std::string_view COMMIT = "COMMIT";
    inline constexpr std::string_view ABORT = "ABORT";
    inline constexpr std::string_view ACK = "ACK";
    inline constexpr std::string_view NACK = "NACK";
    inline constexpr std::string_view DISCONNECT = "DISCONNECT";
    inline constexpr std::string_view MESSAGE = "MESSAGE";
    inline constexpr std::string_view RECEIPT = "RECEIPT";
    inline constexpr std::string_view ERROR = "ERROR";
}

namespace Header
{
    inline constexpr std::string_view ACCEPT_VERSION = "accept-version";
    inline constexpr std::string_view HOST = "host";
    inline constexpr std::string_view LOGIN = "login";
    inline constexpr std::string_view PASSCODE = "passcode";
    inline constexpr std::string_view HEARTBEAT = "heart-beat";
    inline constexpr std::string_view DESTINATION = "destination";
    inline constexpr std::string_view ID = "id";
    inline constexpr std::string_view ACK = "ack";
    inline constexpr std::string_view TRANSACTION = "transaction";
    inline constexpr std::string_view RECEIPT = "receipt";
    inline constexpr std::string_view RECEIPT_ID = "receipt-id";
    inline constexpr std::string_view SUBSCRIPTION = "subscription";
    inline constexpr std::string_view MESSAGE_ID = "message-id";
    inline constexpr std::string_view CONTENT_LENGTH = "content-length";
    inline constexpr std::string_view CONTENT_TYPE = "content-type";
    inline constexpr std::string_view SESSION = "session";
    inline constexpr std::string_view SERVER = "server";
    inline constexpr std::string_view VERSION = "version";
    inline constexpr std::string_view MESSAGE = "message";
}

using Headers = std::vector<std::pair<std::string, std::string>>;

class StompFrame
{
public:
    StompFrame() = default;
    explicit StompFrame(std::string command);

    const std::string& getCommand() const noexcept { return m_command; }
    void setCommand(std::string command) { m_command = std::move(command); }

    const Headers& getHeaders() const noexcept { return m_headers; }
    std::optional<std::string_view> getHeader(std::string_view key) const noexcept;
    void setHeader(std::string key, std::string value);
    void addHeader(std::string key, std::string value);
    bool hasHeader(std::string_view key) const noexcept;

    const std::string& getBody() const noexcept { return m_body; }
    void setBody(std::string body);

    /// @brief Serialize STOMP frame into wire format with trailing NULL byte \0
    std::string serialize() const;

private:
    std::string m_command;
    Headers m_headers;
    std::string m_body;
};

} // namespace Stomp