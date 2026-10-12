#pragma once

#include "Effects/EL84BoutiqueAmpCore.h"

#include <array>
#include <cmath>

namespace openguitarmultifx::tests
{

template <typename Amp>
double measureEL84ControlResponse (const char* parameterId, float value, double frequency, float inputLevel,
                                   const char* secondId = nullptr, float secondValue = 0.5f,
                                   bool reducedOrder = false)
{
    Amp::reducedOrder = reducedOrder;
    Amp amp;
    amp.prepare (48000.0, 512, 1);
    for (auto* parameter : amp.getParameters()->getParameters (true))
        if (auto* floatParameter = dynamic_cast<juce::AudioParameterFloat*> (parameter))
        {
            if (floatParameter->paramID == parameterId) *floatParameter = value;
            if (secondId != nullptr && floatParameter->paramID == secondId) *floatParameter = secondValue;
        }
    juce::AudioBuffer<float> signal (1, 8192);
    for (int i = 0; i < signal.getNumSamples(); ++i)
        signal.setSample (0, i, inputLevel * (float) std::sin (2.0 * juce::MathConstants<double>::pi * frequency * i / 48000.0));
    amp.process (signal);
    double energy = 0.0;
    for (int i = 2048; i < signal.getNumSamples(); ++i)
        energy += (double) signal.getSample (0, i) * signal.getSample (0, i);
    return std::sqrt (energy / (signal.getNumSamples() - 2048));
}

inline double levelDifferenceDb (double a, double b)
{
    return 20.0 * std::log10 (juce::jmax (1.0e-9, a) / juce::jmax (1.0e-9, b));
}

template <typename Amp>
void runEL84BoutiqueAmpTests (juce::UnitTest& test, const char* gainId, const char* toneId,
                              const std::array<double, 5>& dcExpected, const char* masterId,
                              bool toneIncreasesHighFrequency = true, double toneFrequency = 4000.0)
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
    const auto expectDesignValue = [&] (double volts, double expected, const char* label)
    { expectVoltage (volts, expected * 0.85, expected * 1.15, label); };
    expectDesignValue (amp.railPlates(), dcExpected[0], "B+ rail");
    expectDesignValue (amp.debugVoltage (EL84BoutiqueAmpCore::Probe::preampPlate), dcExpected[1], "preamp plate");
    expectDesignValue (amp.debugVoltage (EL84BoutiqueAmpCore::Probe::piPlateA), dcExpected[2], "PI plate A");
    expectDesignValue (amp.debugVoltage (EL84BoutiqueAmpCore::Probe::piPlateB), dcExpected[2], "PI plate B");
    expectDesignValue (amp.debugVoltage (EL84BoutiqueAmpCore::Probe::powerPlateA), dcExpected[3], "power plate A");
    expectDesignValue (amp.debugVoltage (EL84BoutiqueAmpCore::Probe::powerPlateB), dcExpected[3], "power plate B");
    expectDesignValue (amp.debugVoltage (EL84BoutiqueAmpCore::Probe::cathodeBias), dcExpected[4], "shared cathode bias");

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
        if (tone != nullptr)
        {
            const auto range = tone->getNormalisableRange();
            *tone = range.start + toneValue * (range.end - range.start);
        }
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
    const double toneDark = measureTone (toneFrequency, 0.4f, 0.0f, 0.02f);
    const double toneBright = measureTone (toneFrequency, 0.4f, 1.0f, 0.02f);
    test.logMessage ("gain RMS " + juce::String (gainLow, 6) + " -> " + juce::String (gainHigh, 6)
                     + "; " + juce::String (toneFrequency / 1000.0, 1) + " kHz tone RMS "
                     + juce::String (toneDark, 6) + " -> " + juce::String (toneBright, 6));
    test.expect (gainHigh > gainLow * 1.05, "gain up increases output");
    const double toneDeltaDb = levelDifferenceDb (toneBright, toneDark);
    test.expect (toneIncreasesHighFrequency ? toneDeltaDb >= 1.5 : toneDeltaDb <= -1.5,
                 "tone control changes high-frequency response in its specified direction by at least 1.5 dB");
    test.beginTest ("reduced-order master/volume control");
    Amp::reducedOrder = true;
    const double quiet = measureEL84ControlResponse<Amp> (masterId, 0.2f, 220.0, 0.02f, nullptr, 0.5f, true);
    const double loud = measureEL84ControlResponse<Amp> (masterId, 1.0f, 220.0, 0.02f, nullptr, 0.5f, true);
    Amp::reducedOrder = false;
    const double masterDeltaDb = levelDifferenceDb (quiet, loud);
    test.logMessage ("control 0.2/1.0 RMS " + juce::String (quiet, 6) + "/" + juce::String (loud, 6)
                     + "; delta " + juce::String (masterDeltaDb, 2) + " dB");
    test.expect (masterDeltaDb <= -6.0, "reduced-order Master/Volume 0.2 is at least 6 dB below 1.0");

    test.beginTest ("reduced-order power stage tracks the reference");
    for (const double level : { 0.002, 0.02, 0.2 })
    {
        const double reference = measureTone (220.0, 0.6f, 0.5f, (float) level);
        Amp::reducedOrder = true;
        Amp reduced;
        reduced.prepare (sr, 512, 1);
        for (auto* parameter : reduced.getParameters()->getParameters (true))
            if (auto* value = dynamic_cast<juce::AudioParameterFloat*> (parameter))
            {
                if (value->paramID == gainId) *value = 0.6f;
                if (value->paramID == toneId)
                {
                    const auto range = value->getNormalisableRange();
                    *value = range.start + 0.5f * (range.end - range.start);
                }
            }
        juce::AudioBuffer<float> signal (1, 4096);
        for (int i = 0; i < signal.getNumSamples(); ++i)
            signal.setSample (0, i, (float) (level * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr)));
        reduced.process (signal);
        double reducedRms = 0.0;
        for (int i = 512; i < signal.getNumSamples(); ++i)
            reducedRms += (double) signal.getSample (0, i) * signal.getSample (0, i);
        reducedRms = std::sqrt (reducedRms / (signal.getNumSamples() - 512));
        Amp::reducedOrder = false;
        const double levelDeltaDb = 20.0 * std::log10 (juce::jmax (1.0e-9, reducedRms) / juce::jmax (1.0e-9, reference));
        test.logMessage ("220 Hz input " + juce::String (level, 3) + ": full/reduced RMS "
                         + juce::String (reference, 6) + "/" + juce::String (reducedRms, 6)
                         + "; delta " + juce::String (levelDeltaDb, 2) + " dB");
        test.expect (std::abs (levelDeltaDb) < 1.5, "reduced/full tracking within 1.5 dB");
    }
    const auto topToTotal = [&] (bool reducedOrder)
    {
        Amp::reducedOrder = reducedOrder;
        Amp measured;
        measured.prepare (sr, 512, 1);
        for (auto* parameter : measured.getParameters()->getParameters (true))
            if (auto* value = dynamic_cast<juce::AudioParameterFloat*> (parameter))
            {
                if (value->paramID == gainId) *value = 0.6f;
                if (value->paramID == toneId)
                {
                    const auto range = value->getNormalisableRange();
                    *value = range.start + 0.5f * (range.end - range.start);
                }
            }
        juce::AudioBuffer<float> multitone (1, 8192);
        for (int i = 0; i < multitone.getNumSamples(); ++i)
        {
            const double t = (double) i / sr;
            multitone.setSample (0, i, (float) (0.01 * (std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)
                                                          + std::sin (2.0 * juce::MathConstants<double>::pi * 3750.0 * t))));
        }
        measured.process (multitone);
        constexpr int start = 4096, count = 4096;
        std::array<double, count> window {};
        double windowSum = 0.0;
        for (int i = 0; i < count; ++i)
        {
            window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / (count - 1));
            windowSum += window[(size_t) i];
        }
        double totalPower = 0.0, topBandPower = 0.0;
        for (int bin = 1; bin < count / 2; ++bin)
        {
            const double omega = 2.0 * juce::MathConstants<double>::pi * bin / count;
            const double coefficient = 2.0 * std::cos (omega);
            double previous = 0.0, previous2 = 0.0;
            for (int i = 0; i < count; ++i)
            {
                const double sample = multitone.getSample (0, start + i) * window[(size_t) i];
                const double current = sample + coefficient * previous - previous2;
                previous2 = previous;
                previous = current;
            }
            const double magnitudeSquared = juce::jmax (0.0, previous * previous + previous2 * previous2
                                                              - coefficient * previous * previous2);
            const double power = 2.0 * magnitudeSquared / (windowSum * windowSum);
            totalPower += power;
            const double frequency = (double) bin * sr / count;
            if (frequency >= 2000.0 && frequency <= 8000.0)
                topBandPower += power;
        }
        return 10.0 * std::log10 (juce::jmax (1.0e-12, topBandPower) / juce::jmax (1.0e-12, totalPower));
    };
    const double fullTopRatio = topToTotal (false);
    const double reducedTopRatio = topToTotal (true);
    Amp::reducedOrder = false;
    test.logMessage ("2-8 kHz/total ratio at 0.02 input: full/reduced "
                     + juce::String (fullTopRatio, 2) + "/" + juce::String (reducedTopRatio, 2) + " dB");
    test.expect (std::abs (fullTopRatio - reducedTopRatio) < 3.0, "reduced/full spectral ratio within 3 dB");
}

}
