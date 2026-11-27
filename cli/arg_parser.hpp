#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace logloom::cli {

enum class ParseStatus {
  Ok,
  HelpRequested,
  Error,
};

class ArgParser {
 public:
  ArgParser(std::string_view command_name, std::function<void(std::ostream&)> usage_printer);

  void add_flag(
      std::string_view long_name,
      std::string_view short_name,
      std::function<void()> on_flag);

  void add_string_option(
      std::string_view name,
      std::function<bool(std::string_view)> on_value);

  void add_u64_option(
      std::string_view name,
      bool require_positive,
      std::function<void(std::uint64_t)> on_value,
      std::string invalid_message);

  void add_positional_file(std::optional<std::filesystem::path>& out_file);

  [[nodiscard]] ParseStatus parse(int argc, char* argv[], int start_index = 2) const;

 private:
  struct FlagSpec {
    std::string long_name;
    std::string short_name;
    std::function<void()> on_flag;
  };

  struct ValueOptionSpec {
    std::string name;
    std::function<bool(std::string_view)> on_value;
  };

  std::string command_name_;
  std::function<void(std::ostream&)> usage_printer_;
  std::vector<FlagSpec> flags_;
  std::vector<ValueOptionSpec> value_options_;
  std::optional<std::filesystem::path>* positional_file_ = nullptr;

  [[nodiscard]] bool match_flag(std::string_view arg) const;
  [[nodiscard]] bool match_value_option(
      int argc,
      char* argv[],
      int& index,
      std::string_view arg,
      bool& out_ok) const;
};

} // namespace logloom::cli
