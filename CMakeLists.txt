cmake_minimum_required(VERSION 3.22)
project(CraftLimit VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── JUCE ──────────────────────────────────────────────────────────────────────
# Clone JUCE into the project root or set JUCE_PATH to your local JUCE folder.
# git clone https://github.com/juce-framework/JUCE.git
add_subdirectory(JUCE)

juce_add_plugin(CraftLimit
    VERSION                 1.0.0
    COMPANY_NAME            "Craft Audio Tools"
    COMPANY_WEBSITE         ""
    PLUGIN_MANUFACTURER_CODE Caft
    PLUGIN_CODE             Clmt
    FORMATS                 VST3 AU Standalone
    PRODUCT_NAME            "CraftLimit"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES         "Fx" "Dynamics"
    AU_MAIN_TYPE            kAudioUnitType_Effect
)

juce_generate_juce_header(CraftLimit)

target_sources(CraftLimit PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/LimiterDSP.cpp
)

target_include_directories(CraftLimit PRIVATE Source)

target_compile_definitions(CraftLimit PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(CraftLimit PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_core
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
