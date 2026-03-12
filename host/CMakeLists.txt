# DirectPipe Host Application (JUCE-based VST Host)

juce_add_gui_app(DirectPipe
    PRODUCT_NAME "DirectPipe"
    VERSION "4.0.0"
    COMPANY_NAME "DirectPipe"
    BUNDLE_ID "com.directpipe.host"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon_48.png"
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE DPip
    # macOS: microphone access permission (required by macOS 10.14+ for audio input)
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "DirectPipe needs microphone access for real-time audio processing through your VST plugin chain."
    # macOS: merge additional Info.plist keys
    PLIST_TO_MERGE "${CMAKE_CURRENT_SOURCE_DIR}/Resources/MacOSXBundleInfo.plist"
)

target_sources(DirectPipe PRIVATE
    Source/Main.cpp
    Source/MainComponent.h
    Source/MainComponent.cpp
    Source/Audio/AudioEngine.h
    Source/Audio/AudioEngine.cpp
    Source/Audio/VSTChain.h
    Source/Audio/VSTChain.cpp
    Source/Audio/PluginPreloadCache.h
    Source/Audio/PluginPreloadCache.cpp
    Source/Audio/OutputRouter.h
    Source/Audio/OutputRouter.cpp
    Source/Audio/LatencyMonitor.h
    Source/Audio/LatencyMonitor.cpp
    Source/Audio/PluginLoadHelper.h
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
    Source/UI/PresetSlotBar.h
    Source/UI/PresetSlotBar.cpp
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
    Source/UI/StatusUpdater.h
    Source/UI/StatusUpdater.cpp
    Source/UI/UpdateChecker.h
    Source/UI/UpdateChecker.cpp
    Source/Control/ActionHandler.h
    Source/Control/ActionHandler.cpp
    Source/Control/SettingsAutosaver.h
    Source/Control/SettingsAutosaver.cpp
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
    Source/Control/Log.h
    Source/Control/Log.cpp
    Source/IPC/SharedMemWriter.h
    Source/IPC/SharedMemWriter.cpp
    # Platform abstraction headers
    Source/Platform/PlatformAudio.h
    Source/Platform/AutoStart.h
    Source/Platform/ProcessPriority.h
    Source/Platform/MultiInstanceLock.h
)

# ─── Platform-specific source files ───
if(WIN32)
    target_sources(DirectPipe PRIVATE
        Source/Platform/Windows/WindowsAutoStart.cpp
        Source/Platform/Windows/WindowsProcessPriority.cpp
        Source/Platform/Windows/WindowsMultiInstanceLock.cpp
    )
elseif(APPLE)
    target_sources(DirectPipe PRIVATE
        Source/Platform/macOS/MacAutoStart.cpp
        Source/Platform/macOS/MacProcessPriority.cpp
        Source/Platform/macOS/MacMultiInstanceLock.cpp
    )
else()
    target_sources(DirectPipe PRIVATE
        Source/Platform/Linux/LinuxAutoStart.cpp
        Source/Platform/Linux/LinuxProcessPriority.cpp
        Source/Platform/Linux/LinuxMultiInstanceLock.cpp
    )
endif()

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

# ─── ASIO SDK detection (Windows only) ───
set(DIRECTPIPE_HAS_ASIO OFF)
if(WIN32)
    set(ASIO_SDK_DIR "${CMAKE_SOURCE_DIR}/thirdparty/asiosdk")
    if(EXISTS "${ASIO_SDK_DIR}/common/asio.h")
        set(DIRECTPIPE_HAS_ASIO ON)
        message(STATUS "ASIO SDK found at ${ASIO_SDK_DIR}")
    else()
        message(STATUS "ASIO SDK not found (looked in ${ASIO_SDK_DIR}/common/asio.h) — ASIO disabled")
    endif()
endif()

# ─── Platform-common compile definitions ───
target_compile_definitions(DirectPipe PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:DirectPipe,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:DirectPipe,JUCE_VERSION>"
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=$<BOOL:${DIRECTPIPE_HAS_VST2}>
)

# ─── Windows-specific definitions ───
if(WIN32)
    target_compile_definitions(DirectPipe PRIVATE
        JUCE_WASAPI=1
        JUCE_WASAPI_EXCLUSIVE=1
        JUCE_ASIO=$<BOOL:${DIRECTPIPE_HAS_ASIO}>
    )
    if(DIRECTPIPE_HAS_ASIO)
        target_include_directories(DirectPipe PRIVATE "${ASIO_SDK_DIR}/common")
    endif()
endif()

# ─── macOS-specific definitions ───
if(APPLE)
    target_compile_definitions(DirectPipe PRIVATE
        JUCE_PLUGINHOST_AU=1
    )
endif()

# ─── Linux-specific definitions ───
if(UNIX AND NOT APPLE)
    target_compile_definitions(DirectPipe PRIVATE
        JUCE_JACK=1
        JUCE_ALSA=1
    )
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

# ─── macOS ad-hoc code signing (free, no Apple Developer account) ───
# Includes entitlements for: microphone access, VST plugin loading, network server.
# Prevents repeated macOS firewall "allow incoming connections?" dialog.
# Users still need right-click → Open on first launch (Gatekeeper).
# For notarized distribution, replace "-" with "Developer ID Application: ..." identity.
if(APPLE)
    add_custom_command(TARGET DirectPipe POST_BUILD
        COMMAND codesign --force --deep -s -
            --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Entitlements.plist"
            "$<TARGET_BUNDLE_DIR:DirectPipe>"
        COMMENT "Ad-hoc signing DirectPipe.app with entitlements"
    )
endif()
