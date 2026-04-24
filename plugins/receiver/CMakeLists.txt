# DirectPipe Receiver Plugin
# Receives processed audio from DirectPipe host via shared memory IPC

# Build format list based on available SDKs
set(RECEIVER_FORMATS VST3)
if(DIRECTPIPE_HAS_VST2)
    list(APPEND RECEIVER_FORMATS VST)
endif()
if(APPLE)
    list(APPEND RECEIVER_FORMATS AU)
endif()

juce_add_plugin(DirectPipeReceiver
    PLUGIN_MANUFACTURER_CODE DPip
    PLUGIN_CODE DPRx
    FORMATS ${RECEIVER_FORMATS}
    VERSION "4.0.5"
    PRODUCT_NAME "DirectPipe Receiver"
    COMPANY_NAME "DirectPipe"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(DirectPipeReceiver PRIVATE
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
)

juce_generate_juce_header(DirectPipeReceiver)

target_compile_definitions(DirectPipeReceiver PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>
)

target_link_libraries(DirectPipeReceiver PRIVATE
    directpipe-core
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# ─── macOS ad-hoc code signing ───
# Receiver plugin needs signing so OBS (or any DAW) can load it without Gatekeeper blocking.
if(APPLE)
    foreach(fmt IN ITEMS VST3 VST AU)
        if(TARGET DirectPipeReceiver_${fmt})
            add_custom_command(TARGET DirectPipeReceiver_${fmt} POST_BUILD
                COMMAND codesign --force --deep -s - "$<TARGET_BUNDLE_DIR:DirectPipeReceiver_${fmt}>"
                COMMENT "Ad-hoc signing DirectPipeReceiver ${fmt}"
            )
        endif()
    endforeach()
endif()
