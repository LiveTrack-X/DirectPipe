cmake_minimum_required(VERSION 3.22)
if(NOT DEFINED JUCE_SOURCE_DIR OR NOT DEFINED TEST_WORK_DIR)
    message(FATAL_ERROR "JUCE_SOURCE_DIR and TEST_WORK_DIR are required")
endif()
find_package(Git REQUIRED)
set(files
    modules/juce_audio_devices/audio_io/juce_AudioIODevice.h
    modules/juce_audio_devices/native/juce_WASAPI_windows.cpp)
set(original_hashes
    1234da3bd93fda2ba2b4da86df3ad96dc36a6ae976f46fc5d45dd4b66591c475
    e4c9ac34dbe02b4fe62faa9b85b4f26ecf4d1b2a4017b5d1863d637058e3ec91)
set(patched_hashes
    3cf640e690127954ca94888f472b11aa133e839bfd86900310153ed526fd6926
    e288000e2f6d0a22f02d73b65064940d4376c045c7618e4c48db5297caf54673)

function(make_fixture name)
    foreach(index RANGE 0 1)
        list(GET files ${index} relative)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${JUCE_SOURCE_DIR}" show "HEAD:${relative}"
            RESULT_VARIABLE result OUTPUT_VARIABLE pristine ERROR_VARIABLE error)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Cannot read pinned JUCE fixture: ${error}")
        endif()
        string(REPLACE "\r\n" "\n" pristine "${pristine}")
        string(SHA256 actual "${pristine}")
        list(GET original_hashes ${index} expected)
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR "Unexpected pinned JUCE HEAD: ${relative}")
        endif()
        get_filename_component(parent "${TEST_WORK_DIR}/${name}/${relative}" DIRECTORY)
        file(MAKE_DIRECTORY "${parent}")
        file(WRITE "${TEST_WORK_DIR}/${name}/${relative}" "${pristine}")
    endforeach()
endfunction()

function(run_patch name should_pass)
    execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DDIRECTPIPE_JUCE_SOURCE_DIR=${TEST_WORK_DIR}/${name}"
        -P "${CMAKE_CURRENT_LIST_DIR}/DirectPipeJuceEndpointIdentity.cmake"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(should_pass AND NOT result EQUAL 0)
        message(FATAL_ERROR "Patch failed: ${output}${error}")
    elseif(NOT should_pass AND result EQUAL 0)
        message(FATAL_ERROR "Unknown upstream source was silently accepted")
    endif()
endfunction()

function(check_hash name index expected)
    list(GET files ${index} relative)
    file(READ "${TEST_WORK_DIR}/${name}/${relative}" content)
    string(REPLACE "\r\n" "\n" content "${content}")
    string(SHA256 actual "${content}")
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "Unexpected fixture mutation: ${name}/${relative}")
    endif()
endfunction()

make_fixture(clean)
run_patch(clean TRUE)
foreach(index RANGE 0 1)
    list(GET patched_hashes ${index} expected)
    check_hash(clean ${index} "${expected}")
endforeach()
run_patch(clean TRUE)
foreach(index RANGE 0 1)
    list(GET patched_hashes ${index} expected)
    check_hash(clean ${index} "${expected}")
endforeach()

# Validate both source files before modifying either one.
make_fixture(unknown)
list(GET files 1 second)
file(APPEND "${TEST_WORK_DIR}/unknown/${second}" "\n// unexpected upstream change\n")
file(READ "${TEST_WORK_DIR}/unknown/${second}" unknown_content)
string(REPLACE "\r\n" "\n" unknown_content "${unknown_content}")
string(SHA256 unknown_hash "${unknown_content}")
run_patch(unknown FALSE)
list(GET original_hashes 0 original_first)
check_hash(unknown 0 "${original_first}")
check_hash(unknown 1 "${unknown_hash}")

# Interrupted/mixed original+patched trees are safe to finish.
make_fixture(mixed)
list(GET files 0 first)
configure_file("${TEST_WORK_DIR}/clean/${first}" "${TEST_WORK_DIR}/mixed/${first}" COPYONLY)
run_patch(mixed TRUE)
foreach(index RANGE 0 1)
    list(GET patched_hashes ${index} expected)
    check_hash(mixed ${index} "${expected}")
endforeach()
message(STATUS "JUCE endpoint identity guard: clean, idempotent, unknown/no-write, mixed passed")
