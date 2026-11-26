add_executable(logloom_tests
  cli_integration_test.cpp
  jsonl_extractor_test.cpp
  line_index_test.cpp
  line_reader_test.cpp
  mmap_reader_test.cpp
  parallel_search_test.cpp
  search_engine_test.cpp
  smoke_test.cpp
)

add_dependencies(logloom_tests logloom)

target_compile_definitions(logloom_tests
  PRIVATE
    LOGLOOM_CLI_PATH="$<TARGET_FILE:logloom>"
)

target_link_libraries(logloom_tests
  PRIVATE
    logloom::core
    Catch2::Catch2WithMain
    logloom_warnings
)

include(Catch)
catch_discover_tests(logloom_tests)

add_executable(logloom_c_api_test
  c_api_test.c
)

target_link_libraries(logloom_c_api_test
  PRIVATE
    logloom::core
    logloom_warnings
)

add_test(NAME logloom_c_api_test COMMAND logloom_c_api_test)
