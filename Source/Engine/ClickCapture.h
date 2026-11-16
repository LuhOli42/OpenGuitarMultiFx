#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <vector>

namespace openguitarmultifx
{

/** Keeps the last second of the device input and output (mono) in a ring and, when a click is reported, lets the
    control thread save ~150 ms before and after it as a 2-channel WAV (L = input, R = output), so a pop can be looked
    at instead of guessed. The audio thread only writes samples and flags; the WAV is written off it. A capture is
    taken at most every few seconds, and only a handful are kept. */
class ClickCapture
{
public:
    void prepare (double sampleRate)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        size = (int) rate; // 1 s
        input.assign ((size_t) size, 0.0f);
        output.assign ((size_t) size, 0.0f);
        writePos.store (0);
        pendingAt.store (-1);
    }

    /** Audio thread: one block of input and output. `clickInBlock` = a detector fired somewhere in it. */
    void push (const float* in, const float* out, int numSamples, bool clickInBlock, bool clickWasInInput) noexcept
    {
        if (size == 0)
            return;
        auto pos = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            input[(size_t) pos] = in != nullptr ? in[i] : 0.0f;
            output[(size_t) pos] = out != nullptr ? out[i] : 0.0f;
            pos = (pos + 1) % size;
        }
        writePos.store (pos, std::memory_order_release);
        totalWritten += numSamples;

        if (clickInBlock && pendingAt.load (std::memory_order_relaxed) < 0)
        {
            pendingInput.store (clickWasInInput, std::memory_order_relaxed);
            pendingAt.store (totalWritten, std::memory_order_release);
        }
    }

    /** Control thread, from a timer: writes the WAV once enough audio after the click has arrived. */
    void service (const juce::File& folder)
    {
        const auto at = pendingAt.load (std::memory_order_acquire);
        if (at < 0 || size == 0)
            return;
        const int half = (int) (0.15 * rate);
        if (totalWritten - at < half)
            return;

        const auto nowMs = juce::Time::getMillisecondCounter();
        if (savedCount < maxFiles && nowMs - lastSaveMs > 4000)
        {
            // Copy the 2*half samples ending `half` after the click, reading back from the current write position.
            const auto end = writePos.load (std::memory_order_acquire);
            const auto lag = (int) (totalWritten - at) - half; // samples written since the window's end
            juce::AudioBuffer<float> clip (2, 2 * half);
            for (int i = 0; i < 2 * half; ++i)
            {
                const int idx = ((end - lag - 2 * half + i) % size + 2 * size) % size;
                clip.setSample (0, i, input[(size_t) idx]);
                clip.setSample (1, i, output[(size_t) idx]);
            }

            folder.createDirectory();
            const auto name = juce::Time::getCurrentTime().formatted ("click-%Y%m%d-%H%M%S")
                              + (pendingInput.load() ? "-in" : "-out") + ".wav";
            const auto file = folder.getChildFile (name);
            if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
            {
                juce::WavAudioFormat wav;
                if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (
                        wav.createWriterFor (stream.get(), rate, 2, 24, {}, 0)))
                {
                    stream.release();
                    writer->writeFromAudioSampleBuffer (clip, 0, clip.getNumSamples());
                    ++savedCount;
                    lastSaveMs = nowMs;
                    juce::Logger::writeToLog ("ClickCapture: saved " + file.getFullPathName());
                }
            }
        }
        pendingAt.store (-1, std::memory_order_release);
    }

private:
    static constexpr int maxFiles = 12;
    double rate = 48000.0;
    int size = 0;
    std::vector<float> input, output;
    std::atomic<int> writePos { 0 };
    std::atomic<juce::int64> pendingAt { -1 };
    std::atomic<bool> pendingInput { false };
    std::atomic<juce::int64> totalWritten { 0 };
    int savedCount = 0;
    juce::uint32 lastSaveMs = 0;
};

} // namespace openguitarmultifx
