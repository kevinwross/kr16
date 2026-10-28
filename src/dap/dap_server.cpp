#include "kr16/dap.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "platform/socket.hpp"

namespace kr16::dap {
namespace {

constexpr std::string_view kHeaderTerminator = "\r\n\r\n";

char ascii_lower(char value) {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

bool ascii_iequals(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (ascii_lower(left[i]) != ascii_lower(right[i])) {
            return false;
        }
    }
    return true;
}

std::string_view trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           (text[begin] == ' ' || text[begin] == '\t')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::size_t parse_content_length(std::string_view text) {
    if (text.empty()) {
        throw DapProtocolError("Content-Length header is empty");
    }
    std::size_t value = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            throw DapProtocolError(
                "Content-Length header is not a decimal number");
        }
        const auto digit = static_cast<std::size_t>(character - '0');
        if (value > (kDapMaxMessageBytes - digit) / 10) {
            throw DapProtocolError("Content-Length exceeds the size limit");
        }
        value = value * 10 + digit;
    }
    return value;
}

bool write_packet(platform::TcpSocket& socket, std::string_view body,
                  std::string& error) {
    const auto packet = dap_encode_message(body);
    return socket.send_all(std::span<const char>(packet.data(), packet.size()),
                           error) == platform::SocketStatus::Ok;
}

int report_error(const std::string& message) {
    std::cerr << "kr16 dap: " << message << '\n';
    return 1;
}

}  // namespace

std::string dap_encode_message(std::string_view body) {
    std::string packet = "Content-Length: ";
    packet += std::to_string(body.size());
    packet += "\r\n\r\n";
    packet.append(body);
    return packet;
}

void DapFramer::feed(std::string_view bytes) {
    constexpr std::size_t kMaxBuffered =
        kDapMaxHeaderBytes + kDapMaxMessageBytes;
    if (buffer_.size() > kMaxBuffered ||
        bytes.size() > kMaxBuffered - buffer_.size()) {
        throw DapProtocolError("DAP input buffer exceeds the size limit");
    }
    buffer_.append(bytes);
}

std::optional<std::string> DapFramer::next() {
    const auto header_end = buffer_.find(kHeaderTerminator);
    if (header_end == std::string::npos) {
        if (buffer_.size() > kDapMaxHeaderBytes) {
            throw DapProtocolError("DAP headers exceed the size limit");
        }
        return std::nullopt;
    }
    if (header_end > kDapMaxHeaderBytes) {
        throw DapProtocolError("DAP headers exceed the size limit");
    }

    bool have_content_length = false;
    std::size_t content_length = 0;
    std::string_view headers(buffer_.data(), header_end);
    while (!headers.empty()) {
        const auto line_end = headers.find("\r\n");
        const auto line = (line_end == std::string_view::npos)
                              ? headers
                              : headers.substr(0, line_end);
        headers = (line_end == std::string_view::npos)
                      ? std::string_view{}
                      : headers.substr(line_end + 2);
        if (line.empty()) {
            continue;
        }
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) {
            throw DapProtocolError("malformed DAP header line");
        }
        const auto name = trim(line.substr(0, colon));
        const auto value = trim(line.substr(colon + 1));
        if (ascii_iequals(name, "Content-Length")) {
            if (have_content_length) {
                throw DapProtocolError("duplicate Content-Length header");
            }
            have_content_length = true;
            content_length = parse_content_length(value);
        }
    }
    if (!have_content_length) {
        throw DapProtocolError("missing Content-Length header");
    }

    const auto body_start = header_end + kHeaderTerminator.size();
    if (buffer_.size() - body_start < content_length) {
        return std::nullopt;
    }
    std::string body = buffer_.substr(body_start, content_length);
    buffer_.erase(0, body_start + content_length);
    return body;
}

DapServer::DapServer(DapOptions options)
    : options_(std::move(options)) {}

int DapServer::run(std::uint16_t port) {    std::string error;
    auto listener = platform::TcpSocket::listen_on(port, error);
    if (!listener.has_value()) {
        return report_error(error);
    }
    std::cerr << "kr16 dap: listening on 127.0.0.1:" << listener->local_port()
              << '\n';
    if (!options_.ready_file.empty()) {
        std::ofstream ready(options_.ready_file);
        ready << listener->local_port() << '\n';
        ready.close();
    }

    auto client = listener->accept(error);
    if (!client.has_value()) {
        return report_error(error);
    }
    std::cerr << "kr16 dap: client connected\n";

    DapSession session(options_);
    DapFramer framer;
    std::array<char, 4096> buffer{};

    for (;;) {
        std::optional<std::string> body;
        try {
            body = framer.next();
        } catch (const DapProtocolError& problem) {
            return report_error(problem.what());
        }

        while (body.has_value()) {
            std::vector<std::string> messages;
            try {
                messages = session.handle_message(*body);
            } catch (const std::exception& problem) {
                std::cerr << "kr16 dap: request error: " << problem.what()
                          << '\n';
                messages.clear();
            }
            for (const auto& message : messages) {
                if (!write_packet(*client, message, error)) {
                    return report_error(error);
                }
            }
            if (session.should_stop()) {
                return 0;
            }
            try {
                body = framer.next();
            } catch (const DapProtocolError& problem) {
                return report_error(problem.what());
            }
        }

        std::size_t received = 0;
        const auto status = client->recv_some(buffer, received, error);
        if (status == platform::SocketStatus::Closed) {
            return 0;
        }
        if (status != platform::SocketStatus::Ok) {
            return report_error(error);
        }
        try {
            framer.feed(std::string_view(buffer.data(), received));
        } catch (const DapProtocolError& problem) {
            return report_error(problem.what());
        }
    }
}

}  // namespace kr16::dap
