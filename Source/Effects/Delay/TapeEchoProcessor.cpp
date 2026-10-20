#include "TapeEchoProcessor.h"
#include "Effects/FastMath.h"
#include "Effects/IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double twoPi = 6.283185307179586;

    // Head delays at nominal speed (seconds) and which heads each selector position plays.
    // RE-201: heads at 1 : 2 : 3 of the shortest; Repeat Rate moves the motor so head 1 spans ~65-185 ms.
    constexpr std::array<std::array<bool, 4>, 12> spaceEchoModes { {
        { true, false, false, false }, { false, true, false, false }, { false, false, true, false }, { false, true, true, false },
        { true, false, false, false }, { false, true, false, false }, { false, false, true, false }, { true, true, false, false },
        { false, true, true, false }, { true, false, true, false }, { true, true, true, false }, { false, false, false, false } } };
    // Echorec: four heads equally spaced around the drum, ~75 / 150 / 225 / 300 ms.
    constexpr std::array<std::array<bool, 4>, 12> echorecModes { {
        { true, false, false, false }, { false, true, false, false }, { false, false, true, false }, { false, false, false, true },
        { true, true, false, false }, { false, true, true, false }, { false, false, true, true }, { true, false, true, false },
        { false, true, false, true }, { true, true, true, false }, { false, true, true, true }, { true, true, true, true } } };

    const char* const modeNames[12] { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12" };
}

void TapeEchoProcessor::Shelf::set (double sr, double hz, double gainDb, bool high)
{
    const double a = std::pow (10.0, gainDb / 40.0), w = twoPi * hz / sr, cw = std::cos (w), sw = std::sin (w);
    const double alpha = sw / 2.0 * std::sqrt (2.0), k = 2.0 * std::sqrt (a) * alpha;
    double nb0, nb1, nb2, na0, na1, na2;
    if (high)
    {
        nb0 = a * ((a + 1) + (a - 1) * cw + k); nb1 = -2 * a * ((a - 1) + (a + 1) * cw); nb2 = a * ((a + 1) + (a - 1) * cw - k);
        na0 = (a + 1) - (a - 1) * cw + k;        na1 = 2 * ((a - 1) - (a + 1) * cw);    na2 = (a + 1) - (a - 1) * cw - k;
    }
    else
    {
        nb0 = a * ((a + 1) - (a - 1) * cw + k); nb1 = 2 * a * ((a - 1) - (a + 1) * cw); nb2 = a * ((a + 1) - (a - 1) * cw - k);
        na0 = (a + 1) + (a - 1) * cw + k;        na1 = -2 * ((a - 1) + (a + 1) * cw);   na2 = (a + 1) + (a - 1) * cw - k;
    }
    b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
}

TapeEchoProcessor::TapeEchoProcessor (TapeEchoModel m) : model (m)
{
    switch (model)
    {
        case TapeEchoModel::spaceEcho:
            parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("re201", "Space Echo", "|");
            mode = add ("re201_mode", "Mode", 0.0f, 12);
            speed = add ("re201_rate", "Repeat Rate", 0.5f);
            intensity = add ("re201_intensity", "Intensity", 0.4f);
            echoLevel = add ("re201_echo", "Echo Volume", 0.5f);
            reverbLevel = add ("re201_reverb", "Reverb Volume", 0.3f);
            bass = add ("re201_bass", "Bass", 0.5f);
            treble = add ("re201_treble", "Treble", 0.5f);
            spring = std::make_unique<SpringReverbProcessor>();
            break;
        case TapeEchoModel::echoplex:
            parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("ep3", "Echoplex EP-3", "|");
            speed = add ("ep3_delay", "Echo Delay", 0.4f);
            intensity = add ("ep3_repeats", "Repeats", 0.35f);
            echoLevel = add ("ep3_echo", "Echo Volume", 0.5f);
            recordLevel = add ("ep3_record", "Record Volume", 0.6f);
            preamp = std::make_unique<EPStyleBoosterProcessor>();
            break;
        case TapeEchoModel::echorec:
            parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("echorec", "Echorec", "|");
            mode = add ("echorec_selector", "Selector", 0.0f, 12);
            intensity = add ("echorec_swell", "Swell", 0.4f);
            echoLevel = add ("echorec_echo", "Echo Volume", 0.5f);
            bass = add ("echorec_bass", "Bass", 0.5f);
            treble = add ("echorec_treble", "Treble", 0.5f);
            break;
    }
}

const char* TapeEchoProcessor::getName() const
{
    switch (model)
    {
        case TapeEchoModel::spaceEcho: return "Space Echo";
        case TapeEchoModel::echoplex: return "Echoplex EP-3";
        case TapeEchoModel::echorec: return "Echorec";
    }
    return "Tape Echo";
}

juce::AudioParameterFloat* TapeEchoProcessor::add (const juce::String& id, const juce::String& name, float def, int positions)
{
    std::unique_ptr<juce::AudioParameterFloat> p;
    if (positions >= 2)
        p = std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, (float) (positions - 1), 1.0f), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (modeNames[juce::jlimit (0, 11, juce::roundToInt (v))]); }));
    else
        p = std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    auto* raw = p.get();
    parameters->addChild (std::move (p));
    return raw;
}

double TapeEchoProcessor::saturate (double x) noexcept
{
    // Magnetic saturation: linear for normal levels, rounding smoothly into the tape's maximum flux (~1.5 V here).
    const double m = 1.5;
    return x / std::sqrt (1.0 + (x * x) / (m * m));
}

void TapeEchoProcessor::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = newSampleRate;
    for (auto& ch : channels)
    {
        ch.tape.prepare (newSampleRate, model == TapeEchoModel::echoplex ? 0.85 : 0.65);
        ch.headLoss.prepare (newSampleRate, model == TapeEchoModel::echorec ? 5000.0 : 6000.0, 0.6);
    }
    scratch.setSize (juce::jmax (1, numChannels), juce::jmax (1, maxBlockSize), false, true, true);
    if (spring)
    {
        for (auto* prm : spring->getParameters()->getParameters (false))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
            {
                if (f->paramID == "spring_mix") *f = 1.0f;     // its output is mixed here, by Reverb Volume
                if (f->paramID == "spring_decay") *f = 0.75f;
                if (f->paramID == "spring_tone") *f = 0.35f;
            }
        spring->prepare (newSampleRate, maxBlockSize, numChannels);
    }
    if (preamp)
        preamp->prepare (newSampleRate, maxBlockSize, numChannels);
    smoothedSpeed.reset (newSampleRate, 0.3); // a motor does not change speed instantly
    smoothedSpeed.setCurrentAndTargetValue (speed != nullptr ? speed->get() : 0.5f);
    lastBass = lastTreble = -1.0f;
    reset();
}

void TapeEchoProcessor::reset()
{
    for (auto& ch : channels)
    {
        ch.tape.reset();
        ch.headLoss.reset();
        ch.bass.reset();
        ch.treble.reset();
        ch.recordHighpass = ch.lastPlayback = 0.0;
    }
    if (spring) spring->reset();
    if (preamp) preamp->reset();
    wowPhase = flutterPhase = 0.0;
}

void TapeEchoProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);

    // EP-3: everything goes through its record preamp first (the "Echoplex preamp" sound is part of the unit).
    if (preamp)
    {
        if (auto* lvl = dynamic_cast<juce::AudioParameterFloat*> (preamp->getParameters()->getParameters (false)[0]))
            *lvl = recordLevel->get();
        preamp->process (buffer);
    }

    // Space Echo reverb: the spring is fed the input.
    const bool reverbOn = spring && (mode != nullptr) && juce::roundToInt (mode->get()) >= 4;
    if (reverbOn && numSamples <= scratch.getNumSamples())
    {
        for (int c = 0; c < numChannels; ++c)
            scratch.copyFrom (c, 0, buffer, c, 0, numSamples);
        spring->process (scratch);
    }

    if (bass != nullptr && (std::abs (bass->get() - lastBass) > 1.0e-6f || std::abs (treble->get() - lastTreble) > 1.0e-6f))
    {
        lastBass = bass->get();
        lastTreble = treble->get();
        for (auto& ch : channels)
        {
            ch.bass.set (sampleRate, 120.0, 24.0 * (lastBass - 0.5), false);
            ch.treble.set (sampleRate, 3000.0, 24.0 * (lastTreble - 0.5), true);
        }
    }

    const int m = mode != nullptr ? juce::jlimit (0, 11, juce::roundToInt (mode->get())) : 0;
    std::array<bool, 4> heads { true, false, false, false };
    std::array<double, 4> headDelay { 0.0, 0.0, 0.0, 0.0 };
    int headCount = 1;
    if (model == TapeEchoModel::spaceEcho) { heads = spaceEchoModes[(size_t) m]; headCount = 3; }
    if (model == TapeEchoModel::echorec) { heads = echorecModes[(size_t) m]; headCount = 4; }
    int active = 0;
    for (int h = 0; h < headCount; ++h)
        active += heads[(size_t) h] ? 1 : 0;
    const double headNorm = active > 0 ? 1.0 / std::sqrt ((double) active) : 0.0;

    smoothedSpeed.setTargetValue (speed != nullptr ? speed->get() : 0.5f);
    const double feedback = 1.05 * std::pow ((double) intensity->get(), 0.8); // > 1 at full: it runs away, saturation bounds it
    const double echo = echoLevel->get() * 2.0;
    const double reverb = reverbOn ? 2.0 * reverbLevel->get() : 0.0;
    const double wowDepth = model == TapeEchoModel::echorec ? 0.0006 : 0.0025, flutterDepth = model == TapeEchoModel::echorec ? 0.0004 : 0.0012;

    for (int i = 0; i < numSamples; ++i)
    {
        const double s = smoothedSpeed.getNextValue();
        // Transport speed relative to nominal: wow and flutter ride on it.
        const double drift = 1.0 + wowDepth * fast::sinCycles (wowPhase) + flutterDepth * fast::sinCycles (flutterPhase);
        wowPhase += 0.7 / sampleRate;
        flutterPhase += 9.3 / sampleRate;
        if (wowPhase >= 1.0) wowPhase -= 1.0;
        if (flutterPhase >= 1.0) flutterPhase -= 1.0;

        switch (model)
        {
            case TapeEchoModel::spaceEcho:
            {
                const double head1 = 0.065 * std::pow (185.0 / 65.0, 1.0 - s); // Repeat Rate up = faster motor = shorter
                for (int h = 0; h < 3; ++h)
                    headDelay[(size_t) h] = head1 * (h + 1) / drift;
                break;
            }
            case TapeEchoModel::echoplex:
                headDelay[0] = (0.060 + (0.750 - 0.060) * s) / drift; // the record head slides along the tape
                break;
            case TapeEchoModel::echorec:
                for (int h = 0; h < 4; ++h)
                    headDelay[(size_t) h] = 0.075 * (h + 1) / drift;
                break;
        }

        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            auto* data = buffer.getWritePointer (c);
            const double x = data[i];

            // Record: input plus the repeats fed back, through the record amp's coupling and the tape's saturation.
            const double rec = x + feedback * ch.lastPlayback;
            ch.recordHighpass += 0.002 * (rec - ch.recordHighpass);
            ch.tape.push ((float) saturate (rec - ch.recordHighpass));

            double play = 0.0;
            for (int h = 0; h < headCount; ++h)
                if (heads[(size_t) h])
                    play += ch.tape.read (headDelay[(size_t) h] * sampleRate);
            play = ch.headLoss.process (play * headNorm);
            if (bass != nullptr)
                play = ch.treble.process (ch.bass.process (play));
            ch.lastPlayback = play;

            double out = x + echo * play;
            if (reverb > 0.0)
                out += reverb * scratch.getSample (c, i);
            data[i] = (float) out;
        }
    }

    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, numSamples);
}

void TapeEchoProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::tape_delay_svg, IconData::tape_delay_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
