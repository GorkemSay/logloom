add_executable(logloom
  main.cpp
  cli_common.cpp
  arg_parser.cpp
  cmd_trace.cpp
  cmd_search.cpp
  cmd_context.cpp
  cmd_stats.cpp
)

target_link_libraries(logloom
  PRIVATE
    logloom::core
    logloom_warnings
)
