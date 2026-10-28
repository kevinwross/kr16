#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace kr16::platform {

enum class SocketStatus {
    Ok,
    Closed,
    Error,
};

// Small blocking TCP socket wrapper. The header intentionally contains no host
// operating-system headers; all platform code lives in socket.cpp.
class TcpSocket {
public:
    TcpSocket() noexcept = default;
    ~TcpSocket();

    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    // Binds to 127.0.0.1:port and listens for a single client.
    static std::optional<TcpSocket> listen_on(std::uint16_t port,
                                              std::string& error);

    // Blocks until one client connects.
    std::optional<TcpSocket> accept(std::string& error);

    SocketStatus send_all(std::span<const char> data, std::string& error);
    SocketStatus recv_exact(std::span<char> data, std::string& error);
    SocketStatus recv_some(std::span<char> data, std::size_t& received,
                           std::string& error);

    void close() noexcept;
    bool valid() const noexcept;
    std::uint16_t local_port() const noexcept;

private:
    explicit TcpSocket(std::uintptr_t handle) : handle_(handle) {}

    std::uintptr_t handle_ = static_cast<std::uintptr_t>(-1);
};

}  // namespace kr16::platform
