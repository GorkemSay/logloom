#pragma once

namespace logloom::cli {

int run_trace(int argc, char* argv[], bool use_color);
int run_search(int argc, char* argv[], bool use_color);
int run_context(int argc, char* argv[], bool use_color);
int run_stats(int argc, char* argv[]);

} // namespace logloom::cli
