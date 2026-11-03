#include "GuitarToBassProcessor.h"

namespace openguitarmultifx
{
GuitarToBassProcessor::GuitarToBassProcessor() : SynthEffect ("Guitar-to-Bass", "octaver", "guitarToBass")
{
    octaves = addChoice ("octaves", "Octave", { "DOWN 1", "DOWN 2" }, 0);
    mode = addChoice ("mode", "Mode", { "MONO", "POLY", "HYBRID" }, 0);
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
    shifter.prepare (fs);
    poly.prepare (fs);
    mixLevel.prepare (fs);
    onsetFast.set (1.0, 20.0, fs);
    onsetSlow.set (30.0, 300.0, fs);
    // align 50 ms into the note (past the pick transient, which skews POLY's frequency estimates), then a 40 ms
    // fade: measured on the DI notes and chord shapes as the smallest crossfade hole (worst ~-3 dB, median ~-0.5 dB)
    alignAt = (int) std::lround (0.05 * fs);
    handoffAt = alignAt + poly.getHopSamples() + poly.getLatencySamples();
    fadeUp = 1.0 / (0.04 * fs);
    fadeDown = 1.0 / (0.004 * fs);
    refractory = (int) std::lround (0.03 * fs);
    powA = std::exp (-1.0 / (0.015 * fs));
    compA = std::exp (-1.0 / (0.005 * fs));
    reset();
}

void GuitarToBassProcessor::reset()
{
    shifter.reset();
    poly.reset();
    onsetFast.reset(); onsetSlow.reset();
    sinceOnset = 1 << 30; aligned = false; wasRising = false; polyWeight = 0.0;
    pM = pP = pX = 0.0; compGain = 1.0;
    comp.reset();
    hp.reset(); shelf.reset(); toneLp[0].reset(); toneLp[1].reset(); pick.reset(); blendLp.reset();
    lastShape = -1.0;
    lastMode = -1;
}

void GuitarToBassProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int nc = buffer.getNumChannels();
    float* L = buffer.getWritePointer (0);
    float* R = nc > 1 ? buffer.getWritePointer (1) : nullptr;

    updateMix();
    const double ratio = mt::asIndex (octaves) == 0 ? 0.5 : 0.25;
    const int m = mt::asIndex (mode);
    if (m != lastMode)
    {
        // the idle shifter was not fed, so its history is stale; both restart from silence
        if (lastMode >= 0) { shifter.reset(); poly.reset(); }
        lastMode = m;
    }
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
    const double thresholdLin = 0.08; // about -22 dBFS on the shifted signal

    for (int i = 0; i < n; ++i)
    {
        const double inL = L[i], inR = R != nullptr ? R[i] : inL;
        const float mono = (float) (0.5 * (inL + inR));
        const float monoOut = m != 1 ? shifter.process (mono, ratio) : 0.0f;
        const float polyOut = m != 0 ? poly.process (mono, ratio, monoOut) : 0.0f;
        double shifted = m == 0 ? monoOut : polyOut;
        if (m == 2)
        {
            // HYBRID: each note's attack from MONO, then a crossfade into POLY once POLY's partials have been started in
            // phase with MONO's (unaligned, the two paths are ~30 ms apart and a crossfade between them cancels)
            const double ef = onsetFast.process (mono), es = onsetSlow.process (mono);
            const bool rising = ef > 2.0 * es + 1e-4;
            if (rising && ! wasRising && sinceOnset > refractory) { sinceOnset = 0; aligned = false; }
            wasRising = rising;
            if (sinceOnset < handoffAt) ++sinceOnset;
            if (sinceOnset == alignAt) { poly.alignToReference(); aligned = true; }
            if (aligned && sinceOnset >= handoffAt) polyWeight = juce::jmin (1.0, polyWeight + fadeUp);
            else if (sinceOnset < alignAt) polyWeight = juce::jmax (0.0, polyWeight - fadeDown);
            const double gP = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * polyWeight);
            const double mix = monoOut + gP * (polyOut - monoOut);
            // energy compensation: whatever the two paths still cancel during the crossfade is made up, so the
            // mix keeps the level a coherent (in-phase) blend of the two would have
            pM = powA * pM + (1.0 - powA) * (double) monoOut * monoOut;
            pP = powA * pP + (1.0 - powA) * (double) polyOut * polyOut;
            pX = powA * pX + (1.0 - powA) * mix * mix;
            double target = 1.0;
            if (gP > 0.0 && gP < 1.0)
            {
                const double want = (1.0 - gP) * std::sqrt (pM) + gP * std::sqrt (pP);
                target = juce::jlimit (1.0, 2.0, want / std::sqrt (pX + 1e-12));
            }
            compGain = compA * compGain + (1.0 - compA) * target;
            shifted = mix * compGain;
        }

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
