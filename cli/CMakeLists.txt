add_executable(logloom
  main.cpp
)

target_link_libraries(logloom
  PRIVATE
    logloom::core
    logloom_warnings
)
