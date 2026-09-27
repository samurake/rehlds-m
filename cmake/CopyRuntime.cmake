# Copy the runtimes actually resolved by the built launcher. Missing dependencies
# are a build error, rather than a silently incomplete release directory.
if(NOT DEFINED EXECUTABLE OR NOT EXISTS "${EXECUTABLE}")
  message(FATAL_ERROR "Runtime packaging needs a built executable")
endif()
file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES "${EXECUTABLE}"
  RESOLVED_DEPENDENCIES_VAR resolved
  UNRESOLVED_DEPENDENCIES_VAR unresolved
  PRE_INCLUDE_REGEXES "^lib(gcc_s|stdc\\+\\+|svml|intlc)\\.so"
  PRE_EXCLUDE_REGEXES ".*"
)
if(unresolved)
  message(FATAL_ERROR "Unresolved launcher runtimes: ${unresolved}")
endif()
get_filename_component(destination "${EXECUTABLE}" DIRECTORY)
foreach(runtime IN LISTS resolved)
  get_filename_component(name "${runtime}" NAME)
  file(REAL_PATH "${runtime}" source_real)
  set(target_real "${destination}/${name}")
  if(EXISTS "${target_real}")
    file(REAL_PATH "${target_real}" target_real)
  endif()
  if(NOT source_real STREQUAL target_real)
    file(COPY_FILE "${runtime}" "${destination}/${name}" ONLY_IF_DIFFERENT)
  endif()
endforeach()
