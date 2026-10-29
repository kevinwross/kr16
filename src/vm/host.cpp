#include "kr16/host.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace kr16 {
namespace {

namespace fs = std::filesystem;

constexpr std::uint16_t FsOk = 0;
constexpr std::uint16_t FsENoEnt = 1;
constexpr std::uint16_t FsEAcces = 2;
constexpr std::uint16_t FsEInval = 3;
constexpr std::uint16_t FsEBadF = 4;
constexpr std::uint16_t FsEFault = 5;
constexpr std::uint16_t FsENoSpc = 6;
constexpr std::uint16_t FsEExist = 7;
constexpr std::uint16_t FsENoSys = 8;
constexpr std::uint16_t FsEIo = 9;
constexpr std::uint16_t FsEMFILE = 10;
constexpr std::uint16_t FsENameTooLong = 11;
constexpr std::uint16_t FsEIsDir = 13;

constexpr std::uint16_t SOpen = 1;
constexpr std::uint16_t SClose = 2;
constexpr std::uint16_t SRead = 3;
constexpr std::uint16_t SWrite = 4;
constexpr std::uint16_t SSeek = 5;
constexpr std::uint16_t SStat = 6;
constexpr std::uint16_t SUnlink = 7;
constexpr std::uint16_t SRename = 8;
constexpr std::uint16_t SMkdir = 9;
constexpr std::uint16_t SReaddir = 10;
constexpr std::uint16_t STime = 11;
constexpr std::uint16_t SRandom = 12;
constexpr std::uint16_t SFSInfo = 13;

constexpr std::uint16_t ORdOnly = 0;
constexpr std::uint16_t OWrOnly = 1;
constexpr std::uint16_t ORdWr = 2;
constexpr std::uint16_t OCreat = 0x0100;
constexpr std::uint16_t OTrunc = 0x0200;
constexpr std::uint16_t OAppend = 0x0400;
constexpr std::uint16_t OExcl = 0x0800;

constexpr std::uint16_t SeekSet = 0;
constexpr std::uint16_t SeekCur = 1;
constexpr std::uint16_t SeekEnd = 2;

constexpr std::size_t MaxTransfer = 4096;
constexpr std::size_t MaxPath = 255;
constexpr std::size_t MaxOpenFiles = 16;
constexpr std::size_t DirentSize = 64;
constexpr std::size_t MaxDirName = 59;

struct FileHandle {
    std::FILE* file = nullptr;
    bool read = false;
    bool write = false;
    bool directory = false;
    std::vector<fs::directory_entry> entries;
    std::size_t index = 0;

    FileHandle() = default;
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    ~FileHandle() {
        if (file != nullptr) {
            std::fclose(file);
        }
    }
};

struct StatData {
    std::uint16_t mode = 0;
    std::uint32_t size = 0;
    std::uint32_t mtime = 0;
};

bool read_guest(MemoryBus& bus, std::uint16_t address,
                std::span<std::uint8_t> bytes) {
    if (static_cast<std::uint32_t>(address) + bytes.size() > 0xF000u) {
        return false;
    }
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = bus.read8(static_cast<std::uint16_t>(address + i));
    }
    return true;
}

bool write_guest(MemoryBus& bus, std::uint16_t address,
                 std::span<const std::uint8_t> bytes) {
    if (static_cast<std::uint32_t>(address) + bytes.size() > 0xF000u) {
        return false;
    }
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bus.write8(static_cast<std::uint16_t>(address + i), bytes[i]);
    }
    return true;
}

bool read_guest_string(MemoryBus& bus, std::uint16_t address,
                       std::string& value, std::uint16_t& status) {
    value.clear();
    for (std::uint32_t i = 0; i < MaxPath + 1; ++i) {
        if (static_cast<std::uint32_t>(address) + i >= 0xF000u) {
            status = FsEFault;
            return false;
        }
        const char ch = static_cast<char>(
            bus.read8(static_cast<std::uint16_t>(address + i)));
        if (ch == '\0') {
            return true;
        }
        if (value.size() >= MaxPath) {
            status = FsENameTooLong;
            return false;
        }
        value.push_back(ch);
    }
    status = FsENameTooLong;
    return false;
}

bool valid_guest_range(std::uint16_t address, std::uint32_t size) {
    return static_cast<std::uint32_t>(address) + size <= 0xF000u;
}

std::uint32_t get_lo_hi(std::uint16_t lo, std::uint16_t hi) {
    return static_cast<std::uint32_t>(lo) |
           (static_cast<std::uint32_t>(hi) << 16);
}

std::uint32_t file_time_to_unix(fs::file_time_type time) {
    using namespace std::chrono;
    const auto system_time =
        time_point_cast<system_clock::duration>(
            time - fs::file_time_type::clock::now() + system_clock::now());
    const auto seconds = system_clock::to_time_t(system_time);
    return seconds < 0 ? 0u : static_cast<std::uint32_t>(seconds);
}

bool is_reserved_name(const std::string& name) {
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::toupper(ch));
                   });
    static const char* reserved[] = {
        "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
        "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
        "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (const char* item : reserved) {
        if (upper == item) {
            return true;
        }
    }
    return false;
}

std::FILE* open_stream(const fs::path& path, const char* mode) {
#if defined(_WIN32)
    std::FILE* file = nullptr;
    if (fopen_s(&file, path.string().c_str(), mode) != 0) {
        return nullptr;
    }
    return file;
#else
    return std::fopen(path.string().c_str(), mode);
#endif
}

}  // namespace

struct HostServices::Impl {
    explicit Impl(HostFsOptions options_in)
        : options(std::move(options_in)),
          started(std::chrono::steady_clock::now()),
          random(0x4B523136u) {}

    HostFsOptions options;
    std::chrono::steady_clock::time_point started;
    std::mt19937 random;
    std::array<std::unique_ptr<FileHandle>, MaxOpenFiles> files;
    bool root_ready = false;

    std::uint16_t ensure_root() {
        if (!options.enabled) {
            return FsENoSys;
        }
        std::error_code ec;
        if (!root_ready) {
            if (options.root.empty()) {
                return FsEIo;
            }
            const bool exists = fs::exists(options.root, ec);
            if (!exists) {
                if (options.root_explicit) {
                    return FsENoEnt;
                }
                fs::create_directories(options.root, ec);
                if (ec) {
                    return FsEIo;
                }
            }
            if (ec || !fs::is_directory(options.root, ec) || ec) {
                return FsEIo;
            }
            root_ready = true;
        }
        return FsOk;
    }

    std::uint16_t normalize(const std::string& guest_path,
                            fs::path& result, bool allow_missing) {
        if (guest_path.empty() || guest_path.size() > MaxPath) {
            return guest_path.size() > MaxPath ? FsENameTooLong : FsEInval;
        }
        std::string relative = guest_path;
        if (relative.front() == '/') {
            relative.erase(relative.begin());
        }
        if (relative.empty()) {
            return FsEInval;
        }
        for (const char ch : relative) {
            const unsigned char byte = static_cast<unsigned char>(ch);
            if (ch == '\\' || ch == ':' || byte < 32 || byte == 127) {
                return FsEAcces;
            }
        }
        std::vector<std::string> parts;
        std::size_t begin = 0;
        while (begin <= relative.size()) {
            const auto slash = relative.find('/', begin);
            const std::string part = relative.substr(
                begin, slash == std::string::npos
                           ? std::string::npos
                           : slash - begin);
            if (part.empty() || part == "." || part == "..") {
                return FsEAcces;
            }
            if (part.back() == '.' || part.back() == ' ') {
                return FsEAcces;
            }
            if (is_reserved_name(part)) {
                return FsEAcces;
            }
            parts.push_back(part);
            if (slash == std::string::npos) {
                break;
            }
            begin = slash + 1;
        }
        std::error_code ec;
        const fs::path root = fs::absolute(options.root, ec).lexically_normal();
        if (ec) {
            return FsEIo;
        }
        fs::path candidate = root;
        for (const auto& part : parts) {
            candidate /= part;
        }
        candidate = candidate.lexically_normal();
        const fs::path relative_check = candidate.lexically_relative(root);
        if (relative_check.empty() || relative_check == ".." ||
            relative_check.string().rfind("..", 0) == 0) {
            return FsEAcces;
        }
        fs::path check = root;
        for (const auto& part : parts) {
            check /= part;
            const fs::file_status link = fs::symlink_status(check, ec);
            if (ec) {
                ec.clear();
                if (allow_missing) {
                    break;
                }
                return FsENoEnt;
            }
            if (fs::is_symlink(link)) {
                return FsEAcces;
            }
        }
        result = candidate;
        return FsOk;
    }

    std::uint64_t used_bytes() {
        std::uint64_t total = 0;
        std::error_code ec;
        if (!fs::exists(options.root, ec) ||
            !fs::is_directory(options.root, ec)) {
            return 0;
        }
        for (fs::recursive_directory_iterator it(options.root, ec), end;
             !ec && it != end; it.increment(ec)) {
            const auto status = it->symlink_status(ec);
            if (ec) {
                ec.clear();
                continue;
            }
            if (fs::is_symlink(status)) {
                continue;
            }
            if (fs::is_regular_file(status)) {
                total += it->file_size(ec);
                if (ec) {
                    ec.clear();
                }
            }
        }
        return total;
    }

    FileHandle* handle(std::uint16_t fd) {
        if (fd < 3 || fd >= 3 + files.size()) {
            return nullptr;
        }
        return files[fd - 3].get();
    }

    std::uint16_t open_file(const std::string& guest_path,
                            std::uint16_t flags, std::uint16_t& fd) {
        const std::uint16_t status = ensure_root();
        if (status != FsOk) {
            return status;
        }
        const std::uint16_t access = flags & 3;
        const bool want_read = access == ORdOnly || access == ORdWr;
        const bool want_write = access == OWrOnly || access == ORdWr;
        const bool create = (flags & OCreat) != 0;
        const bool truncate = (flags & OTrunc) != 0;
        const bool append = (flags & OAppend) != 0;
        const bool exclusive = (flags & OExcl) != 0;
        if (options.readonly && (want_write || create || truncate)) {
            return FsEAcces;
        }
        if (!want_read && !want_write) {
            return FsEInval;
        }
        fs::path path;
        const std::uint16_t path_status =
            normalize(guest_path, path, create);
        if (path_status != FsOk) {
            return path_status;
        }
        std::error_code ec;
        const bool exists = fs::exists(path, ec);
        if (ec) {
            return FsENoEnt;
        }
        if (create && exclusive && exists) {
            return FsEExist;
        }
        if (!exists && !create) {
            return FsENoEnt;
        }
        if (exists) {
            const auto current = fs::symlink_status(path, ec);
            if (ec) {
                return FsENoEnt;
            }
            if (fs::is_symlink(current)) {
                return FsEAcces;
            }
            if (fs::is_directory(current)) {
                if (want_write || truncate || append || create) {
                    return FsEIsDir;
                }
                auto directory = std::make_unique<FileHandle>();
                directory->directory = true;
                directory->read = true;
                for (fs::directory_iterator it(path, ec), end;
                     !ec && it != end; it.increment(ec)) {
                    directory->entries.push_back(*it);
                }
                if (ec) {
                    return FsEIo;
                }
                for (std::size_t i = 0; i < files.size(); ++i) {
                    if (!files[i]) {
                        files[i] = std::move(directory);
                        fd = static_cast<std::uint16_t>(i + 3);
                        return FsOk;
                    }
                }
                return FsEMFILE;
            }
        } else if (want_read && !want_write) {
            return FsENoEnt;
        }

        std::string mode;
        if (exists) {
            if (truncate) {
                mode = want_read ? "w+b" : "wb";
            } else if (append) {
                mode = want_read ? "a+b" : "ab";
            } else if (want_read && want_write) {
                mode = "r+b";
            } else if (want_read) {
                mode = "rb";
            } else {
                mode = "r+b";
            }
        } else {
            if (!want_write) {
                return FsEInval;
            }
            if (append) {
                mode = want_read ? "a+b" : "ab";
            } else {
                mode = want_read ? "w+b" : "wb";
            }
        }
        std::FILE* file = open_stream(path, mode.c_str());
        if (file == nullptr) {
            return create ? FsEIo : FsENoEnt;
        }
        int new_fd = -1;
        for (std::size_t i = 0; i < files.size(); ++i) {
            if (!files[i]) {
                auto handle_value = std::make_unique<FileHandle>();
                handle_value->file = file;
                handle_value->read = want_read;
                handle_value->write = want_write;
                files[i] = std::move(handle_value);
                new_fd = static_cast<int>(i + 3);
                break;
            }
        }
        if (new_fd < 0) {
            std::fclose(file);
            return FsEMFILE;
        }
        fd = static_cast<std::uint16_t>(new_fd);
        return FsOk;
    }

    std::uint16_t close_file(std::uint16_t fd) {
        if (fd < 3 || fd >= 3 + files.size() || !files[fd - 3]) {
            return FsEBadF;
        }
        files[fd - 3].reset();
        return FsOk;
    }

    std::uint16_t read_file(std::uint16_t fd, MemoryBus& bus,
                            std::uint16_t buffer, std::uint16_t count,
                            std::uint16_t& read_count) {
        FileHandle* file = handle(fd);
        if (file == nullptr || file->directory) {
            return FsEBadF;
        }
        if (!file->read) {
            return FsEAcces;
        }
        if (count > MaxTransfer || !valid_guest_range(buffer, count)) {
            return FsEInval;
        }
        std::vector<std::uint8_t> bytes(count);
        const std::size_t actual =
            std::fread(bytes.data(), 1, bytes.size(), file->file);
        if (actual == 0 && std::ferror(file->file) != 0) {
            return FsEIo;
        }
        if (!write_guest(bus, buffer,
                         std::span<const std::uint8_t>(bytes.data(), actual))) {
            return FsEFault;
        }
        read_count = static_cast<std::uint16_t>(actual);
        return FsOk;
    }

    std::uint16_t write_file(std::uint16_t fd, MemoryBus& bus,
                             std::uint16_t buffer, std::uint16_t count,
                             std::uint16_t& written) {
        FileHandle* file = handle(fd);
        if (file == nullptr || file->directory) {
            return FsEBadF;
        }
        if (!file->write) {
            return FsEAcces;
        }
        if (count > MaxTransfer || !valid_guest_range(buffer, count)) {
            return FsEInval;
        }
        std::vector<std::uint8_t> bytes(count);
        if (!read_guest(bus, buffer, bytes)) {
            return FsEFault;
        }
        std::uint32_t current_size = 0;
        if (std::FILE* f = file->file) {
            const long position = std::ftell(f);
            if (position > 0) {
                current_size = static_cast<std::uint32_t>(position);
            }
        }
        const std::uint64_t used = used_bytes();
        const std::uint64_t base =
            used > current_size ? used - current_size : 0;
        const std::uint64_t allowed =
            options.quota > base ? options.quota - base : 0;
        std::size_t to_write = count;
        bool quota_short = false;
        if (to_write > allowed) {
            to_write = static_cast<std::size_t>(allowed);
            quota_short = true;
        }
        const std::size_t actual =
            std::fwrite(bytes.data(), 1, to_write, file->file);
        if (actual != to_write || std::ferror(file->file) != 0) {
            return FsEIo;
        }
        written = static_cast<std::uint16_t>(actual);
        return quota_short ? FsENoSpc : FsOk;
    }

    std::uint16_t seek_file(std::uint16_t fd, std::int32_t offset,
                            std::uint16_t whence, std::uint32_t& result) {
        FileHandle* file = handle(fd);
        if (file == nullptr || file->directory) {
            return FsEBadF;
        }
        int origin = SEEK_SET;
        if (whence == SeekCur) {
            origin = SEEK_CUR;
        } else if (whence == SeekEnd) {
            origin = SEEK_END;
        } else if (whence != SeekSet) {
            return FsEInval;
        }
        if (std::fseek(file->file, static_cast<long>(offset), origin) != 0) {
            return FsEInval;
        }
        const long position = std::ftell(file->file);
        if (position < 0) {
            return FsEInval;
        }
        result = static_cast<std::uint32_t>(position);
        return FsOk;
    }

    std::uint16_t stat_file(const std::string& guest_path,
                            StatData& result) {
        const std::uint16_t status = ensure_root();
        if (status != FsOk) {
            return status;
        }
        fs::path path;
        const std::uint16_t path_status =
            normalize(guest_path, path, false);
        if (path_status != FsOk) {
            return path_status;
        }
        std::error_code ec;
        const auto link = fs::symlink_status(path, ec);
        if (ec || !fs::exists(link)) {
            return FsENoEnt;
        }
        if (fs::is_symlink(link)) {
            return FsEAcces;
        }
        if (fs::is_regular_file(link)) {
            result.mode = 0x8000;
            result.size =
                static_cast<std::uint32_t>(fs::file_size(path, ec));
            if (ec) {
                return FsEIo;
            }
            const auto time = fs::last_write_time(path, ec);
            result.mtime = ec ? 0 : file_time_to_unix(time);
            return FsOk;
        }
        if (fs::is_directory(link)) {
            result.mode = 0x4000;
            result.size = 0;
            result.mtime = 0;
            return FsOk;
        }
        return FsEInval;
    }

    std::uint16_t unlink_file(const std::string& guest_path) {
        if (options.readonly) {
            return FsEAcces;
        }
        fs::path path;
        const std::uint16_t status = normalize(guest_path, path, false);
        if (status != FsOk) {
            return status;
        }
        std::error_code ec;
        const auto link = fs::symlink_status(path, ec);
        if (ec || !fs::exists(link)) {
            return FsENoEnt;
        }
        if (fs::is_directory(link)) {
            return FsEIsDir;
        }
        if (fs::is_symlink(link)) {
            return FsEAcces;
        }
        return fs::remove(path, ec) && !ec ? FsOk : FsEIo;
    }

    std::uint16_t rename_file(const std::string& old_path,
                              const std::string& new_path) {
        if (options.readonly) {
            return FsEAcces;
        }
        fs::path old_file;
        fs::path new_file;
        std::uint16_t status = normalize(old_path, old_file, false);
        if (status != FsOk) {
            return status;
        }
        status = normalize(new_path, new_file, true);
        if (status != FsOk) {
            return status;
        }
        std::error_code ec;
        if (fs::exists(new_file, ec)) {
            return FsEExist;
        }
        fs::rename(old_file, new_file, ec);
        return ec ? FsEIo : FsOk;
    }

    std::uint16_t make_directory(const std::string& guest_path) {
        if (options.readonly) {
            return FsEAcces;
        }
        fs::path path;
        const std::uint16_t status = normalize(guest_path, path, true);
        if (status != FsOk) {
            return status;
        }
        std::error_code ec;
        if (fs::exists(path, ec)) {
            return FsEExist;
        }
        return fs::create_directory(path, ec) && !ec ? FsOk : FsEIo;
    }

    std::uint16_t read_directory(std::uint16_t fd, MemoryBus& bus,
                                 std::uint16_t buffer, std::uint16_t count,
                                 std::uint16_t& result_count) {
        FileHandle* file = handle(fd);
        if (file == nullptr || !file->directory) {
            return FsEBadF;
        }
        if (count > MaxTransfer / DirentSize ||
            !valid_guest_range(buffer,
                               static_cast<std::uint32_t>(count) *
                                   DirentSize)) {
            return FsEInval;
        }
        std::uint16_t written = 0;
        while (written < count && file->index < file->entries.size()) {
            const auto& entry = file->entries[file->index++];
            std::error_code ec;
            const auto status = entry.symlink_status(ec);
            if (ec || fs::is_symlink(status)) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            if (name.empty() || name.size() > MaxDirName) {
                continue;
            }
            std::array<std::uint8_t, DirentSize> bytes{};
            const std::uint16_t type =
                fs::is_directory(status)
                    ? 2
                    : fs::is_regular_file(status) ? 1 : 0;
            bytes[0] = static_cast<std::uint8_t>(type & 0xFF);
            bytes[1] = static_cast<std::uint8_t>(type >> 8);
            bytes[2] = static_cast<std::uint8_t>(name.size() & 0xFF);
            bytes[3] = static_cast<std::uint8_t>(name.size() >> 8);
            std::copy(name.begin(), name.end(), bytes.begin() + 4);
            if (!write_guest(bus,
                             static_cast<std::uint16_t>(
                                 buffer + written * DirentSize),
                             bytes)) {
                return FsEFault;
            }
            ++written;
        }
        result_count = written;
        return FsOk;
    }

    std::uint16_t fsinfo(MemoryBus& bus, std::uint16_t buffer) {
        if (!valid_guest_range(buffer, 12)) {
            return FsEFault;
        }
        const std::uint32_t used =
            static_cast<std::uint32_t>(std::min<std::uint64_t>(
                used_bytes(), 0xFFFFFFFFu));
        const std::uint32_t quota = options.quota;
        const std::uint32_t free = quota > used ? quota - used : 0;
        const std::uint16_t words[6] = {
            static_cast<std::uint16_t>(used & 0xFFFF),
            static_cast<std::uint16_t>(used >> 16),
            static_cast<std::uint16_t>(quota & 0xFFFF),
            static_cast<std::uint16_t>(quota >> 16),
            static_cast<std::uint16_t>(free & 0xFFFF),
            static_cast<std::uint16_t>(free >> 16)};
        for (std::size_t i = 0; i < 6; ++i) {
            bus.write16(static_cast<std::uint16_t>(buffer + i * 2), words[i]);
        }
        return FsOk;
    }

    std::uint32_t milliseconds() const {
        return static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started)
                .count() &
            0xFFFFFFFFu);
    }
};

HostServices::HostServices(bool trace, HostFsOptions fs)
    : impl_(std::make_unique<Impl>(std::move(fs))),
      trace_enabled_(trace) {}

HostServices::~HostServices() = default;
HostServices::HostServices(HostServices&&) noexcept = default;
HostServices& HostServices::operator=(HostServices&&) noexcept = default;

bool HostServices::valid_block(std::uint16_t block) const {
    return (block & 1u) == 0 &&
           static_cast<std::uint32_t>(block) + 16 <= 0xF000;
}

std::uint16_t HostServices::call(std::uint8_t service, std::uint16_t block,
                                 MemoryBus& bus) {
    ++call_count_;
    if (!valid_block(block)) {
        last_status_ = FsEFault;
        if (trace_enabled_) {
            std::ostringstream line;
            line << "SYS #" << static_cast<unsigned>(service)
                 << " block=0x" << std::hex << block << std::dec
                 << " status=" << FsEFault << " (invalid block)\n";
            trace_ += line.str();
        }
        return FsEFault;
    }

    HostCallBlock values;
    values.status = bus.read16(block);
    for (std::size_t i = 0; i < 3; ++i) {
        values.results[i] =
            bus.read16(static_cast<std::uint16_t>(block + 2 + i * 2));
    }
    for (std::size_t i = 0; i < 4; ++i) {
        values.arguments[i] =
            bus.read16(static_cast<std::uint16_t>(block + 8 + i * 2));
    }

    std::uint16_t status = FsOk;
    if (service == 0) {
        values.results[0] = HostAbiVersion;
        std::uint16_t capabilities = (1u << 2) | (1u << 3) | (1u << 4);
        if (impl_->options.enabled) {
            capabilities |= (1u << 0) | (1u << 1) | (1u << 5);
        }
        values.results[1] = capabilities;
        values.results[2] = 0;
    } else if (service == SOpen) {
        std::string path;
        if (!read_guest_string(bus, values.arguments[0], path, status)) {
            status = status == 0 ? FsEFault : status;
        } else {
            std::uint16_t fd = 0;
            status = impl_->open_file(path, values.arguments[1], fd);
            values.results[0] = fd;
        }
    } else if (service == SClose) {
        status = impl_->close_file(values.arguments[0]);
    } else if (service == SRead) {
        std::uint16_t bytes = 0;
        status = impl_->read_file(values.arguments[0], bus,
                                  values.arguments[1], values.arguments[2],
                                  bytes);
        values.results[0] = bytes;
    } else if (service == SWrite) {
        std::uint16_t bytes = 0;
        if (values.arguments[0] == 1 || values.arguments[0] == 2) {
            const std::uint16_t buffer = values.arguments[1];
            const std::uint16_t count = values.arguments[2];
            if (count > MaxTransfer ||
                !valid_guest_range(buffer, count)) {
                status = count > MaxTransfer ? FsEInval : FsEFault;
            } else {
                std::vector<std::uint8_t> data(count);
                if (!read_guest(bus, buffer, data)) {
                    status = FsEFault;
                } else {
                    for (const auto byte : data) {
                        bus.write8(0xF000, byte);
                    }
                    bytes = count;
                }
            }
        } else {
            status = impl_->write_file(values.arguments[0], bus,
                                       values.arguments[1],
                                       values.arguments[2], bytes);
        }
        values.results[0] = bytes;
    } else if (service == SSeek) {
        const std::int32_t offset = static_cast<std::int32_t>(
            get_lo_hi(values.arguments[1], values.arguments[2]));
        std::uint32_t result = 0;
        status = impl_->seek_file(values.arguments[0], offset,
                                  values.arguments[3], result);
        values.results[0] = static_cast<std::uint16_t>(result & 0xFFFF);
        values.results[1] = static_cast<std::uint16_t>(result >> 16);
    } else if (service == SStat) {
        std::string path;
        if (!read_guest_string(bus, values.arguments[0], path, status)) {
            status = status == 0 ? FsEFault : status;
        } else if (!valid_guest_range(values.arguments[1], 12)) {
            status = FsEFault;
        } else {
            StatData data;
            status = impl_->stat_file(path, data);
            if (status == FsOk) {
                bus.write16(values.arguments[1], data.mode);
                bus.write16(static_cast<std::uint16_t>(
                                values.arguments[1] + 2),
                            0);
                bus.write16(
                    static_cast<std::uint16_t>(values.arguments[1] + 4),
                    static_cast<std::uint16_t>(data.size & 0xFFFF));
                bus.write16(
                    static_cast<std::uint16_t>(values.arguments[1] + 6),
                    static_cast<std::uint16_t>(data.size >> 16));
                bus.write16(
                    static_cast<std::uint16_t>(values.arguments[1] + 8),
                    static_cast<std::uint16_t>(data.mtime & 0xFFFF));
                bus.write16(
                    static_cast<std::uint16_t>(values.arguments[1] + 10),
                    static_cast<std::uint16_t>(data.mtime >> 16));
            }
        }
    } else if (service == SUnlink) {
        std::string path;
        if (!read_guest_string(bus, values.arguments[0], path, status)) {
            status = status == 0 ? FsEFault : status;
        } else {
            status = impl_->unlink_file(path);
        }
    } else if (service == SRename) {
        std::string old_path;
        std::string new_path;
        if (!read_guest_string(bus, values.arguments[0], old_path, status) ||
            !read_guest_string(bus, values.arguments[1], new_path, status)) {
            status = status == 0 ? FsEFault : status;
        } else {
            status = impl_->rename_file(old_path, new_path);
        }
    } else if (service == SMkdir) {
        std::string path;
        if (!read_guest_string(bus, values.arguments[0], path, status)) {
            status = status == 0 ? FsEFault : status;
        } else {
            status = impl_->make_directory(path);
        }
    } else if (service == SReaddir) {
        std::uint16_t count = 0;
        status = impl_->read_directory(values.arguments[0], bus,
                                       values.arguments[1],
                                       values.arguments[2], count);
        values.results[0] = count;
    } else if (service == STime) {
        const std::uint32_t millis = impl_->milliseconds();
        values.results[0] = static_cast<std::uint16_t>(millis & 0xFFFF);
        values.results[1] = static_cast<std::uint16_t>(millis >> 16);
    } else if (service == SRandom) {
        values.results[0] = static_cast<std::uint16_t>(impl_->random());
    } else if (service == SFSInfo) {
        status = impl_->fsinfo(bus, values.arguments[0]);
    } else {
        status = FsENoSys;
        values.results[0] = 0;
        values.results[1] = 0;
        values.results[2] = 0;
    }

    values.status = status;
    bus.write16(block, status);
    for (std::size_t i = 0; i < 3; ++i) {
        bus.write16(static_cast<std::uint16_t>(block + 2 + i * 2),
                    values.results[i]);
    }
    last_status_ = status;
    if (trace_enabled_) {
        std::ostringstream line;
        line << "SYS #" << static_cast<unsigned>(service)
             << " block=0x" << std::hex << block << std::dec
             << " status=" << status << " result0=" << values.results[0]
             << " result1=" << values.results[1] << "\n";
        trace_ += line.str();
    }
    return status;
}

}  // namespace kr16
