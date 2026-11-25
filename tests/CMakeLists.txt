add_executable(logloom_tests
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
