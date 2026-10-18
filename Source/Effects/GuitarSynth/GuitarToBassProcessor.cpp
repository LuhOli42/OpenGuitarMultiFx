#include "GuitarToBassProcessor.h"

namespace openguitarmultifx
{
namespace
{
    constexpr int kControlEvery = 32;
    constexpr double kMinGrainSeconds = 0.012, kDefaultGrainSeconds = 0.04, kMaxGrainSeconds = 0.12;
}

GuitarToBassProcessor::GuitarToBassProcessor() : SynthEffect ("Guitar-to-Bass", "octaver", "guitarToBass")
{
    octaves = addChoice ("octaves", "Octave", { "DOWN 1", "DOWN 2" }, 0);
    voicing = addChoice ("voicing", "Voicing", { "FINGER", "PICK", "ROUND" }, 0);
    tone = addFloat ("tone", "Tone", 0, 100, 50, 1.0f, "%", 0);
    body = addFloat ("body", "Body", 0, 12, 4, 1.0f, "dB");
    grit = addFloat ("grit", "Grit", 0, 100, 15, 1.0f, "%", 0);
    squash = addFloat ("squash", "Squash", 0, 100, 40, 1.0f, "%", 0);
    guitarBlend = addFloat ("guitar", "Guitar", 0, 50, 8, 1.0f, "%", 0);
    addMixLevel (100);
}

void GuitarToBassProcessor::prepare (double sampleRate, int, int)
{
    fs = sampleRate;
    tracker.prepare (fs);
    line.prepare ((int) (0.3 * fs));
    mixLevel.prepare (fs);
    reset();
}

void GuitarToBassProcessor::reset()
{
    tracker.reset();
    line.reset();
    comp.reset();
    hp.reset(); shelf.reset(); toneLp[0].reset(); toneLp[1].reset(); pick.reset(); blendLp.reset();
    phi = 0.0;
    grainSamples = kDefaultGrainSeconds * fs;
    lastShape = -1.0;
    controlCounter = 0;
}

void GuitarToBassProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int nc = buffer.getNumChannels();
    float* L = buffer.getWritePointer (0);
    float* R = nc > 1 ? buffer.getWritePointer (1) : nullptr;

    updateMix();
    const double ratio = mt::asIndex (octaves) == 0 ? 0.5 : 0.25;
    const int v = mt::asIndex (voicing);
    // voicing: tone multiplier, extra body, pick-attack shelf gain
    const double toneMul = v == 0 ? 1.0 : (v == 1 ? 1.8 : 0.55);
    const double bodyDb = body->get() + (v == 2 ? 3.0 : 0.0);
    const double pickDb = v == 1 ? 4.0 : 0.0;
    const double toneHz = 450.0 * std::pow (10.0, tone->get() / 100.0 * 1.0) * toneMul; // 450 Hz .. 4.5 kHz (x voicing)
    const double gritAmt = grit->get() / 100.0, squashAmt = squash->get() / 100.0, blend = guitarBlend->get() / 100.0;
    const double shape = bodyDb * 31.0 + toneHz + pickDb * 7.0;
    if (shape != lastShape)
    {
        lastShape = shape;
        hp.highpass (35.0, fs, 0.707);
        shelf.shelf (130.0, fs, bodyDb, true);
        toneLp[0].lowpass (juce::jmin (toneHz, 0.45 * fs), fs, 0.54);
        toneLp[1].lowpass (juce::jmin (toneHz, 0.45 * fs), fs, 1.31);
        pick.shelf (1800.0, fs, pickDb, false);
        blendLp.lowpass (200.0, fs, 0.707);
    }
    comp.set (4.0, 140.0, fs);
    const double grainCoef = std::exp (-(double) kControlEvery / (0.03 * fs)); // applied once per control step
    const double base = 2.0;
    const double thresholdLin = 0.08; // about -22 dBFS on the shifted signal

    for (int i = 0; i < n; ++i)
    {
        const double inL = L[i], inR = R != nullptr ? R[i] : inL;
        const float mono = (float) (0.5 * (inL + inR));
        line.push (mono);
        tracker.push (mono);

        if (controlCounter == 0 && tracker.voiced && tracker.hz > 30.0)
        {
            // grain = m * period / ratio: each grain then reads a whole number of input periods and the half-grain
            // offset between the two reads is a whole number of periods too, for -1 and -2 octaves alike
            const double unit = fs / tracker.hz / ratio;
            const double m = juce::jmax (1.0, std::round (kMinGrainSeconds * fs / unit));
            const double target = juce::jmin (kMaxGrainSeconds * fs, m * unit);
            grainSamples = target + (grainSamples - target) * grainCoef;
        }
        controlCounter = (controlCounter + 1) % kControlEvery;

        phi += (1.0 - ratio) / grainSamples;
        phi -= std::floor (phi);
        double q = phi + 0.5;
        q -= std::floor (q);
        const double tri = 1.0 - std::abs (2.0 * phi - 1.0);
        const double w1 = tri * tri * (3.0 - 2.0 * tri);
        const double shifted = w1 * line.read (base + phi * grainSamples) + (1.0 - w1) * line.read (base + q * grainSamples);

        double x = hp.process (shifted);
        x = shelf.process (x);
        x = toneLp[1].process (toneLp[0].process (x));
        if (pickDb != 0.0) x = pick.process (x);
        if (gritAmt > 0.0)
        {
            const double d = 1.0 + 4.0 * gritAmt;
            const double s = juce::jlimit (-1.2, 1.2, x * d + 0.12 * gritAmt * x * x * d); // even-harmonic lean
            x = (s - (4.0 / 27.0) * s * s * s * 0.67) / d * (1.0 + 0.5 * gritAmt);
        }
        const double e = comp.process (x);
        const double over = e > thresholdLin ? thresholdLin / e : 1.0;
        const double gain = 1.0 + squashAmt * (over - 1.0) * 0.85 + squashAmt * 0.6;
        const double bassOut = x * gain;
        const double wet = bassOut + blend * blendLp.process (mono) * 2.0;

        const auto gn = nextGains();
        L[i] = (float) gn.apply (inL, wet);
        if (R != nullptr) R[i] = (float) gn.apply (inR, wet);
    }
}

} // namespace openguitarmultifx
