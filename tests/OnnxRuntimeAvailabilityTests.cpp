// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

// When the bundled ONNX Runtime cannot be loaded (on Windows: no Visual C++
// Redistributable), the ONNX Runtime API pointer stays null and any Ort:: call
// crashes. This recreates exactly that state - a null API and isAvailable()
// false - and checks that every entry point refuses instead of calling in, and
// that the user is told why and how to fix it. Without the guards this test
// crashes rather than failing a CHECK.

#include "../Source/Audio/EditorController.h"
#include "../Source/Audio/FCPEPitchDetector.h"
#include "../Source/Audio/GAMEDetector.h"
#include "../Source/Audio/RMVPEPitchDetector.h"
#include "../Source/Audio/SynthesisEngineType.h"
#include "../Source/Audio/Vocoder.h"
#include "../Source/Models/Project.h"
#include "../Source/UI/SettingsComponent.h"
#include "../Source/Utils/Localization.h"
#include "../Source/Utils/OnnxRuntime.h"
#include "TestAssert.h"
#include <onnxruntime_cxx_api.h>
#include <iostream>

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    CHECK(OnnxRuntime::useLinkedRuntimeForTesting());
    CHECK(OnnxRuntime::isAvailable());
    CHECK(OnnxRuntime::getUnavailableReason().isEmpty());

    const juce::String reason =
        "Could not load Kinu Tune's ONNX Runtime: LoadLibrary(onnxruntime.dll) "
        "failed with Windows error 126";
    OnnxRuntime::forceUnavailableForTesting(reason);
    Ort::InitApi(nullptr);

    CHECK(!OnnxRuntime::isAvailable());
    CHECK(OnnxRuntime::getUnavailableReason() == reason);

    // The message names what is off and carries the reason, in English and
    // in Japanese.
    for (const auto *language : {"en", "ja"})
    {
        Localization::getInstance().setLanguage(language);
        const auto title = TR("error.onnx_runtime_unavailable_title");
        const auto body = TR("error.onnx_runtime_unavailable");
        CHECK(title != "error.onnx_runtime_unavailable_title");
        CHECK(body != "error.onnx_runtime_unavailable");
        CHECK(TR("error.install_vc_redist") != "error.install_vc_redist");

        const auto message = OnnxRuntime::getUnavailableMessage();
        CHECK(message.contains(body));
        CHECK(message.contains(reason));
    }
    Localization::getInstance().setLanguage("ja");
    const auto japaneseTitle = TR("error.onnx_runtime_unavailable_title");
    Localization::getInstance().setLanguage("en");
    CHECK(japaneseTitle != TR("error.onnx_runtime_unavailable_title"));

    // Each place that would create an Ort::Env refuses first.
    {
        FCPEPitchDetector fcpe;
        CHECK(!fcpe.loadModel(juce::File("/nonexistent/fcpe.onnx")));
        CHECK(fcpe.getLastError() == reason);
    }
    {
        RMVPEPitchDetector rmvpe;
        CHECK(!rmvpe.loadModel(juce::File("/nonexistent/rmvpe.onnx")));
        CHECK(rmvpe.getLastError() == reason);
    }
    {
        // A readable config.json, so that without the guard loadModels would
        // go on to create its Ort::Env.
        const auto gameDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("OnnxRuntimeAvailabilityTests-GAME");
        gameDir.createDirectory();
        CHECK(gameDir.getChildFile("config.json")
                  .replaceWithText(R"({"samplerate": 44100, "timestep": 0.01, "loop": true, "embedding_dim": 256})"));
        GAMEDetector game;
        CHECK(!game.loadModels(gameDir));
        CHECK(!game.isLoaded());
        gameDir.deleteRecursively();
    }

    // The device lists ask ONNX Runtime for its providers.
    CHECK(SettingsComponent::getAvailableDevices().contains("CPU"));
    // Whether AI Resynthesis can be chosen asks ONNX Runtime on Windows; with
    // no runtime the option is not offered at all.
#if JUCE_WINDOWS
    CHECK(getAiResynthesisSupport() == AiResynthesisSupport::Unavailable);
#else
    getAiResynthesisSupport();
#endif
    {
        Vocoder vocoder;
        CHECK(!Vocoder::isOnnxRuntimeAvailable());
        CHECK(!vocoder.loadModel(juce::File("/nonexistent/pc_nsf_hifigan.onnx")));
        CHECK(!vocoder.isLoaded());
    }

    // Analysis stops before any model is touched.
    {
        EditorController controller(false);
        Project project;
        auto &audio = project.getAudioData();
        audio.sampleRate = 44100;
        audio.waveform.setSize(1, 44100);
        audio.waveform.clear();

        bool progressReported = false;
        bool completed = false;
        controller.analyzeAudio(
            project, [&](double, const juce::String &) { progressReported = true; },
            [&] { completed = true; });
        CHECK(!progressReported);
        CHECK(!completed);
    }

    OnnxRuntime::forceUnavailableForTesting({});
    Ort::InitApi(OrtGetApiBase()->GetApi(ORT_API_VERSION));
    CHECK(OnnxRuntime::isAvailable());

    std::cout << "OnnxRuntimeAvailabilityTests passed\n";
    return 0;
}
