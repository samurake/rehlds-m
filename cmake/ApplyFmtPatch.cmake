# The 12.2.0 fallback uint128 lacks operator~, needed for i386 long-double hex.
# Backport only the three-line operator, not a moving branch:
# https://github.com/fmtlib/fmt/commit/588b3a0f8f6a8bcf2a959cae882d5b2703e86737
find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" apply --check "${PATCH_FILE}"
  WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE can_apply ERROR_QUIET)
if(can_apply EQUAL 0)
  execute_process(COMMAND "${GIT_EXECUTABLE}" apply "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE patched)
  if(NOT patched EQUAL 0)
    message(FATAL_ERROR "Could not apply fmt i386 compatibility patch")
  endif()
else()
  execute_process(COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE already_applied ERROR_QUIET)
  if(NOT already_applied EQUAL 0)
    message(FATAL_ERROR "fmt source does not match the pinned compatibility patch")
  endif()
endif()
