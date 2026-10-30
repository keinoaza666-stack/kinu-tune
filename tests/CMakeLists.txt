# Unit tests.
#
# Each test is a standalone executable with its own main(), matching the style
# the files were already written in. They link pitchnet_core, which exposes the
# JUCE modules PUBLIC, so a test gets every include path it needs by linking
# that one target.
#
# Built only when PITCHNET_BUILD_TESTS=ON, so a normal build and CI are
# unaffected:
#
#   cmake -B build -DPITCHNET_BUILD_TESTS=ON
#   cmake --build build
#   ctest --test-dir build --output-on-failure

set(PITCHNET_TEST_SOURCES
    AraRegionMergeTests.cpp
    AraRegionSliceTests.cpp
    FormantShiftTests.cpp
    HarmonyIntervalTests.cpp
    HostSyncTempoTests.cpp
    NoteGainCurveTests.cpp
    NonAraLifecycleTests.cpp
    OriginalWaveformRestoreTests.cpp
    PitchDrawingTests.cpp
    PitchDriftTests.cpp
    PluginStateArchiveTests.cpp
    ProjectModificationRangeTests.cpp
    SynthesisEngineShareTests.cpp
    TimelineCoordinateTests.cpp
    VisualWaveformEnvelopeTests.cpp)

foreach(test_source IN LISTS PITCHNET_TEST_SOURCES)
    get_filename_component(test_name ${test_source} NAME_WE)

    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE pitchnet_core)
    target_compile_features(${test_name} PRIVATE cxx_std_17)

    # The tests assert on results rather than returning a failure code, so a
    # failed assertion aborts the process and CTest sees a non-zero exit.
    add_test(NAME ${test_name} COMMAND ${test_name})

    # NOTE: assert() compiles to nothing under NDEBUG, which release
    # configurations set - so a test written with bare assert() passes
    # vacuously there. Undefining NDEBUG for the target is not the fix: it
    # changes the compile flags for every JUCE source the target pulls in,
    # forcing a full rebuild of JUCE per test. Tests should use a check macro
    # that does not depend on NDEBUG instead; see TestAssert.h.
endforeach()

# The capture state machine belongs to the plug-in rather than pitchnet_core.
target_sources(NonAraLifecycleTests PRIVATE
    ../Source/Plugin/NonAraCaptureController.cpp)

add_executable(PitchMacroScopeTests PitchMacroScopeTests.cpp)
target_link_libraries(PitchMacroScopeTests PRIVATE pitchnet_core)
target_compile_features(PitchMacroScopeTests PRIVATE cxx_std_17)
add_test(NAME PitchMacroScopeTests COMMAND PitchMacroScopeTests)

add_executable(TrackUndoTests TrackUndoTests.cpp)
target_link_libraries(TrackUndoTests PRIVATE pitchnet_core)
target_compile_features(TrackUndoTests PRIVATE cxx_std_17)
add_test(NAME TrackUndoTests COMMAND TrackUndoTests)

add_executable(TrackGestureTests TrackGestureTests.cpp)
target_link_libraries(TrackGestureTests PRIVATE pitchnet_ui)
target_compile_features(TrackGestureTests PRIVATE cxx_std_17)
add_test(NAME TrackGestureTests COMMAND TrackGestureTests)

add_executable(PitchMacroRenderTests PitchMacroRenderTests.cpp)
target_link_libraries(PitchMacroRenderTests PRIVATE pitchnet_ui)
target_compile_features(PitchMacroRenderTests PRIVATE cxx_std_17)
add_test(NAME PitchMacroRenderTests COMMAND PitchMacroRenderTests)

add_executable(HarmonyApplyTests HarmonyApplyTests.cpp)
target_link_libraries(HarmonyApplyTests PRIVATE pitchnet_ui)
target_compile_features(HarmonyApplyTests PRIVATE cxx_std_17)
add_test(NAME HarmonyApplyTests COMMAND HarmonyApplyTests)

# Not a test: renders UI snapshots for visual checks (no audio device needed).
add_executable(RenderSnapshots RenderSnapshots.cpp)
target_link_libraries(RenderSnapshots PRIVATE pitchnet_ui)
target_compile_features(RenderSnapshots PRIVATE cxx_std_17)

add_executable(VariAudioControlTests VariAudioControlTests.cpp)
target_link_libraries(VariAudioControlTests PRIVATE pitchnet_ui)
target_compile_features(VariAudioControlTests PRIVATE cxx_std_17)
add_test(NAME VariAudioControlTests COMMAND VariAudioControlTests)

add_executable(VariAudioInspectorTests VariAudioInspectorTests.cpp)
target_link_libraries(VariAudioInspectorTests PRIVATE pitchnet_ui)
target_compile_features(VariAudioInspectorTests PRIVATE cxx_std_17)
add_test(NAME VariAudioInspectorTests COMMAND VariAudioInspectorTests)

add_executable(AuditionOutputTests AuditionOutputTests.cpp)
target_link_libraries(AuditionOutputTests PRIVATE pitchnet_ui)
target_compile_features(AuditionOutputTests PRIVATE cxx_std_17)
add_test(NAME AuditionOutputTests COMMAND AuditionOutputTests)

add_executable(AuditionHandoffTests AuditionHandoffTests.cpp)
target_link_libraries(AuditionHandoffTests PRIVATE pitchnet_ui)
target_compile_features(AuditionHandoffTests PRIVATE cxx_std_17)
add_test(NAME AuditionHandoffTests COMMAND AuditionHandoffTests)

add_executable(AiResynthesisChoiceTests AiResynthesisChoiceTests.cpp)
target_link_libraries(AiResynthesisChoiceTests PRIVATE pitchnet_ui)
target_compile_features(AiResynthesisChoiceTests PRIVATE cxx_std_17)
add_test(NAME AiResynthesisChoiceTests COMMAND AiResynthesisChoiceTests)

add_executable(OnnxRuntimeAvailabilityTests OnnxRuntimeAvailabilityTests.cpp)
target_link_libraries(OnnxRuntimeAvailabilityTests PRIVATE pitchnet_ui)
target_compile_features(OnnxRuntimeAvailabilityTests PRIVATE cxx_std_17)
add_test(NAME OnnxRuntimeAvailabilityTests COMMAND OnnxRuntimeAvailabilityTests)

# Loads the shipped models and checks analysis and synthesis with numbers. The
# CI Windows job builds and runs only this target (the other tests stay on
# macOS), because nothing else runs a model on Windows. The test takes the
# models folder as its argument; GAME is downloaded into it at configure time.
add_executable(ModelInferenceTests ModelInferenceTests.cpp)
target_link_libraries(ModelInferenceTests PRIVATE pitchnet_core)
target_compile_features(ModelInferenceTests PRIVATE cxx_std_17)
add_test(NAME ModelInferenceTests
         COMMAND ModelInferenceTests "${CMAKE_SOURCE_DIR}/Resources/models")
if(WIN32 AND ONNXRUNTIME_DLLS)
    # Like the plug-in, the test loads onnxruntime.dll from its own folder.
    foreach(DLL ${ONNXRUNTIME_DLLS})
        add_custom_command(TARGET ModelInferenceTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DLL}" "$<TARGET_FILE_DIR:ModelInferenceTests>")
    endforeach()
endif()

# Calls that must go through one entry point (see SourceGuard.cmake).
foreach(guard_rule IN ITEMS pitch-writes audition-output render-controllers)
    add_test(NAME SourceGuard_${guard_rule}
             COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/Source
                     -DRULE=${guard_rule}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/SourceGuard.cmake)
endforeach()
