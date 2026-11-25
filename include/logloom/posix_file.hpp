#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <system_error>

namespace logloom {

/// RAII wrapper around a strictly read-only POSIX file descriptor.
class PosixFile {
public:
  PosixFile() noexcept = default;
  ~PosixFile();

  PosixFile(const PosixFile&) = delete;
  PosixFile& operator=(const PosixFile&) = delete;

  PosixFile(PosixFile&& other) noexcept;
  PosixFile& operator=(PosixFile&& other) noexcept;

  /// Opens an existing regular file in read-only mode (O_RDONLY | O_CLOEXEC).
  [[nodiscard]] static std::optional<PosixFile> open_read_only(
      const std::filesystem::path& path,
      std::error_code& ec) noexcept;

  [[nodiscard]] bool is_open() const noexcept;
  [[nodiscard]] int fd() const noexcept;
  [[nodiscard]] std::uint64_t size() const noexcept;

  /// Performs a single EINTR-retrying pread at `offset`. Returns bytes read (0 on EOF or error).
  [[nodiscard]] std::size_t pread_some(
      std::uint64_t offset,
      std::span<char> buffer,
      std::error_code& ec) const noexcept;

  /// Reads until `buffer` is filled, EOF is reached, or an error occurs.
  [[nodiscard]] std::size_t pread_full(
      std::uint64_t offset,
      std::span<char> buffer,
      std::error_code& ec) const noexcept;

private:
  explicit PosixFile(int fd, std::uint64_t size) noexcept;
  void close_fd() noexcept;

  int fd_ = -1;
  std::uint64_t size_ = 0;
};

} // namespace logloom
