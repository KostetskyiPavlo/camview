#pragma once

#include <unistd.h>
#include <utility>

namespace camview {

// Owns a POSIX file descriptor and closes it on destruction. Move-only.
class UniqueFd {
public:
    UniqueFd() noexcept = default;
    explicit UniqueFd(int fd) noexcept : fd_(fd) {}
    ~UniqueFd() { reset(); }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool isValid() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return isValid(); }

    // Gives up ownership without closing.
    int release() noexcept { return std::exchange(fd_, -1); }

    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) {
            // No retry on EINTR: on Linux the descriptor is released even when close() fails,
            // and retrying could close a descriptor that another thread has just opened.
            ::close(fd_);
        }
        fd_ = fd;
    }

private:
    int fd_{-1};
};

} // namespace camview