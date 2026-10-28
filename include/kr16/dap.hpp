#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kr16::dap {

// Program and execution settings for a debug session. The CLI fills these in;
// a launch request may override the program path and the stop-on-entry policy.
struct DapOptions {
    std::string program;
    bool raw = false;
    std::uint16_t base = 0x0100;
    std::string bios_path;
    bool no_bios = false;
    bool stop_on_entry = true;
    // Bound for a single continue/step request so a runaway guest cannot hang
    // the adapter. Reaching the limit reports a paused stop.
    std::uint64_t max_run_instructions = 1'000'000;
};

// Raised for malformed DAP framing headers.
class DapProtocolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline constexpr std::size_t kDapMaxMessageBytes = 4u * 1024u * 1024u;
inline constexpr std::size_t kDapMaxHeaderBytes = 64u * 1024u;

// Encodes a JSON message body as a Content-Length framed DAP packet.
std::string dap_encode_message(std::string_view body);

// Incremental Content-Length frame decoder. Feed arbitrary chunks, then pull
// complete message bodies with next(). Throws DapProtocolError for malformed
// or oversized headers.
class DapFramer {
public:
    void feed(std::string_view bytes);

    // Returns std::nullopt when more data is needed. Throws DapProtocolError.
    std::optional<std::string> next();

    bool empty() const noexcept { return buffer_.empty(); }
    std::size_t buffered() const noexcept { return buffer_.size(); }
    void reset() noexcept { buffer_.clear(); }

private:
    std::string buffer_;
};

// A single-threaded debug session. handle_message() consumes one JSON request
// body (without Content-Length framing) and returns zero or more JSON message
// bodies (without framing) in the order they should be sent. This makes the
// protocol layer testable without sockets or threads.
class DapSession {
public:
    explicit DapSession(DapOptions options = {});
    ~DapSession();

    DapSession(DapSession&&) noexcept;
    DapSession& operator=(DapSession&&) noexcept;
    DapSession(const DapSession&) = delete;
    DapSession& operator=(const DapSession&) = delete;

    std::vector<std::string> handle_message(std::string_view message);

    // True after disconnect; the server should stop listening/serving.
    bool should_stop() const noexcept;
    bool terminated() const noexcept;
    bool launched() const noexcept;

    // Sequence number that will be assigned to the next outgoing message.
    std::int64_t last_sequence() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// One-client TCP DAP server. run() listens on 127.0.0.1, accepts exactly one
// client, serves requests until disconnect or termination, then closes.
class DapServer {
public:
    explicit DapServer(DapOptions options);

    // Returns 0 for a clean session end, non-zero for socket/protocol errors.
    int run(std::uint16_t port);

private:
    DapOptions options_;
};

}  // namespace kr16::dap
