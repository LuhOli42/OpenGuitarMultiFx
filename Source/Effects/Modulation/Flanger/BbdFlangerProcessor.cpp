#include "BbdFlangerProcessor.h"
#include "Effects/IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    using namespace analog;
    using Role = BbdFlangerProcessor::Role;

    struct KnobSpec
    {
        const char* id;
        const char* name;
        Role role;
        float defaultValue;
        int positions = 0; // 0 = continuous knob; >= 2 = switch with these many positions
        std::vector<const char*> labels {};
    };

    struct ModelSpec
    {
        const char* prefix;
        const char* name;
        std::vector<KnobSpec> knobs;
    };

    const ModelSpec& specFor (FlangerModel m)
    {
        static const ModelSpec mxr { "mxr117", "MXR M117",
            { { "manual", "Manual", Role::manual, 0.5f }, { "width", "Width", Role::width, 0.5f },
              { "speed", "Speed", Role::rate, 0.5f }, { "regen", "Regen", Role::regen, 0.5f } } };
        static const ModelSpec mistress { "mistress", "Electric Mistress",
            { { "rate", "Rate", Role::rate, 0.5f }, { "range", "Range", Role::manual, 0.5f },
              { "color", "Color", Role::regen, 0.5f },
              { "matrix", "Mode", Role::matrix, 0.0f, 2, { "Flange", "Filter Matrix" } } } };
        static const ModelSpec bf2 { "bf2", "BF-2",
            { { "manual", "Manual", Role::manual, 0.5f }, { "depth", "Depth", Role::width, 0.5f },
              { "rate", "Rate", Role::rate, 0.5f }, { "res", "Res", Role::regen, 0.5f } } };
        static const ModelSpec ada { "ada", "A/DA Flanger",
            { { "manual", "Manual", Role::manual, 0.5f }, { "range", "Range", Role::width, 0.5f },
              { "speed", "Speed", Role::rate, 0.5f }, { "enhance", "Enhance", Role::regen, 0.5f },
              { "threshold", "Threshold", Role::threshold, 0.0f },
              { "harmonics", "Harmonics", Role::evenOdd, 0.0f, 2, { "Even", "Odd" } } } };
        static const ModelSpec tyco { "tyco", "Pedalflanger",
            { { "pedal", "Pedal", Role::manual, 0.5f }, { "spread", "Spread", Role::width, 0.5f },
              { "intensity", "Intensity", Role::regen, 0.5f }, { "rate", "Rate", Role::rate, 0.5f },
              { "mode", "Mode", Role::autoMode, 0.0f, 2, { "Pedal", "Auto" } } } };
        static const ModelSpec echo { "echoflanger", "Echo Flanger",
            { { "mode", "Mode", Role::echoMode, 0.0f, 4, { "Flange", "Chorus", "Slapback", "Filter Matrix" } },
              { "rate", "Rate", Role::rate, 0.5f }, { "width", "Width", Role::width, 0.5f },
              { "tune", "Tune", Role::manual, 0.5f }, { "feedback", "Feedback", Role::regen, 0.4f },
              { "blend", "Blend", Role::blend, 1.0f, 2, { "Effect", "Blend" } } } };
        static const ModelSpec fx75 { "fx75", "FX75",
            { { "delay", "Delay", Role::manual, 0.5f }, { "width", "Width", Role::width, 0.5f },
              { "speed", "Speed", Role::rate, 0.5f }, { "regen", "Regen", Role::regen, 0.5f } } };
        static const ModelSpec fl301 { "fl301", "FL-301",
            { { "delay", "Delay Time", Role::manual, 0.5f }, { "speed", "Speed", Role::rate, 0.5f },
              { "width", "Width", Role::width, 0.5f }, { "regen", "Regeneration", Role::regen, 0.5f } } };

        switch (m)
        {
            case FlangerModel::mxr117: return mxr;
            case FlangerModel::electricMistress: return mistress;
            case FlangerModel::bf2: return bf2;
            case FlangerModel::ada: return ada;
            case FlangerModel::tycobrahe: return tyco;
            case FlangerModel::echoFlanger: return echo;
            case FlangerModel::fx75: return fx75;
            case FlangerModel::fl301: return fl301;
        }
        return mxr;
    }

    // A shelf the way these pedals build pre-emphasis around an op-amp: (1 + sC (Ra + Rb)) / (1 + sC Ra).
    Rational shelfUp (double ra, double rb, double c) { return { { 1.0, c * (ra + rb) }, { 1.0, c * ra } }; }

    // Boss's (CE-2/BF-2) inverting pre-emphasis and the matching de-emphasis feedback.
    Rational bossPreEmphasis() { return inverting (parallelRC (47.0e3, 100.0e-12), parallel (resistor (47.0e3), seriesRC (10.0e3, 6.8e-9))); }
    Rational deEmphasisFeedback (double r, double rShelf, double cShelf, double cHf)
    {
        return parallel (parallel (resistor (r), seriesRC (rShelf, cShelf)), capacitor (cHf));
    }

    double magnitudeAt (const Rational& h, double hz) { return std::abs (h.at (Complex (0.0, 2.0 * juce::MathConstants<double>::pi * hz))); }
    double magnitudeAt (const std::vector<LadderNode>& l, double hz) { return magnitudeAt (ladderTransfer (l), hz); }
}

BbdFlangerProcessor::BbdFlangerProcessor (FlangerModel m) : model (m)
{
    buildParameters();
}

const char* BbdFlangerProcessor::getName() const { return specFor (model).name; }

void BbdFlangerProcessor::buildParameters()
{
    const auto& spec = specFor (model);
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (spec.prefix, spec.name, "|");
    for (const auto& k : spec.knobs)
    {
        const juce::String id = juce::String (spec.prefix) + "_" + k.id;
        std::unique_ptr<juce::AudioParameterFloat> p;
        if (k.positions >= 2)
        {
            auto labels = k.labels;
            p = std::make_unique<juce::AudioParameterFloat> (id, k.name, juce::NormalisableRange<float> (0.0f, (float) (k.positions - 1), 1.0f), k.defaultValue,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([labels] (float v, int)
                {
                    const int i = juce::jlimit (0, (int) labels.size() - 1, juce::roundToInt (v));
                    return juce::String (labels[(size_t) i]);
                }));
        }
        else
        {
            p = std::make_unique<juce::AudioParameterFloat> (id, k.name, juce::NormalisableRange<float> (0.0f, 1.0f), k.defaultValue);
        }
        knobs.push_back ({ k.role, p.get() });
        parameters->addChild (std::move (p));
    }
}

float BbdFlangerProcessor::knob (Role role, float fallback) const noexcept
{
    for (const auto& k : knobs)
        if (k.role == role)
            return k.param->get();
    return fallback;
}

void BbdFlangerProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    dt = 1.0 / newSampleRate;

    Rational input = gain (1.0), dryMix = gain (1.0), wetMix = gain (1.0), regen = gain (1.0);
    std::vector<LadderNode> bbdIn, bbdOut;
    std::optional<std::pair<double, double>> feedbackLimiter; // Rf, Rs (1N4148 unless noted)
    diodes::Diode limiterDiode = diodes::silicon1N4148;
    std::optional<double> shuntClipperOhms;
    double wetToDry = 1.0, warp = 2340.0;
    bool regenAbsolute = false;
    exponentialLfo = compander = compressorOnly = false;
    limiterInputOhms = 1.0;
    railVolts = 3.5;

    switch (model)
    {
        case FlangerModel::mxr117:
        {
            // +15 V single supply. NE5532 input: 1 + 10K / (1K + 33n) -- +21 dB above 4.8 kHz.
            input = cascade (couplingHighpass (0.1e-6, 470.0e3), shelfUp (1.0e3, 10.0e3, 33.0e-9));
            feedbackLimiter = std::make_pair (100.0e3, 27.0e3); // 100K || (27K + 1N4148 pair)
            limiterInputOhms = 150.0e3;
            bbdIn = { { 1.0e3, 0.0, 10.0e-9, 0.0, 0.0 } };                                  // 1K / 10n before the SAD1024
            bbdOut = { { 7.5e3, 0.0, 0.0, 6.8e-9, 0.0 }, { 2.4e3, 0.0, 3.3e-9, 0.0, 0.0 } };   // 4558 Sallen-Key
            const auto zf = deEmphasisFeedback (100.0e3, 10.0e3, 3.3e-9, 1.0e-12);
            dryMix = inverting (zf, resistor (150.0e3));
            wetMix = inverting (zf, resistor (82.0e3));
            regen = couplingHighpass (15.0e-9, 120.0e3);
            loopMax = 100.0 / 120.0;
            stages = 512;
            minDelay = 0.35e-3; maxDelay = 7.0e-3;
            rateMin = 0.08; rateMax = 8.6;
            railVolts = 6.5;
            warp = 4800.0;
            break;
        }
        case FlangerModel::electricMistress:
        {
            // 9 V. 130K / 39n in, buffer, then IC2 inverting: (470R + 4K7 || 47n) in, 5K6 out -- the 1n across the
            // 5K6 is the only anti-alias pole the Mistress has, so it is given to the BBD as its input filter.
            input = cascade (couplingHighpass (39.0e-9, 130.0e3), inverting (resistor (5.6e3), series (resistor (470.0), parallelRC (4.7e3, 47.0e-9))));
            bbdIn = { { 5.6e3, 0.0, 1.0e-9, 0.0, 0.0 } };
            // 470R into 10K || 3n3 -- barely a filter (~107 kHz). On the pedal the clock images that leaves are
            // ultrasonic; in a 48 kHz model they would fold back as audible aliasing the pedal never had, so two
            // ~17 kHz poles stand in for "nothing above the audio band survives" (see the doc).
            bbdOut = { { 470.0, 0.0, 3.3e-9, 0.0, 10.0e3 }, { 1.0e3, 0.0, 9.4e-9, 0.0, 0.0 }, { 1.0e3, 0.0, 9.4e-9, 0.0, 0.0 } };
            // Passive mix: dry through 22n + 13K, wet through 82n + 8K2, into 510R + 47n to ground.
            const auto yd = reciprocal (seriesRC (13.0e3, 22.0e-9)), yw = reciprocal (seriesRC (8.2e3, 82.0e-9));
            const auto ysum = sum (sum (yd, yw), reciprocal (seriesRC (510.0, 47.0e-9)));
            dryMix = cascade (yd, reciprocal (ysum));
            wetMix = cascade (yw, reciprocal (ysum));
            wetToDry = 0.0; // keep the circuit's own balance
            // Color: the wet back through the 10K trim, 470R + 47n, the pot and 2n2 into IC2's 5K6 summing node --
            // a differentiator, so the regeneration is mostly treble.
            regen = inverting (resistor (5.6e3), seriesRC (5.0e3, 2.2e-9));
            regenAbsolute = true;
            loopMax = 1.0;
            stages = 512;
            minDelay = 0.4e-3; maxDelay = 8.0e-3;
            rateMin = 0.005; rateMax = 1.29;
            warp = 1000.0;
            break;
        }
        case FlangerModel::bf2:
        {
            input = cascade (couplingHighpass (0.047e-6, 470.0e3), bossPreEmphasis());
            feedbackLimiter = std::make_pair (47.0e3, 4.7e3); // R12 || (R11 + D3/D4)
            limiterInputOhms = 47.0e3;
            bbdIn = { { 10.0e3, 0.0, 0.0, 12.0e-9, 0.0 }, { 10.0e3, 0.0, 150.0e-12, 0.0, 0.0 } };
            bbdOut = { { 10.0e3, 0.0, 3.9e-9, 0.0, 0.0 }, { 10.0e3, 0.0, 0.0, 8.2e-9, 0.0 }, { 10.0e3, 0.0, 330.0e-12, 0.0, 0.0 } };
            const auto zf = deEmphasisFeedback (47.0e3, 10.0e3, 6.8e-9, 100.0e-12);
            dryMix = inverting (zf, resistor (47.0e3));
            wetMix = inverting (zf, resistor (27.0e3));
            regen = couplingHighpass (0.047e-6, 22.0e3);
            loopMax = 0.85;
            stages = 1024;
            minDelay = 1.0e-3; maxDelay = 13.0e-3;
            rateMin = 0.027; rateMax = 4.6;
            break;
        }
        case FlangerModel::ada:
        {
            // No reliable schematic: the published behaviour. Compressor ahead of the BBD, a band-pass in the loop,
            // 0.4-15 ms (the 40:1 sweep), 20 Hz - 16 kHz.
            input = couplingHighpass (0.1e-6, 500.0e3);
            compressorOnly = true;
            bbdIn = { { 10.0e3, 0.0, 1.3e-9, 0.0, 0.0 }, { 10.0e3, 0.0, 0.0, 3.3e-9, 0.0 }, { 10.0e3, 0.0, 190.0e-12, 0.0, 0.0 } };
            bbdOut = bbdIn;
            regen = cascade (couplingHighpass (1.0e-6, 1.0e3), Rational { { 1.0 }, { 1.0, 1.0 / (2.0 * juce::MathConstants<double>::pi * 5000.0) } });
            loopMax = 0.97;
            exponentialLfo = true;
            stages = 1024;
            minDelay = 0.4e-3; maxDelay = 15.0e-3;
            rateMin = 0.04; rateMax = 10.0;
            railVolts = 6.0;
            break;
        }
        case FlangerModel::tycobrahe:
        {
            input = couplingHighpass (100.0e-9, 377.0e3);
            limiterDiode = diodes::germanium;
            shuntClipperOhms = 10.0e3; // the Intensity path's Ge pair D1/D2
            bbdIn = { { 4.7e3, 0.0, 1.0e-9, 0.0, 0.0 } };
            bbdOut = { { 47.0e3, 0.0, 330.0e-12, 0.0, 0.0 }, { 1.0e3, 0.0, 9.4e-9, 0.0, 0.0 } }; // + the audio-band limit, as the Mistress
            regen = couplingHighpass (10.0e-9, 330.0e3);
            loopMax = 0.9;
            stages = 512;                           // MN3001
            minDelay = 256.0 / 509.0e3;             // the drawing: 51 .. 509 kHz at TP1
            maxDelay = 256.0 / 51.0e3;
            rateMin = 0.1; rateMax = 5.0;
            break;
        }
        case FlangerModel::echoFlanger:
        {
            input = couplingHighpass (0.047e-6, 100.0e3);
            compander = true;
            bbdIn = { { 3.01e3, 0.0, 3.0e-9, 0.0, 0.0 }, { 3.01e3, 0.0, 0.0, 47.0e-9, 0.0 }, { 3.01e3, 0.0, 2.7e-9, 0.0, 0.0 } };
            bbdOut = { { 3.01e3, 0.0, 2.2e-9, 0.0, 0.0 }, { 3.01e3, 0.0, 0.0, 18.0e-9, 0.0 }, { 3.01e3, 0.0, 4.5e-9, 0.0, 0.0 } };
            regen = couplingHighpass (1.0e-6, 18.0e3);
            loopMax = 0.85;
            stages = 1024;
            minDelay = 0.5e-3; maxDelay = 10.0e-3;
            rateMin = 0.13; rateMax = 14.0;
            railVolts = 6.5;
            break;
        }
        case FlangerModel::fx75:
        {
            input = cascade (couplingHighpass (0.01e-6, 470.0e3), shelfUp (10.0e3, 47.0e3, 6.3e-9));
            bbdIn = { { 10.0e3, 0.0, 1.0e-9, 0.0, 0.0 }, { 47.0e3, 0.0, 120.0e-12, 0.0, 0.0 } };
            shuntClipperOhms = 4.7e3; // 4.7K into the 1N4148 pair before the MN3007
            bbdOut = { { 33.0e3, 0.0, 1.0e-9, 0.0, 0.0 }, { 33.0e3, 0.0, 0.0, 5.0e-9, 0.0 }, { 33.0e3, 0.0, 120.0e-12, 0.0, 0.0 } };
            const auto zf = deEmphasisFeedback (47.0e3, 10.0e3, 6.3e-9, 120.0e-12);
            dryMix = inverting (zf, resistor (47.0e3));
            wetMix = inverting (zf, resistor (47.0e3));
            regen = couplingHighpass (0.022e-6, 33.0e3);
            loopMax = 0.85;
            stages = 1024;
            minDelay = 0.8e-3; maxDelay = 10.0e-3;
            rateMin = 0.1; rateMax = 10.0;
            break;
        }
        case FlangerModel::fl301:
        {
            input = cascade (couplingHighpass (0.047e-6, 510.0e3), shelfUp (5.1e3, 51.0e3, 3.0e-9));
            bbdIn = { { 24.0e3, 0.0, 0.0, 1.0e-9, 0.0 }, { 24.0e3, 0.0, 470.0e-12, 0.0, 0.0 } };
            bbdOut = { { 24.0e3, 0.0, 0.0, 2.2e-9, 0.0 }, { 24.0e3, 0.0, 820.0e-12, 0.0, 0.0 } };
            const auto zf = deEmphasisFeedback (51.0e3, 5.1e3, 3.0e-9, 51.0e-12);
            dryMix = inverting (zf, resistor (51.0e3));
            wetMix = inverting (zf, resistor (51.0e3));
            regen = couplingHighpass (1.0e-6, 10.0e3);
            loopMax = 0.85;
            stages = 1024;
            minDelay = 1.0e-3; maxDelay = 13.0e-3;
            rateMin = 0.1; rateMax = 10.0;
            break;
        }
    }

    // Wet / dry balance at 1 kHz: the circuit's own (wetToDry == 0) or equal level -- what these pedals' mixers are
    // set up for (an equal mix gives the deepest notches); the reconstruction stages' exact make-up gain is not legible
    // on most of the drawings, so the level is set where the design intends it rather than guessed from parts.
    const double bbdChain = magnitudeAt (bbdIn, 1000.0) * magnitudeAt (bbdOut, 1000.0);
    const double limiterGain = feedbackLimiter ? feedbackLimiter->first / limiterInputOhms : 1.0;
    const double dryAt1k = magnitudeAt (dryMix, 1000.0), wetAt1k = magnitudeAt (wetMix, 1000.0) * bbdChain * limiterGain;
    wetNorm = wetToDry > 0.0 ? wetToDry * dryAt1k / std::max (1.0e-12, wetAt1k) : 1.0;
    // Regeneration: scaled so the loop's PEAK gain (50 Hz - 20 kHz, the filters' resonance included) is the knob's
    // loop gain -- normalising at one frequency let a peaky reconstruction filter push the loop past 1.
    double loopPeak = 0.0;
    for (double hz = 50.0; hz < 20000.0; hz *= 1.05)
        loopPeak = std::max (loopPeak, magnitudeAt (regen, hz) * magnitudeAt (bbdIn, hz) * magnitudeAt (bbdOut, hz) * limiterGain);
    regenNorm = regenAbsolute ? 1.0 : 1.0 / std::max (1.0e-12, loopPeak);

    const auto inFilter = bbdchorus::bbdFilter (bbdIn), outFilter = bbdchorus::bbdFilter (bbdOut);
    for (auto& ch : channels)
    {
        ch.input.prepare (input, newSampleRate, warp);
        ch.regenFilter.prepare (regen, newSampleRate, 1000.0);
        ch.dryMix.prepare (dryMix, newSampleRate, warp);
        ch.wetMix.prepare (wetMix, newSampleRate, warp);
        ch.bbd.prepare (newSampleRate, stages, inFilter, outFilter);
        if (model == FlangerModel::echoFlanger)
            ch.bbdLong.prepare (newSampleRate, 2 * stages, inFilter, outFilter); // Slapback switches in IC4
        ch.feedbackLimiter.reset();
        ch.shuntClipper.reset();
        if (feedbackLimiter)
            ch.feedbackLimiter.emplace (feedbackLimiter->first, feedbackLimiter->second, limiterDiode);
        if (shuntClipperOhms)
            ch.shuntClipper.emplace (*shuntClipperOhms, limiterDiode);
        ch.compressor.prepare (newSampleRate, 1.0e-6, 0.3);
        ch.expander.prepare (newSampleRate, 1.0e-6, 0.3);
    }

    for (auto* s : { &smoothedManual, &smoothedWidth, &smoothedRegen })
        s->reset (newSampleRate, 0.05);
    smoothedManual.setCurrentAndTargetValue (knob (Role::manual, 0.5f));
    smoothedWidth.setCurrentAndTargetValue (knob (Role::width, 1.0f));
    smoothedRegen.setCurrentAndTargetValue (knob (Role::regen, 0.0f));

    envCoeff = 1.0 - std::exp (-1.0 / (0.01 * newSampleRate));
    gateCoeff = 1.0 - std::exp (-1.0 / (0.02 * newSampleRate));
    lfo.setThresholds (-1.0, 1.0);
    reset();
}

void BbdFlangerProcessor::reset()
{
    for (auto& ch : channels)
    {
        ch.input.reset();
        ch.regenFilter.reset();
        ch.dryMix.reset();
        ch.wetMix.reset();
        ch.bbd.reset();
        ch.bbdLong.reset();
        if (ch.feedbackLimiter) ch.feedbackLimiter->reset();
        if (ch.shuntClipper) ch.shuntClipper->reset();
        ch.compressor.reset();
        ch.expander.reset();
        ch.lastWet = 0.0;
    }
    lfo.reset (0.0, true);
    envelope = 0.0;
    gateGain = 1.0;
    shortcut.reset();
}

double BbdFlangerProcessor::processSample (Channel& ch, double x, double clockHz, bool longLine, double regenGain, double wetGain, double dryGain) noexcept
{
    const double pre = bbdchorus::railClip (ch.input.process (x), -railVolts, railVolts);
    // A compander (Echo Flanger) or compressor (A/DA) sits in the forward path only: the regeneration comes back from the
    // BBD's own output and re-enters after the compressor (the Echo Flanger's C12 / R11 return), so the loop gain is
    // not at the mercy of two envelope followers.
    const double driven = compander ? bbdchorus::railClip (ch.compressor.compress (pre), -railVolts, railVolts)
                                    : (compressorOnly ? ch.compressor.compressDown (pre) : pre);
    const double fed = driven + regenGain * regenNorm * ch.regenFilter.process (ch.lastWet);

    double toBbd = fed;
    if (ch.feedbackLimiter)
        toBbd = -ch.feedbackLimiter->process (fed / limiterInputOhms); // the summer inverts; sign kept for the mix
    else
        toBbd = bbdchorus::railClip (fed, -railVolts, railVolts);
    if (ch.shuntClipper)
        toBbd = ch.shuntClipper->process (toBbd);

    const double raw = longLine ? ch.bbdLong.process (toBbd, clockHz) : ch.bbd.process (toBbd, clockHz);
    ch.lastWet = raw;
    const double wet = compander ? bbdchorus::railClip (ch.expander.expand (raw), -railVolts, railVolts) : raw;

    const double out = dryGain * ch.dryMix.process (pre) + wetGain * wetNorm * ch.wetMix.process (wet);
    return bbdchorus::railClip (out, -railVolts, railVolts);
}

void BbdFlangerProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    // Rate pots: exponential in the knob (log pots / log-law timing) between the circuit's two ends.
    lfoHz = rateMin * std::pow (rateMax / rateMin, (double) knob (Role::rate, 0.5f));
    lfo.setSlopes (4.0 * lfoHz, 4.0 * lfoHz);

    smoothedManual.setTargetValue (knob (Role::manual, 0.5f));
    smoothedWidth.setTargetValue (knob (Role::width, 1.0f));
    smoothedRegen.setTargetValue (knob (Role::regen, 0.0f));

    const int echoMode = juce::roundToInt (knob (Role::echoMode, 0.0f));
    const bool frozen = knob (Role::matrix, 0.0f) >= 0.5f || echoMode == 2 || echoMode == 3
                        || (model == FlangerModel::tycobrahe && knob (Role::autoMode, 1.0f) < 0.5f);
    const bool longLine = model == FlangerModel::echoFlanger && echoMode == 2;
    const double regenSign = knob (Role::evenOdd, 0.0f) >= 0.5f ? -1.0 : 1.0;
    const double dryGain = (model == FlangerModel::echoFlanger && knob (Role::blend, 1.0f) < 0.5f) ? 0.0 : 1.0;
    const double threshold = knob (Role::threshold, 0.0f);

    // Echo Flanger modes shift the delay window: Chorus a longer one, Slapback ~8x (2048 stages, C37 slows the clock).
    double lo = minDelay, hi = maxDelay;
    if (echoMode == 1) { lo = 4.0e-3; hi = 20.0e-3; }
    if (echoMode == 2) { lo = minDelay * 8.4; hi = maxDelay * 8.4; }
    const int lineStages = longLine ? 2 * stages : stages;

    for (int i = 0; i < numSamples; ++i)
    {
        const double manual = smoothedManual.getNextValue();
        const double width = smoothedWidth.getNextValue();
        const double regenGain = regenSign * loopMax * smoothedRegen.getNextValue();

        double sweep = frozen ? 0.0 : lfo.advance (dt);
        if (exponentialLfo) // A/DA: the sweep dwells at the long end and snaps through the short one
            sweep = 2.0 * (std::exp (2.5 * (sweep + 1.0) * 0.5) - 1.0) / (std::exp (2.5) - 1.0) - 1.0;

        // Manual sets where the sweep sits, Width how far it goes: position 0 = longest delay, 1 = shortest.
        const double u = model == FlangerModel::electricMistress
                             ? (frozen ? manual : manual * (0.5 + 0.5 * sweep))  // Range scales the whole LFO
                             : juce::jlimit (0.0, 1.0, manual + (frozen ? 0.0 : 0.5 * width * sweep));
        lastDelay = hi * std::pow (lo / hi, u);
        const double clockHz = 0.5 * lineStages / lastDelay;

        // A/DA Threshold: below it the flanging is gated off (the dry passes).
        double wetGain = 1.0;
        if (threshold > 0.0)
        {
            const double x0 = std::abs ((double) buffer.getReadPointer (0)[i]);
            envelope += envCoeff * (x0 - envelope);
            const double open = envelope > 0.2 * threshold * threshold ? 1.0 : 0.0;
            gateGain += gateCoeff * (open - gateGain);
            wetGain = gateGain;
        }

        for (int c = 0; c < solveChannels; ++c)
        {
            auto* data = buffer.getWritePointer (c);
            data[i] = (float) processSample (channels[(size_t) c], (double) data[i], clockHz, longLine, regenGain, wetGain, dryGain);
        }
    }

    shortcut.end (buffer);
}

void BbdFlangerProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::flanger_svg, IconData::flanger_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
