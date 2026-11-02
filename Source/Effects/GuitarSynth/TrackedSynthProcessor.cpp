#include "TrackedSynthProcessor.h"

namespace openguitarmultifx
{
namespace
{
    constexpr int kControlEvery = 16;
}

TrackedSynthProcessor::TrackedSynthProcessor() : SynthEffect ("Tracked Synth", "pitch_shift", "trackedSynth")
{
    wave = addChoice ("wave", "Wave", { "SAW", "SQUARE", "TRIANGLE", "SINE" }, 0);
    octave = addFloat ("octave", "Octave", -2, 1, 0, 1.0f, "oct", 0);
    sub = addFloat ("sub", "Sub Osc", 0, 100, 40, 1.0f, "%", 0);
    detune = addFloat ("detune", "Detune", 0, 40, 8, 1.0f, "ct", 0);
    glide = addFloat ("glide", "Glide", 0, 500, 20, 0.5f, "ms", 0);
    cutoff = addFloat ("cutoff", "Cutoff", 80, 12000, 1800, 0.35f, "Hz", 0);
    resonance = addFloat ("resonance", "Resonance", 0, 90, 25, 1.0f, "%", 0);
    envAmount = addFloat ("envAmount", "Filter Env", 0, 100, 50, 1.0f, "%", 0);
    release = addFloat ("release", "Release", 20, 800, 160, 0.5f, "ms", 0);
    dynamics = addFloat ("dynamics", "Dynamics", 0, 100, 70, 1.0f, "%", 0);
    sensitivity = addFloat ("sensitivity", "Sensitivity", -60, -20, -46, 1.0f, "dB", 0);
    dryGuitar = addFloat ("dry", "Guitar", 0, 100, 0, 1.0f, "%", 0);
    addMixLevel (100);
}

void TrackedSynthProcessor::prepare (double sampleRate, int, int)
{
    fs = sampleRate;
    tracker.prepare (fs);
    mixLevel.prepare (fs);
    reset();
}

void TrackedSynthProcessor::reset()
{
    tracker.reset();
    env.reset();
    voices = {};
    filter.reset();
    norm = 1.0; lowest = -1; clock = 0; gate = false; controlCounter = 0;
}

void TrackedSynthProcessor::assignVoices (double octMul)
{
    const auto& slots = tracker.getSlots();
    const int legatoWindow = (int) (0.06 * fs);

    for (auto& v : voices) // notes that ended (or whose slot moved on to another note) release
        if (v.slot >= 0 && ! (slots[(size_t) v.slot].active && slots[(size_t) v.slot].id == v.slotId))
        {
            v.slot = -1;
            v.releasedAt = clock;
        }

    int activeCount = 0;
    double strongest = 0.0;
    for (const auto& s : slots)
        if (s.active) { ++activeCount; strongest = std::max (strongest, s.strength); }

    for (int si = 0; si < (int) slots.size(); ++si)
    {
        const auto& s = slots[(size_t) si];
        if (! s.active) continue;
        Voice* owner = nullptr;
        for (auto& v : voices)
            if (v.slot == si && v.slotId == s.id) owner = &v;
        if (owner == nullptr)
        {
            // a single-note line hands the new note to the voice that just let go, so Glide is portamento
            Voice* legato = nullptr;
            if (activeCount == 1)
                for (auto& v : voices)
                    if (v.slot < 0 && clock - v.releasedAt < legatoWindow && v.amp > 1e-3) legato = &v;
            owner = legato;
            if (owner == nullptr)
            {
                for (auto& v : voices)
                    if (v.slot < 0 && (owner == nullptr || v.amp < owner->amp)) owner = &v;
            }
            if (owner == nullptr) continue;
            const bool snap = owner != legato;
            owner->slot = si;
            owner->slotId = s.id;
            if (snap) owner->hz = 0.0;
        }
        owner->targetHz = s.hz * octMul;
        owner->weight = strongest > 0.0 ? juce::jlimit (0.35, 1.0, std::sqrt (s.strength / strongest)) : 1.0;
    }

    // with no active note the sub stays on the last lowest voice so it fades with that voice's release
    int newLowest = -1;
    for (int i = 0; i < (int) voices.size(); ++i)
        if (voices[(size_t) i].slot >= 0 && (newLowest < 0 || voices[(size_t) i].targetHz < voices[(size_t) newLowest].targetHz)) newLowest = i;
    if (newLowest >= 0) lowest = newLowest;
}

void TrackedSynthProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int nc = buffer.getNumChannels();
    float* L = buffer.getWritePointer (0);
    float* R = nc > 1 ? buffer.getWritePointer (1) : nullptr;

    updateMix();
    const auto w = (gs::Wave) mt::asIndex (wave);
    const double octMul = std::exp2 ((double) juce::roundToInt (octave->get()));
    const double detMul = std::exp2 (detune->get() / 1200.0);
    const double subLevel = sub->get() / 100.0;
    const double glideCoef = glide->get() < 1.0f ? 0.0 : std::exp (-1.0 / (glide->get() * 1e-3 * fs));
    const double baseCut = cutoff->get(), res = resonance->get() / 100.0, envAmt = envAmount->get() / 100.0;
    const double dyn = dynamics->get() / 100.0, guitar = dryGuitar->get() / 100.0;
    const double thr = juce::Decibels::decibelsToGain ((double) sensitivity->get());
    env.set (1.5, release->get(), fs);
    const double attackCoef = std::exp (-1.0 / (0.004 * fs));
    const double releaseCoef = std::exp (-1.0 / (0.25 * release->get() * 1e-3 * fs));
    const double normCoef = std::exp (-1.0 / (0.02 * fs));

    for (int i = 0; i < n; ++i)
    {
        const double inL = L[i], inR = R != nullptr ? R[i] : inL;
        const float mono = (float) (0.5 * (inL + inR));
        const double e = env.process (mono);
        if (tracker.push (mono)) assignVoices (octMul);
        gate = e > (gate ? 0.7 * thr : thr);
        ++clock;

        const double lvl = (1.0 - dyn) * 0.6 + dyn * juce::jmin (1.0, e * 5.0);
        int sounding = 0;
        double y = 0.0;
        for (int vi = 0; vi < (int) voices.size(); ++vi)
        {
            auto& v = voices[(size_t) vi];
            const bool on = gate && v.slot >= 0;
            if (on)
            {
                ++sounding;
                v.hz = v.hz <= 0.0 ? v.targetHz : v.targetHz + (v.hz - v.targetHz) * glideCoef;
            }
            const double target = on ? lvl * v.weight : 0.0;
            v.amp = target + (v.amp - target) * (target > v.amp ? attackCoef : releaseCoef);
            if (v.hz <= 0.0 || v.amp < 1e-5) continue;
            const double inc = v.hz / fs;
            double s = v.osc.next (w, inc) + v.osc2.next (w, inc * detMul) * 0.8;
            if (vi == lowest) s += subLevel * v.sub.next (gs::Wave::square, 0.5 * inc) * 0.9;
            y += s * v.amp;
        }
        const double normTarget = 1.0 / std::sqrt ((double) juce::jmax (1, sounding));
        norm = normTarget + (norm - normTarget) * normCoef;
        y *= 0.22 * norm; // lands a realistic DI (peak ~0.4) within ~2 LU of the dry guitar

        if (controlCounter == 0)
        {
            const double sweep = envAmt * juce::jmin (1.0, e * 6.0) * 4.0;
            filter.set (juce::jmin (16000.0, baseCut * std::exp2 (sweep)), res, fs);
        }
        controlCounter = (controlCounter + 1) % kControlEvery;
        const double wet = gs::softClip (filter.lp (y));

        const auto gn = nextGains(); // the dry guitar rides inside the wet path so Mix = 0 stays a clean bypass
        L[i] = (float) gn.apply (inL, wet + guitar * inL);
        if (R != nullptr) R[i] = (float) gn.apply (inR, wet + guitar * inR);
    }
}

} // namespace openguitarmultifx
