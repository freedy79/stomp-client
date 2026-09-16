#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace Stomp
{

class ITransport
{
public:
    virtual ~ITransport() = default;

    /// @brief Opens connection to given URL (e.g. ws://host:port/path or wss://...)
    virtual bool open(std::string_view url) = 0;

    /// @brief Closes connection
    virtual void close() = 0;

    /// @brief Check if transport is currently connected
    virtual bool isConnected() const = 0;

    /// @brief Send binary/text data packet over the transport
    virtual bool send(std::span<const uint8_t> data) = 0;

    /// @brief Convenience overload for string data
    bool send(std::string_view data)
    {
        return send(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(data.data()), data.size()));
    }

    /// @brief Set callback for incoming raw data
    virtual void setOnData(std::function<void(std::span<const uint8_t>)> onData) = 0;

    /// @brief Set callback for connection errors
    virtual void setOnError(std::function<void(std::string_view errorMsg)> onError) = 0;

    /// @brief Set callback for connection close
    virtual void setOnClose(std::function<void()> onClose) = 0;
};

} // namespace Stomp
