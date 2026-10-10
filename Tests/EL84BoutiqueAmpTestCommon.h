#pragma once

#include "Effects/EL84BoutiqueAmpCore.h"

#include <cmath>

namespace openguitarmultifx::tests
{

template <typename Amp>
void runEL84BoutiqueAmpTests (juce::UnitTest& test, const char* gainId, const char* toneId)
{
    constexpr double sr = 48000.0;
    Amp::reducedOrder = false;
    Amp amp;
    amp.prepare (sr, 512, 1);

    test.beginTest ("DC operating point");
    test.expect (amp.isDcConverged(), "DC operating point converged");
    test.logMessage ("B+ " + juce::String (amp.railPlates(), 1)
                     + " V; preamp " + juce::String (amp.debugVoltage (EL84BoutiqueAmpCore::Probe::preampPlate), 1)
                     + " V; PI " + juce::String (amp.debugVoltage (EL84BoutiqueAmpCore::Probe::piPlateA), 1)
                     + "/" + juce::String (amp.debugVoltage (EL84BoutiqueAmpCore::Probe::piPlateB), 1)
                     + " V; power " + juce::String (amp.debugVoltage (EL84BoutiqueAmpCore::Probe::powerPlateA), 1)
                     + "/" + juce::String (amp.debugVoltage (EL84BoutiqueAmpCore::Probe::powerPlateB), 1)
                     + " V; cathode " + juce::String (amp.debugVoltage (EL84BoutiqueAmpCore::Probe::cathodeBias), 1) + " V");
    const auto expectVoltage = [&] (double volts, double lower, double upper, const char* label)
    { test.expect (volts >= lower && volts <= upper, juce::String (label) + ": " + juce::String (volts, 2) + " V"); };
    expectVoltage (amp.railPlates(), 280.0, 450.0, "B+ rail");
    expectVoltage (amp.debugVoltage (EL84BoutiqueAmpCore::Probe::preampPlate), 20.0, 380.0, "preamp plate");
    expectVoltage (amp.debugVoltage (EL84BoutiqueAmpCore::Probe::piPlateA), 100.0, 390.0, "PI plate A");
    expectVoltage (amp.debugVoltage (EL84BoutiqueAmpCore::Probe::piPlateB), 100.0, 390.0, "PI plate B");
    expectVoltage (amp.debugVoltage (EL84BoutiqueAmpCore::Probe::powerPlateA), 100.0, 450.0, "power plate A");
    expectVoltage (amp.debugVoltage (EL84BoutiqueAmpCore::Probe::powerPlateB), 100.0, 450.0, "power plate B");
    expectVoltage (amp.debugVoltage (EL84BoutiqueAmpCore::Probe::cathodeBias), 1.0, 30.0, "shared cathode bias");
    expectVoltage (amp.debugVoltage (EL84BoutiqueAmpCore::Probe::screen), 200.0, 350.0, "screen voltage");

    test.beginTest ("ten seconds of silence remain quiet");
    juce::AudioBuffer<float> silence (1, 512);
    silence.clear();
    for (int block = 0; block < (int) (1.5 * sr / 512.0); ++block)
    {
        silence.clear();
        amp.process (silence);
    }
    double silencePeak = 0.0;
    for (int block = 0; block < (int) (10.0 * sr / 512.0); ++block)
    {
        silence.clear();
        amp.process (silence);
        for (int i = 0; i < silence.getNumSamples(); ++i)
            silencePeak = juce::jmax (silencePeak, std::abs ((double) silence.getSample (0, i)));
    }
    test.logMessage ("silence peak " + juce::String (silencePeak, 6));
    test.expect (silencePeak < 0.01, "silence peak below 0.01");

    test.beginTest ("plucked note output stays finite and bounded");
    double peak = 0.0;
    for (int block = 0; block < 180; ++block)
    {
        juce::AudioBuffer<float> signal (1, 512);
        for (int i = 0; i < signal.getNumSamples(); ++i)
        {
            const double t = (double) (block * 512 + i) / sr;
            const double envelope = std::exp (-3.0 * t);
            signal.setSample (0, i, (float) (0.35 * envelope * std::sin (2.0 * juce::MathConstants<double>::pi * 82.4 * t)));
        }
        amp.process (signal);
        for (int i = 0; i < signal.getNumSamples(); ++i)
        {
            const double value = signal.getSample (0, i);
            test.expect (std::isfinite (value), "plucked output is finite");
            peak = juce::jmax (peak, std::abs (value));
        }
    }
    test.expect (peak > 0.005 && peak < 1.5, "plucked output is audible and bounded");
    test.expect (amp.getSolveFailureRate() < 0.05, "Newton failure rate below 5%");

    const auto measureTone = [&] (double frequency, float gainValue, float toneValue, float inputLevel = 0.002f)
    {
        Amp::reducedOrder = false;
        amp.reset();
        juce::AudioParameterFloat* gain = nullptr;
        juce::AudioParameterFloat* tone = nullptr;
        for (auto* parameter : amp.getParameters()->getParameters (true))
            if (auto* value = dynamic_cast<juce::AudioParameterFloat*> (parameter))
            {
                if (value->paramID == gainId) gain = value;
                if (value->paramID == toneId) tone = value;
            }
        if (gain != nullptr) *gain = gainValue;
        if (tone != nullptr) *tone = toneValue;
        juce::AudioBuffer<float> signal (1, 4096);
        for (int i = 0; i < signal.getNumSamples(); ++i)
            signal.setSample (0, i, inputLevel * (float) std::sin (2.0 * juce::MathConstants<double>::pi * frequency * i / sr));
        amp.process (signal);
        double rms = 0.0;
        for (int i = 512; i < signal.getNumSamples(); ++i)
            rms += (double) signal.getSample (0, i) * signal.getSample (0, i);
        return std::sqrt (rms / (signal.getNumSamples() - 512));
    };

    test.beginTest ("gain and tone controls affect the signal");
    const double gainLow = measureTone (440.0, 0.1f, 0.5f);
    const double gainHigh = measureTone (440.0, 0.9f, 0.5f);
    const double toneDark = measureTone (4000.0, 0.6f, 0.1f);
    const double toneBright = measureTone (4000.0, 0.6f, 0.9f);
    test.logMessage ("gain RMS " + juce::String (gainLow, 6) + " -> " + juce::String (gainHigh, 6)
                     + "; 4 kHz tone RMS " + juce::String (toneDark, 6) + " -> " + juce::String (toneBright, 6));
    test.expect (gainHigh > gainLow * 1.05, "gain up increases output");
    test.expect (std::abs (toneBright - toneDark) > 1.0e-5, "tone control changes the high-frequency response");

    test.beginTest ("reduced-order power stage tracks the reference");
    const double reference = measureTone (220.0, 0.6f, 0.5f, 0.2f);
    Amp::reducedOrder = true;
    Amp reduced;
    reduced.prepare (sr, 512, 1);
    juce::AudioBuffer<float> signal (1, 4096);
    for (int i = 0; i < signal.getNumSamples(); ++i)
        signal.setSample (0, i, (float) (0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr)));
    reduced.process (signal);
    double reducedRms = 0.0;
    for (int i = 512; i < signal.getNumSamples(); ++i)
        reducedRms += (double) signal.getSample (0, i) * signal.getSample (0, i);
    reducedRms = std::sqrt (reducedRms / (signal.getNumSamples() - 512));
    Amp::reducedOrder = false;
    const double levelDeltaDb = 20.0 * std::log10 (juce::jmax (1.0e-9, reducedRms) / juce::jmax (1.0e-9, reference));
    test.logMessage ("reference/reduced RMS " + juce::String (reference, 6) + "/" + juce::String (reducedRms, 6)
                     + "; delta " + juce::String (levelDeltaDb, 2) + " dB");
    test.expect (std::abs (levelDeltaDb) < 1.5, "reduced/reference level difference within 1.5 dB");
}

}
