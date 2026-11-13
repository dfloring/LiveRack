cmake_minimum_required(VERSION 3.22)
project(LiveRack VERSION 0.21.0)
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")  # Apple silicon
endif()

# Use a local JUCE checkout if given (-DJUCE_DIR=...), otherwise download it
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_gui_app(LiveRack
    PRODUCT_NAME "LiveRack"
    COMPANY_NAME "LiveRack"
    BUNDLE_ID    "com.liverack.host"
    ICON_BIG     "${CMAKE_CURRENT_SOURCE_DIR}/Resources/LiveRackIcon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "LiveRack needs audio input to process your console's channels."
    PLIST_TO_MERGE "<plist><dict><key>NSLocalNetworkUsageDescription</key><string>LiveRack talks to your mixing console on the local network to follow scene recalls.</string></dict></plist>")

target_sources(LiveRack PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/RackEngine.cpp
    Source/IsolatedPlugin.cpp
    Source/ServerMode.cpp
    Source/ConsoleLink.cpp
    Source/ConsoleLinkPanel.cpp
    Source/RackStripsView.cpp
    Source/RemoteConnection.cpp
    Source/PluginDock.cpp
    Source/PluginLibrary.cpp
    Source/PluginPicker.cpp
    Source/GainReduction.cpp
    Source/ParameterInspector.cpp
    Source/Actions.cpp
    Source/ControlBars.cpp
    Source/OscRemote.cpp
    Source/MidiControl.cpp
    Source/RoutingMatrix.cpp
    Source/SnapshotsPanel.cpp
    Source/ServerCheck.cpp
    Source/DevicesPanel.cpp
    Source/Shortcuts.cpp
    Source/WebRemote.cpp
    Source/SocketUtil.cpp
    Source/PluginViewCapture.cpp
    Source/PluginViewCaptureWindows.cpp)

# Plugin windows on the web remote: ScreenCaptureKit capture on the Mac
if(APPLE)
    target_sources(LiveRack PRIVATE Source/PluginViewCaptureMac.mm)
    set_source_files_properties(Source/PluginViewCaptureMac.mm PROPERTIES COMPILE_OPTIONS "-fno-objc-arc")
    target_link_libraries(LiveRack PRIVATE
        "-weak_framework ScreenCaptureKit"
        "-framework CoreMedia" "-framework CoreVideo" "-framework ImageIO")
endif()

# The iPad / web remote page and icon, compiled into the app
juce_add_binary_data(LiveRackWebData SOURCES
    Resources/web/remote.html
    Resources/LiveRackIcon.png)

# Windows: ASIO drivers (Dante Virtual Soundcard, SoundGrid, interfaces) need
# Steinberg's ASIO SDK (GPLv3 option). Pass -DASIO_SDK_DIR=<folder with common/iasiodrv.h>
if(WIN32)
    set(ASIO_SDK_DIR "" CACHE PATH "ASIO SDK folder (contains common/iasiodrv.h)")
    if(ASIO_SDK_DIR)
        target_include_directories(LiveRack PRIVATE "${ASIO_SDK_DIR}/common")
        target_compile_definitions(LiveRack PRIVATE JUCE_ASIO=1)
        message(STATUS "LiveRack: ASIO enabled from ${ASIO_SDK_DIR}")
    else()
        message(WARNING "LiveRack: no ASIO_SDK_DIR given, building without ASIO (WASAPI only)")
    endif()
endif()

target_compile_definitions(LiveRack PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:LiveRack,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:LiveRack,JUCE_VERSION>")

# Linux preview builds (used for UI screenshots) run without these extras
if(UNIX AND NOT APPLE AND LIVERACK_PREVIEW)
    target_compile_definitions(LiveRack PRIVATE
        JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0
        JUCE_USE_XCURSOR=0 JUCE_USE_XSHM=0 JUCE_USE_XRENDER=0 LIVERACK_PREVIEW_HOOKS=1)
endif()

target_link_libraries(LiveRack
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_osc
        LiveRackWebData
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
