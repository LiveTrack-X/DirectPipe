# DirectPipe extensions to JUCE 7.0.12. See JuceEndpointIdentity.md for provenance.
# Validate every input before writing any file; fail closed on upstream changes.
function(directpipe_patch_juce_endpoint_identity source_dir)
    set(files
        modules/juce_audio_devices/audio_io/juce_AudioIODevice.h
        modules/juce_audio_devices/native/juce_WASAPI_windows.cpp
        extras/Build/juceaide/CMakeLists.txt)
    set(original_hashes
        1234da3bd93fda2ba2b4da86df3ad96dc36a6ae976f46fc5d45dd4b66591c475
        e4c9ac34dbe02b4fe62faa9b85b4f26ecf4d1b2a4017b5d1863d637058e3ec91
        70a92317dd9b5bb283f0d1ba0e3c7f603ccd97da322ee69d9c473ad03b690912)
    set(patched_hashes
        3cf640e690127954ca94888f472b11aa133e839bfd86900310153ed526fd6926
        e288000e2f6d0a22f02d73b65064940d4376c045c7618e4c48db5297caf54673
        5c9549ea2698b671551498af72e50b4772ac21bccf9183e7feb56879d2a2d495)
    set(anchor0 "    virtual int getXRunCount() const noexcept;")
    set(addition0 "\n\n    /** DirectPipe host extension: exact opened Windows capture endpoint ID.\n        Returns empty when unsupported; this query never changes device state. */\n    virtual String getWindowsInputEndpointId() const { return {}; }")
    set(anchor1 "    int getXRunCount() const noexcept override              { return inputDevice != nullptr ? inputDevice->xruns : -1; }")
    set(addition1 "\n    String getWindowsInputEndpointId() const override      { return inputDevice != nullptr ? inputDeviceId : String{}; }")
    set(anchor2 [=[    # Looks like we're bootstrapping, reinvoke CMake]=])
    # Official JUCE 5023fc6 + f95a5d4: preserve the configured macOS minimum
    # for the host helper, while not passing an iOS cross-compilation target.
    set(addition2 [=[    set(PASSTHROUGH_ARGS "")

    if(CMAKE_OSX_DEPLOYMENT_TARGET AND NOT CMAKE_CROSSCOMPILING)
        list(APPEND PASSTHROUGH_ARGS "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()

]=])
    set(install_prefix_line [=[            "-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}"]=])
    set(passthrough_line [=[            ${PASSTHROUGH_ARGS}]=])
    foreach(index RANGE 0 2)
        list(GET files ${index} relative)
        set(path "${source_dir}/${relative}")
        if(NOT EXISTS "${path}")
            message(FATAL_ERROR "Missing pinned JUCE 7.0.12 source: ${path}")
        endif()
        file(READ "${path}" content)
        string(REPLACE "\r\n" "\n" content "${content}")
        string(SHA256 actual "${content}")
        list(GET original_hashes ${index} original)
        list(GET patched_hashes ${index} patched)
        set(write${index} FALSE)
        if(actual STREQUAL original)
            if(index EQUAL 2)
                string(REPLACE "${anchor2}" "${addition2}${anchor2}" content "${content}")
                string(REPLACE "${install_prefix_line}" "${install_prefix_line}\n${passthrough_line}" content "${content}")
            else()
                string(REPLACE "${anchor${index}}" "${anchor${index}}${addition${index}}" content "${content}")
            endif()
            string(SHA256 actual "${content}")
            if(NOT actual STREQUAL patched)
                message(FATAL_ERROR "JUCE extension did not match expected patch: ${relative}")
            endif()
            set(write${index} TRUE)
        elseif(NOT actual STREQUAL patched)
            message(FATAL_ERROR "Unknown JUCE source (${relative}, SHA256 ${actual}). Review the guarded extensions before updating the JUCE 7.0.12 pin; no files were changed.")
        endif()
        set(content${index} "${content}")
    endforeach()
    foreach(index RANGE 0 2)
        if(write${index})
            list(GET files ${index} relative)
            file(WRITE "${source_dir}/${relative}" "${content${index}}")
        endif()
    endforeach()
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    if(NOT DEFINED DIRECTPIPE_JUCE_SOURCE_DIR)
        message(FATAL_ERROR "DIRECTPIPE_JUCE_SOURCE_DIR is required")
    endif()
    directpipe_patch_juce_endpoint_identity("${DIRECTPIPE_JUCE_SOURCE_DIR}")
endif()
