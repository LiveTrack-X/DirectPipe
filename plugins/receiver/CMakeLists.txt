# DirectPipe Receiver VST2 Plugin
# Receives processed audio from DirectPipe host via shared memory IPC

juce_add_plugin(DirectPipeReceiver
    PLUGIN_MANUFACTURER_CODE DPip
    PLUGIN_CODE DPRx
    FORMATS VST
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
)

target_link_libraries(DirectPipeReceiver PRIVATE
    directpipe-core
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
