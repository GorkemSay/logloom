#include "logloom/version.hpp"

namespace logloom {

std::string_view version() noexcept {
  return LOGLOOM_VERSION_STRING;
}

} // namespace logloom
