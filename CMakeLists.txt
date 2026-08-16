cmake_minimum_required(VERSION 3.22)

project(SidebandMaw VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SIDEBANDMAW_BUILD_PLUGIN "Build the JUCE plug-in targets" ON)
option(SIDEBANDMAW_BUILD_TESTS "Build tests" ON)
set(SIDEBANDMAW_ARTIFACT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/artifacts" CACHE PATH
    "Directory for final VST3, AU, and Standalone products")
set(EHL_JUCE_SOURCE_DIR "" CACHE PATH "Existing JUCE source checkout; avoids network when set")

add_library(sidebandmaw_dsp STATIC
    Source/dsp/SidebandMawCore.cpp)

target_include_directories(sidebandmaw_dsp PUBLIC Source)
target_compile_features(sidebandmaw_dsp PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(sidebandmaw_dsp PRIVATE /W4)
else()
    target_compile_options(sidebandmaw_dsp PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SIDEBANDMAW_BUILD_TESTS)
    enable_testing()
    add_executable(SidebandMawTests Tests/TestMain.cpp)
    target_link_libraries(SidebandMawTests PRIVATE sidebandmaw_dsp)
    add_test(NAME SidebandMawDSPTests COMMAND SidebandMawTests)
endif()

if(SIDEBANDMAW_BUILD_PLUGIN)
    include(FetchContent)

    if(EHL_JUCE_SOURCE_DIR)
        if(NOT EXISTS "${EHL_JUCE_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "EHL_JUCE_SOURCE_DIR does not contain JUCE: ${EHL_JUCE_SOURCE_DIR}")
        endif()
        add_subdirectory("${EHL_JUCE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/juce")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
        add_subdirectory(JUCE)
    else()
        FetchContent_Declare(
            JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.15
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    add_subdirectory(modules/juce-ehl-design-module)

    set(SIDEBANDMAW_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND SIDEBANDMAW_FORMATS AU)
    endif()

    set(EHL_COPY_PLUGIN_AFTER_BUILD_DEFAULT OFF)
    if(APPLE AND NOT DEFINED ENV{CI})
        set(EHL_COPY_PLUGIN_AFTER_BUILD_DEFAULT ON)
    endif()
    option(EHL_COPY_PLUGIN_AFTER_BUILD
        "Copy built plug-in formats to the current user's standard plug-in folders"
        ${EHL_COPY_PLUGIN_AFTER_BUILD_DEFAULT})

    set(EHL_PLUGIN_COPY_ARGS
        COPY_PLUGIN_AFTER_BUILD ${EHL_COPY_PLUGIN_AFTER_BUILD})
    if(APPLE)
        list(APPEND EHL_PLUGIN_COPY_ARGS
            VST3_COPY_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3"
            AU_COPY_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/Components")
    endif()

    juce_add_plugin(SidebandMaw
        VERSION ${PROJECT_VERSION}
        COMPANY_NAME "EsionHsrahLatigid"
        BUNDLE_ID "jp.ehl.sidebandmaw"
        PRODUCT_NAME "SidebandMaw"
        DESCRIPTION "SSB frequency shift, ring modulation, and folded sideband feedback effect"
        PLUGIN_MANUFACTURER_CODE EHL_
        PLUGIN_CODE SbMw
        FORMATS ${SIDEBANDMAW_FORMATS}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "SidebandMaw requires microphone access to process live audio input."
        ${EHL_PLUGIN_COPY_ARGS})

    target_sources(SidebandMaw PRIVATE
        Source/ParameterIDs.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h)

    target_compile_definitions(SidebandMaw PUBLIC
        JUCE_REPORT_APP_USAGE=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0)

    target_link_libraries(SidebandMaw
        PRIVATE
            sidebandmaw_dsp
            EHL::JuceDesign
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    target_sources(SidebandMaw_Standalone PRIVATE Source/StandaloneApp.cpp)
    target_compile_definitions(SidebandMaw_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

    set(sidebandmaw_stage_platform "linux-x64")
    if(APPLE)
        set(sidebandmaw_stage_platform "macos-arm64")
    elseif(WIN32)
        set(sidebandmaw_stage_platform "windows-x64")
    endif()
    set(sidebandmaw_juce_products "${CMAKE_CURRENT_BINARY_DIR}/SidebandMaw_artefacts/$<CONFIG>")
    set(sidebandmaw_staged_products "${SIDEBANDMAW_ARTIFACT_DIR}/plugin-release/${sidebandmaw_stage_platform}")
    set(sidebandmaw_artifact_dependencies SidebandMaw_VST3 SidebandMaw_Standalone)
    set(sidebandmaw_artifact_commands
        COMMAND ${CMAKE_COMMAND} -E make_directory "${sidebandmaw_staged_products}/vst3"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${sidebandmaw_staged_products}/standalone"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${sidebandmaw_staged_products}/au")

    if(APPLE)
        list(APPEND sidebandmaw_artifact_commands
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${sidebandmaw_staged_products}/vst3/sidebandmaw_vst3_plugin.vst3"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${sidebandmaw_staged_products}/standalone/sidebandmaw_standalone_plugin.app"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${sidebandmaw_staged_products}/au/sidebandmaw_au_plugin.component"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${sidebandmaw_juce_products}/VST3/SidebandMaw.vst3" "${sidebandmaw_staged_products}/vst3/sidebandmaw_vst3_plugin.vst3"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${sidebandmaw_juce_products}/Standalone/SidebandMaw.app" "${sidebandmaw_staged_products}/standalone/sidebandmaw_standalone_plugin.app")
    elseif(WIN32)
        list(APPEND sidebandmaw_artifact_commands
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${sidebandmaw_staged_products}/vst3/sidebandmaw_vst3_plugin.vst3"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${sidebandmaw_juce_products}/VST3/SidebandMaw.vst3" "${sidebandmaw_staged_products}/vst3/sidebandmaw_vst3_plugin.vst3"
            COMMAND ${CMAKE_COMMAND} -E copy
                    "${sidebandmaw_juce_products}/Standalone/SidebandMaw.exe" "${sidebandmaw_staged_products}/standalone/sidebandmaw_standalone_plugin.exe")
    else()
        list(APPEND sidebandmaw_artifact_commands
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${sidebandmaw_staged_products}/vst3/sidebandmaw_vst3_plugin.vst3"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${sidebandmaw_juce_products}/VST3/SidebandMaw.vst3" "${sidebandmaw_staged_products}/vst3/sidebandmaw_vst3_plugin.vst3"
            COMMAND ${CMAKE_COMMAND} -E copy
                    "${sidebandmaw_juce_products}/Standalone/SidebandMaw" "${sidebandmaw_staged_products}/standalone/sidebandmaw_standalone_plugin")
    endif()

    if(TARGET SidebandMaw_AU)
        list(APPEND sidebandmaw_artifact_dependencies SidebandMaw_AU)
        list(APPEND sidebandmaw_artifact_commands
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${sidebandmaw_juce_products}/AU/SidebandMaw.component" "${sidebandmaw_staged_products}/au/sidebandmaw_au_plugin.component")
    endif()

    if(APPLE)
        list(APPEND sidebandmaw_artifact_commands
            COMMAND codesign --force --deep --sign -
                    "${sidebandmaw_staged_products}/vst3/sidebandmaw_vst3_plugin.vst3"
            COMMAND codesign --force --deep --sign -
                    "${sidebandmaw_staged_products}/standalone/sidebandmaw_standalone_plugin.app"
            COMMAND codesign --force --deep --sign -
                    "${sidebandmaw_staged_products}/au/sidebandmaw_au_plugin.component")
    endif()

    list(APPEND sidebandmaw_artifact_commands
        COMMAND ${CMAKE_COMMAND}
            -DPRODUCT_NAME=SidebandMaw
            -DSLUG=sidebandmaw
            -DBUNDLE_ID=jp.ehl.sidebandmaw
            -DPLUGIN_CODE=SbMw
            -DSTAGE_DIR=${sidebandmaw_staged_products}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteArtifactsManifest.cmake")

    add_custom_target(ehl_stage_products
        ${sidebandmaw_artifact_commands}
        DEPENDS ${sidebandmaw_artifact_dependencies}
        COMMENT "Staging SidebandMaw products under ${SIDEBANDMAW_ARTIFACT_DIR}/plugin-release")

    add_custom_target(SidebandMaw_Artifacts DEPENDS ehl_stage_products)

    if(SIDEBANDMAW_BUILD_TESTS)
        add_executable(SidebandMawIntegrationTests Tests/PluginIntegrationTest.cpp)
        target_include_directories(SidebandMawIntegrationTests PRIVATE Source)
        target_link_libraries(SidebandMawIntegrationTests PRIVATE SidebandMaw juce::juce_audio_utils)
        add_dependencies(SidebandMawIntegrationTests SidebandMaw_Artifacts)

        juce_add_console_app(SidebandMawPluginLoadTest
            PRODUCT_NAME "SidebandMaw Plugin Load Test")
        target_sources(SidebandMawPluginLoadTest PRIVATE Tests/PluginLoadTest.cpp)
        target_include_directories(SidebandMawPluginLoadTest PRIVATE Source)
        target_link_libraries(SidebandMawPluginLoadTest PRIVATE
            juce::juce_audio_processors_headless
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
        target_compile_definitions(SidebandMawPluginLoadTest PRIVATE
            JUCE_PLUGINHOST_VST3=1
            JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0)
        add_dependencies(SidebandMawPluginLoadTest SidebandMaw_VST3)

        add_test(NAME SidebandMawPluginIntegrationTests COMMAND SidebandMawIntegrationTests)
        add_test(NAME SidebandMawHostVST3
            COMMAND SidebandMawPluginLoadTest
                    "${sidebandmaw_juce_products}/VST3/SidebandMaw.vst3"
                    "SidebandMaw")
        add_test(
            NAME SidebandMawArtifactChecks
            COMMAND ${CMAKE_COMMAND}
                    -DSTAGE_DIR=${sidebandmaw_staged_products}
                    -DSLUG=sidebandmaw
                    -DEXPECT_AU=$<BOOL:${APPLE}>
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/Tests/VerifyArtifacts.cmake)
    endif()
endif()
