#include "EffectRegistry.h"
#include "Effects/GuvnorStyleDistortionProcessor.h"
#include "Effects/RatStyleDistortionProcessor.h"
#include "Effects/OpAmpClipperDistortionProcessor.h"
#include "Effects/BluesBreakerStyleOverdriveProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace openguitarmultifx
{

/** Dev-only probe (PEDAL_PROBE=<registry key>): random knob motion + plucked notes + hot bursts, looking for a pedal that
    goes silent, non-finite or stuck. */
class PedalDeathProbe : public juce::UnitTest
{
public:
    PedalDeathProbe() : juce::UnitTest ("PedalDeathProbe", "Probe") {}

    void runTest() override
    {
        const char* key = std::getenv ("PEDAL_PROBE");
        if (key == nullptr)
            return;

        beginTest (juce::String ("death probe: ") + key);
        std::unique_ptr<juce::ScopedNoDenormals> noDenormals;
        if (std::getenv ("PEDAL_PROBE_FTZ") != nullptr)
            noDenormals = std::make_unique<juce::ScopedNoDenormals>();
        EffectRegistry registry;
        registerBuiltInEffects (registry);
        const bool direct = std::getenv ("PEDAL_PROBE_DIRECT") != nullptr;

        for (int trial = 0; trial < 6; ++trial)
        {
            std::unique_ptr<EffectProcessor> fx;
            GuvnorStyleDistortionProcessor* guv = nullptr;
            RatStyleDistortionProcessor* rat = nullptr;
            OpAmpClipperDistortionProcessor* clip = nullptr;
            BluesBreakerStyleOverdriveProcessor* bb = nullptr;
            if (direct && juce::String (key).startsWith ("Rat"))
            {
                auto g = std::make_unique<RatStyleDistortionProcessor>();
                rat = g.get();
                fx = std::move (g);
            }
            else if (direct && juce::String (key).startsWith ("DOD"))
            {
                auto g = std::make_unique<OpAmpClipperDistortionProcessor> (OpAmpClipperDistortionProcessor::Model::dod250);
                clip = g.get();
                fx = std::move (g);
            }
            else if (direct && juce::String (key).startsWith ("Blues"))
            {
                auto g = std::make_unique<BluesBreakerStyleOverdriveProcessor>();
                bb = g.get();
                fx = std::move (g);
            }
            else if (direct)
            {
                auto g = std::make_unique<GuvnorStyleDistortionProcessor>();
                guv = g.get();
                fx = std::move (g);
            }
            else
                fx = registry.create (key);
            const double sr = 48000.0;
            fx->prepare (sr, 128, 2);
            auto params = fx->getParameters()->getParameters (true);
            juce::Random rng (1234 + trial);

            juce::AudioBuffer<float> buf (2, 128);
            long long deadBlocks = 0, nonFinite = 0, blocks = 0;
            double noteFreq = 110.0, noteAmp = 0.3, notePhase = 0.0;
            long long sinceNote = 0;
            int hotBurstLeft = 0;
            long long firstDead = -1;
            int slowLogged = 0;
            double totalMs = 0.0;

            for (long long b = 0; b < (long long) (40.0 * sr / 128.0); ++b)
            {
                if (rng.nextInt (40) == 0) // knob jump
                    for (auto* p : params)
                        if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                        {
                            const float v = rng.nextInt (4) == 0 ? (rng.nextBool() ? 0.0f : 1.0f) : rng.nextFloat();
                            *f = f->getNormalisableRange().convertFrom0to1 (v);
                        }
                if (rng.nextInt (300) == 0)
                    hotBurstLeft = 40;

                double inSq = 0.0, outSq = 0.0;
                bool levelUp = true;
                for (auto* p : params)
                    if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p); f != nullptr && (f->getName (20).containsIgnoreCase ("level") || f->getName (20).containsIgnoreCase ("volume") || f->getName (20).containsIgnoreCase ("output")) && f->get() < 0.02f)
                        levelUp = false;
                for (int i = 0; i < 128; ++i)
                {
                    if (++sinceNote > (long long) (0.6 * sr * (0.5 + rng.nextFloat())))
                    {
                        sinceNote = 0;
                        noteFreq = 82.0 * std::pow (2.0, rng.nextInt (30) / 12.0);
                        noteAmp = 0.05 + 0.6 * rng.nextFloat();
                    }
                    noteAmp *= 0.99998;
                    notePhase += 2.0 * juce::MathConstants<double>::pi * noteFreq / sr;
                    double x = noteAmp * (std::sin (notePhase) + 0.4 * std::sin (2.0 * notePhase) + 0.2 * std::sin (3.0 * notePhase));
                    if (hotBurstLeft > 0)
                        x *= 6.0;
                    buf.setSample (0, i, (float) x);
                    buf.setSample (1, i, (float) x);
                    inSq += x * x;
                }
                if (hotBurstLeft > 0)
                    --hotBurstLeft;

                const auto t0 = std::chrono::steady_clock::now();
                fx->process (buf);
                const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
                if (ms > 6.0 && slowLogged < 3)
                {
                    ++slowLogged;
                    juce::String knobs;
                    for (auto* p : params)
                        if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                            knobs += f->getName (20) + "=" + juce::String (f->get(), 3) + " ";
                    logMessage ("  SLOW block " + juce::String (b) + ": " + juce::String (ms, 1) + " ms (budget 2.7), in rms " + juce::String (std::sqrt (inSq / 128.0), 3)
                                + (hotBurstLeft > 0 ? " HOT" : "") + " knobs " + knobs
                                + (guv != nullptr ? " iterA " + juce::String (guv->debugIterationsA(), 2) + " iterB " + juce::String (guv->debugIterationsB(), 2) + " failRate " + juce::String (guv->getSolveFailureRate(), 5) + " stage2 " + juce::String (guv->debugStage2Out(), 3) + " led " + juce::String (guv->debugLedNode(), 3) : juce::String()));
                }
                totalMs += ms;
                bool finite = true;
                for (int i = 0; i < 128; ++i)
                {
                    const float y = buf.getSample (0, i);
                    finite = finite && std::isfinite (y);
                    outSq += (double) y * y;
                }
                ++blocks;
                if (! finite)
                    ++nonFinite;
                if (inSq / 128.0 > 1.0e-3 && outSq / 128.0 < 1.0e-10 && levelUp)
                {
                    ++deadBlocks;
                    if (firstDead < 0)
                    {
                        firstDead = b;
                        juce::String knobs;
                        for (auto* p : params)
                            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                                knobs += f->getName (20) + "=" + juce::String (f->get(), 3) + " ";
                        logMessage ("  first dead block " + juce::String (b) + ": in rms " + juce::String (std::sqrt (inSq / 128.0), 3) + " knobs " + knobs
                                    + (guv != nullptr ? " stage1 " + juce::String (guv->debugStage1Out(), 3) + " stage2 " + juce::String (guv->debugStage2Out(), 3)
                                                        + " led " + juce::String (guv->debugLedNode(), 3) + " fail " + juce::String (guv->getSolveFailureRate(), 5) : juce::String()));
                    }
                }
            }
            logMessage ("trial " + juce::String (trial) + ": blocks " + juce::String (blocks) + ", dead " + juce::String (deadBlocks)
                        + " (first at block " + juce::String (firstDead) + "), non-finite " + juce::String (nonFinite) + ", avg " + juce::String (totalMs / (double) blocks, 3) + " ms/block"
                        + (guv != nullptr ? " failRate " + juce::String (guv->getSolveFailureRate(), 6) : juce::String())
                        + (rat != nullptr ? " failRate " + juce::String (rat->getSolveFailureRate(), 6) : juce::String())
                        + (clip != nullptr ? " failRate " + juce::String (clip->getSolveFailureRate(), 6) : juce::String())
                        + (bb != nullptr ? " failRate " + juce::String (bb->getSolveFailureRate(), 6) : juce::String()));
        }
    }
};

static PedalDeathProbe pedalDeathProbe;

} // namespace openguitarmultifx

#include "Effects/BassmanStyleAmplifierProcessor.h"

namespace openguitarmultifx
{

/** Dev-only probe (BASSMAN_PROBE=<pedal registry key>): a loud pedal (every knob at its maximum) into the Bassman, with
    plucked notes; counts the amp's solver failures and recoveries (a recovery resets the amp and clicks) and looks for
    output discontinuities. */
class BassmanHotInputProbe : public juce::UnitTest
{
public:
    BassmanHotInputProbe() : juce::UnitTest ("BassmanHotInputProbe", "Probe") {}

    void runTest() override
    {
        const char* pedalKey = std::getenv ("BASSMAN_PROBE");
        if (pedalKey == nullptr)
            return;
        const double gainScale = std::getenv ("BASSMAN_PROBE_SCALE") != nullptr ? std::atof (std::getenv ("BASSMAN_PROBE_SCALE")) : 1.0;

        beginTest (juce::String ("Bassman hot input: ") + pedalKey);
        juce::ScopedNoDenormals noDenormals;
        EffectRegistry registry;
        registerBuiltInEffects (registry);
        // BASSMAN_SR: the global feedback loop with an inductive speaker load is only marginally stable at 48 kHz
        // (docs/circuits/Bassman5F6A.md) -- running the same signal at 96 kHz tells apart "the model is unstable"
        // from "the amp really does kick that hard".
        const double sr = std::getenv ("BASSMAN_SR") != nullptr ? std::atof (std::getenv ("BASSMAN_SR")) : 48000.0;

        auto pedal = registry.create (pedalKey);
        pedal->prepare (sr, 128, 2);
        for (auto* p : pedal->getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                *f = f->getNormalisableRange().end;

        BassmanStyleAmplifierProcessor amp;
        amp.prepare (sr, 128, 2);
        if (std::getenv ("BASSMAN_KNOBS_MAX") != nullptr)
            for (auto* p : amp.getParameters()->getParameters (true))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                    if (f->paramID == "bm_vol_normal" || f->paramID == "bm_vol_bright" || f->paramID == "bm_power")
                        *f = f->getNormalisableRange().end;
        if (std::getenv ("BASSMAN_INPUT") != nullptr)
            for (auto* p : amp.getParameters()->getParameters (true))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                    if (f->paramID == "bm_input")
                        *f = (float) std::atof (std::getenv ("BASSMAN_INPUT"));
        // Speaker load and Presence both shape the global feedback loop, which the docs flag as marginally stable
        // at 48 kHz with an inductive load -- sweeping them separates "physical transformer kick" from "the loop
        // burst into oscillation for a few samples".
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
            {
                if (std::getenv ("BASSMAN_SPEAKER") != nullptr && f->paramID == "bm_speaker")
                    *f = (float) std::atof (std::getenv ("BASSMAN_SPEAKER"));
                if (std::getenv ("BASSMAN_PRESENCE") != nullptr && f->paramID == "bm_presence")
                    *f = (float) std::atof (std::getenv ("BASSMAN_PRESENCE"));
            }

        juce::Random rng (99);
        juce::AudioBuffer<float> buf (2, 128);
        double noteFreq = 110.0, noteAmp = 0.3, notePhase = 0.0;
        long long sinceNote = 0;
        double pedalPeak = 0.0, worstJump = 0.0, worstJumpRatio = 0.0;
        float last = 0.0f;
        double rmsAcc = 0.0;
        long long rmsCount = 0, popCount = 0;
        int lastRecoveries = 0, loggedFails = 0;
        double ampMs = 0.0, pedalMs = 0.0;
        long long blocksTimed = 0;
        for (long long b = 0; b < (long long) (30.0 * sr / 128.0); ++b)
        {
            for (int i = 0; i < 128; ++i)
            {
                if (++sinceNote > (long long) (0.5 * sr * (0.5 + rng.nextFloat())))
                {
                    sinceNote = 0;
                    noteFreq = 82.0 * std::pow (2.0, rng.nextInt (30) / 12.0);
                    noteAmp = 0.1 + 0.6 * rng.nextFloat();
                }
                noteAmp *= 0.99998;
                notePhase += 2.0 * juce::MathConstants<double>::pi * noteFreq / sr;
                const float x = (float) (noteAmp * (std::sin (notePhase) + 0.4 * std::sin (2.0 * notePhase) + 0.2 * std::sin (3.0 * notePhase)));
                buf.setSample (0, i, x);
                buf.setSample (1, i, x);
            }
            {
                const auto p0 = std::chrono::steady_clock::now();
                pedal->process (buf);
                pedalMs += std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - p0).count();
            }
            for (int i = 0; i < 128; ++i)
            {
                pedalPeak = juce::jmax (pedalPeak, (double) std::abs (buf.getSample (0, i)));
                buf.setSample (0, i, (float) (gainScale * buf.getSample (0, i)));
                buf.setSample (1, i, (float) (gainScale * buf.getSample (1, i)));
            }
            const long long failsBefore = amp.debugPowerFailures() + amp.debugPreFailures();
            double inPeak = 0.0;
            for (int i = 0; i < 128; ++i)
                inPeak = juce::jmax (inPeak, (double) std::abs (buf.getSample (0, i)));
            const double tFrom = std::getenv ("BASSMAN_TRACE_FROM") ? std::atof (std::getenv ("BASSMAN_TRACE_FROM")) : -1.0;
            const double tTo = std::getenv ("BASSMAN_TRACE_TO") ? std::atof (std::getenv ("BASSMAN_TRACE_TO")) : -1.0;
            if (tFrom >= 0.0 && (double) b * 128.0 / sr >= tFrom && (double) b * 128.0 / sr <= tTo)
            {
                juce::AudioBuffer<float> one (2, 1);
                using P = BassmanStyleAmplifierProcessor::Probe;
                for (int i = 0; i < 128; ++i)
                {
                    one.setSample (0, 0, buf.getSample (0, i));
                    one.setSample (1, 0, buf.getSample (1, i));
                    amp.process (one);
                    buf.setSample (0, i, one.getSample (0, 0));
                    buf.setSample (1, i, one.getSample (1, 0));
                    if ((i % (std::getenv ("BASSMAN_TRACE_STRIDE") ? std::atoi (std::getenv ("BASSMAN_TRACE_STRIDE")) : 16)) == 0)
                        logMessage ("TR " + juce::String ((double) (b * 128 + i) / sr, 5) + " in " + juce::String (buf.getSample (0, i), 2) + " fol " + juce::String (amp.debugVoltage (P::followerOut), 0) + " tone " + juce::String (amp.debugVoltage (P::toneStackOut), 1) + " piG " + juce::String (amp.debugVoltage (P::phaseInverterGrid), 1) + " gA " + juce::String (amp.debugVoltage (P::powerGridA), 1) + " pA " + juce::String (amp.debugVoltage (P::powerPlateA), 0) + " pB " + juce::String (amp.debugVoltage (P::powerPlateB), 0) + " spk " + juce::String (amp.debugVoltage (P::speaker), 1) + " fails " + juce::String (amp.debugPowerFailures()));
                }
            }
            else
            {
                const auto a0 = std::chrono::steady_clock::now();
                amp.process (buf);
                ampMs += std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - a0).count();
                ++blocksTimed;
            }
            if (amp.debugPowerFailures() + amp.debugPreFailures() != failsBefore && loggedFails < 4)
            {
                ++loggedFails;
                using P = BassmanStyleAmplifierProcessor::Probe;
                logMessage ("  fail block at " + juce::String ((double) b * 128.0 / sr, 3) + " s: amp input peak " + juce::String (inPeak, 2) + " V; follower " + juce::String (amp.debugVoltage (P::followerOut), 1)
                            + " toneOut " + juce::String (amp.debugVoltage (P::toneStackOut), 1) + " piGrid " + juce::String (amp.debugVoltage (P::phaseInverterGrid), 1)
                            + " pwrGridA " + juce::String (amp.debugVoltage (P::powerGridA), 1) + " pwrPlateA " + juce::String (amp.debugVoltage (P::powerPlateA), 1)
                            + " pwrPlateB " + juce::String (amp.debugVoltage (P::powerPlateB), 1) + " speaker " + juce::String (amp.debugVoltage (P::speaker), 1));
            }
            for (int i = 0; i < 128; ++i)
            {
                const float y = buf.getSample (0, i);
                const double jump = std::abs (y - last);
                const double rms = std::sqrt (rmsAcc);
                if (++rmsCount > 48000 && rms > 1.0e-4)
                {
                    worstJump = juce::jmax (worstJump, jump);
                    worstJumpRatio = juce::jmax (worstJumpRatio, jump / rms);
                }
                rmsAcc = 0.9996 * rmsAcc + 0.0004 * (double) y * y;
                last = y;
            }
            if (amp.debugRecoveries() != lastRecoveries)
            {
                ++popCount;
                lastRecoveries = amp.debugRecoveries();
                logMessage ("  recovery at " + juce::String ((double) b * 128.0 / sr, 2) + " s");
            }
        }
        logMessage ("pedal peak " + juce::String (pedalPeak, 2) + " V into the amp; amp failRate " + juce::String (amp.getSolveFailureRate(), 6)
                    + " (pre " + juce::String (amp.debugPreFailures()) + ", power " + juce::String (amp.debugPowerFailures()) + "), recoveries " + juce::String (amp.debugRecoveries()) + ", worst sample jump " + juce::String (worstJump, 3)
                    + " (" + juce::String (worstJumpRatio, 1) + "x the running rms)");
        if (blocksTimed > 0)
        {
            const double blockBudgetMs = 128.0 / sr * 1000.0;
            const double ampPctOfCore = 100.0 * (ampMs / (double) blocksTimed) / blockBudgetMs;
            const double pedalPctOfCore = 100.0 * (pedalMs / (double) blocksTimed) / blockBudgetMs;
            logMessage ("CPU: pedal " + juce::String (pedalPctOfCore, 2) + "% amp " + juce::String (ampPctOfCore, 2)
                        + "% (pre iters/sample " + juce::String (amp.debugIterations (0), 2) + ", power iters/sample "
                        + juce::String (amp.debugIterations (1), 2) + ")");
        }
        logMessage ("sanity rejects (solver converged, output HELD for that sample -> a step): "
                    + juce::String (amp.debugSanityRejects()) + " = "
                    + juce::String (amp.debugSanityRejects() / 30.0, 1) + "/s; worst rejected speaker volts "
                    + juce::String (amp.debugWorstRejectedVolts(), 1) + " V; largest ACCEPTED (legitimate) swing "
                    + juce::String (amp.debugWorstSaneVolts(), 1) + " V");
        {
        }
    }
};

static BassmanHotInputProbe bassmanHotInputProbe;

/** Dev-only (BASSMAN_TOL_SWEEP=1): what Newton's convergence tolerance actually buys. The solver stops when the
    leftover error mapped to a node is below `nodeTolerance`, 10 uV by default -- on a power stage that swings
    +-400 V that is 25 parts per billion, far below anything audible, and every extra iteration it demands costs
    the same as the ones that did the real work. This renders the SAME hot-pedal-into-the-amp signal through one
    amp per tolerance and reports, against the 10 uV reference: cost, iterations per sample, and how far the
    output actually moved (peak and RMS of the difference, in dB relative to the reference's own level). */
class BassmanToleranceSweep : public juce::UnitTest
{
public:
    BassmanToleranceSweep() : juce::UnitTest ("BassmanToleranceSweep", "Probe") {}

    void runTest() override
    {
        if (std::getenv ("BASSMAN_TOL_SWEEP") == nullptr)
            return;

        beginTest ("Newton node tolerance: cost against fidelity");
        juce::ScopedNoDenormals noDenormals;
        const double sr = 48000.0;
        const int blockSize = 128;
        const double seconds = std::getenv ("BASSMAN_TOL_SECONDS") != nullptr ? std::atof (std::getenv ("BASSMAN_TOL_SECONDS")) : 8.0;
        const int totalBlocks = (int) (seconds * sr / blockSize);
        const bool knobsMax = std::getenv ("BASSMAN_KNOBS_MAX") != nullptr;
        const int inputChoice = std::getenv ("BASSMAN_INPUT") != nullptr ? std::atoi (std::getenv ("BASSMAN_INPUT")) : 0;

        // The pedal in front is rendered ONCE and reused, so every tolerance sees a bit-identical input.
        std::vector<float> drive;
        drive.reserve ((size_t) (totalBlocks * blockSize));
        {
            EffectRegistry registry;
            registerBuiltInEffects (registry);
            auto pedal = registry.create ("HM2StyleDistortion");
            pedal->prepare (sr, blockSize, 2);
            for (auto* p : pedal->getParameters()->getParameters (true))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                    *f = f->getNormalisableRange().end;

            juce::Random rng (99);
            juce::AudioBuffer<float> buf (2, blockSize);
            double noteFreq = 110.0, noteAmp = 0.3, notePhase = 0.0;
            long long sinceNote = 0;
            for (int b = 0; b < totalBlocks; ++b)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    if (++sinceNote > (long long) (0.5 * sr * (0.5 + rng.nextFloat())))
                    {
                        sinceNote = 0;
                        noteFreq = 82.0 * std::pow (2.0, rng.nextInt (30) / 12.0);
                        noteAmp = 0.1 + 0.6 * rng.nextFloat();
                    }
                    noteAmp *= 0.99998;
                    notePhase += 2.0 * juce::MathConstants<double>::pi * noteFreq / sr;
                    const float x = (float) (noteAmp * (std::sin (notePhase) + 0.4 * std::sin (2.0 * notePhase) + 0.2 * std::sin (3.0 * notePhase)));
                    buf.setSample (0, i, x);
                    buf.setSample (1, i, x);
                }
                pedal->process (buf);
                for (int i = 0; i < blockSize; ++i)
                    drive.push_back (buf.getSample (0, i));
            }
        }

        struct Run { double tol, pct, preIters, powIters; int recoveries; std::vector<float> out; };
        std::vector<Run> runs;
        for (double tol : { 1.0e-5, 1.0e-4, 1.0e-3, 5.0e-3, 2.0e-2 })
        {
            const double previousDefault = NodalCircuit::defaultNodeTolerance;
            NodalCircuit::defaultNodeTolerance = tol;
            BassmanStyleAmplifierProcessor amp;   // picks the default up at construction/prepare
            amp.prepare (sr, blockSize, 2);
            NodalCircuit::defaultNodeTolerance = previousDefault;

            for (auto* p : amp.getParameters()->getParameters (true))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                {
                    if (knobsMax && (f->paramID == "bm_vol_normal" || f->paramID == "bm_vol_bright" || f->paramID == "bm_power"))
                        *f = f->getNormalisableRange().end;
                    if (f->paramID == "bm_input")
                        *f = (float) inputChoice;
                }

            Run r { tol, 0.0, 0.0, 0.0, 0, {} };
            r.out.reserve (drive.size());
            juce::AudioBuffer<float> buf (2, blockSize);
            double ms = 0.0;
            for (int b = 0; b < totalBlocks; ++b)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const float x = drive[(size_t) (b * blockSize + i)];
                    buf.setSample (0, i, x);
                    buf.setSample (1, i, x);
                }
                const auto t0 = std::chrono::steady_clock::now();
                amp.process (buf);
                ms += std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
                for (int i = 0; i < blockSize; ++i)
                    r.out.push_back (buf.getSample (0, i));
            }
            r.pct = 100.0 * (ms / (double) totalBlocks) / (blockSize / sr * 1000.0);
            r.preIters = amp.debugIterations (0);
            r.powIters = amp.debugIterations (1);
            r.recoveries = amp.debugRecoveries();
            runs.push_back (std::move (r));
        }

        const auto& ref = runs.front();
        double refPeak = 0.0, refSq = 0.0;
        for (float v : ref.out) { refPeak = juce::jmax (refPeak, (double) std::abs (v)); refSq += (double) v * v; }
        const double refRms = std::sqrt (refSq / (double) ref.out.size());

        for (const auto& r : runs)
        {
            double dPeak = 0.0, dSq = 0.0;
            for (size_t i = 0; i < r.out.size(); ++i)
            {
                const double d = (double) r.out[i] - (double) ref.out[i];
                dPeak = juce::jmax (dPeak, std::abs (d));
                dSq += d * d;
            }
            const double dRms = std::sqrt (dSq / (double) r.out.size());
            const auto dB = [] (double a, double b) { return 20.0 * std::log10 (juce::jmax (a, 1.0e-12) / juce::jmax (b, 1.0e-12)); };
            logMessage ("tol " + juce::String (r.tol * 1.0e6, 0) + " uV: " + juce::String (r.pct, 2) + "% of a core"
                        + "  iters pre " + juce::String (r.preIters, 2) + " pow " + juce::String (r.powIters, 2)
                        + "  recoveries " + juce::String (r.recoveries)
                        + "  diff vs 10uV: peak " + juce::String (dB (dPeak, refPeak), 1) + " dB, rms "
                        + juce::String (dB (dRms, refRms), 1) + " dB");
        }
    }
};

static BassmanToleranceSweep bassmanToleranceSweep;

/** Dev-only (BASSMAN_SNUB_FIDELITY=1): what the primary snubber costs tonally. Renders the same guitar-like
    signal through the amp with and without it and compares octave-band energy -- the project's standard way of
    judging a circuit change (a waveform difference is meaningless here: the amp is chaotic at high gain). */
class BassmanSnubberFidelity : public juce::UnitTest
{
public:
    BassmanSnubberFidelity() : juce::UnitTest ("BassmanSnubberFidelity", "Probe") {}

    void runTest() override
    {
        if (std::getenv ("BASSMAN_SNUB_FIDELITY") == nullptr)
            return;

        beginTest ("primary snubber: octave-band cost");
        juce::ScopedNoDenormals noDenormals;
        const double sr = 48000.0;
        const int block = 512, blocks = 300;

        const auto render = [&] (const char* r, const char* c, float drive)
        {
            if (r != nullptr) { setenv ("BM_SNUB_R", r, 1); setenv ("BM_SNUB_C", c, 1); }
            else { unsetenv ("BM_SNUB_R"); unsetenv ("BM_SNUB_C"); }
            BassmanStyleAmplifierProcessor amp;
            amp.prepare (sr, block, 1);
            for (auto* p : amp.getParameters()->getParameters (true))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                    if (f->paramID == "bm_vol_normal")
                        *f = drive;
            juce::AudioBuffer<float> buf (1, block);
            std::vector<float> out;
            out.reserve ((size_t) (blocks * block));
            double phase = 0.0;
            juce::Random rng (7);
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < block; ++i)
                {
                    // a broadband, guitar-level excitation so every octave band is driven
                    phase += 2.0 * juce::MathConstants<double>::pi * 220.0 / sr;
                    const double x = 0.25 * (std::sin (phase) + 0.3 * std::sin (3.0 * phase)) + 0.02 * (rng.nextDouble() - 0.5);
                    buf.setSample (0, i, (float) x);
                }
                amp.process (buf);
                for (int i = 0; i < block; ++i)
                    out.push_back (buf.getSample (0, i));
            }
            unsetenv ("BM_SNUB_R"); unsetenv ("BM_SNUB_C");
            return out;
        };

        juce::dsp::FFT fft (14);
        const auto band = [&] (const std::vector<float>& x, double lo, double hi)
        {
            double total = 0.0;
            for (int off = 20000; off + 16384 < (int) x.size(); off += 16384)
            {
                std::vector<float> d (32768, 0.0f);
                for (int i = 0; i < 16384; ++i)
                    d[(size_t) i] = x[(size_t) (off + i)] * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / 16384.0f));
                fft.performFrequencyOnlyForwardTransform (d.data());
                for (int k = (int) (lo / sr * 32768); k < (int) (hi / sr * 32768) && k < 16384; ++k)
                    total += (double) d[(size_t) k] * d[(size_t) k];
            }
            return 10.0 * std::log10 (total + 1.0e-30);
        };

        for (float drive : { 0.35f, 1.0f })
        {
            const auto base = render (nullptr, nullptr, drive);
            const auto snub = render ("3300", "3.3e-9", drive);
            juce::String line;
            double worst = 0.0;
            for (auto [lo, hi] : { std::pair<double, double> { 60, 125 }, { 125, 250 }, { 250, 500 }, { 500, 1000 },
                                   { 1000, 2000 }, { 2000, 4000 }, { 4000, 8000 }, { 8000, 16000 } })
            {
                const double d = band (snub, lo, hi) - band (base, lo, hi);
                worst = juce::jmax (worst, std::abs (d));
                line += juce::String ((int) lo) + "-" + juce::String ((int) hi) + "Hz " + juce::String (d, 2) + " dB  ";
            }
            logMessage ("Volume " + juce::String (drive, 2) + ": " + line);
            logMessage ("   worst band " + juce::String (worst, 2) + " dB");
        }
    }
};

static BassmanSnubberFidelity bassmanSnubberFidelity;

} // namespace openguitarmultifx
