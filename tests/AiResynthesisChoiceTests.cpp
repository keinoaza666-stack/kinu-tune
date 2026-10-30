// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

// The engine radios on the right panel must never leave the user stuck: an AI
// Resynthesis option that cannot be clicked must not stay selected, or picking
// Classic once leaves no way back (the Windows report of 2026-10-30). And where
// the vocoder runs on the CPU only - the Windows release - it stays selectable.

#include "../Source/UI/ParameterPanel.h"
#include "../Source/Utils/Localization.h"
#include "TestAssert.h"

namespace
{
juce::ToggleButton *findToggle(juce::Component &root, const juce::String &text)
{
    for (auto *child : root.getChildren())
    {
        if (auto *toggle = dynamic_cast<juce::ToggleButton *>(child))
            if (toggle->getButtonText() == text)
                return toggle;
        if (auto *found = findToggle(*child, text))
            return found;
    }
    return nullptr;
}

// triggerClick goes through the same isEnabled() gate a mouse click does
// (juce::Button drops both on a disabled button) and is delivered as a message.
void click(juce::ToggleButton &button)
{
    button.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
}

// What MainComponent wires up: the saved engine first, then the callback that
// saves a change, then whether AI Resynthesis can run here.
struct Panel
{
    ParameterPanel panel;
    SynthesisEngineType saved;
    int notifications = 0;
    juce::ToggleButton *ai = nullptr;
    juce::ToggleButton *classic = nullptr;

    Panel(SynthesisEngineType fromSettings, AiResynthesisSupport support) : saved(fromSettings)
    {
        panel.setSynthesisEngine(fromSettings);
        panel.onSynthesisEngineChanged = [this](SynthesisEngineType type)
        {
            saved = type;
            ++notifications;
        };
        panel.setAiResynthesisSupport(support);
        ai = findToggle(panel, TR("param.engine_vocoder"));
        classic = findToggle(panel, TR("param.engine_psola"));
        CHECK(ai != nullptr);
        CHECK(classic != nullptr);
    }
};
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    // CPU only (the Windows release): AI starts selected where it is the
    // default, Classic can be picked, and AI can be picked again.
    {
        Panel p(SynthesisEngineType::Vocoder, AiResynthesisSupport::CpuOnly);
        CHECK(p.ai->isEnabled());
        CHECK(p.panel.getSynthesisEngine() == SynthesisEngineType::Vocoder);
        CHECK(p.notifications == 0);
        // The usual description plus the CPU note, which must be translated
        // (TR returns the key itself when it is missing).
        CHECK(TR("tooltip.engine_vocoder_cpu_note") != "tooltip.engine_vocoder_cpu_note");
        CHECK(p.ai->getTooltip() == TR("tooltip.engine_vocoder") + "\n" +
                                        TR("tooltip.engine_vocoder_cpu_note"));

        click(*p.classic);
        CHECK(p.panel.getSynthesisEngine() == SynthesisEngineType::Psola);
        CHECK(p.saved == SynthesisEngineType::Psola);

        click(*p.ai);
        CHECK(p.panel.getSynthesisEngine() == SynthesisEngineType::Vocoder);
        CHECK(p.saved == SynthesisEngineType::Vocoder);
        CHECK(p.ai->getToggleState());
        CHECK(!p.classic->getToggleState());
    }

    // Reopened after Classic was saved: AI can still be picked.
    {
        Panel p(SynthesisEngineType::Psola, AiResynthesisSupport::CpuOnly);
        CHECK(p.panel.getSynthesisEngine() == SynthesisEngineType::Psola);
        click(*p.ai);
        CHECK(p.panel.getSynthesisEngine() == SynthesisEngineType::Vocoder);
        CHECK(p.saved == SynthesisEngineType::Vocoder);
    }

    // Accelerated (macOS, Windows with DirectML): selectable, plain tooltip.
    {
        Panel p(SynthesisEngineType::Vocoder, AiResynthesisSupport::Accelerated);
        CHECK(p.ai->isEnabled());
        CHECK(p.panel.getSynthesisEngine() == SynthesisEngineType::Vocoder);
        CHECK(p.notifications == 0);
        CHECK(p.ai->getTooltip() == TR("tooltip.engine_vocoder"));
    }

    // Unavailable (ONNX Runtime not loaded, Linux): an AI choice that cannot
    // run is moved to Classic, and the move is reported so that it is saved
    // and reaches the renderer.
    {
        Panel p(SynthesisEngineType::Vocoder, AiResynthesisSupport::Unavailable);
        CHECK(!p.ai->isEnabled());
        CHECK(p.panel.getSynthesisEngine() == SynthesisEngineType::Psola);
        CHECK(!p.ai->getToggleState());
        CHECK(p.classic->getToggleState());
        CHECK(p.saved == SynthesisEngineType::Psola);
        CHECK(p.notifications == 1);
        CHECK(p.ai->getTooltip() == TR("tooltip.engine_vocoder_unavailable"));

        // A later silent request for the vocoder (the controller syncing the
        // panel) is moved and reported the same way.
        p.panel.setSynthesisEngine(SynthesisEngineType::Vocoder);
        CHECK(p.panel.getSynthesisEngine() == SynthesisEngineType::Psola);
        CHECK(p.notifications == 2);

        // Clicking the disabled option changes nothing.
        click(*p.ai);
        CHECK(p.panel.getSynthesisEngine() == SynthesisEngineType::Psola);
        CHECK(p.notifications == 2);
    }

    std::printf("AiResynthesisChoiceTests passed\n");
    return 0;
}
