add_executable(logloom-gen
  logloom_gen.cpp
)

target_link_libraries(logloom-gen
  PRIVATE
    logloom_warnings
)

add_executable(logloom-bench
  logloom_bench.cpp
)

target_link_libraries(logloom-bench
  PRIVATE
    logloom::core
    logloom_warnings
)
