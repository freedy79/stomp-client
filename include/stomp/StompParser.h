// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 freedy79

#pragma once

#include "stomp/StompFrame.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace Stomp
{

class StompParser
{
public:
    using FrameCallback = std::function<void(StompFrame)>;
    using HeartbeatCallback = std::function<void()>;

    StompParser() = default;

    /// @brief Set callback invoked whenever a full valid STOMP frame is parsed
    void setOnFrame(FrameCallback callback) { m_onFrame = std::move(callback); }

    /// @brief Set callback invoked whenever a heartbeat (EOL outside a frame) is received
    void setOnHeartbeat(HeartbeatCallback callback) { m_onHeartbeat = std::move(callback); }

    /// @brief Feed incoming raw chunk from network transport
    void feed(std::span<const uint8_t> data);

    /// @brief Reset parser state
    void reset();

private:
    enum class State
    {
        WAIT_COMMAND,
        READ_HEADERS,
        READ_BODY_BY_LENGTH,
        READ_BODY_UNTIL_NULL,
    };

    void processBuffer();
    bool parseHeaderLine(std::string_view line);

    State m_state = State::WAIT_COMMAND;
    std::string m_buffer;
    StompFrame m_currentFrame;
    size_t m_contentLength = 0;

    FrameCallback m_onFrame;
    HeartbeatCallback m_onHeartbeat;
};

} // namespace Stomp