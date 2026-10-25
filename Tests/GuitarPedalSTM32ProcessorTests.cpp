#include "Effects/GuitarPedalSTM32Processor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

namespace openguitarmultifx
{

namespace
{
    void setParameter (GuitarPedalSTM32Processor& processor, const juce::String& id, float value)
    {
        for (auto* parameter : processor.getParameters()->getParameters (true))
            if (auto* floatParameter = dynamic_cast<juce::AudioParameterFloat*> (parameter);
                floatParameter != nullptr && floatParameter->paramID == id)
                *floatParameter = value;
    }

    std::vector<float> process (GuitarPedalSTM32Processor& processor, const std::vector<float>& input, int blockSize = 256)
    {
        std::vector<float> output (input.size());
        juce::AudioBuffer<float> block (1, blockSize);
        for (size_t offset = 0; offset < input.size(); offset += static_cast<size_t> (blockSize))
        {
            const auto count = static_cast<int> (std::min (static_cast<size_t> (blockSize), input.size() - offset));
            for (int i = 0; i < count; ++i)
                block.setSample (0, i, input[offset + static_cast<size_t> (i)]);
            block.setSize (1, count, false, false, true);
            processor.process (block);
            for (int i = 0; i < count; ++i)
                output[offset + static_cast<size_t> (i)] = block.getSample (0, i);
            block.setSize (1, blockSize, false, false, true);
        }
        return output;
    }

    double rms (const std::vector<float>& samples, size_t start, size_t end)
    {
        double sum = 0.0;
        end = std::min (end, samples.size());
        for (auto i = start; i < end; ++i)
            sum += static_cast<double> (samples[i]) * samples[i];
        return end > start ? std::sqrt (sum / static_cast<double> (end - start)) : 0.0;
    }

    bool allFinite (const std::vector<float>& samples)
    {
        return std::all_of (samples.begin(), samples.end(), [] (float sample) { return std::isfinite (sample); });
    }

    double amplitudeAt (const std::vector<float>& samples, size_t start, size_t end, double frequency, double sampleRate)
    {
        double real = 0.0;
        double imaginary = 0.0;
        const auto count = std::min (end, samples.size()) - start;
        for (size_t i = start; i < start + count; ++i)
        {
            const auto phase = 2.0 * juce::MathConstants<double>::pi * frequency * static_cast<double> (i - start) / sampleRate;
            real += static_cast<double> (samples[i]) * std::cos (phase);
            imaginary -= static_cast<double> (samples[i]) * std::sin (phase);
        }
        return count > 0 ? 2.0 * std::hypot (real, imaginary) / static_cast<double> (count) : 0.0;
    }

    double crossingFrequency (const std::vector<float>& samples, size_t start, size_t end, double sampleRate)
    {
        int crossings = 0;
        end = std::min (end, samples.size());
        for (auto i = start + 1; i < end; ++i)
            if (samples[i - 1] <= 0.0f && samples[i] > 0.0f)
                ++crossings;
        return end > start ? static_cast<double> (crossings) * sampleRate / static_cast<double> (end - start) : 0.0;
    }

    void prepare (GuitarPedalSTM32Processor& processor, double sampleRate)
    {
        processor.prepare (sampleRate, 256, 1);
    }
}

class GuitarPedalSTM32ProcessorTests final : public juce::UnitTest
{
public:
    GuitarPedalSTM32ProcessorTests() : juce::UnitTest ("GuitarPedalSTM32Processor", "DSP") {}

    void runTest() override
    {
        beginTest ("Plucked tone remains audible and leaves a wet tail for every routing and delay mode");
        for (int route = 0; route < 3; ++route)
        {
            for (int mode = 0; mode < 3; ++mode)
            {
                GuitarPedalSTM32Processor processor;
                setParameter (processor, "gpstm_routing", static_cast<float> (route));
                setParameter (processor, "gpstm_delay_mode", static_cast<float> (mode));
                setParameter (processor, "gpstm_delay_time", 200.0f);
                setParameter (processor, "gpstm_delay_feedback", 0.3f);
                prepare (processor, 48000.0);

                std::vector<float> input (96000, 0.0f);
                for (size_t i = 0; i < 48000; ++i)
                {
                    const auto time = static_cast<double> (i) / 48000.0;
                    input[i] = static_cast<float> (0.5 * std::exp (-4.0 * time)
                                                   * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * time));
                }

                const auto output = process (processor, input);
                const auto inputRms = rms (input, 0, input.size());
                const auto outputRms = rms (output, 0, output.size());
                expect (allFinite (output), "Routing " + juce::String (route) + ", mode " + juce::String (mode) + " produced non-finite samples");
                expect (outputRms >= inputRms * 0.501 && outputRms <= inputRms * 3.981,
                        "Output RMS was outside -6..+12 dB of input (route " + juce::String (route) + ", mode " + juce::String (mode) + ")");
                expect (rms (output, 48000, 96000) > 1.0e-7,
                        "Wet tail was silent (route " + juce::String (route) + ", mode " + juce::String (mode) + ")");
            }
        }

        beginTest ("Zero mix on both paths is unity dry");
        {
            GuitarPedalSTM32Processor processor;
            setParameter (processor, "gpstm_reverb_mix", 0.0f);
            setParameter (processor, "gpstm_delay_mix", 0.0f);
            prepare (processor, 48000.0);
            std::vector<float> input (8192);
            for (size_t i = 0; i < input.size(); ++i)
                input[i] = static_cast<float> (0.2 * std::sin (0.017 * static_cast<double> (i)));
            const auto output = process (processor, input);
            bool equal = true;
            for (size_t i = 0; i < input.size(); ++i)
                equal = equal && std::abs (output[i] - input[i]) <= 1.0e-6f;
            expect (equal);
        }

        beginTest ("Normal delay first echo follows the millisecond delay time");
        for (const auto sampleRate : { 48000.0, 44100.0, 96000.0 })
        {
            GuitarPedalSTM32Processor processor;
            setParameter (processor, "gpstm_routing", 1.0f);
            setParameter (processor, "gpstm_delay_mode", 0.0f);
            setParameter (processor, "gpstm_reverb_mix", 0.0f);
            setParameter (processor, "gpstm_delay_mix", 1.0f);
            setParameter (processor, "gpstm_delay_time", 250.0f);
            setParameter (processor, "gpstm_delay_feedback", 0.0f);
            prepare (processor, sampleRate);
            std::vector<float> input (static_cast<size_t> (sampleRate * 0.5), 0.0f);
            input[0] = 1.0f;
            const auto output = process (processor, input);

            const auto expected = static_cast<size_t> (std::round (0.25 * sampleRate));
            auto peak = expected;
            for (size_t i = 1; i < output.size(); ++i)
                if (std::abs (output[i]) > std::abs (output[peak]))
                    peak = i;
            expect (std::abs (output[peak]) > 0.5f, "Delay echo was not audible");
            expectWithinAbsoluteError (static_cast<double> (peak) / sampleRate, 0.25, 0.002,
                                       "First delay echo should be at 250 ms");
            expect (allFinite (output));
        }

        beginTest ("Octave mode shifts the delayed component from 220 Hz to 440 Hz");
        {
            GuitarPedalSTM32Processor processor;
            setParameter (processor, "gpstm_routing", 1.0f);
            setParameter (processor, "gpstm_delay_mode", 1.0f);
            setParameter (processor, "gpstm_reverb_mix", 0.0f);
            setParameter (processor, "gpstm_delay_mix", 1.0f);
            setParameter (processor, "gpstm_delay_time", 300.0f);
            setParameter (processor, "gpstm_delay_feedback", 0.0f);
            prepare (processor, 48000.0);
            std::vector<float> input (76800);
            for (size_t i = 0; i < input.size(); ++i)
                input[i] = static_cast<float> (0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * static_cast<double> (i) / 48000.0));
            auto output = process (processor, input);
            for (size_t i = 0; i < output.size(); ++i)
                output[i] -= input[i];

            double bestFrequency = 0.0;
            double bestAmplitude = 0.0;
            for (double frequency = 400.0; frequency <= 480.0; frequency += 1.0)
            {
                const auto amplitude = amplitudeAt (output, 24000, output.size(), frequency, 48000.0);
                if (amplitude > bestAmplitude)
                {
                    bestAmplitude = amplitude;
                    bestFrequency = frequency;
                }
            }
            expectWithinAbsoluteError (bestFrequency, 440.0, 22.0);
            expect (bestAmplitude > amplitudeAt (output, 24000, output.size(), 220.0, 48000.0));
        }

        beginTest ("Reverse mode returns a rising chirp in descending frequency order");
        {
            GuitarPedalSTM32Processor processor;
            setParameter (processor, "gpstm_routing", 1.0f);
            setParameter (processor, "gpstm_delay_mode", 2.0f);
            setParameter (processor, "gpstm_reverb_mix", 0.0f);
            setParameter (processor, "gpstm_delay_mix", 1.0f);
            setParameter (processor, "gpstm_delay_time", 200.0f);
            setParameter (processor, "gpstm_delay_feedback", 0.0f);
            prepare (processor, 48000.0);
            std::vector<float> input (72000, 0.0f);
            for (size_t i = 0; i < 19200; ++i)
            {
                const auto time = static_cast<double> (i) / 48000.0;
                const auto phase = 2.0 * juce::MathConstants<double>::pi * (1000.0 * time + 2500.0 * time * time);
                input[i] = static_cast<float> (0.4 * std::sin (phase));
            }
            const auto output = process (processor, input);
            const auto earlyFrequency = crossingFrequency (output, 24000, 28800, 48000.0);
            const auto lateFrequency = crossingFrequency (output, 31200, 36000, 48000.0);
            expect (allFinite (output));
            expect (earlyFrequency > lateFrequency * 1.2,
                    "Reverse chirp frequencies should descend; early " + juce::String (earlyFrequency)
                        + " Hz, late " + juce::String (lateFrequency) + " Hz");
        }

        beginTest ("Hold sustains the effect tail and release restores decay");
        {
            GuitarPedalSTM32Processor processor;
            prepare (processor, 48000.0);
            std::vector<float> noise (48000);
            uint32_t randomState = 0x12345678u;
            for (size_t i = 0; i < noise.size(); ++i)
            {
                randomState = randomState * 1664525u + 1013904223u;
                noise[i] = (static_cast<float> (randomState >> 8) / 16777215.5f - 1.0f) * 0.35f;
            }
            process (processor, noise);

            setParameter (processor, "gpstm_hold", 1.0f);
            const std::vector<float> silence (240000, 0.0f);
            const auto heldOutput = process (processor, silence);
            const auto heldStartRms = rms (heldOutput, 0, 48000);
            const auto heldEndRms = rms (heldOutput, 192000, 240000);
            const auto heldPeak = std::max_element (heldOutput.begin(), heldOutput.end(),
                                                    [] (float a, float b) { return std::abs (a) < std::abs (b); });
            expect (heldStartRms > 0.0);
            expect (heldEndRms > heldStartRms * 0.01, "Held tail fell below -40 dB");
            expect (heldPeak == heldOutput.end() || std::abs (*heldPeak) < 2.0f, "Hold peak exceeded 2.0");

            setParameter (processor, "gpstm_hold", 0.0f);
            const auto releasedOutput = process (processor, silence);
            expect (rms (releasedOutput, 192000, 240000) < heldEndRms,
                    "Released tail should decay compared with the held tail");
        }

        beginTest ("Maximum reverb time and filter remain bounded over 30 seconds");
        {
            GuitarPedalSTM32Processor processor;
            setParameter (processor, "gpstm_routing", 1.0f);
            setParameter (processor, "gpstm_reverb_mix", 1.0f);
            setParameter (processor, "gpstm_reverb_time", 1.0f);
            setParameter (processor, "gpstm_reverb_filter", 1.0f);
            setParameter (processor, "gpstm_delay_mix", 0.0f);
            prepare (processor, 48000.0);
            std::vector<float> noise (1440000);
            uint32_t randomState = 0x9e3779b9u;
            for (size_t i = 0; i < noise.size(); ++i)
            {
                randomState = randomState * 1664525u + 1013904223u;
                noise[i] = (static_cast<float> (randomState >> 8) / 16777215.5f - 1.0f) * 0.25f;
            }
            const auto output = process (processor, noise, 512);
            const auto peak = std::accumulate (output.begin(), output.end(), 0.0f,
                                               [] (float maximum, float sample) { return std::max (maximum, std::abs (sample)); });
            expect (allFinite (output));
            expect (peak < 4.0f, "Peak exceeded 4.0: " + juce::String (peak));
        }
    }
};

static GuitarPedalSTM32ProcessorTests guitarPedalSTM32ProcessorTests;

} // namespace openguitarmultifx
