#-------------------------------------------------------------------------------
# Project Definition
#-------------------------------------------------------------------------------

project("HLDS Launcher")

#-------------------------------------------------------------------------------
# Target Definition
#-------------------------------------------------------------------------------

set(TARGET_NAME "hlds")
add_executable("${TARGET_NAME}")

# Keep the launcher identity available without loading the engine or game files.
find_package(Git QUIET)
set(REHLDSM_REVISION "unknown")
if(GIT_FOUND)
  execute_process(COMMAND "${GIT_EXECUTABLE}" -c "safe.directory=${CMAKE_SOURCE_DIR}"
    rev-parse --short=12 HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE revision OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(revision MATCHES "^[0-9a-f]+$")
    set(REHLDSM_REVISION "${revision}")
    execute_process(COMMAND "${GIT_EXECUTABLE}" -c "safe.directory=${CMAKE_SOURCE_DIR}"
      diff --quiet HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      RESULT_VARIABLE source_dirty ERROR_QUIET)
    if(source_dirty)
      string(APPEND REHLDSM_REVISION "-dirty")
    endif()
  endif()
endif()
target_compile_definitions("${TARGET_NAME}" PRIVATE
  REHLDSM_VERSION="${CMAKE_PROJECT_VERSION}"
  REHLDSM_REVISION="${REHLDSM_REVISION}"
)
if(BUILD_UNIT_TESTS)
  add_test(NAME launcher_version COMMAND "$<TARGET_FILE:${TARGET_NAME}>" --version)
  set_tests_properties(launcher_version PROPERTIES TIMEOUT 10
    PASS_REGULAR_EXPRESSION "ReHLDS-M ${CMAKE_PROJECT_VERSION} [(]")
endif()

#-------------------------------------------------------------------------------
# Source Files
#-------------------------------------------------------------------------------

set(HPP_SOURCES_DIR "${PROJECT_SOURCE_DIR}/include/${TARGET_NAME}")
set(CPP_SOURCES_DIR "${PROJECT_SOURCE_DIR}/src/${TARGET_NAME}")

target_sources("${TARGET_NAME}"
  PRIVATE
    "${CPP_SOURCES_DIR}/${TARGET_NAME}.main.cpp"
)

if(WIN32)
  set(EXE_RESOURCES "${TARGET_NAME}.exe.rc")
  set(EXE_MANIFEST "${TARGET_NAME}.exe.manifest")

  configure_file("${EXE_RESOURCES}.in" "${PROJECT_BINARY_DIR}/${EXE_RESOURCES}" @ONLY)
  configure_file("${EXE_MANIFEST}.in" "${PROJECT_BINARY_DIR}/${EXE_MANIFEST}" @ONLY)

  target_sources("${TARGET_NAME}"
    PRIVATE
      "${PROJECT_BINARY_DIR}/${EXE_RESOURCES}"
      "${PROJECT_BINARY_DIR}/${EXE_MANIFEST}"
  )
endif()

#-------------------------------------------------------------------------------
# Target Properties
#-------------------------------------------------------------------------------

set_target_properties("${TARGET_NAME}"
  PROPERTIES
    ENABLE_EXPORTS ON
    RUNTIME_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}"
    POSITION_INDEPENDENT_CODE OFF
)

if(UNIX AND NOT APPLE)
  set(SUFFIX_NAME "_linux")

  set_target_properties("${TARGET_NAME}"
    PROPERTIES
      SUFFIX "${SUFFIX_NAME}"
  )
endif()

#-------------------------------------------------------------------------------
# Linker Flags
#-------------------------------------------------------------------------------

target_link_options("${TARGET_NAME}"
  PRIVATE
    $<$<PLATFORM_ID:Windows>:
      "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
      /MANIFEST
    >
)

#-------------------------------------------------------------------------------
# Link Libraries
#-------------------------------------------------------------------------------

target_link_libraries("${TARGET_NAME}"
  PRIVATE
    "${LIB_COMMON}"
    "${LIB_CORE}"
    "${LIB_MODEL}"
    "${LIB_PRESENTER}"
    "${LIB_VIEW}"
)

#-------------------------------------------------------------------------------
# Post-Build Actions
#-------------------------------------------------------------------------------

if(UNIX AND NOT APPLE)
  add_custom_command(TARGET "${TARGET_NAME}" POST_BUILD
    COMMAND "${CMAKE_COMMAND}" "-DEXECUTABLE=$<TARGET_FILE:${TARGET_NAME}>"
      -P "${CMAKE_SOURCE_DIR}/cmake/CopyRuntime.cmake"
    VERBATIM
  )
endif()
