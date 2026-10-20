#include "PhaserPedalProcessor.h"
#include "Effects/FastMath.h"
#include "Effects/IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    double triangle (double phase) { return phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase; } // -1..1
}

PhaserPedalProcessor::PhaserPedalProcessor (PhaserModel m) : model (m)
{
    juce::String prefix, name;
    switch (model)
    {
        case PhaserModel::phase90: prefix = "phase90"; name = "Phase 90"; break;
        case PhaserModel::phase45: prefix = "phase45"; name = "Phase 45"; break;
        case PhaserModel::smallStone: prefix = "smallstone"; name = "Small Stone"; break;
        case PhaserModel::ps1: prefix = "ps1"; name = "PS-1"; break;
        case PhaserModel::biPhase: prefix = "biphase"; name = "Bi-Phase"; break;
        case PhaserModel::uniVibe: prefix = "univibe905"; name = "Uni-Vibe 905"; break;
        case PhaserModel::pt909: prefix = "pt909"; name = "PT-909"; break;
        case PhaserModel::microFazer: prefix = "microfazer"; name = "Micro Fazer"; break;
    }
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (prefix, name, "|");

    switch (model)
    {
        case PhaserModel::phase90:
            rate = add (prefix + "_speed", "Speed", 0.4f);
            modeSwitch = add (prefix + "_logo", "Circuit", 0.0f, 2, { "Script", "Block" });
            break;
        case PhaserModel::phase45:
        case PhaserModel::microFazer:
            rate = add (prefix + "_speed", "Speed", 0.4f);
            break;
        case PhaserModel::smallStone:
            rate = add (prefix + "_rate", "Rate", 0.4f);
            modeSwitch = add (prefix + "_color", "Color", 0.0f, 2, { "Off", "On" });
            break;
        case PhaserModel::ps1:
            modeSwitch = add (prefix + "_speed", "Speed", 1.0f, 3, { "Slow", "Medium", "Fast" });
            break;
        case PhaserModel::biPhase:
            rate = add (prefix + "_rate", "Rate", 0.4f);
            depth = add (prefix + "_depth", "Depth", 0.8f);
            feedback = add (prefix + "_feedback", "Feedback", 0.0f, 2, { "Off", "On" });
            shape = add (prefix + "_shape", "Sweep", 0.0f, 2, { "Sine", "Square" });
            modeSwitch = add (prefix + "_mode", "Mode", 0.0f, 2, { "Stereo", "Series" });
            break;
        case PhaserModel::uniVibe:
            rate = add (prefix + "_speed", "Speed", 0.4f);
            depth = add (prefix + "_intensity", "Intensity", 0.7f);
            modeSwitch = add (prefix + "_mode", "Mode", 0.0f, 2, { "Chorus", "Vibrato" });
            break;
        case PhaserModel::pt909:
            rate = add (prefix + "_speed", "Speed", 0.4f);
            depth = add (prefix + "_width", "Width", 0.7f);
            feedback = add (prefix + "_feedback", "Feedback", 0.3f);
            break;
    }
}

const char* PhaserPedalProcessor::getName() const
{
    switch (model)
    {
        case PhaserModel::phase90: return "Phase 90";
        case PhaserModel::phase45: return "Phase 45";
        case PhaserModel::smallStone: return "Small Stone";
        case PhaserModel::ps1: return "PS-1";
        case PhaserModel::biPhase: return "Bi-Phase";
        case PhaserModel::uniVibe: return "Uni-Vibe 905";
        case PhaserModel::pt909: return "PT-909";
        case PhaserModel::microFazer: return "Micro Fazer";
    }
    return "Phaser";
}

juce::AudioParameterFloat* PhaserPedalProcessor::add (const juce::String& id, const juce::String& name, float def, int positions,
                                                      juce::StringArray labels)
{
    std::unique_ptr<juce::AudioParameterFloat> p;
    if (positions >= 2)
        p = std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, (float) (positions - 1), 1.0f), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([labels] (float v, int)
            { return labels[juce::jlimit (0, labels.size() - 1, juce::roundToInt (v))]; }));
    else
        p = std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    auto* raw = p.get();
    parameters->addChild (std::move (p));
    return raw;
}

void PhaserPedalProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    caps.fill (47.0e-9);
    feedbackStage = 0;
    switch (model)
    {
        case PhaserModel::phase90:
            // 2N5952: Idss 4-8 mA, cut-off -1.3..-3.5 V (ElectroSmash). The trimmer sets the LFO so Vgs swings
            // from near 0 toward cut-off.
            numStages = 4; fixedOhms = 24.0e3; jfet = { 6.0e-3, -2.5 };
            rateMin = 0.135; rateMax = 6.5; feedbackStage = 1; // R28 joins stage 2's input
            break;
        case PhaserModel::phase45:
            numStages = 2; fixedOhms = 10.0e3; jfet = { 6.0e-3, -2.5 };
            rateMin = 0.08; rateMax = 5.0;
            break;
        case PhaserModel::smallStone:
            numStages = 4; caps.fill (6.8e-9); rateMin = 0.05; rateMax = 9.0;
            break;
        case PhaserModel::ps1:
            numStages = 6; caps.fill (10.0e-9); fixedOhms = 100.0e3; jfet = { 2.0e-3, -2.0 };
            break;
        case PhaserModel::biPhase:
            numStages = 6; caps.fill (47.0e-9); rateMin = 0.05; rateMax = 10.0;
            break;
        case PhaserModel::uniVibe:
            numStages = 4; caps = { 15.0e-9, 220.0e-9, 470.0e-12, 4.7e-9, 0.0, 0.0 }; rateMin = 0.6; rateMax = 10.0;
            break;
        case PhaserModel::pt909:
            numStages = 4; fixedOhms = 30.0e3; jfet = { 3.0e-3, -1.5 };
            rateMin = 0.1; rateMax = 8.0;
            break;
        case PhaserModel::microFazer:
            numStages = 4; caps.fill (50.0e-9); fixedOhms = 100.0e3; jfet = { 3.0e-3, -1.5 };
            rateMin = 0.34; rateMax = 13.0; // Rate 1M log + 27K into 4.7u, Schmitt 220K / 33K
            break;
    }
    // The bias trimmer on every one of these pedals sets the LFO so the FETs work near cut-off, where their resistance
    // is comparable to the fixed resistor across them: centre at 0.76 Vp, swinging +-0.22 |Vp|.
    vgsCentre = 0.76 * jfet.vp;
    vgsSwing = 0.22 * std::abs (jfet.vp);
    for (auto& ch : channels)
        for (auto* chain : { &ch.a, &ch.b })
            for (auto& l : chain->ldr)
            {
                if (model == PhaserModel::uniVibe)
                    l.prepare (newSampleRate, 0.04, 0.012, 0.09); // a 6.3 V lamp, then the cell
                else
                    l.prepare (newSampleRate, 0.0, 0.004, 0.03);  // the Bi-Phase's LED-lit opto cells
            }
    reset();
}

void PhaserPedalProcessor::reset()
{
    for (auto& ch : channels)
        for (auto* chain : { &ch.a, &ch.b })
        {
            for (auto& s : chain->stages)
                s.reset();
            for (auto& l : chain->ldr)
                l.reset (0.3);
            chain->lastWet = 0.0;
        }
    phaseA = 0.0;
    phaseB = 0.25;
    shortcut.reset();
}

double PhaserPedalProcessor::elementOhms (Chain& chain, int stage, double sweep, double vds) noexcept
{
    switch (model)
    {
        case PhaserModel::smallStone:
        {
            // CA3094: the OTA's gm follows its bias current, which the LFO sets through a transistor -- close to
            // exponential in the LFO voltage. As a variable resistor the stage behaves like R = k / gm.
            const double corner = 90.0 * std::pow (45.0, 0.5 * (sweep + 1.0)); // ~90 Hz .. 4 kHz with 6.8n
            return 1.0 / (2.0 * juce::MathConstants<double>::pi * corner * caps[(size_t) stage]);
        }
        case PhaserModel::biPhase:
            return chain.ldr[(size_t) stage].process (0.5 * (sweep + 1.0), 1.5e3, 60.0e3);
        case PhaserModel::uniVibe:
            return chain.ldr[(size_t) stage].process (lampDrive, 4.0e3, 500.0e3);
        case PhaserModel::phase90:
        case PhaserModel::phase45:
        case PhaserModel::ps1:
        case PhaserModel::pt909:
        case PhaserModel::microFazer:
        default:
        {
            const double r = jfet.ohms (vgsCentre + vgsSwing * sweep, vds);
            return fixedOhms * r / (fixedOhms + r);
        }
    }
}

double PhaserPedalProcessor::runChain (Chain& chain, double x, double sweep, double fb, bool) noexcept
{
    double y = x;
    // Opto cells under one lamp / LED see the same light and are the same part: one cell's resistance serves them all.
    const bool opto = model == PhaserModel::biPhase || model == PhaserModel::uniVibe;
    const double optoOhms = opto ? elementOhms (chain, 0, sweep, 0.0) : 0.0;
    for (int k = 0; k < numStages; ++k)
    {
        if (k == feedbackStage && std::abs (fb) > 0.0)
            y += fb * chain.lastWet;
        // Vds across a JFET = the signal at the stage's RC node (its capacitor's far side), about the bias.
        const double r = opto ? optoOhms : elementOhms (chain, k, sweep, chain.stages[(size_t) k].state() * 0.5);
        if (k == 0)
            lastCorner = 1.0 / (2.0 * juce::MathConstants<double>::pi * r * caps[0]);
        y = chain.stages[(size_t) k].process (y, r, caps[(size_t) k], sampleRate);
    }
    chain.lastWet = y;
    return y;
}

void PhaserPedalProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    double hz;
    if (model == PhaserModel::ps1)
    {
        static constexpr double speeds[3] { 0.15, 0.75, 5.5 };
        hz = speeds[juce::jlimit (0, 2, juce::roundToInt (modeSwitch->get()))];
    }
    else
        hz = rateMin * std::pow (rateMax / rateMin, (double) rate->get());

    double fb = 0.0;
    if (model == PhaserModel::phase90 && modeSwitch->get() >= 0.5f) fb = 10.0 / 24.0;            // R28 (Block logo)
    if (model == PhaserModel::smallStone && modeSwitch->get() >= 0.5f) fb = 0.62;                 // Color
    if (model == PhaserModel::pt909) fb = 0.75 * feedback->get();
    if (model == PhaserModel::biPhase && feedback->get() >= 0.5f) fb = 0.55;
    const double dep = depth != nullptr ? (double) depth->get() : 1.0;
    const bool squareSweep = shape != nullptr && shape->get() >= 0.5f;
    const bool vibrato = model == PhaserModel::uniVibe && modeSwitch->get() >= 0.5f;
    const bool series = model == PhaserModel::biPhase && modeSwitch->get() >= 0.5f;
    const bool stereo = model == PhaserModel::biPhase && ! series;

    const int solve = stereo ? 1 : shortcut.begin (channels, buffer, sampleRate);
    auto* left = buffer.getWritePointer (0);
    auto* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        phaseA += hz / sampleRate;
        if (phaseA >= 1.0) phaseA -= 1.0;
        phaseB += hz / sampleRate;
        if (phaseB >= 1.0) phaseB -= 1.0;

        double sweepA, sweepB;
        if (model == PhaserModel::biPhase)
        {
            auto wave = [squareSweep] (double p) { return squareSweep ? (p < 0.5 ? 1.0 : -1.0) : fast::sinCycles (p); };
            sweepA = dep * wave (phaseA);
            sweepB = dep * wave (phaseB); // the second sweep generator, a quarter cycle on
        }
        else if (model == PhaserModel::uniVibe)
        {
            // Phase-shift oscillator: a sine, slightly lopsided; Intensity scales the lamp drive.
            const double s = fast::sinCycles (phaseA);
            lampDrive = 0.08 + 0.85 * dep * 0.5 * (1.0 + s + 0.12 * s * s - 0.12);
            sweepA = sweepB = 0.0;
        }
        else
            sweepA = sweepB = triangle (phaseA);

        if (stereo)
        {
            const double x = right != nullptr ? 0.5 * ((double) left[i] + (double) right[i]) : (double) left[i];
            const double wa = runChain (channels[0].a, x, sweepA, fb, false);
            const double wb = runChain (channels[0].b, x, sweepB, fb, true);
            left[i] = (float) (0.5 * (x + wa));
            if (right != nullptr)
                right[i] = (float) (0.5 * (x + wb));
            continue;
        }

        for (int c = 0; c < solve; ++c)
        {
            auto& ch = channels[(size_t) c];
            auto* data = buffer.getWritePointer (c);
            const double x = data[i];
            double wet = runChain (ch.a, x, sweepA, fb, false);
            if (series)
                wet = runChain (ch.b, wet, sweepB, fb, true);
            data[i] = (float) (vibrato ? wet : 0.5 * (x + wet));
        }
    }

    if (stereo)
    {
        for (int c = 2; c < numChannels; ++c)
            buffer.copyFrom (c, 0, buffer, 0, 0, numSamples);
    }
    else
        shortcut.end (buffer);
}

void PhaserPedalProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> phaserIcon = icon::loadSvg (IconData::phaser_svg, IconData::phaser_svgSize);
    static const std::unique_ptr<juce::Drawable> vibeIcon = icon::loadSvg (IconData::uni_vibe_svg, IconData::uni_vibe_svgSize);
    icon::drawSvg (g, b, model == PhaserModel::uniVibe ? vibeIcon.get() : phaserIcon.get());
}

} // namespace openguitarmultifx
