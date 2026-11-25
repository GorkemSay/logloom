#include "logloom/version.hpp"

#include <iostream>
#include <string_view>

namespace {

void print_usage(std::ostream& out) {
  out << "Usage: logloom [OPTIONS]\n"
      << "\n"
      << "High-performance, local-first log and JSONL trace inspector.\n"
      << "\n"
      << "Options:\n"
      << "  -h, --help     Show this help message and exit\n"
      << "  -V, --version  Show version information and exit\n";
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc <= 1) {
    print_usage(std::cout);
    return 0;
  }

  const std::string_view arg{argv[1]};
  if (arg == "--version" || arg == "-V") {
    std::cout << "logloom " << logloom::version() << '\n';
    return 0;
  }

  if (arg == "--help" || arg == "-h") {
    print_usage(std::cout);
    return 0;
  }

  std::cerr << "logloom: unknown option '" << arg << "'\n"
            << "Try 'logloom --help' for usage.\n";
  return 2;
}
