#include "OctaveFuzzProcessor.h"
#include "Effects/IconKit.h"
#include "Effects/PotTaper.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    using namespace analog;

    constexpr double supply = 9.0;
    // 1N4148: Is 2.52 nA, n 1.752. 1N34A germanium: Is ~0.2 uA, n ~1.3. Small-signal NPN/PNP junctions: Is 1e-14, n 1.
    constexpr double siIs = 2.52e-9, siNVt = 1.752 * 0.02585, geIs = 2.0e-7, geNVt = 1.3 * 0.02585;
    const NodalCircuit::BjtParams smallSignal { 1.0e-14, 0.02585, 300.0, 4.0, 0.0, 0.0, 0.0 };
    const NodalCircuit::BjtParams darlington { 1.0e-14, 0.02585, 5000.0, 4.0, 0.0, 0.0, 0.0 }; // MPS-A13: beta ~5000
    constexpr double superFuzzDrivePot = 50.0e3;
}

OctaveFuzzProcessor::OctaveFuzzProcessor (OctaveFuzzModel m) : model (m)
{
    juce::String prefix, name;
    switch (model)
    {
        case OctaveFuzzModel::octavia: prefix = "octavia"; name = "Octavia"; break;
        case OctaveFuzzModel::greenRinger: prefix = "greenringer"; name = "Green Ringer"; break;
        case OctaveFuzzModel::superFuzz: prefix = "superfuzz"; name = "Super-Fuzz"; break;
        case OctaveFuzzModel::toneMachine: prefix = "tonemachine"; name = "Tone Machine"; break;
    }
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (prefix, name, "|");
    switch (model)
    {
        case OctaveFuzzModel::octavia:
            drive = add (prefix + "_boost", "Boost", 0.7f);
            level = add (prefix + "_level", "Level", 0.5f);
            break;
        case OctaveFuzzModel::greenRinger:
            level = add (prefix + "_level", "Level", 0.5f);
            break;
        case OctaveFuzzModel::superFuzz:
            drive = add (prefix + "_expander", "Expander", 0.7f);
            level = add (prefix + "_volume", "Volume", 0.5f);
            toggle = add (prefix + "_tone", "Tone", 0.0f, 2, { "Normal", "Notch" });
            break;
        case OctaveFuzzModel::toneMachine:
            drive = add (prefix + "_sustain", "Sustain", 0.6f);
            tone = add (prefix + "_tone", "Tone", 0.5f);
            level = add (prefix + "_volume", "Volume", 0.5f);
            toggle = add (prefix + "_octave", "Octave", 1.0f, 2, { "Off", "On" });
            break;
    }
}

const char* OctaveFuzzProcessor::getName() const
{
    switch (model)
    {
        case OctaveFuzzModel::octavia: return "Octavia";
        case OctaveFuzzModel::greenRinger: return "Green Ringer";
        case OctaveFuzzModel::superFuzz: return "Super-Fuzz";
        case OctaveFuzzModel::toneMachine: return "Tone Machine";
    }
    return "Octave Fuzz";
}

juce::AudioParameterFloat* OctaveFuzzProcessor::add (const juce::String& id, const juce::String& name, float def, int positions,
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

void OctaveFuzzProcessor::buildChannel (Channel& ch)
{
    auto& c = ch.c;
    const auto gnd = NodalCircuit::ground;
    const auto rail = c.addNode(), in = c.addNode();
    c.addSource (rail, supply);
    ch.srcIn = c.addSource (in, 0.0);

    switch (model)
    {
        case OctaveFuzzModel::octavia:
        {
            // Input gain: the 2N3906 / MPS-A13 feedback pair (gain set by the 10K "RLG" Boost trimmer) reduced to one
            // stage biased mid-rail: 39K load, 3K9 emitter (~0.13 mA, collector ~4.1 V), Boost (10K) in series with 22 u
            // across the emitter resistor -- a gain of ~x13 at minimum to ~x180 at full Boost. Then the MPS-A13 splitter
            // (470K / 330K, 20K / 20K) behind .01 u.
            const auto b1 = c.addNode(), c1 = c.addNode(), e1 = c.addNode(), byp = c.addNode();
            c.addCapacitor (in, b1, 0.01e-6);
            c.addResistor (rail, b1, 470.0e3);
            c.addResistor (b1, gnd, 68.0e3);
            c.addResistor (rail, c1, 39.0e3);
            c.addBjt (c1, b1, e1, false, darlington);
            c.addResistor (e1, gnd, 3.9e3);
            ch.rDriveBypass = c.addResistor (e1, byp, 3.0e3);
            c.addCapacitor (byp, gnd, 22.0e-6);

            const auto b2 = c.addNode(), c2 = c.addNode(), e2 = c.addNode();
            c.addCapacitor (c1, b2, 0.01e-6);
            c.addResistor (rail, b2, 470.0e3);
            c.addResistor (b2, gnd, 330.0e3);
            c.addResistor (rail, c2, 20.0e3);
            c.addBjt (c2, b2, e2, false, darlington);
            c.addResistor (e2, gnd, 20.0e3);

            // 22 u into each 1N4148; the anodes are DC-fed from the splitter's own collector (2.2M) and emitter (820K),
            // so both diodes idle a few uA forward -- no dead zone -- and the higher half wins on the 100K load.
            const auto na = c.addNode(), nb = c.addNode(), nr = c.addNode();
            c.addCapacitor (c2, na, 22.0e-6);
            c.addCapacitor (e2, nb, 22.0e-6);
            c.addResistor (c2, na, 2.2e6);
            c.addResistor (e2, nb, 820.0e3);
            c.addDiode (na, nr, siIs, siNVt);
            c.addDiode (nb, nr, siIs, siNVt);
            c.addResistor (nr, gnd, 100.0e3);
            ch.nRect = nr;
            ch.nStage = c1;
            c.setInitialGuess (b1, 1.14);
            c.setInitialGuess (e1, 0.49);
            c.setInitialGuess (c1, 4.1);
            c.setInitialGuess (b2, 3.7);
            c.setInitialGuess (c2, 6.0);
            c.setInitialGuess (e2, 2.5);
            break;
        }
        case OctaveFuzzModel::greenRinger:
        {
            // Polarity from a working unit's measured voltages (Q1 b/e/c 1.8 / 1.4 / 5.2 V, Q2 b/e/c 5.2 / 5.9 / 3.3 V; the
            // redraw's device labels are mirrored): Q1 an NPN with 560K / 160K on the base, 6K2 emitter, 18K collector
            // (x2.9), its collector straight into a PNP splitter's base (10K emitter to the supply, 10K collector to ground).
            const auto b1 = c.addNode(), c1 = c.addNode(), e1 = c.addNode();
            c.addCapacitor (in, b1, 0.047e-6);
            c.addResistor (rail, b1, 560.0e3);
            c.addResistor (b1, gnd, 160.0e3);
            c.addResistor (rail, c1, 18.0e3);
            c.addBjt (c1, b1, e1, false, smallSignal);
            c.addResistor (e1, gnd, 6.2e3);

            const auto c2 = c.addNode(), e2 = c.addNode();
            c.addResistor (rail, e2, 10.0e3);
            c.addBjt (c2, c1, e2, true, smallSignal);
            c.addResistor (c2, gnd, 10.0e3);

            // The "dual diode" (an NPN's two junctions, low gain): .047 u from each splitter output, each held at 4.5 V by
            // 66K from a 22K / 22K divider, joined at the output follower's base -- whichever half is higher drives it. The
            // follower (10K emitter) is represented by its input resistance, beta x 10K, which also sets the junctions'
            // few-uA idle current (no dead zone).
            const auto ka = c.addNode(), kb = c.addNode(), vb = c.addNode(), nr = c.addNode();
            c.addCapacitor (c2, ka, 0.047e-6);
            c.addCapacitor (e2, kb, 0.047e-6);
            c.addResistor (ka, vb, 66.0e3);
            c.addResistor (kb, vb, 66.0e3);
            c.addResistor (rail, vb, 22.0e3);
            c.addResistor (vb, gnd, 22.0e3);
            c.addDiode (ka, nr, 1.0e-14, 0.02585);
            c.addDiode (kb, nr, 1.0e-14, 0.02585);
            c.addResistor (nr, gnd, 300.0 * 10.0e3);
            ch.nRect = nr;
            ch.nStage = c2;
            c.setInitialGuess (b1, 2.0);
            c.setInitialGuess (e1, 1.4);
            c.setInitialGuess (c1, 5.2);
            c.setInitialGuess (e2, 5.9);
            c.setInitialGuess (c2, 3.3);
            c.setInitialGuess (vb, 4.5);
            c.setInitialGuess (ka, 4.5);
            c.setInitialGuess (kb, 4.5);
            c.setInitialGuess (nr, 4.0);
            break;
        }
        case OctaveFuzzModel::superFuzz:
        {
            // Block A (its own netlist): the input stage, one transistor with the drawn 470K collector-to-base feedback
            // and 100K to ground (0.8 V on the base, as marked), 47K / 1K8. The emitter follower after it is an ideal
            // buffer here; the Expander (Drive, 50K) divides its output into block B.
            auto& a = ch.pre;
            const auto railA = a.addNode(), inA = a.addNode();
            a.addSource (railA, supply);
            ch.srcPre = a.addSource (inA, 0.0);
            const auto x = a.addNode(), b1 = a.addNode(), c1 = a.addNode(), e1 = a.addNode();
            a.addCapacitor (inA, x, 10.0e-6);
            a.addResistor (x, b1, 22.0e3);
            a.addResistor (b1, gnd, 100.0e3);
            a.addResistor (c1, b1, 470.0e3);
            a.addResistor (railA, c1, 47.0e3);
            a.addBjt (c1, b1, e1, false, smallSignal);
            a.addResistor (e1, gnd, 1.8e3);
            a.setInitialGuess (b1, 0.8);
            a.setInitialGuess (c1, 4.7);
            a.setInitialGuess (e1, 0.15);
            ch.nPre = c1;

            // Block B: the Drive pot's Thevenin source, 10 u into the splitter (220K / 150K, 10K / 10K), its two outputs
            // through 10 u + 470R into Q4 / Q5. Those two have their emitters joined on 1K8 (bypassed) and collectors
            // tied on one 10K: each is a common-emitter exponential, driven in antiphase, so the summed collector
            // current goes as cosh (v / Vt) -- the octave (and the compression) of the Super-Fuzz. The Octave Balance
            // trimmer is at its centre: 22K + 5K under each base.
            const auto d = c.addNode(), b3 = c.addNode(), c3 = c.addNode(), e3 = c.addNode();
            ch.rDriveSeries = c.addResistor (in, d, 0.25 * superFuzzDrivePot);
            c.addCapacitor (d, b3, 10.0e-6);
            c.addResistor (rail, b3, 220.0e3);
            c.addResistor (b3, gnd, 150.0e3);
            c.addResistor (rail, c3, 10.0e3);
            c.addBjt (c3, b3, e3, false, smallSignal);
            c.addResistor (e3, gnd, 10.0e3);

            const auto xa = c.addNode(), xb = c.addNode();
            const auto b4 = c.addNode(), b5 = c.addNode(), e45 = c.addNode(), c45 = c.addNode();
            c.addCapacitor (c3, xa, 10.0e-6);
            c.addCapacitor (e3, xb, 10.0e-6);
            c.addResistor (xa, b4, 470.0);
            c.addResistor (xb, b5, 470.0);
            for (auto bq : { b4, b5 })
            {
                c.addResistor (rail, bq, 100.0e3);
                c.addResistor (bq, gnd, 27.0e3);
            }
            c.addResistor (rail, c45, 10.0e3);
            c.addBjt (c45, b4, e45, false, smallSignal);
            c.addBjt (c45, b5, e45, false, smallSignal);
            c.addResistor (e45, gnd, 1.8e3);
            c.addCapacitor (e45, gnd, 10.0e-6);
            ch.nRect = c45;
            ch.nStage = c3;
            c.setInitialGuess (b3, 3.6);
            c.setInitialGuess (c3, 6.0);
            c.setInitialGuess (e3, 3.0);
            c.setInitialGuess (b4, 1.9);
            c.setInitialGuess (b5, 1.9);
            c.setInitialGuess (e45, 1.15);
            c.setInitialGuess (c45, 3.3);
            break;
        }
        case OctaveFuzzModel::toneMachine:
        {
            // As drawn (2N3565s): Q1 47K / 1K, its collector straight into the splitter's base (4K7 / 4K7); Q1 biased by DC
            // feedback from the splitter's emitter through 100K + 100K, the midpoint decoupled by 0.1 u. Q1 idles at ~0.16 mA
            // with its collector at ~1.5 V and the splitter's emitter at ~0.9 V -- the emitter half has under a volt of
            // room below it, so a hard-driven note clips one half early (the pedal's gated, uneven octave).
            const auto b1 = c.addNode(), c1 = c.addNode(), e1 = c.addNode();
            c.addCapacitor (in, b1, 0.1e-6);
            c.addResistor (rail, c1, 47.0e3);
            c.addBjt (c1, b1, e1, false, smallSignal);
            c.addResistor (e1, gnd, 1.0e3);
            const auto c2 = c.addNode(), e2 = c.addNode(), mid = c.addNode();
            c.addResistor (rail, c2, 4.7e3);
            c.addBjt (c2, c1, e2, false, smallSignal);
            c.addResistor (e2, gnd, 4.7e3);
            c.addResistor (e2, mid, 100.0e3);
            c.addResistor (mid, b1, 100.0e3);
            c.addCapacitor (mid, gnd, 0.1e-6);

            // 10 u into each 1N34A, 100K to ground on each, summed on the Octave switch's 100K.
            const auto na = c.addNode(), nb = c.addNode(), nr = c.addNode();
            c.addCapacitor (c2, na, 10.0e-6);
            c.addCapacitor (e2, nb, 10.0e-6);
            c.addResistor (na, gnd, 100.0e3);
            c.addResistor (nb, gnd, 100.0e3);
            c.addDiode (na, nr, geIs, geNVt);
            c.addDiode (nb, nr, geIs, geNVt);
            c.addResistor (nr, gnd, 100.0e3);
            ch.nRect = nr;
            ch.nStage = c2;
            c.setInitialGuess (b1, 0.78);
            c.setInitialGuess (c1, 1.5);
            c.setInitialGuess (c2, 8.1);
            c.setInitialGuess (e2, 0.87);
            break;
        }
    }
}

void OctaveFuzzProcessor::updatePots()
{
    if (drive == nullptr)
        return;
    const double d = drive->get();
    if (std::abs (d - lastDrive) < 1.0e-6)
        return;
    lastDrive = d;
    for (auto& ch : channels)
    {
        if (model == OctaveFuzzModel::octavia && ch.rDriveBypass >= 0)
            ch.c.setResistance (ch.rDriveBypass, std::max (10.0, 10.0e3 * (1.0 - pots::audio (d)))); // Boost: 10K in the bypass
        if (model == OctaveFuzzModel::superFuzz && ch.rDriveSeries >= 0)
        {
            const double f = pots::audio (d);
            driveFraction = f;
            ch.c.setResistance (ch.rDriveSeries, std::max (1.0, f * (1.0 - f) * superFuzzDrivePot)); // the wiper's Thevenin
        }
    }
}

void OctaveFuzzProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
        if (model == OctaveFuzzModel::octavia || model == OctaveFuzzModel::greenRinger)
            ch.post.prepare (couplingHighpass (0.01e-6, 1.0e6), newSampleRate, 0.0); // .01 u into the level pot
        else
            ch.post.prepare (couplingHighpass (10.0e-6, 10.0e3), newSampleRate, 0.0);
        if (model == OctaveFuzzModel::superFuzz)
        {
            // Notch: a ~1 kHz mid scoop when switched in.
            const double w0 = 2.0 * juce::MathConstants<double>::pi * 1000.0, q = 0.7;
            ch.toneLow.prepare (Rational { { w0 * w0, 0.0, 1.0 }, { w0 * w0, w0 / q, 1.0 } }, newSampleRate, 1000.0);
        }
        else if (model == OctaveFuzzModel::toneMachine)
        {
            // Tone pot between a 3300p high-pass leg and a 22K + 4K7 / .047 low-pass leg.
            ch.toneLow.prepare (Rational { { 1.0 }, { 1.0, 26.7e3 * 0.047e-6 } }, newSampleRate, 0.0);
            ch.toneHigh.prepare (couplingHighpass (3300.0e-12, 26.7e3), newSampleRate, 0.0);
        }
    }
    lastDrive = -1.0;
    updatePots();
    for (auto& ch : channels)
    {
        if (model == OctaveFuzzModel::superFuzz)
        {
            ch.pre.prepare (newSampleRate);
            ch.preRest = ch.pre.voltage (ch.nPre);
        }
        ch.c.prepare (newSampleRate);
        ch.stageRest = ch.c.voltage (ch.nStage);
        ch.rectRest = ch.c.voltage (ch.nRect);
    }
    smoothedLevel.reset (newSampleRate, paramRampSeconds);
    smoothedLevel.setCurrentAndTargetValue (level->get());
    for (auto& cl : clippers)
        cl.reset();
    sampleCount = failureCount = 0;
    shortcut.reset();
}

void OctaveFuzzProcessor::reset()
{
    if (sampleRate > 0.0)
        prepare (sampleRate, 0, 0);
}

void OctaveFuzzProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;
    updatePots();
    smoothedLevel.setTargetValue (level->get());
    const int numSamples = buffer.getNumSamples();
    const int solve = shortcut.begin (channels, buffer, sampleRate);
    const bool notchOn = model == OctaveFuzzModel::superFuzz && toggle->get() >= 0.5f;
    const bool octaveOn = model != OctaveFuzzModel::toneMachine || toggle->get() >= 0.5f;
    const double toneMix = tone != nullptr ? (double) tone->get() : 0.5;
    // Tone Machine Sustain: the 50K Drive pot (220R under it) into Q3, 10K / 1K with the emitter bypassed (~x150),
    // whose collector (10K) drives the 1N34A pair to ground.
    const double sustainGain = model == OctaveFuzzModel::toneMachine ? 150.0 * pots::audio (drive->get()) + 0.5 : 1.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const double lvl = pots::audio (smoothedLevel.getNextValue());
        for (int c = 0; c < solve; ++c)
        {
            auto& ch = channels[(size_t) c];
            auto* data = buffer.getWritePointer (c);
            double drive = (double) data[i];
            bool ok = true;
            if (model == OctaveFuzzModel::superFuzz)
            {
                ch.pre.setSource (ch.srcPre, drive);
                ok = ch.pre.solveSample();
                drive = driveFraction * (ch.pre.voltage (ch.nPre) - ch.preRest);
            }
            ch.c.setSource (ch.srcIn, drive);
            ok = ch.c.solveSample() && ok;
            if (c == 0) { ++sampleCount; if (! ok) ++failureCount; }

            double y = octaveOn ? ch.c.voltage (ch.nRect) - ch.rectRest : ch.c.voltage (ch.nStage) - ch.stageRest;
            y = ch.post.process (y);
            if (model == OctaveFuzzModel::toneMachine)
            {
                y = clippers[(size_t) c].process (sustainGain * y);
                y = (1.0 - toneMix) * ch.toneLow.process (y) + toneMix * ch.toneHigh.process (y);
            }
            else if (model == OctaveFuzzModel::superFuzz)
            {
                // The tied collectors (10K) into the anti-parallel 1N34A pair to ground, then Normal / Notch.
                y = clippers[(size_t) c].process (y);
                if (notchOn)
                    y = ch.toneLow.process (y);
            }
            data[i] = (float) (lvl * y);
        }
    }
    shortcut.end (buffer);
}

void OctaveFuzzProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::octaver_svg, IconData::octaver_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
