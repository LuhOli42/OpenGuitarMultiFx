#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>

namespace openguitarmultifx
{

/**
    Live view of what the board is doing, drawn over the UI once a second: clock and busy-% of the slow cores (UI, cpu0-5)
    and of the fast cores (audio, cpu6-7), the GPU's busy-% (the PowerVR driver reports it; its clock is driver-managed and not
    exposed), and the hottest thermal zone. Bars turn amber past 70% busy and red past 90% -- the number a clock should be
    steered towards. It takes no clicks. Everything comes from /proc and /sys (no process is started); on a machine without
    those files the missing numbers read "n/a".
*/
class SystemMonitor : public juce::Component,
                      private juce::Timer
{
public:
    SystemMonitor()
    {
        setInterceptsMouseClicks (false, false);
        setOpaque (false);
    }

    void setRunning (bool shouldRun)
    {
        setVisible (shouldRun);
        if (shouldRun)
        {
            sample();
            startTimerHz (1);
        }
        else
            stopTimer();
    }

    static constexpr int preferredWidth = 460, preferredHeight = 168;

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme::current();
        auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.78f));
        g.fillRoundedRectangle (bounds, 12.0f);
        g.setColour (t.lineStrong);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 12.0f, 1.0f);

        auto area = bounds.reduced (14.0f, 10.0f);
        g.setColour (t.textMuted);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("SYSTEM  (tap TEMP to hide)", area.removeFromTop (16.0f).toNearestInt(), juce::Justification::centredLeft);

        row (g, area.removeFromTop (34.0f), "SLOW cores 0-5 (UI)", little);
        row (g, area.removeFromTop (34.0f), "FAST cores 6-7 (audio)", big);

        // GPU: busy-% from the driver; the clock is not exposed
        auto gpuRow = area.removeFromTop (34.0f);
        drawBusy (g, gpuRow, "GPU (PowerVR)", gpuBusy, gpuBusy < 0 ? "n/a" : "3D " + juce::String (gpu3d) + "%  |  clock: driver-managed");

        g.setColour (hottest >= 95.0f ? juce::Colour (0xffff4d6a) : hottest >= 85.0f ? juce::Colour (0xffffb020) : t.text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("TEMP " + (hottest > 0.0f ? juce::String (hottest, 0) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")) + "C" : juce::String ("n/a")),
                    area.removeFromTop (18.0f).toNearestInt(), juce::Justification::centredLeft);
    }

private:
    struct Cluster
    {
        int firstCore = 0, lastCore = 0;
        float minMHz = 0.0f, maxMHz = 0.0f, avgBusy = -1.0f, peakBusy = -1.0f;
    };

    static juce::Colour busyColour (float busy)
    {
        return busy >= 90.0f ? juce::Colour (0xffff4d6a) : busy >= 70.0f ? juce::Colour (0xffffb020) : juce::Colour (0xff3bd68a);
    }

    void drawBusy (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, float busy, const juce::String& detail) const
    {
        const auto& t = theme::current();
        auto top = r.removeFromTop (16.0f);
        g.setColour (t.text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (title, top.removeFromLeft (190.0f).toNearestInt(), juce::Justification::centredLeft);
        g.setColour (t.textMuted);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (detail, top.toNearestInt(), juce::Justification::centredRight);

        const auto bar = r.removeFromTop (8.0f).reduced (0.0f, 1.0f).translated (0.0f, 4.0f);
        g.setColour (t.raisedHigh);
        g.fillRoundedRectangle (bar, 3.0f);
        if (busy >= 0.0f)
        {
            g.setColour (busyColour (busy));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, busy / 100.0f)), 3.0f);
            // the 70% mark: where the clock should be steered to
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.fillRect (bar.getX() + bar.getWidth() * 0.70f - 0.5f, bar.getY() - 2.0f, 1.0f, bar.getHeight() + 4.0f);
        }
    }

    void row (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, const Cluster& c) const
    {
        juce::String detail;
        if (c.avgBusy >= 0.0f)
            detail = juce::String ((int) c.minMHz) + (c.maxMHz > c.minMHz + 1.0f ? "-" + juce::String ((int) c.maxMHz) : juce::String())
                     + " MHz  |  busy avg " + juce::String ((int) c.avgBusy) + "%  peak core " + juce::String ((int) c.peakBusy) + "%";
        else
            detail = "n/a";
        drawBusy (g, r, title, c.peakBusy, detail);
    }

    static juce::String read (const juce::String& path)
    {
        return juce::File (path).loadFileAsString().trim();
    }

    void sample()
    {
       #if JUCE_LINUX
        // per-core busy-% from /proc/stat deltas
        std::array<std::uint64_t, 8> total {}, idle {};
        const auto stat = juce::StringArray::fromLines (read ("/proc/stat"));
        for (const auto& line : stat)
        {
            if (! line.startsWith ("cpu") || line.length() < 4 || ! juce::CharacterFunctions::isDigit (line[3]))
                continue;
            const auto tokens = juce::StringArray::fromTokens (line, " ", "");
            const int core = tokens[0].substring (3).getIntValue();
            if (core < 0 || core > 7 || tokens.size() < 9)
                continue;
            std::uint64_t sum = 0;
            for (int i = 1; i <= 8; ++i)
                sum += (std::uint64_t) tokens[i].getLargeIntValue();
            total[(size_t) core] = sum;
            idle[(size_t) core] = (std::uint64_t) tokens[4].getLargeIntValue() + (std::uint64_t) tokens[5].getLargeIntValue();
        }

        auto fill = [&] (Cluster& c, int first, int last)
        {
            c.firstCore = first;
            c.lastCore = last;
            c.minMHz = 1.0e9f;
            c.maxMHz = 0.0f;
            float sum = 0.0f, peak = 0.0f;
            int counted = 0;
            for (int core = first; core <= last; ++core)
            {
                const auto mhz = (float) read ("/sys/devices/system/cpu/cpu" + juce::String (core) + "/cpufreq/scaling_cur_freq").getIntValue() / 1000.0f;
                if (mhz > 0.0f)
                {
                    c.minMHz = juce::jmin (c.minMHz, mhz);
                    c.maxMHz = juce::jmax (c.maxMHz, mhz);
                }
                const auto dTotal = total[(size_t) core] - prevTotal[(size_t) core];
                const auto dIdle = idle[(size_t) core] - prevIdle[(size_t) core];
                if (havePrev && dTotal > 0)
                {
                    const float busy = 100.0f * (1.0f - (float) dIdle / (float) dTotal);
                    sum += busy;
                    peak = juce::jmax (peak, busy);
                    ++counted;
                }
            }
            if (c.maxMHz <= 0.0f)
                c.minMHz = 0.0f;
            c.avgBusy = counted > 0 ? sum / (float) counted : -1.0f;
            c.peakBusy = counted > 0 ? peak : -1.0f;
        };
        fill (little, 0, 5);
        fill (big, 6, 7);
        prevTotal = total;
        prevIdle = idle;
        havePrev = true;

        // GPU busy-% from the PowerVR driver (debugfs, root only)
        gpuBusy = -1.0f;
        gpu3d = 0;
        const auto gpu = read ("/sys/kernel/debug/pvr/status");
        if (const int at = gpu.indexOf ("GPU Utilisation:"); at >= 0)
            gpuBusy = (float) gpu.substring (at + 16).getIntValue();
        if (const int at = gpu.indexOf ("3D:"); at >= 0)
            gpu3d = gpu.substring (at + 3).getIntValue();

        hottest = 0.0f;
        for (int zone = 0; zone < 8; ++zone)
        {
            const auto text = read ("/sys/class/thermal/thermal_zone" + juce::String (zone) + "/temp");
            if (text.isNotEmpty())
                hottest = juce::jmax (hottest, (float) text.getIntValue() / 1000.0f);
        }
       #endif
        repaint();
    }

    void timerCallback() override { sample(); }

    Cluster little, big;
    std::array<std::uint64_t, 8> prevTotal {}, prevIdle {};
    bool havePrev = false;
    float gpuBusy = -1.0f, hottest = 0.0f;
    int gpu3d = 0;
};

} // namespace openguitarmultifx
