#pragma once

#include <string_view>

namespace logloom {

/// Returns the semantic version string of liblogloom.
[[nodiscard]] std::string_view version() noexcept;

} // namespace logloom
