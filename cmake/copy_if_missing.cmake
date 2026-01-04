# copy_if_missing.cmake
# Usage:
#   cmake -Dsrc=... -Ddst=... -P copy_if_missing.cmake

if(NOT DEFINED src OR NOT DEFINED dst)
    message(FATAL_ERROR "copy_if_missing.cmake: missing -Dsrc or -Ddst")
endif()

if(EXISTS "${dst}")
    # Do not overwrite user-edited config.
    return()
endif()

get_filename_component(dst_dir "${dst}" DIRECTORY)
file(MAKE_DIRECTORY "${dst_dir}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy "${src}" "${dst}"
    RESULT_VARIABLE rc
)

if(NOT rc EQUAL 0)
    message(FATAL_ERROR "Failed to copy default config: ${src} -> ${dst}")
endif()
