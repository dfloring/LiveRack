# Plugins for LiveRack's automated tests: three of JUCE's example plugins and a
# compressor that reports gain reduction like commercial dynamics plugins do.
#   cmake -S tests/demo-plugins -B build-plugins -DJUCE_DIR=/path/to/JUCE
cmake_minimum_required(VERSION 3.22)
project(LiveRackTestPlugins VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)

if(NOT JUCE_DIR)
    message(FATAL_ERROR "Give the JUCE checkout: -DJUCE_DIR=...")
endif()
add_subdirectory(${JUCE_DIR} JUCE)

juce_add_pip(${JUCE_DIR}/examples/Plugins/DSPModulePluginDemo.h)
juce_add_pip(${JUCE_DIR}/examples/Plugins/GainPluginDemo.h)
juce_add_pip(${JUCE_DIR}/examples/Plugins/NoiseGatePluginDemo.h)

# No web views or curl: keeps the build free of GTK / WebKit
foreach(t DSPModulePluginDemo GainPlugin NoiseGatePlugin)
    if(TARGET ${t})
        target_compile_definitions(${t} PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    endif()
endforeach()

juce_add_plugin(DemoCompressor
    FORMATS VST3
    PRODUCT_NAME "DemoCompressor"
    COMPANY_NAME "LiveRackTest"
    PLUGIN_MANUFACTURER_CODE Lrtt
    PLUGIN_CODE Dcmp)
target_sources(DemoCompressor PRIVATE comp/DemoCompressor.cpp)
target_compile_definitions(DemoCompressor PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(DemoCompressor PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)

# A plugin with an optional side chain input (output = main + side chain)
juce_add_plugin(SidechainProbe
    FORMATS VST3
    PRODUCT_NAME "SidechainProbe"
    COMPANY_NAME "LiveRackTest"
    PLUGIN_MANUFACTURER_CODE Lrtt
    PLUGIN_CODE Scpr)
target_sources(SidechainProbe PRIVATE sidechain/SidechainProbe.cpp)
target_compile_definitions(SidechainProbe PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(SidechainProbe PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)
# The test command builds DemoCompressor_VST3; this brings the probe along without changing it
add_dependencies(DemoCompressor_VST3 SidechainProbe_VST3)
