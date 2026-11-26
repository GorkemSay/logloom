#include "logloom/posix_file.hpp"

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace logloom {

PosixFile::PosixFile(int fd, std::uint64_t size) noexcept : fd_(fd), size_(size) {}

PosixFile::~PosixFile() {
  close_fd();
}

PosixFile::PosixFile(PosixFile&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)), size_(std::exchange(other.size_, 0)) {}

PosixFile& PosixFile::operator=(PosixFile&& other) noexcept {
  if (this != &other) {
    close_fd();
    fd_ = std::exchange(other.fd_, -1);
    size_ = std::exchange(other.size_, 0);
  }
  return *this;
}

std::optional<PosixFile> PosixFile::open_read_only(
    const std::filesystem::path& path,
    std::error_code& ec) noexcept {
  ec.clear();

  int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    ec = std::error_code(errno, std::generic_category());
    return std::nullopt;
  }

  struct stat st {};
  if (::fstat(fd, &st) < 0) {
    const int err = errno;
    ::close(fd);
    ec = std::error_code(err, std::generic_category());
    return std::nullopt;
  }

  if (S_ISDIR(st.st_mode)) {
    ::close(fd);
    ec = std::make_error_code(std::errc::is_a_directory);
    return std::nullopt;
  }

  if (!S_ISREG(st.st_mode)) {
    ::close(fd);
    ec = std::make_error_code(std::errc::invalid_argument);
    return std::nullopt;
  }

  const std::uint64_t file_size =
      st.st_size > 0 ? static_cast<std::uint64_t>(st.st_size) : 0ULL;
  return PosixFile(fd, file_size);
}

bool PosixFile::is_open() const noexcept {
  return fd_ >= 0;
}

int PosixFile::fd() const noexcept {
  return fd_;
}

std::uint64_t PosixFile::size() const noexcept {
  return size_;
}

std::size_t PosixFile::pread_some(
    std::uint64_t offset,
    std::span<char> buffer,
    std::error_code& ec) const noexcept {
  ec.clear();

  if (!is_open()) {
    ec = std::make_error_code(std::errc::bad_file_descriptor);
    return 0;
  }

  if (buffer.empty()) {
    return 0;
  }

  if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return 0;
  }

  const std::size_t max_read =
      static_cast<std::size_t>(std::numeric_limits<ssize_t>::max());
  const std::size_t to_read = std::min(buffer.size(), max_read);

  ssize_t n = 0;
  do {
    n = ::pread(fd_, buffer.data(), to_read, static_cast<off_t>(offset));
  } while (n < 0 && errno == EINTR);

  if (n < 0) {
    ec = std::error_code(errno, std::generic_category());
    return 0;
  }

  return static_cast<std::size_t>(n);
}

std::size_t PosixFile::pread_full(
    std::uint64_t offset,
    std::span<char> buffer,
    std::error_code& ec) const noexcept {
  ec.clear();
  std::size_t total_read = 0;

  while (!buffer.empty()) {
    const std::size_t n = pread_some(offset, buffer, ec);
    if (ec || n == 0) {
      break;
    }
    total_read += n;
    offset += n;
    buffer = buffer.subspan(n);
  }

  return total_read;
}

void PosixFile::close_fd() noexcept {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  size_ = 0;
}

} // namespace logloom
