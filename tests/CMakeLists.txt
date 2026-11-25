add_executable(logloom_tests
  jsonl_extractor_test.cpp
  line_index_test.cpp
  line_reader_test.cpp
  search_engine_test.cpp
  smoke_test.cpp
)

target_link_libraries(logloom_tests
  PRIVATE
    logloom::core
    Catch2::Catch2WithMain
    logloom_warnings
)

include(Catch)
catch_discover_tests(logloom_tests)
