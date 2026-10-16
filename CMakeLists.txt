cmake_minimum_required(VERSION 3.22)

# macOS: universal binary (Apple Silicon + Intel), runs on macOS 10.15+. Must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
if (APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
endif()

project(Chiller VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(CH_BUILD_PLUGIN "Build the plugin (needs JUCE)" ON)
option(CH_BUILD_TOOLS "Build developer tools (UI screenshot renderer; needs JUCE)" OFF)
set(CH_TEST_FILTER "" CACHE STRING "Only compile src/tests/test_<filter>*.cpp (empty = all)")

# ---- Pure C++ DSP library (no JUCE) -------------------------------------------------------
set(CH_DSP_GLOBS "src/dsp/*.cpp;src/engine/*.cpp" CACHE STRING "Globs (recursive) of DSP sources to compile")
file(GLOB_RECURSE CH_DSP_SOURCES CONFIGURE_DEPENDS ${CH_DSP_GLOBS})
add_library(chiller_dsp STATIC src/dsp/DspStub.cpp ${CH_DSP_SOURCES})
target_include_directories(chiller_dsp PUBLIC src)
set_target_properties(chiller_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)   # linked into shared plugin modules (Linux)
if (MSVC)
    target_compile_options(chiller_dsp PRIVATE /W4 /fp:fast /wd4100 /wd4244 /wd4305)
    target_compile_definitions(chiller_dsp PUBLIC _USE_MATH_DEFINES NOMINMAX)
else()
    target_compile_options(chiller_dsp PRIVATE -Wall -Wextra)
endif()

# ---- Unit tests ---------------------------------------------------------------------------
file(GLOB CH_TEST_SOURCES CONFIGURE_DEPENDS src/tests/test_${CH_TEST_FILTER}*.cpp)
add_executable(chiller_tests src/tests/TestMain.cpp ${CH_TEST_SOURCES})
target_link_libraries(chiller_tests PRIVATE chiller_dsp)

# ---- Plugin ---------------------------------------------------------------------------------
if (CH_BUILD_PLUGIN)
    add_subdirectory(libs/JUCE)
    # CLAP support (https://github.com/free-audio/clap-juce-extensions), optional
    set(CH_HAS_CLAP OFF)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/clap-juce-extensions/CMakeLists.txt")
        add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)
        set(CH_HAS_CLAP ON)
    endif()
    set(CH_FORMATS VST3 Standalone)
    if (APPLE)
        list(APPEND CH_FORMATS AU) # Logic Pro / GarageBand
    endif()
    if (UNIX AND NOT APPLE)
        list(APPEND CH_FORMATS LV2)
    endif()

    juce_add_plugin(Chiller
        COMPANY_NAME "Neuroforge"            # shared suite manufacturer with NeuroForge
        PLUGIN_MANUFACTURER_CODE Nfrg
        PLUGIN_CODE Chl1
        FORMATS ${CH_FORMATS}
        PRODUCT_NAME "Chiller"
        BUNDLE_ID com.huuba91.chiller
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Dynamics EQ
        AU_MAIN_TYPE kAudioUnitType_Effect
        LV2URI https://github.com/huuba91/chiller)

    file(GLOB_RECURSE CH_APP_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp src/Plugin*.cpp)
    target_sources(Chiller PRIVATE ${CH_APP_SOURCES})
    target_compile_definitions(Chiller PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    juce_add_binary_data(ChAssets SOURCES
        assets/fonts/RobotoCondensed-Regular.ttf assets/fonts/RobotoCondensed-Medium.ttf
        assets/fonts/RobotoCondensed-SemiBold.ttf assets/fonts/RobotoCondensed-Bold.ttf)
    target_link_libraries(Chiller PRIVATE chiller_dsp ChAssets
        juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

    if (CH_HAS_CLAP)
        clap_juce_extensions_plugin(TARGET Chiller
            CLAP_ID "com.huuba91.chiller"
            CLAP_FEATURES audio-effect equalizer filter mixing mastering)
    endif()

    # ---- Developer tool: render the editor to a PNG (see tools/uishot/main.cpp) ----------------
    if (CH_BUILD_TOOLS)
        juce_add_console_app(chiller_uishot PRODUCT_NAME "chiller_uishot")
        target_sources(chiller_uishot PRIVATE tools/uishot/main.cpp ${CH_APP_SOURCES})
        target_compile_definitions(chiller_uishot PRIVATE
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(chiller_uishot PRIVATE chiller_dsp ChAssets
            juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
    endif()
endif()
