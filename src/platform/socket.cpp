#include "socket.hpp"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace kr16::platform {
namespace {

#if defined(_WIN32)
using NativeSocket = SOCKET;
using NativeLength = int;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
constexpr int kSendFlags = 0;
#else
using NativeSocket = int;
using NativeLength = socklen_t;
constexpr NativeSocket kInvalidSocket = -1;
#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif
#endif

NativeSocket to_native(std::uintptr_t handle) {
    return static_cast<NativeSocket>(handle);
}

std::uintptr_t from_native(NativeSocket socket) {
    return static_cast<std::uintptr_t>(socket);
}

constexpr std::uintptr_t kInvalidHandle = static_cast<std::uintptr_t>(-1);

std::string last_error(const char* operation) {
#if defined(_WIN32)
    return std::string(operation) + " failed (WSA error " +
           std::to_string(WSAGetLastError()) + ")";
#else
    return std::string(operation) + " failed: " +
           std::generic_category().message(errno);
#endif
}

bool ensure_winsock(std::string& error) {
#if defined(_WIN32)
    static std::once_flag once;
    static bool ready = false;
    static std::string problem;
    std::call_once(once, [] {
        WSADATA data{};
        const int code = WSAStartup(MAKEWORD(2, 2), &data);
        if (code != 0) {
            problem = "WSAStartup failed with error " + std::to_string(code);
        } else {
            ready = true;
        }
    });
    if (!ready) {
        error = problem;
    }
    return ready;
#else
    static_cast<void>(error);
    return true;
#endif
}

void close_native(NativeSocket socket) noexcept {
#if defined(_WIN32)
    ::closesocket(socket);
#else
    ::close(socket);
#endif
}

bool set_reuse_address(NativeSocket socket) {
#if defined(_WIN32)
    const char enabled = 1;
    return ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enabled,
                        static_cast<int>(sizeof(enabled))) == 0;
#else
    const int enabled = 1;
    return ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enabled,
                        static_cast<socklen_t>(sizeof(enabled))) == 0;
#endif
}

bool would_interrupt() {
#if defined(_WIN32)
    return WSAGetLastError() == WSAEINTR;
#else
    return errno == EINTR;
#endif
}

bool is_connection_reset() {
#if defined(_WIN32)
    const int code = WSAGetLastError();
    return code == WSAECONNRESET || code == WSAECONNABORTED;
#else
    return errno == ECONNRESET || errno == EPIPE;
#endif
}

}  // namespace

TcpSocket::~TcpSocket() { close(); }

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : handle_(other.handle_) {
    other.handle_ = kInvalidHandle;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        other.handle_ = kInvalidHandle;
    }
    return *this;
}

std::optional<TcpSocket> TcpSocket::listen_on(std::uint16_t port,
                                              std::string& error) {
    if (!ensure_winsock(error)) {
        return std::nullopt;
    }
    const NativeSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == kInvalidSocket) {
        error = last_error("socket");
        return std::nullopt;
    }
    TcpSocket result(from_native(socket));

    static_cast<void>(set_reuse_address(socket));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(socket, reinterpret_cast<sockaddr*>(&address),
               static_cast<NativeLength>(sizeof(address))) != 0) {
        error = last_error("bind");
        result.close();
        return std::nullopt;
    }
    if (::listen(socket, 1) != 0) {
        error = last_error("listen");
        result.close();
        return std::nullopt;
    }
    return result;
}

std::optional<TcpSocket> TcpSocket::accept(std::string& error) {
    if (!valid()) {
        error = "accept failed: socket is closed";
        return std::nullopt;
    }
    const NativeSocket socket = ::accept(to_native(handle_), nullptr, nullptr);
    if (socket == kInvalidSocket) {
        error = last_error("accept");
        return std::nullopt;
    }
    TcpSocket result(from_native(socket));
#if defined(SO_NOSIGPIPE)
    const int enabled = 1;
    static_cast<void>(::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled,
                                   static_cast<socklen_t>(sizeof(enabled))));
#endif
    return result;
}

SocketStatus TcpSocket::send_all(std::span<const char> data,
                                 std::string& error) {
    if (!valid()) {
        error = "send failed: socket is closed";
        return SocketStatus::Error;
    }
    const char* cursor = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
#if defined(_WIN32)
        const auto chunk = static_cast<int>(
            std::min<std::size_t>(remaining, static_cast<std::size_t>(INT_MAX)));
        const int sent =
            ::send(to_native(handle_), cursor, chunk, kSendFlags);
#else
        const auto sent = ::send(to_native(handle_), cursor, remaining,
                                 kSendFlags);
#endif
        if (sent < 0) {
            if (would_interrupt()) {
                continue;
            }
            if (is_connection_reset()) {
                error = "connection closed while sending";
                return SocketStatus::Closed;
            }
            error = last_error("send");
            return SocketStatus::Error;
        }
        if (sent == 0) {
            error = "connection closed while sending";
            return SocketStatus::Closed;
        }
        const auto consumed = static_cast<std::size_t>(sent);
        cursor += consumed;
        remaining -= consumed;
    }
    return SocketStatus::Ok;
}

SocketStatus TcpSocket::recv_some(std::span<char> data, std::size_t& received,
                                  std::string& error) {
    received = 0;
    if (!valid()) {
        error = "receive failed: socket is closed";
        return SocketStatus::Error;
    }
    if (data.empty()) {
        return SocketStatus::Ok;
    }
    for (;;) {
#if defined(_WIN32)
        const auto chunk = static_cast<int>(
            std::min<std::size_t>(data.size(), static_cast<std::size_t>(INT_MAX)));
        const int got = ::recv(to_native(handle_), data.data(), chunk, 0);
#else
        const auto got =
            ::recv(to_native(handle_), data.data(), data.size(), 0);
#endif
        if (got > 0) {
            received = static_cast<std::size_t>(got);
            return SocketStatus::Ok;
        }
        if (got == 0) {
            return SocketStatus::Closed;
        }
        if (would_interrupt()) {
            continue;
        }
        if (is_connection_reset()) {
            error = "connection reset by peer";
            return SocketStatus::Closed;
        }
        error = last_error("recv");
        return SocketStatus::Error;
    }
}

SocketStatus TcpSocket::recv_exact(std::span<char> data, std::string& error) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        std::size_t received = 0;
        const auto status =
            recv_some(data.subspan(offset), received, error);
        if (status != SocketStatus::Ok) {
            if (status == SocketStatus::Closed && offset > 0) {
                error = "connection closed after a partial read";
                return SocketStatus::Error;
            }
            return status;
        }
        offset += received;
    }
    return SocketStatus::Ok;
}

void TcpSocket::close() noexcept {
    if (valid()) {
        close_native(to_native(handle_));
        handle_ = kInvalidHandle;
    }
}

bool TcpSocket::valid() const noexcept {
    return handle_ != kInvalidHandle;
}

std::uint16_t TcpSocket::local_port() const noexcept {
    if (!valid()) {
        return 0;
    }
    sockaddr_in address{};
    auto length = static_cast<NativeLength>(sizeof(address));
    if (::getsockname(to_native(handle_),
                      reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        return 0;
    }
    return ntohs(address.sin_port);
}

}  // namespace kr16::platform
