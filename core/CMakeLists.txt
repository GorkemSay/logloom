add_library(logloom_core STATIC
  jsonl_extractor.cpp
  line_index.cpp
  line_reader.cpp
  posix_file.cpp
  version.cpp
)
add_library(logloom::core ALIAS logloom_core)

set_target_properties(logloom_core PROPERTIES OUTPUT_NAME logloom)

target_include_directories(logloom_core
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_compile_definitions(logloom_core
  PRIVATE
    LOGLOOM_VERSION_STRING="${PROJECT_VERSION}"
)

target_link_libraries(logloom_core
  PRIVATE
    logloom_warnings
)
