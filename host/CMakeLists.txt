# DirectPipe Host Application (JUCE-based VST Host)

juce_add_gui_app(DirectPipe
    PRODUCT_NAME "DirectPipe"
    VERSION "3.1.0"
    COMPANY_NAME "DirectPipe"
    BUNDLE_ID "com.directpipe.host"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE DPip
)

target_sources(DirectPipe PRIVATE
    Source/Main.cpp
    Source/MainComponent.h
    Source/MainComponent.cpp
    Source/Audio/AudioEngine.h
    Source/Audio/AudioEngine.cpp
    Source/Audio/VSTChain.h
    Source/Audio/VSTChain.cpp
    Source/Audio/OutputRouter.h
    Source/Audio/OutputRouter.cpp
    Source/Audio/LatencyMonitor.h
    Source/Audio/LatencyMonitor.cpp
    Source/Audio/VirtualMicOutput.h
    Source/Audio/VirtualMicOutput.cpp
    Source/IPC/SharedMemWriter.h
    Source/IPC/SharedMemWriter.cpp
    Source/UI/DeviceSelector.h
    Source/UI/DeviceSelector.cpp
    Source/UI/PluginChainEditor.h
    Source/UI/PluginChainEditor.cpp
    Source/UI/LevelMeter.h
    Source/UI/LevelMeter.cpp
    Source/UI/PresetManager.h
    Source/UI/PresetManager.cpp
    Source/UI/DirectPipeLookAndFeel.h
    Source/UI/DirectPipeLookAndFeel.cpp
    Source/UI/AudioSettings.h
    Source/UI/AudioSettings.cpp
    Source/UI/OutputPanel.h
    Source/UI/OutputPanel.cpp
    Source/UI/ControlSettingsPanel.h
    Source/UI/ControlSettingsPanel.cpp
    Source/Control/ActionDispatcher.h
    Source/Control/ActionDispatcher.cpp
    Source/Control/StateBroadcaster.h
    Source/Control/StateBroadcaster.cpp
    Source/Control/HotkeyHandler.h
    Source/Control/HotkeyHandler.cpp
    Source/Control/MidiHandler.h
    Source/Control/MidiHandler.cpp
    Source/Control/WebSocketServer.h
    Source/Control/WebSocketServer.cpp
    Source/Control/HttpApiServer.h
    Source/Control/HttpApiServer.cpp
    Source/Control/ControlMapping.h
    Source/Control/ControlMapping.cpp
    Source/Control/ControlManager.h
    Source/Control/ControlManager.cpp
)

target_compile_definitions(DirectPipe PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:DirectPipe,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:DirectPipe,JUCE_VERSION>"
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_WASAPI=1
    JUCE_WASAPI_EXCLUSIVE=0
)

target_link_libraries(DirectPipe PRIVATE
    directpipe-core
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
