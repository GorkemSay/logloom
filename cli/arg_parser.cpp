#include "arg_parser.hpp"
#include "cli_common.hpp"

#include <iostream>
#include <utility>

namespace logloom::cli {

ArgParser::ArgParser(
    std::string_view command_name,
    std::function<void(std::ostream&)> usage_printer)
    : command_name_(command_name),
      usage_printer_(std::move(usage_printer)) {}

void ArgParser::add_flag(
    std::string_view long_name,
    std::string_view short_name,
    std::function<void()> on_flag) {
  flags_.push_back(FlagSpec{
      .long_name = std::string(long_name),
      .short_name = std::string(short_name),
      .on_flag = std::move(on_flag),
  });
}

void ArgParser::add_string_option(
    std::string_view name,
    std::function<bool(std::string_view)> on_value) {
  value_options_.push_back(ValueOptionSpec{
      .name = std::string(name),
      .on_value = std::move(on_value),
  });
}

void ArgParser::add_u64_option(
    std::string_view name,
    bool require_positive,
    std::function<void(std::uint64_t)> on_value,
    std::string invalid_message) {
  add_string_option(
      name,
      [require_positive,
       cb = std::move(on_value),
       msg = std::move(invalid_message)](std::string_view raw) {
        std::uint64_t parsed = 0;
        if (!parse_u64(raw, parsed) || (require_positive && parsed == 0)) {
          std::cerr << msg;
          return false;
        }
        cb(parsed);
        return true;
      });
}

void ArgParser::add_positional_file(std::optional<std::filesystem::path>& out_file) {
  positional_file_ = &out_file;
}

bool ArgParser::match_flag(std::string_view arg) const {
  for (const auto& flag : flags_) {
    const bool matches_long = !flag.long_name.empty() && arg == flag.long_name;
    const bool matches_short = !flag.short_name.empty() && arg == flag.short_name;
    if (matches_long || matches_short) {
      flag.on_flag();
      return true;
    }
  }
  return false;
}

bool ArgParser::match_value_option(
    int argc,
    char* argv[],
    int& index,
    std::string_view arg,
    bool& out_ok) const {
  for (const auto& opt : value_options_) {
    std::string_view value;
    if (arg == opt.name) {
      if (index + 1 >= argc) {
        std::cerr << "logloom: option '" << opt.name << "' requires a value\n";
        out_ok = false;
        return true;
      }
      value = argv[++index];
      out_ok = opt.on_value(value);
      return true;
    }

    const bool has_equals_form =
        arg.size() > opt.name.size() + 1 &&
        arg.starts_with(opt.name) &&
        arg[opt.name.size()] == '=';
    if (has_equals_form) {
      value = arg.substr(opt.name.size() + 1);
      out_ok = opt.on_value(value);
      return true;
    }
  }
  return false;
}

ParseStatus ArgParser::parse(int argc, char* argv[], int start_index) const {
  for (int i = start_index; i < argc; ++i) {
    const std::string_view arg{argv[i]};

    if (arg == "-h" || arg == "--help") {
      usage_printer_(std::cout);
      return ParseStatus::HelpRequested;
    }
    if (match_flag(arg)) {
      continue;
    }

    bool option_ok = true;
    if (match_value_option(argc, argv, i, arg, option_ok)) {
      if (!option_ok) {
        return ParseStatus::Error;
      }
      continue;
    }

    if (arg.starts_with("-")) {
      std::cerr << "logloom " << command_name_ << ": unknown option '" << arg << "'\n";
      return ParseStatus::Error;
    }

    if (positional_file_ != nullptr && !positional_file_->has_value()) {
      *positional_file_ = std::filesystem::path(arg);
      continue;
    }

    std::cerr << "logloom " << command_name_
              << ": unexpected positional argument '" << arg << "'\n";
    return ParseStatus::Error;
  }

  return ParseStatus::Ok;
}

} // namespace logloom::cli
