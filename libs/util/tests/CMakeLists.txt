#-------------------------------------------------------------------------------
# Project Definition
#-------------------------------------------------------------------------------

project("Util Unit Tests")

#-------------------------------------------------------------------------------
# Target Definition
#-------------------------------------------------------------------------------

set(TESTED_TARGET_NAME "util")
set(TARGET_NAME "${TESTED_TARGET_NAME}_tests")
add_executable("${TARGET_NAME}")

#-------------------------------------------------------------------------------
# Source Files
#-------------------------------------------------------------------------------

set(HPP_SOURCES_DIR "${PROJECT_SOURCE_DIR}/include/${TESTED_TARGET_NAME}")
set(CPP_SOURCES_DIR "${PROJECT_SOURCE_DIR}/src/${TESTED_TARGET_NAME}")

target_sources("${TARGET_NAME}"
  PRIVATE
    "${CPP_SOURCES_DIR}/circular_buffer.cpp"
    "${CPP_SOURCES_DIR}/observable.cpp"
    "${CPP_SOURCES_DIR}/threadsafe_queue.cpp"
    "${CPP_SOURCES_DIR}/logger.cpp"
)

#-------------------------------------------------------------------------------
# Link Libraries
#-------------------------------------------------------------------------------

target_link_libraries("${TARGET_NAME}"
  PRIVATE
    "${LIB_UTIL}"
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
