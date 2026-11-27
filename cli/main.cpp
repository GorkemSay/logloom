#include "cli_common.hpp"
#include "commands.hpp"

#include "logloom/version.hpp"

#include <iostream>
#include <string_view>

namespace {

void print_global_usage(std::ostream& out) {
  out << "Usage: logloom <command> [options]\n"
      << "       logloom [--help | --version]\n"
      << "\n"
      << "Commands:\n"
      << "  trace    Isolate all lines matching a trace ID\n"
      << "  search   Search log lines by text, regex, or level\n"
      << "  context  Show +/-N surrounding lines around a line number\n"
      << "  stats    Print structural summary and diagnostics for a log file\n"
      << "\n"
      << "Options:\n"
      << "  -h, --help     Show help message and exit\n"
      << "  -V, --version  Show version information and exit\n";
}

} // namespace

int main(int argc, char* argv[]) {
  using namespace logloom::cli;

  if (argc <= 1) {
    print_global_usage(std::cout);
    return kExitOk;
  }

  const std::string_view cmd{argv[1]};
  if (cmd == "--version" || cmd == "-V") {
    std::cout << "logloom " << logloom::version() << '\n';
    return kExitOk;
  }
  if (cmd == "--help" || cmd == "-h") {
    print_global_usage(std::cout);
    return kExitOk;
  }

  const bool use_color = should_use_color();

  if (cmd == "trace") {
    return run_trace(argc, argv, use_color);
  }
  if (cmd == "search") {
    return run_search(argc, argv, use_color);
  }
  if (cmd == "context") {
    return run_context(argc, argv, use_color);
  }
  if (cmd == "stats") {
    return run_stats(argc, argv);
  }

  std::cerr << "logloom: unknown command or option '" << cmd << "'\n"
            << "Try 'logloom --help' for usage.\n";
  return kExitError;
}
