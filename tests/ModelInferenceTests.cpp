// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

// Loads the models Kinu Tune ships and checks, with numbers, that analysis and
// synthesis work on the platform this runs on. The other tests load no model,
// so until this one existed nothing had ever run a model on Windows except a
// user's DAW. The CI Windows job builds and runs only this test.
//
// A two-second harmonic tone at 220 Hz (A3, MIDI 57) goes through:
//   1. FCPE          the pitch detector must report 220 Hz
//   2. GAME          note segmentation must find a note at MIDI 57
//   3. the vocoder   AI resynthesis of the tone's mel spectrogram with an F0
//                    a minor third higher must sound at 261.6 Hz (measured
//                    with FCPE), both through infer() and inferWithPitchShift()
//   4. PSOLA         the Classic engine shifting the tone by the same interval
//                    must also sound at 261.6 Hz
//   5. the panel     AI Resynthesis must be selectable on this platform
//                    (getAiResynthesisSupport is not Unavailable)
//
// argv[1] is the models folder (Resources/models). On macOS the vocoder uses
// the Core ML model when it sits next to pc_nsf_hifigan.onnx and the ONNX
// model otherwise, so the test covers whichever backend the folder provides.

#include "../Source/Audio/FCPEPitchDetector.h"
#include "../Source/Audio/GAMEDetector.h"
#include "../Source/Audio/Synthesis/PsolaSynthesizer.h"
#include "../Source/Audio/SynthesisEngineType.h"
#include "../Source/Audio/Vocoder.h"
#include "../Source/Utils/Constants.h"
#include "../Source/Utils/MelSpectrogram.h"
#include "../Source/Utils/OnnxRuntime.h"
#include "TestAssert.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr int kSampleRate = 44100;
constexpr double kSourceHz = 220.0; // A3
constexpr double kSourceMidi = 57.0;
constexpr double kSeconds = 2.0;
constexpr double kShiftSemitones = 3.0; // a minor third, as the harmony buttons use
constexpr double kTolerancePercent = 3.0; // about half a semitone

double shifted(double hz) { return hz * std::pow(2.0, kShiftSemitones / 12.0); }

// A tone with harmonics, a slow vibrato, a little tremolo and a soft start and
// end: near enough to a sung vowel for the models, which were trained on
// singing (a bare steady sine is "not a voice" to the note segmenter).
std::vector<float> makeTone(double hz)
{
    const auto numSamples = static_cast<size_t>(kSeconds * kSampleRate);
    std::vector<float> tone(numSamples);
    const double fade = 0.05 * kSampleRate;
    const double pi = juce::MathConstants<double>::pi;
    constexpr double vibratoRateHz = 5.5;
    constexpr double vibratoDepthCents = 40.0;
    constexpr double tremoloDepth = 0.1;
    double phase = 0.0; // of the fundamental, advanced sample by sample so the vibrato is a true frequency modulation
    for (size_t i = 0; i < numSamples; ++i)
    {
        const double t = static_cast<double>(i) / kSampleRate;
        const double cents = vibratoDepthCents * std::sin(2.0 * pi * vibratoRateHz * t);
        const double instantHz = hz * std::pow(2.0, cents / 1200.0);
        phase += 2.0 * pi * instantHz / kSampleRate;
        double value = 0.0;
        for (int harmonic = 1; harmonic <= 8; ++harmonic)
            value += std::sin(phase * harmonic) / harmonic;
        value *= 1.0 - tremoloDepth * 0.5 * (1.0 - std::cos(2.0 * pi * vibratoRateHz * t));
        double envelope = 1.0;
        if (static_cast<double>(i) < fade)
            envelope = static_cast<double>(i) / fade;
        else if (static_cast<double>(numSamples - 1 - i) < fade)
            envelope = static_cast<double>(numSamples - 1 - i) / fade;
        tone[i] = static_cast<float>(0.25 * envelope * value);
    }
    return tone;
}

// The median of the voiced frames: one number for "what pitch is this".
float voicedMedianHz(const std::vector<float> &f0)
{
    std::vector<float> voiced;
    for (float hz : f0)
        if (hz > 0.0f)
            voiced.push_back(hz);
    if (voiced.empty())
        return 0.0f;
    std::sort(voiced.begin(), voiced.end());
    return voiced[voiced.size() / 2];
}

size_t voicedCount(const std::vector<float> &f0)
{
    return static_cast<size_t>(std::count_if(f0.begin(), f0.end(), [](float hz) { return hz > 0.0f; }));
}

bool withinPercent(double value, double target, double percent)
{
    return std::abs(value - target) <= target * percent / 100.0;
}

float peak(const std::vector<float> &audio)
{
    float result = 0.0f;
    for (float sample : audio)
        result = std::max(result, std::abs(sample));
    return result;
}

// Pitch of a rendered signal, measured the same way the plug-in analyses audio.
float measureHz(FCPEPitchDetector &fcpe, const std::vector<float> &audio, int sampleRate)
{
    const auto f0 = fcpe.extractF0(audio.data(), static_cast<int>(audio.size()), sampleRate);
    return voicedMedianHz(f0);
}
} // namespace

int main(int argc, char **argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    CHECK(argc >= 2); // the models folder
    const juce::File models(juce::String::fromUTF8(argv[1]));
    CHECK(models.isDirectory());

    CHECK(OnnxRuntime::useLinkedRuntimeForTesting());
    CHECK(OnnxRuntime::isAvailable());

    const auto tone = makeTone(kSourceHz);

    // 1. FCPE finds the tone's pitch.
    FCPEPitchDetector fcpe;
    CHECK(fcpe.loadModel(models.getChildFile("fcpe.onnx"),
                         models.getChildFile("mel_filterbank.bin"),
                         models.getChildFile("cent_table.bin")));
    const auto f0 = fcpe.extractF0(tone.data(), static_cast<int>(tone.size()), kSampleRate);
    const float detectedHz = voicedMedianHz(f0);
    std::printf("FCPE: %zu frames, %zu voiced, median %.1f Hz (expected %.1f)\n",
                f0.size(), voicedCount(f0), detectedHz, kSourceHz);
    CHECK(!f0.empty());
    CHECK(voicedCount(f0) * 2 > f0.size());
    CHECK(withinPercent(detectedHz, kSourceHz, kTolerancePercent));

    // 2. GAME segments the tone into a note at MIDI 57.
    GAMEDetector game;
    CHECK(game.loadModels(models.getChildFile("GAME")));
    const auto notes = game.detectNotes(tone.data(), static_cast<int>(tone.size()), kSampleRate);
    for (const auto &note : notes)
        std::printf("GAME event: frames %d-%d MIDI %.2f%s\n", note.startFrame, note.endFrame,
                    note.midiNote, note.isRest ? " (rest)" : "");
    const GAMEDetector::NoteEvent *longest = nullptr;
    for (const auto &note : notes)
        if (!note.isRest && (longest == nullptr ||
                             note.endFrame - note.startFrame > longest->endFrame - longest->startFrame))
            longest = &note;
    std::printf("GAME: %zu events, longest note MIDI %.2f over frames %d-%d (expected %.0f)\n",
                notes.size(), longest ? longest->midiNote : 0.0f,
                longest ? longest->startFrame : 0, longest ? longest->endFrame : 0, kSourceMidi);
    CHECK(longest != nullptr);
    CHECK(std::abs(longest->midiNote - kSourceMidi) <= 1.0);

    // 3. The vocoder resynthesises the tone at the F0 it is given.
    Vocoder vocoder;
    CHECK(vocoder.loadModel(models.getChildFile("pc_nsf_hifigan.onnx")));
    MelSpectrogram melComputer(kSampleRate, N_FFT, HOP_SIZE, NUM_MELS, FMIN, FMAX);
    const auto mel = melComputer.compute(tone.data(), static_cast<int>(tone.size()));
    CHECK(!mel.empty());
    const double targetHz = shifted(kSourceHz);

    const std::vector<float> f0Target(mel.size(), static_cast<float>(targetHz));
    const auto resynth = vocoder.infer(mel, f0Target);
    const float resynthHz = measureHz(fcpe, resynth, vocoder.getSampleRate());
    std::printf("Vocoder infer: %zu samples from %zu frames, peak %.3f, %.1f Hz (expected %.1f)\n",
                resynth.size(), mel.size(), peak(resynth), resynthHz, targetHz);
    CHECK(!resynth.empty());
    CHECK(peak(resynth) > 0.01f);
    CHECK(withinPercent(resynthHz, targetHz, kTolerancePercent));

    const std::vector<float> f0Source(mel.size(), static_cast<float>(kSourceHz));
    const auto pitchShifted = vocoder.inferWithPitchShift(mel, f0Source, static_cast<float>(kShiftSemitones));
    const float pitchShiftedHz = measureHz(fcpe, pitchShifted, vocoder.getSampleRate());
    std::printf("Vocoder inferWithPitchShift(+%.0f): %zu samples, %.1f Hz (expected %.1f)\n",
                kShiftSemitones, pitchShifted.size(), pitchShiftedHz, targetHz);
    CHECK(!pitchShifted.empty());
    CHECK(withinPercent(pitchShiftedHz, targetHz, kTolerancePercent));

    // 4. PSOLA (the Classic engine) shifts the tone by the same interval.
    DspSynthesisRequest request;
    request.source = tone;
    request.sourceStartSample = 0;
    request.hopSize = HOP_SIZE;
    request.sampleRate = kSampleRate;
    request.numOutputFrames = static_cast<int>(tone.size()) / HOP_SIZE;
    const int sourceFrames = request.numOutputFrames + 2;
    request.sourceF0.assign(static_cast<size_t>(sourceFrames), static_cast<float>(kSourceHz));
    request.sourceVoiced.assign(static_cast<size_t>(sourceFrames), 1);
    request.sourceFrame.resize(static_cast<size_t>(request.numOutputFrames));
    for (int frame = 0; frame < request.numOutputFrames; ++frame)
        request.sourceFrame[static_cast<size_t>(frame)] = static_cast<double>(frame);
    request.pitchRatio.assign(static_cast<size_t>(request.numOutputFrames),
                              static_cast<float>(targetHz / kSourceHz));
    const auto psola = PsolaSynthesizer::render(request);
    const float psolaHz = measureHz(fcpe, psola, kSampleRate);
    std::printf("PSOLA: %zu samples (expected %d), peak %.3f, %.1f Hz (expected %.1f)\n",
                psola.size(), request.numOutputFrames * HOP_SIZE, peak(psola), psolaHz, targetHz);
    CHECK(psola.size() == static_cast<size_t>(request.numOutputFrames * HOP_SIZE));
    CHECK(peak(psola) > 0.01f);
    CHECK(withinPercent(psolaHz, targetHz, kTolerancePercent));

    // 5. The vocoder that just ran can also be chosen on the right panel. The
    //    Windows release is CPU-only, and its AI option used to be disabled
    //    while still the default, so Classic, once picked, could not be undone.
    const auto support = getAiResynthesisSupport();
    std::printf("AI Resynthesis support: %s\n",
                support == AiResynthesisSupport::Accelerated ? "accelerated"
                : support == AiResynthesisSupport::CpuOnly   ? "CPU only"
                                                             : "unavailable");
#if JUCE_MAC
    CHECK(support == AiResynthesisSupport::Accelerated);
#elif JUCE_WINDOWS
    CHECK(support != AiResynthesisSupport::Unavailable);
#endif

    std::printf("ModelInferenceTests: analysis and synthesis work on this platform\n");
    return 0;
}
