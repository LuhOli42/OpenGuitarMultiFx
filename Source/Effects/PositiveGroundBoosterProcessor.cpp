#include "PositiveGroundBoosterProcessor.h"

namespace openguitarmultifx
{

PositiveGroundBoosterProcessor::PositiveGroundBoosterProcessor()
{
    auto boostParam = std::make_unique<juce::AudioParameterFloat> (
        "posboost_boost", "Boost", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    boost = boostParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "posboost", "Positive Ground Booster", "|", std::move (boostParam));

    for (auto& ch : channels)
        ch.transistor.setParameters (5.0e-12, 25.85e-3, 200.0, 4.0);
}

void PositiveGroundBoosterProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;

    smoothedBoostPotResistance.reset (newSampleRate, 0.02);
    smoothedBoostPotResistance.setCurrentAndTargetValue (boostPotMax * boost->get() * boost->get());

    for (auto& ch : channels)
    {
        ch.c1.prepare (newSampleRate, c1Value);
        ch.c4.prepare (newSampleRate, c4Value);
        ch.c2.prepare (newSampleRate, c2Value);

        // Warm-start at the circuit's approximate DC operating point (PNP,
        // mirrored/NPN-equivalent coordinates -- see docs/circuits/
        // PositiveGroundBooster.md): base a little below the R1/R2
        // divider point, emitter near it minus ~0.55V (forward Vbe),
        // collector partway up toward the supply. Newton-Raphson would
        // find the real point from any reasonable start, but starting
        // close means the very first processed sample doesn't have to
        // converge from a wild guess.
        const float dividerVoltage = supplyVoltage * r2 / (r1 + r2);
        ch.transistor.reset (dividerVoltage, dividerVoltage - 0.55, supplyVoltage * 0.5);
    }
}

void PositiveGroundBoosterProcessor::reset()
{
    for (auto& ch : channels)
    {
        ch.c1.reset();
        ch.c4.reset();
        ch.c2.reset();
    }
}

void PositiveGroundBoosterProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int numSamples = buffer.getNumSamples();

    smoothedBoostPotResistance.setTargetValue (boostPotMax * boost->get() * boost->get());

    // Sample-outer, channel-inner: R3 (the boost pot) is one real,
    // physically shared component, not a per-channel one -- advancing its
    // SmoothedValue once per sample (not once per channel) keeps a stereo
    // signal's two channels seeing the exact same pot value at each
    // instant, the way the one real pot the circuit describes would.
    for (int i = 0; i < numSamples; ++i)
    {
        const float r3 = smoothedBoostPotResistance.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& state = channels[(size_t) ch];
            auto* data = buffer.getWritePointer (ch);

            // Mirror the real input into NPN-equivalent coordinates (see
            // EbersMollBJT.h's class doc comment on why PNP is handled by
            // negating every voltage going in and coming back out).
            const double xMirrored = -(double) data[i];

            // --- Base node Thevenin equivalent: R1 to +supply, R2 to
            // ground, C1's companion model in series with the (mirrored)
            // input signal. ---
            const double reqC1 = (double) state.c1.getEquivalentResistance();
            const double histC1 = (double) state.c1.getHistoryVoltage();
            const double gB = 1.0 / r1 + 1.0 / r2 + 1.0 / reqC1;
            const double rthB = 1.0 / gB;
            // C1 has an ideal source (x) IN SERIES with it, unlike C4/C2
            // which connect directly between their node and a fixed rail
            // -- that series source flips the sign on the history term
            // relative to the "direct" capacitor branches below (verified
            // against a hist=0 plain-resistor-divider sanity check: with
            // x acting as a fixed external voltage through Req, current
            // into the base node from this branch is (x - hist - V_base)/Req,
            // not (x + hist - V_base)/Req).
            const double vthB = (supplyVoltage / r1 + 0.0 / r2 + (xMirrored - histC1) / reqC1) / gB;

            // --- Emitter node Thevenin equivalent: R4 || C4's companion
            // model, both to ground. ---
            const double reqC4 = (double) state.c4.getEquivalentResistance();
            const double histC4 = (double) state.c4.getHistoryVoltage();
            const double gE = 1.0 / r4 + 1.0 / reqC4;
            const double rthE = 1.0 / gE;
            const double vthE = (histC4 / reqC4) / gE;

            // --- Collector node Thevenin equivalent: R3 (boost pot) to
            // +supply, in parallel with C2's companion model in series
            // with the assumed downstream load resistance, to ground. ---
            const double reqC2 = (double) state.c2.getEquivalentResistance();
            const double histC2 = (double) state.c2.getHistoryVoltage();
            const double r3d = (double) juce::jmax (1.0f, r3); // avoid a literal short (R=0) reaching the solver
            const double outputBranchR = reqC2 + (double) outputLoadResistance;
            const double gC = 1.0 / r3d + 1.0 / outputBranchR;
            const double rthC = 1.0 / gC;
            const double vthC = (supplyVoltage / r3d + histC2 / outputBranchR) / gC;

            // On non-convergence, solve() itself falls back to the last
            // known-good point (see its own doc comment) -- nothing extra
            // needed here.
            double vb, ve, vc;
            state.transistor.solve (rthB, vthB, rthE, vthE, rthC, vthC, vb, ve, vc);
            state.lastVb = -vb; // mirror back to real (unmirrored) voltages -- see getDebugBiasPoint()
            state.lastVe = -ve;
            state.lastVc = -vc;

            // Advance each capacitor's history state from this sample's
            // solved node voltages.
            const double vC1 = xMirrored - vb;
            state.c1.updateState ((float) vC1, (float) ((vC1 - histC1) / reqC1));

            const double vC4 = ve;
            state.c4.updateState ((float) vC4, (float) ((vC4 - histC4) / reqC4));

            const double outputBranchCurrent = (vc - histC2) / outputBranchR;
            const double vC2 = outputBranchCurrent * reqC2 + histC2;
            state.c2.updateState ((float) vC2, (float) outputBranchCurrent);

            // Output node sits between C2 and the assumed load resistor,
            // referenced to (mirrored) ground -- mirror back to the real
            // (unmirrored) output signal.
            const double outputNodeMirrored = outputBranchCurrent * (double) outputLoadResistance;
            data[i] = (float) -outputNodeMirrored;
        }
    }
}

PositiveGroundBoosterProcessor::DebugBiasPoint PositiveGroundBoosterProcessor::getDebugBiasPoint() const noexcept
{
    auto& ch = channels[0];
    return { (float) ch.lastVb, (float) ch.lastVe, (float) ch.lastVc };
}

} // namespace openguitarmultifx
