#-------------------------------------------------------------------------------
# Project Definition
#-------------------------------------------------------------------------------

project("Model Unit Tests")

#-------------------------------------------------------------------------------
# Target Definition
#-------------------------------------------------------------------------------

set(TESTED_TARGET_NAME "model")
set(TARGET_NAME "${TESTED_TARGET_NAME}_tests")
add_executable("${TARGET_NAME}")

#-------------------------------------------------------------------------------
# Source Files
#-------------------------------------------------------------------------------

set(HPP_SOURCES_DIR "${PROJECT_SOURCE_DIR}/include/${TESTED_TARGET_NAME}")
set(CPP_SOURCES_DIR "${PROJECT_SOURCE_DIR}/src/${TESTED_TARGET_NAME}")

target_sources("${TARGET_NAME}"
  PRIVATE
    "${CPP_SOURCES_DIR}/userinput_history.cpp"
    "${CPP_SOURCES_DIR}/server_loop.cpp"
    "${CPP_SOURCES_DIR}/frame_metrics.cpp"
    "${CPP_SOURCES_DIR}/metrics_reporter.cpp"
)

#-------------------------------------------------------------------------------
# Link Libraries
#-------------------------------------------------------------------------------

target_link_libraries("${TARGET_NAME}"
  PRIVATE
    "${LIB_MODEL}"
    "${LIB_GTEST}"
)

#-------------------------------------------------------------------------------
# Test Configuration
#-------------------------------------------------------------------------------

gtest_discover_tests("${TARGET_NAME}" PROPERTIES TIMEOUT 10)

#-------------------------------------------------------------------------------
# Code Coverage
#-------------------------------------------------------------------------------

if(CODE_COVERAGE)
  generate_coverage_target("${TARGET_NAME}")
endif()
