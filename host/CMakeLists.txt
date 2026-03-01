# DirectPipe Host Application (JUCE-based VST Host)

juce_add_gui_app(DirectPipe
    PRODUCT_NAME "DirectPipe"
    VERSION "3.8.0"
    COMPANY_NAME "DirectPipe"
    BUNDLE_ID "com.directpipe.host"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon_48.png"
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
    Source/Audio/AudioRingBuffer.h
    Source/Audio/MonitorOutput.h
    Source/Audio/MonitorOutput.cpp
    Source/Audio/AudioRecorder.h
    Source/Audio/AudioRecorder.cpp
    # DeviceSelector removed — merged into AudioSettings
    Source/UI/PluginChainEditor.h
    Source/UI/PluginChainEditor.cpp
    Source/UI/PluginScanner.h
    Source/UI/PluginScanner.cpp
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
    Source/UI/SettingsExporter.h
    Source/UI/SettingsExporter.cpp
    Source/UI/NotificationBar.h
    Source/UI/NotificationBar.cpp
    Source/UI/LogPanel.h
    Source/UI/LogPanel.cpp
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
    Source/IPC/SharedMemWriter.h
    Source/IPC/SharedMemWriter.cpp
)

juce_add_binary_data(DirectPipeBinaryData
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        Resources/icon.png
        Resources/icon_16.png
        Resources/icon_32.png
        Resources/icon_48.png
)

juce_generate_juce_header(DirectPipe)

# ─── ASIO SDK detection ───
set(ASIO_SDK_DIR "${CMAKE_SOURCE_DIR}/thirdparty/asiosdk")
if(EXISTS "${ASIO_SDK_DIR}/common/asio.h")
    set(DIRECTPIPE_HAS_ASIO ON)
    message(STATUS "ASIO SDK found at ${ASIO_SDK_DIR}")
else()
    set(DIRECTPIPE_HAS_ASIO OFF)
    message(STATUS "ASIO SDK not found (looked in ${ASIO_SDK_DIR}/common/asio.h) — ASIO disabled")
endif()

target_compile_definitions(DirectPipe PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:DirectPipe,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:DirectPipe,JUCE_VERSION>"
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=1
    JUCE_WASAPI=1
    JUCE_WASAPI_EXCLUSIVE=0
    JUCE_ASIO=$<BOOL:${DIRECTPIPE_HAS_ASIO}>
)

if(DIRECTPIPE_HAS_ASIO)
    target_include_directories(DirectPipe PRIVATE "${ASIO_SDK_DIR}/common")
endif()

target_link_libraries(DirectPipe PRIVATE
    DirectPipeBinaryData
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
