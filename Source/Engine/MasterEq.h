#pragma once

#include "Biquad.h"
#include "DeferredReclaimer.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <vector>

namespace openguitarmultifx
{

/** One band of the master-bus equaliser, as the user edits it. Plain POD:
    the UI serializes these directly (MasterBusSettings.h) and hands them to
    MasterEq::setBands().

    `q` means something different per type -- one stored field, interpreted
    where the coefficients are computed:
      peak / notch         -> band Q (0.3 .. 18)
      lowShelf / highShelf -> shelf slope S (0.2 .. 2.0, 1 = gentlest standard shelf)
      lowCut / highCut     -> resonance multiplier on the Butterworth stage Qs (0.5 .. 4, 1 = flat passband edge)
    `gainDb` is unused by the cut types; `slopeDbOct` (12/24/36/48) is used only by them. */
struct MasterEqBand
{
    enum class Type { lowCut, lowShelf, peak, notch, highShelf, highCut };

    Type type = Type::peak;
    float freqHz = 1000.0f;
    float gainDb = 0.0f;
    float q = 1.0f;
    int slopeDbOct = 12;
    bool enabled = true;
};

/**
    Free-form parametric EQ on the master bus (post-SignalGraph, pre-master-volume) --
    the ReaEQ/FabFilter-style editor's DSP half. Any number of bands up to maxBands,
    each one or more cascaded cookbook biquads (cut bands expand into Butterworth
    stages for 12/24/36/48 dB/oct).

    Realtime contract (Source/Engine/AGENTS.md): setBands() runs on the CONTROL
    thread -- it computes every section's coefficients, packs them into a plain
    CoeffSet and publishes through DeferredReclaimer (the same atomic-swap
    primitive the graph itself uses, and the reason the old CoeffSet can never be
    read while half-written). The audio thread in process() only ever calls
    currentRaw(), copies the POD coefficients into its live Biquads when the
    published set changes -- keeping z1/z2 filter state, so a knob drag doesn't
    restart the filters -- and runs the cascade. No allocation, no lock, no
    computation beyond the multiply-accumulates on the audio thread.
*/
class MasterEq
{
public:
    static constexpr int maxBands = 16;
    static constexpr int maxSectionsPerBand = 4;                    // 48 dB/oct cut = 4 biquads
    static constexpr int maxSections = maxBands * maxSectionsPerBand;

    void prepare (double sampleRate, int numChannels) noexcept;
    void reset() noexcept;

    /** Control thread. Rebuilds the coefficient set from `bands` and publishes
        it for the audio thread. */
    void setBands (const std::vector<MasterEqBand>& bands);

    /** Control thread. The bands as last set (for the settings file + editor). */
    const std::vector<MasterEqBand>& getBands() const noexcept { return bands; }

    /** Audio thread. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    /** Control thread -- frees retired CoeffSets (the usual DeferredReclaimer
        discipline; AudioEngine calls this from its 10 Hz timer alongside the
        graph's own sweep). */
    void sweep() { coeffSlot.sweep(); }

    /** Combined magnitude (linear) of `bands` at `freqHz` -- for the editor's
        response curve and for tests. Control thread / offline only: builds a
        throwaway coefficient set. */
    static double magnitudeAt (const std::vector<MasterEqBand>& bands, double freqHz, double sampleRate) noexcept;

    /** "Low Cut" / "Peak" / ... for the editor. */
    static const char* displayName (MasterEqBand::Type type) noexcept;
    static MasterEqBand::Type typeForIndex (int i) noexcept
    {
        static constexpr MasterEqBand::Type order[] = { MasterEqBand::Type::lowCut, MasterEqBand::Type::lowShelf,
                                                        MasterEqBand::Type::peak, MasterEqBand::Type::notch,
                                                        MasterEqBand::Type::highShelf, MasterEqBand::Type::highCut };
        return order[juce::jlimit (0, 5, i)];
    }

private:
    /** POD coefficient block -- the payload swapped to the audio thread. */
    struct CoeffSet
    {
        struct Section { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };
        std::array<Section, maxSections> sections;
        int count = 0;
    };

    /** Fills `out` with every section `bands` expands to. Shared by setBands()
        and magnitudeAt() so the editor's drawn curve is exactly what the audio
        thread will run. */
    static void computeSections (const std::vector<MasterEqBand>& bands, double sampleRate, CoeffSet& out) noexcept;

    std::vector<MasterEqBand> bands;          // control thread
    double sampleRate = 0.0;                  // control thread (written in prepare/setBands)

    DeferredReclaimer<CoeffSet> coeffSlot;

    // Audio thread only: the live cascade, one column per channel. Coefficients
    // are copied out of the published CoeffSet when it changes -- the Biquads'
    // z-state is deliberately NOT touched, so re-tuning mid-note doesn't pop.
    std::array<std::array<Biquad, maxSections>, 2> live;
    int liveSectionCount = 0;
    const CoeffSet* applied = nullptr;
    int numLiveChannels = 0;
};

} // namespace openguitarmultifx
