# Apply a patch to a fetched dependency, unless it is already applied.
#
# FetchContent runs PATCH_COMMAND when it populates, and populates again whenever the declaration
# changes -- without re-cloning. A plain `git apply` then fails on a tree that is already patched,
# and the build stops with something that reads as "the dependency is broken" rather than "there
# was nothing to do". Checking the reverse first is what makes re-running a no-op.
#
#   cmake -DPATCH_FILE=<abs path> -DPATCH_DIR=<abs path> -P apply-patch.cmake

if(NOT DEFINED PATCH_FILE OR NOT DEFINED PATCH_DIR)
  message(FATAL_ERROR "apply-patch.cmake needs -DPATCH_FILE= and -DPATCH_DIR=")
endif()

find_package(Git QUIET REQUIRED)

execute_process(
  COMMAND "${GIT_EXECUTABLE}" apply --reverse --check --ignore-whitespace "${PATCH_FILE}"
  WORKING_DIRECTORY "${PATCH_DIR}"
  RESULT_VARIABLE already_applied
  OUTPUT_QUIET ERROR_QUIET
)

if(already_applied EQUAL 0)
  message(STATUS "already applied: ${PATCH_FILE}")
  return()
endif()

execute_process(
  COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace "${PATCH_FILE}"
  WORKING_DIRECTORY "${PATCH_DIR}"
  RESULT_VARIABLE applied
  ERROR_VARIABLE apply_error
)

if(NOT applied EQUAL 0)
  message(FATAL_ERROR "could not apply ${PATCH_FILE} in ${PATCH_DIR}:\n${apply_error}")
endif()

message(STATUS "applied: ${PATCH_FILE}")
