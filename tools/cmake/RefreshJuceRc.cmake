cmake_minimum_required(VERSION 3.15)

foreach(required_var INFO_FILE RC_FILE JUCEAIDE)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "${required_var} is required")
    endif()
endforeach()

if(NOT EXISTS "${INFO_FILE}")
    message(FATAL_ERROR "JUCE info file not found: ${INFO_FILE}")
endif()

file(SHA256 "${INFO_FILE}" info_hash)
set(stamp_file "${RC_FILE}.info.sha256")
set(needs_refresh FALSE)

if(NOT EXISTS "${RC_FILE}")
    set(needs_refresh TRUE)
elseif(NOT EXISTS "${stamp_file}")
    set(needs_refresh TRUE)
else()
    file(READ "${stamp_file}" old_hash)
    string(STRIP "${old_hash}" old_hash)
    if(NOT old_hash STREQUAL info_hash)
        set(needs_refresh TRUE)
    endif()
endif()

if(needs_refresh)
    get_filename_component(rc_dir "${RC_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${rc_dir}")
    execute_process(
        COMMAND "${JUCEAIDE}" rcfile "${INFO_FILE}" "${RC_FILE}"
        RESULT_VARIABLE rc_result
        OUTPUT_VARIABLE rc_output
        ERROR_VARIABLE rc_error)
    if(NOT rc_result EQUAL 0)
        message(FATAL_ERROR "Failed to refresh JUCE resource rc: ${rc_error}${rc_output}")
    endif()
    file(WRITE "${stamp_file}" "${info_hash}\n")
endif()
