#include "EffectRegistry.h"

#include "Effects/CompressorProcessor.h"
#include "Effects/DelayProcessor.h"
#include "Effects/TapeDelayProcessor.h"
#include "Effects/GateProcessor.h"
#include "Effects/IRLoaderProcessor.h"
#include "Effects/DynamicCabProcessor.h"
#include "Effects/NAMProcessor.h"
#include "Effects/OversampledEffect.h"
#include <atomic>
#include "Effects/OutputTrimEffect.h"
#include "Effects/PickupLoadEffect.h"
#include "Effects/SlewRateLimitEffect.h"
#include "Effects/TransistorBandwidthEffect.h"
#include "Effects/PositiveGroundBoosterProcessor.h"
#include "Effects/DS1StyleDistortionProcessor.h"
#include "Effects/OD1StyleOverdriveProcessor.h"
#include "Effects/TubeScreamerStyleOverdriveProcessor.h"
#include "Effects/CentaurStyleOverdriveProcessor.h"
#include "Effects/BD2StyleOverdriveProcessor.h"
#include "Effects/BassmanStyleAmplifierProcessor.h"
#include "Effects/SuperLeadStyleAmplifierProcessor.h"
#include "Effects/TwinReverbStyleAmplifierProcessor.h"
#include "Effects/DeluxeReverbStyleAmplifierProcessor.h"
#include "Effects/JC120StyleAmplifierProcessor.h"
#include "Effects/JTM45StyleAmplifierProcessor.h"
#include "Effects/JCM800StyleAmplifierProcessor.h"
#include "Effects/AC15StyleAmplifierProcessor.h"
#include "Effects/AC30StyleAmplifierProcessor.h"
#include "Effects/SLO100StyleAmplifierProcessor.h"
#include "Effects/MarkIICPlusStyleAmplifierProcessor.h"
#include "Effects/DualRectifierStyleAmplifierProcessor.h"
#include "Effects/EVH5150StyleAmplifierProcessor.h"
#include "Effects/ENGLPowerballStyleAmplifierProcessor.h"
#include "Effects/RockerverbStyleAmplifierProcessor.h"
#include "Effects/SVTStyleAmplifierProcessor.h"
#include "Effects/TrainwreckExpressStyleAmplifierProcessor.h"
#include "Effects/KometConcordeStyleAmplifierProcessor.h"
#include "Effects/BluesBreakerStyleOverdriveProcessor.h"
#include "Effects/GuvnorStyleDistortionProcessor.h"
#include "Effects/OpAmpClipperDistortionProcessor.h"
#include "Effects/RatStyleDistortionProcessor.h"
#include "Effects/GE7StyleEqualizerProcessor.h"
#include "Effects/ParametricEQProcessor.h"
#include "Effects/RingModProcessor.h"
#include "Effects/DS201StyleNoiseGateProcessor.h"
#include "Effects/NS2StyleNoiseSuppressorProcessor.h"
#include "Effects/DynaCompStyleCompressorProcessor.h"
#include "Effects/SqueezerStyleCompressorProcessor.h"
#include "Effects/Dbx160StyleCompressorProcessor.h"
#include "Effects/GSeriesStyleBusCompressorProcessor.h"
#include "Effects/Urei1176StyleCompressorProcessor.h"
#include "Effects/La2aStyleCompressorProcessor.h"
#include "Effects/CrunchBoxStyleDistortionProcessor.h"
#include "Effects/ZendriveStyleOverdriveProcessor.h"
#include "Effects/OCDStyleOverdriveProcessor.h"
#include "Effects/BigMuffStyleFuzzProcessor.h"
#include "Effects/DT1StyleDistortionProcessor.h"
#include "Effects/ODR1StyleOverdriveProcessor.h"
#include "Effects/OverdriverStyleOverdriveProcessor.h"
#include "Effects/EPStyleBoosterProcessor.h"
#include "Effects/TubeDriverStyleOverdriveProcessor.h"
#include "Effects/MetalZoneStyleDistortionProcessor.h"
#include "Effects/FuzzFaceStyleFuzzProcessor.h"
#include "Effects/ToneBenderStyleFuzzProcessor.h"
#include "Effects/HM2StyleDistortionProcessor.h"
#include "Effects/ReverbProcessor.h"
#include "Effects/SpringReverbProcessor.h"
#include "Effects/HallReverbProcessor.h"
#include "Effects/PlateReverbProcessor.h"
#include "Effects/RoomReverbProcessor.h"
#include "Effects/ShimmerReverbProcessor.h"
#include "Effects/GatedReverbProcessor.h"
#include "Effects/AnalogDelayProcessor.h"
#include "Effects/DualDelayProcessor.h"
#include "Effects/MultiTapDelayProcessor.h"
#include "Effects/TremoloProcessor.h"
#include "Effects/ChorusProcessor.h"
#include "Effects/VibratoProcessor.h"
#include "Effects/FlangerProcessor.h"
#include "Effects/PhaserProcessor.h"
#include "Effects/RotaryProcessor.h"
#include "Effects/UniVibeProcessor.h"
#include "Effects/PitchModProcessor.h"
#include "Effects/PitchShiftProcessor.h"
#include "Effects/OctaverProcessor.h"
#include "Effects/HarmonizerProcessor.h"
#include "Effects/LooperProcessor.h"
#include "Effects/PingPongDelayProcessor.h"
#include "Effects/ReverseDelayProcessor.h"
#include "Effects/HoldProcessor.h"

namespace openguitarmultifx
{

namespace
{
    /** Wraps a clipping pedal so it runs at 2x (order 1) or 4x (order 2) the host rate: what a real circuit's
        harmonics above Nyquist would otherwise fold back as inharmonic fizz. Factors come from the measurements
        in docs/circuits/Oversampling.md -- the pedals that are absent from the list (Centaur, Booster, the
        old Overdrive) were already clean at 1x (< -80 dB) and are not worth the CPU. */
    struct Orders { int eco, balanced, high; }; // 0 = none, 1 = 2x, 2 = 4x

    template <typename Processor, typename... Args>
    std::unique_ptr<EffectProcessor> oversampled (Orders orders, Args&&... args)
    {
        using Q = EffectRegistry::OversamplingQuality;
        const auto quality = EffectRegistry::getOversamplingQuality();
        const int order = quality == Q::eco ? orders.eco : (quality == Q::high ? orders.high : orders.balanced);
        auto processor = std::make_unique<Processor> (std::forward<Args> (args)...);
        if (order <= 0)
            return processor;
        return std::make_unique<OversampledEffect> (std::move (processor), order);
    }

    /** Same as oversampled<>(), plus the real op-amp's own slew rate (SlewRateLimitEffect, docs/circuits/HarshnessDiagnosis.md):
        applied to the raw processor BEFORE the oversampling wrap, so it runs at the oversampled rate -- the physically
        correct one to slew-limit at. `slewVoltsPerUs` is the chip's datasheet spec (a hard-clipping op-amp pedal's edges
        cannot move faster than this in reality; an ideal solver's could). */
    template <typename Processor, typename... Args>
    std::unique_ptr<EffectProcessor> oversampledSlew (Orders orders, double slewVoltsPerUs, Args&&... args)
    {
        using Q = EffectRegistry::OversamplingQuality;
        const auto quality = EffectRegistry::getOversamplingQuality();
        const int order = quality == Q::eco ? orders.eco : (quality == Q::high ? orders.high : orders.balanced);
        std::unique_ptr<EffectProcessor> processor = std::make_unique<Processor> (std::forward<Args> (args)...);
        processor = std::make_unique<SlewRateLimitEffect> (std::move (processor), slewVoltsPerUs);
        if (order <= 0)
            return processor;
        return std::make_unique<OversampledEffect> (std::move (processor), order);
    }

    /** Sets a pedal's noon-everything setting to unity gain for the reference signal in PedalUnityLevelTests (an E3
        with harmonics 1..10 at 1/k amplitude, 0.1 RMS = -20 dBFS): the trims are minus the measured RMS gain there,
        so the pedals are interchangeable without the amp after them being driven 20 dB harder by one than another.
        Re-measure (the test prints them) after touching a pedal's circuit. */
    /** The guitar's pickup (R + L) and a standard cable (shunt C) reacting with THIS pedal's own input impedance --
        docs/circuits/PickupLoading.md. Universal: every real guitar rig has this in front of whatever pedal it feeds,
        so every drive/distortion/fuzz pedal is wrapped with its own (documented where the schematic gives it, else a
        representative estimate for the topology -- see the call sites). */
    std::unique_ptr<EffectProcessor> pickupLoaded (std::unique_ptr<EffectProcessor> pedal, double inputImpedanceOhms)
    {
        return std::make_unique<PickupLoadEffect> (std::move (pedal), inputImpedanceOhms);
    }

    std::unique_ptr<EffectProcessor> trimmed (std::unique_ptr<EffectProcessor> pedal, float trimDb)
    {
        return std::make_unique<OutputTrimEffect> (std::move (pedal), trimDb);
    }
}

namespace
{
    juce::File qualityFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("OpenGuitarMultiFx").getChildFile ("render_quality.txt");
    }

    /** "eco" / "normal" (also "balanced") / "high"; anything else (or an empty string) is not a choice. */
    bool parseQuality (const juce::String& text, EffectRegistry::OversamplingQuality& out)
    {
        using Q = EffectRegistry::OversamplingQuality;
        const auto t = text.trim().toLowerCase();
        if (t == "eco") { out = Q::eco; return true; }
        if (t == "normal" || t == "balanced") { out = Q::balanced; return true; }
        if (t == "high") { out = Q::high; return true; }
        return false;
    }

    std::atomic<int>& qualityStorage()
    {
        static std::atomic<int> value = [] {
            using Q = EffectRegistry::OversamplingQuality;
            Q q = Q::eco; // eco unless the user chose otherwise
            if (qualityFile().existsAsFile())
                parseQuality (qualityFile().loadFileAsString(), q);
            parseQuality (juce::SystemStats::getEnvironmentVariable ("OGMFX_QUALITY", {}), q); // development override
            return (int) q;
        }();
        return value;
    }
}

void EffectRegistry::setOversamplingQuality (OversamplingQuality quality) noexcept { qualityStorage().store ((int) quality); }
EffectRegistry::OversamplingQuality EffectRegistry::getOversamplingQuality() noexcept { return (OversamplingQuality) qualityStorage().load(); }

void EffectRegistry::saveOversamplingQuality()
{
    const char* names[] = { "eco", "normal", "high" };
    qualityFile().getParentDirectory().createDirectory();
    qualityFile().replaceWithText (names[(int) getOversamplingQuality()]);
}

bool EffectRegistry::dependsOnOversamplingQuality (const juce::String& key) const { return qualityDependentKeys.count (key) > 0; }

void registerBuiltInEffects (EffectRegistry& registry)
{
    registry.registerType ("NoiseGate", [] { return std::make_unique<GateProcessor>(); });
    registry.registerType ("Compressor", [] { return std::make_unique<CompressorProcessor>(); });

    // Physically-modelled (Ebers-Moll transistor, not neural) -- see
    // Source/Effects/AGENTS.md's decision log and docs/circuits/
    // PositiveGroundBooster.md for the full circuit-fidelity rationale.
    // The real circuit's transistor is the only treble limit it has (docs/circuits/PositiveGroundBooster.md: no
    // collector/Miller capacitor in the schematic); the hand-derived companion-model solve has no capacitance there
    // at all (infinite bandwidth), which measured an unbounded, ever-rising gain with frequency instead of the real
    // germanium transistor's own fT settling it -- docs/circuits/HarshnessDiagnosis.md. 6 kHz is an estimate.
    registry.registerType ("PositiveGroundBooster", [] { return trimmed (pickupLoaded (std::make_unique<TransistorBandwidthEffect> (std::make_unique<PositiveGroundBoosterProcessor>(), 6000.0), 470000.0), -3.2f); });
    // DS-1, HM-2 and the RAT / RAT 2 hard-clip so hard that at 1x they leave -23 .. -33 dB of in-band non-harmonic energy on a 1.1 kHz note
    // (docs/circuits/HarshnessDiagnosis.md): their Eco tier is 2x, not 1x.
    registry.markQualityDependent ("DS1StyleDistortion");
    registry.registerType ("DS1StyleDistortion", [] { return trimmed (pickupLoaded (oversampledSlew<DS1StyleDistortionProcessor> (Orders { 1, 2, 3 }, 0.6), 470000.0), 2.87f); });
    registry.markQualityDependent ("OD1StyleOverdrive");
    registry.registerType ("OD1StyleOverdrive", [] { return trimmed (pickupLoaded (oversampled<OD1StyleOverdriveProcessor> (Orders { 0, 0, 1 }), 1000000.0), 1.18f); });

    // One class, three models (TS808/TS9/TS10 share one circuit, see
    // docs/circuits/TubeScreamerStyleOverdrive.md).
    using TSModel = TubeScreamerStyleOverdriveProcessor::Model;
    registry.markQualityDependent ("TS808StyleOverdrive");
    registry.registerType ("TS808StyleOverdrive", [] { return trimmed (pickupLoaded (oversampled<TubeScreamerStyleOverdriveProcessor> (Orders { 0, 0, 1 }, TSModel::ts808), 500000.0), 10.88f); });
    registry.markQualityDependent ("TS9StyleOverdrive");
    registry.registerType ("TS9StyleOverdrive", [] { return trimmed (pickupLoaded (oversampled<TubeScreamerStyleOverdriveProcessor> (Orders { 0, 0, 1 }, TSModel::ts9), 500000.0), 10.84f); });
    // One class, two models (USA V3 and the Russian green one share one circuit, see
    // docs/circuits/BigMuffStyleFuzz.md).
    using MuffModel = BigMuffStyleFuzzProcessor::Model;
    registry.markQualityDependent ("BigMuffStyleFuzz");
    registry.registerType ("BigMuffStyleFuzz", [] { return trimmed (pickupLoaded (oversampled<BigMuffStyleFuzzProcessor> (Orders { 1, 1, 2 }, MuffModel::usV3), 65000.0), -5.42f); });
    registry.markQualityDependent ("SovtekBigMuffStyleFuzz");
    registry.registerType ("SovtekBigMuffStyleFuzz", [] { return trimmed (pickupLoaded (oversampled<BigMuffStyleFuzzProcessor> (Orders { 1, 1, 2 }, MuffModel::sovtekFirstEdition), 65000.0), -9.72f); });
    registry.markQualityDependent ("RussianBigMuffStyleFuzz");
    registry.registerType ("RussianBigMuffStyleFuzz", [] { return trimmed (pickupLoaded (oversampled<BigMuffStyleFuzzProcessor> (Orders { 1, 1, 2 }, MuffModel::russianGreen), 65000.0), -9.71f); });
    using FFModel = FuzzFaceStyleFuzzProcessor::Model;
    registry.markQualityDependent ("FuzzFaceStyleFuzz");
    registry.registerType ("FuzzFaceStyleFuzz", [] { return trimmed (pickupLoaded (oversampled<FuzzFaceStyleFuzzProcessor> (Orders { 1, 1, 2 }, FFModel::germanium), 15000.0), 13.40f); });
    registry.markQualityDependent ("SiliconFuzzFaceStyleFuzz");
    registry.registerType ("SiliconFuzzFaceStyleFuzz", [] { return trimmed (pickupLoaded (oversampled<FuzzFaceStyleFuzzProcessor> (Orders { 1, 1, 2 }, FFModel::silicon), 15000.0), 19.93f); });
    using TBModel = ToneBenderStyleFuzzProcessor::Model;
    registry.markQualityDependent ("ToneBenderStyleFuzz");
    registry.registerType ("ToneBenderStyleFuzz", [] { return trimmed (pickupLoaded (oversampled<ToneBenderStyleFuzzProcessor> (Orders { 1, 2, 3 }, TBModel::germanium), 8000.0), 6.14f); });
    registry.markQualityDependent ("SiliconToneBenderStyleFuzz");
    registry.registerType ("SiliconToneBenderStyleFuzz", [] { return trimmed (pickupLoaded (oversampled<ToneBenderStyleFuzzProcessor> (Orders { 1, 2, 3 }, TBModel::silicon), 8000.0), 1.29f); });
    registry.markQualityDependent ("OverdriverStyleOverdrive");
    registry.registerType ("OverdriverStyleOverdrive", [] { return trimmed (pickupLoaded (oversampled<OverdriverStyleOverdriveProcessor> (Orders { 1, 1, 2 }), 1000000.0), -12.73f); });
    registry.markQualityDependent ("MetalZoneStyleDistortion");
    registry.registerType ("MetalZoneStyleDistortion", [] { return trimmed (pickupLoaded (oversampledSlew<MetalZoneStyleDistortionProcessor> (Orders { 1, 1, 2 }, 2.0), 470000.0), 2.23f); });
    registry.markQualityDependent ("TubeDriverStyleOverdrive");
    registry.registerType ("TubeDriverStyleOverdrive", [] { return trimmed (pickupLoaded (oversampledSlew<TubeDriverStyleOverdriveProcessor> (Orders { 1, 1, 2 }, 13.0), 1000000.0), 4.01f); });
    registry.markQualityDependent ("EPStyleBooster");
    // Same missing treble limit as the Rangemaster-style booster above (docs/circuits/EPStyleBooster.md's own
    // netlist has no collector/output shunt capacitor either): 7 kHz is an estimate.
    registry.registerType ("EPStyleBooster", [] { return trimmed (pickupLoaded (std::make_unique<TransistorBandwidthEffect> (oversampled<EPStyleBoosterProcessor> (Orders { 0, 1, 1 }), 7000.0), 1000000.0), -6.66f); });
    registry.markQualityDependent ("ODR1StyleOverdrive");
    registry.registerType ("ODR1StyleOverdrive", [] { return trimmed (pickupLoaded (oversampledSlew<ODR1StyleOverdriveProcessor> (Orders { 1, 1, 2 }, 1.7), 1000000.0), -1.77f); });
    registry.markQualityDependent ("DT1StyleDistortion");
    registry.registerType ("DT1StyleDistortion", [] { return trimmed (pickupLoaded (oversampledSlew<DT1StyleDistortionProcessor> (Orders { 1, 1, 2 }, 1.7), 1000000.0), 10.17f); });
    registry.markQualityDependent ("HM2StyleDistortion");
    registry.registerType ("HM2StyleDistortion", [] { return trimmed (pickupLoaded (oversampledSlew<HM2StyleDistortionProcessor> (Orders { 1, 2, 2 }, 2.0), 470000.0), 6.3f); });
    using ClipModel = OpAmpClipperDistortionProcessor::Model;
    registry.markQualityDependent ("DistortionPlusStyleDistortion");
    registry.registerType ("DistortionPlusStyleDistortion", [] { return trimmed (pickupLoaded (oversampledSlew<OpAmpClipperDistortionProcessor> (Orders { 0, 0, 1 }, 0.5, ClipModel::distortionPlus), 1000000.0), 8.95f); });
    registry.markQualityDependent ("DOD250StyleOverdrive");
    registry.registerType ("DOD250StyleOverdrive", [] { return trimmed (pickupLoaded (oversampledSlew<OpAmpClipperDistortionProcessor> (Orders { 0, 0, 1 }, 0.5, ClipModel::dod250), 1000000.0), 4.02f); });
    registry.markQualityDependent ("GuvnorStyleDistortion");
    registry.registerType ("GuvnorStyleDistortion", [] { return trimmed (pickupLoaded (oversampledSlew<GuvnorStyleDistortionProcessor> (Orders { 0, 0, 1 }, 13.0), 1000000.0), 4.43f); });
    registry.markQualityDependent ("BluesBreakerStyleOverdrive");
    registry.registerType ("BluesBreakerStyleOverdrive", [] { return trimmed (pickupLoaded (oversampledSlew<BluesBreakerStyleOverdriveProcessor> (Orders { 0, 0, 1 }, 13.0), 1000000.0), 8.89f); });
    registry.markQualityDependent ("RatStyleDistortion");
    registry.registerType ("RatStyleDistortion", [] { return trimmed (pickupLoaded (oversampledSlew<RatStyleDistortionProcessor> (Orders { 1, 2, 3 }, 0.3), 1000000.0), 1.17f); });
    // The RAT 2 and the Turbo RAT share the RAT's class (docs/circuits/RatStyleDistortion.md, "Versions"); trims are
    // set from PedalUnityLevelTests' printout.
    using RatModel = RatStyleDistortionProcessor::Model;
    registry.markQualityDependent ("RAT2StyleDistortion");
    registry.registerType ("RAT2StyleDistortion", [] { return trimmed (pickupLoaded (oversampledSlew<RatStyleDistortionProcessor> (Orders { 1, 2, 3 }, 0.3, RatModel::rat2), 1000000.0), 1.25f); });
    registry.markQualityDependent ("TurboRatStyleDistortion");
    registry.registerType ("TurboRatStyleDistortion", [] { return trimmed (pickupLoaded (oversampledSlew<RatStyleDistortionProcessor> (Orders { 1, 1, 2 }, 0.17, RatModel::turbo), 1000000.0), -7.85f); });
    // Linear circuit (no clipping stage worth oversampling): a graphic equaliser from the manufacturer's diagram.
    registry.registerType ("GE7StyleEqualizer", [] { return trimmed (std::make_unique<GE7StyleEqualizerProcessor>(), 0.65f); }); // its own -0.65 dB at flat, trimmed to unity
    registry.registerType ("ParametricEQ", [] { return std::make_unique<ParametricEQProcessor>(); });
    registry.registerType ("DS201StyleNoiseGate", [] { return std::make_unique<DS201StyleNoiseGateProcessor>(); });
    registry.registerType ("NS2StyleNoiseSuppressor", [] { return std::make_unique<NS2StyleNoiseSuppressorProcessor>(); });
    // OTA compressors (docs/circuits/DynaCompStyleCompressor.md): the OTA's tanh is soft and the levels are small: no oversampling.
    using CompModel = DynaCompStyleCompressorProcessor::Model;
    registry.registerType ("DynaCompStyleCompressor", [] { return trimmed (std::make_unique<DynaCompStyleCompressorProcessor> (CompModel::dynaComp), -2.21f); });
    // Studio compressors (behavioural, calibrated to the manuals): docs/circuits/StudioCompressors.md
    registry.registerType ("Dbx160StyleCompressor", [] { return std::make_unique<Dbx160StyleCompressorProcessor>(); });
    registry.registerType ("GSeriesStyleBusCompressor", [] { return std::make_unique<GSeriesStyleBusCompressorProcessor>(); });
    registry.registerType ("Urei1176StyleCompressor", [] { return std::make_unique<Urei1176StyleCompressorProcessor>(); });
    registry.registerType ("La2aStyleCompressor", [] { return std::make_unique<La2aStyleCompressorProcessor>(); });
    registry.registerType ("SqueezerStyleCompressor", [] { return trimmed (std::make_unique<SqueezerStyleCompressorProcessor>(), 3.86f); });
    registry.registerType ("RossStyleCompressor", [] { return trimmed (std::make_unique<DynaCompStyleCompressorProcessor> (CompModel::ross), -0.55f); });
    // The diode bridge's harmonics fall only 20 dB per octave (Parker): oversampled like the clipping pedals (tiers set from the alias measurement).
    registry.markQualityDependent ("RingMod");
    registry.registerType ("RingMod", [] { return oversampled<RingModProcessor> (Orders { 1, 2, 3 }); });
    registry.markQualityDependent ("CrunchBoxStyleDistortion");
    registry.registerType ("CrunchBoxStyleDistortion", [] { return trimmed (pickupLoaded (oversampledSlew<CrunchBoxStyleDistortionProcessor> (Orders { 1, 1, 2 }, 7.0), 1000000.0), -14.25f); });
    registry.markQualityDependent ("ZendriveStyleOverdrive");
    registry.registerType ("ZendriveStyleOverdrive", [] { return trimmed (pickupLoaded (oversampledSlew<ZendriveStyleOverdriveProcessor> (Orders { 0, 0, 1 }, 20.0), 1000000.0), -10.14f); });
    registry.markQualityDependent ("OCDStyleOverdrive");
    registry.registerType ("OCDStyleOverdrive", [] { return trimmed (pickupLoaded (oversampledSlew<OCDStyleOverdriveProcessor> (Orders { 1, 1, 2 }, 13.0), 1000000.0), -14.37f); });
    registry.markQualityDependent ("BD2StyleOverdrive");
    registry.registerType ("BD2StyleOverdrive", [] { return trimmed (pickupLoaded (oversampledSlew<BD2StyleOverdriveProcessor> (Orders { 1, 1, 2 }, 2.0), 1000000.0), -4.42f); });
    registry.registerType ("CentaurStyleOverdrive", [] { return trimmed (pickupLoaded (std::make_unique<CentaurStyleOverdriveProcessor>(), 1000000.0), -12.09f); });
    registry.markQualityDependent ("TS10StyleOverdrive");
    registry.registerType ("TS10StyleOverdrive", [] { return trimmed (pickupLoaded (oversampled<TubeScreamerStyleOverdriveProcessor> (Orders { 0, 0, 1 }, TSModel::ts10), 500000.0), 11.34f); });

    // A full tube amplifier modelled from its schematic (docs/circuits/Bassman5F6A.md): 8 tubes, an output transformer,
    // global feedback and a sagging supply. Quality tiers: 1x, 1x, 2x (the tubes' clipping is soft; see the doc).
    // Behavioural power stage (docs/circuits/SuperLead1959.md, "reduced-order power stage"): the class defaults to the full
    // reference netlist (every OTHER test in SuperLeadStyleAmplifierProcessorTests.cpp assumes that topology) -- turned on
    // HERE, once, for the real app only, after the user listened to it against the TS808+Super Lead preset and approved it.
    SuperLeadStyleAmplifierProcessor::reducedOrder = true;
    registry.markQualityDependent ("SuperLeadStyleAmplifier");
    // -18.99f (was -15.83f): re-measured 2026-09-27 via PedalUnityLevelTests -- the reference's own raw gain had drifted
    // ~2.76 dB since -15.83 was last calibrated (unrelated to reducedOrder, likely an earlier fix this session), and
    // reducedOrder itself adds another ~0.4 dB on top (see docs/circuits/SuperLead1959.md).
    registry.registerType ("SuperLeadStyleAmplifier", [] { return trimmed (oversampled<SuperLeadStyleAmplifierProcessor> (Orders { 0, 0, 1 }), -18.99f); });
    // Behavioural power stage (docs/circuits/Bassman5F6A.md, "reduced-order power stage"): same reasoning and the same
    // 2026-09-27 user approval as the Super Lead's own switch just above.
    BassmanStyleAmplifierProcessor::reducedOrder = true;
    registry.markQualityDependent ("BassmanStyleAmplifier");
    registry.registerType ("BassmanStyleAmplifier", [] { return trimmed (oversampled<BassmanStyleAmplifierProcessor> (Orders { 0, 0, 1 }), -13.94f); });
    // docs/circuits/TwinReverbAB763.md. reducedOrder calibrated (TR_POWERCAL) and verified (level tracks the reference
    // within 0.01-0.06 dB, PedalUnityLevelTests passing) -- shipped as the default, same as the Super Lead/Bassman.
    TwinReverbStyleAmplifierProcessor::reducedOrder = true;
    registry.markQualityDependent ("TwinReverbStyleAmplifier");
    registry.registerType ("TwinReverbStyleAmplifier", [] { return trimmed (oversampled<TwinReverbStyleAmplifierProcessor> (Orders { 0, 0, 1 }), 4.19f); });
    // docs/circuits/DeluxeReverbAB763.md. reducedOrder calibrated (DR_POWERCAL) and verified (level tracks the reference
    // within 0.00-0.02 dB, PedalUnityLevelTests passing) -- shipped as the default, same as the Super Lead/Bassman/Twin
    // Reverb. Trim placeholder pending PedalUnityLevelTests measurement.
    DeluxeReverbStyleAmplifierProcessor::reducedOrder = true;
    registry.markQualityDependent ("DeluxeReverbStyleAmplifier");
    registry.registerType ("DeluxeReverbStyleAmplifier", [] { return trimmed (oversampled<DeluxeReverbStyleAmplifierProcessor> (Orders { 0, 0, 1 }), -0.06f); });
    // docs/circuits/JC120JazzChorus.md. Solid-state (op-amp preamp, a saturating-op-amp power stage standing in for the
    // real discrete Class AB output pair, real BBD chorus).
    registry.registerType ("JC120StyleAmplifier", [] { return trimmed (std::make_unique<JC120StyleAmplifierProcessor>(), 14.10f); });
    // docs/circuits/JTM45Marshall.md. reducedOrder calibrated (JM_POWERCAL) against this amp's own (stabilised, see
    // the doc's "power-stage instability" section) reference model -- shipped as the default, same as the other amps.
    JTM45StyleAmplifierProcessor::reducedOrder = true;
    registry.markQualityDependent ("JTM45StyleAmplifier");
    registry.registerType ("JTM45StyleAmplifier", [] { return trimmed (oversampled<JTM45StyleAmplifierProcessor> (Orders { 0, 0, 1 }), 21.60f); });
    // docs/circuits/JCM8002203.md. Reuses the Super Lead's own (already stable) power section unchanged; the preamp is
    // newly built (4 cascaded gain stages). reducedOrder placeholder trim pending its own J8_POWERCAL measurement.
    JCM800StyleAmplifierProcessor::reducedOrder = true;
    registry.markQualityDependent ("JCM800StyleAmplifier");
    registry.registerType ("JCM800StyleAmplifier", [] { return trimmed (oversampled<JCM800StyleAmplifierProcessor> (Orders { 0, 0, 1 }), -17.47f); });
    // docs/circuits/AC15Twin.md. Genuinely new family: EF86 pentode preamp, cathodyne (split-load) phase inverter,
    // cathode-biased EL84 pair, no global feedback. reducedOrder calibrated (A15_POWERCAL) against this amp's own
    // stable reference (no instability investigation needed, unlike the JTM45/JCM800).
    AC15StyleAmplifierProcessor::reducedOrder = true;
    registry.markQualityDependent ("AC15StyleAmplifier");
    registry.registerType ("AC15StyleAmplifier", [] { return trimmed (oversampled<AC15StyleAmplifierProcessor> (Orders { 0, 0, 1 }), 4.30f); });
    // docs/circuits/AC30TopBoost.md. Top Boost channel only. Cathode-biased 4xEL84 (two parallel pairs), a genuine
    // long-tailed-pair phase inverter, no global feedback (confirmed absent on the factory power-amp drawing).
    AC30StyleAmplifierProcessor::reducedOrder = true;
    registry.markQualityDependent ("AC30StyleAmplifier");
    registry.registerType ("AC30StyleAmplifier", [] { return trimmed (oversampled<AC30StyleAmplifierProcessor> (Orders { 0, 0, 1 }), -11.63f); });
    // docs/circuits/SLO100.md. Soldano SLO-100 OD channel. Five cascaded 12AX7 gain stages, TMB tone stack,
    // LTP PI, 4x6L6GC fixed-bias push-pull. reducedOrder calibrated placeholder (Twin Reverb's 6L6GC constants).
    SLO100StyleAmplifierProcessor::reducedOrder = true;
    registry.markQualityDependent ("SLO100StyleAmplifier");
    registry.registerType ("SLO100StyleAmplifier", [] { return trimmed (oversampled<SLO100StyleAmplifierProcessor> (Orders { 0, 0, 1 }), -18.92f); });
    // docs/circuits/MarkIICPlus.md. Mesa/Boogie Mark IIC+ Lead channel. Five cascaded 12AX7 gain stages with unique
    // inter-stage EQ/gain network, TMB tone stack, LTP PI, 4x6L6GC fixed-bias push-pull. reducedOrder calibrated
    // placeholder (Twin Reverb's 6L6GC constants).
    MarkIICPlusStyleAmplifierProcessor::reducedOrder = true;
    registry.markQualityDependent ("MarkIICPlusStyleAmplifier");
    registry.registerType ("MarkIICPlusStyleAmplifier", [] { return trimmed (oversampled<MarkIICPlusStyleAmplifierProcessor> (Orders { 0, 0, 1 }), -11.79f); });
    // docs/circuits/DualRectifier.md. Mesa/Boogie Dual Rectifier RED channel. Four cascaded 12AX7 gain stages with
    // unbypassed compression stage, TMB tone stack with Master, LTP PI, 4x6L6GC fixed-bias push-pull.
    DualRectifierStyleAmplifierProcessor::reducedOrder = true;
    registry.markQualityDependent ("DualRectifierStyleAmplifier");
    registry.registerType ("DualRectifierStyleAmplifier", [] { return trimmed (oversampled<DualRectifierStyleAmplifierProcessor> (Orders { 0, 0, 1 }), -19.19f); });
    // docs/circuits/EVH5150.md. Peavey/EVH 5150 Ultra channel. Five cascaded 12AX7 gain stages plus
    // post-tonestack gain recovery, TMB tone stack, LTP PI, 4x6L6GC fixed-bias push-pull.
    EVH5150StyleAmplifierProcessor::reducedOrder = true;
    registry.markQualityDependent ("EVH5150StyleAmplifier");
    registry.registerType ("EVH5150StyleAmplifier", [] { return trimmed (oversampled<EVH5150StyleAmplifierProcessor> (Orders { 0, 0, 1 }), -9.38f); });
    // docs/circuits/ENGLPowerball.md. ENGL Powerball Hi Lead channel. Six cascaded 12AX7 gain stages
    // (3 pre-tonestack + 3 post-tonestack), FMV tone stack, LTP PI, 4x6L6GC fixed-bias push-pull.
    ENGLPowerballStyleAmplifierProcessor::reducedOrder = true;
    registry.markQualityDependent ("ENGLPowerballStyleAmplifier");
    registry.registerType ("ENGLPowerballStyleAmplifier", [] { return trimmed (oversampled<ENGLPowerballStyleAmplifierProcessor> (Orders { 0, 0, 1 }), -18.37f); });
    // Orange Rockerverb 50 MK1 Dirty channel. Four cascaded 12AX7 gain stages, FMV tone stack,
    // LTP PI, 4x6V6 fixed-bias push-pull.
    RockerverbStyleAmplifierProcessor::reducedOrder = true;
    registry.markQualityDependent ("RockerverbStyleAmplifier");
    registry.registerType ("RockerverbStyleAmplifier", [] { return trimmed (oversampled<RockerverbStyleAmplifierProcessor> (Orders { 0, 0, 1 }), -5.74f); });

    // The Ampeg SVT-CL bass head, modelled from its service schematic (docs/circuits/AmpegSVT.md): four 12AX7
    // in the preamp, Baxandall + tapped-inductor mid section, a 12AX7 phase splitter, two 12AU7 drivers and
    // six 6550s. No reducedOrder yet: the full reference netlist always runs (trim measured by PedalUnityLevelTests).
    registry.markQualityDependent ("SVTStyleAmplifier");
    // Measured +10.32 dB at noon for the PedalUnityLevel reference, after the level-shifter tap
    // bypass (0.1 uF to ground) was removed -- it had low-passed the driver->6550 drive ~30 dB.
    registry.registerType ("SVTStyleAmplifier", [] { return trimmed (oversampled<SVTStyleAmplifierProcessor> (Orders { 0, 0, 1 }), -10.32f); });
    // Trainwreck Express (docs/circuits/TrainwreckExpress.md) and the Trainwreck-descended Komet Concorde
    // (docs/circuits/KometConcorde.md): three-12AX7 anode-driven preamps, LTP, 2x EL34, no global feedback. Full netlist.
    registry.markQualityDependent ("TrainwreckExpressStyleAmplifier");
    registry.registerType ("TrainwreckExpressStyleAmplifier", [] { return trimmed (oversampled<TrainwreckExpressStyleAmplifierProcessor> (Orders { 0, 0, 1 }), -19.03f); });
    registry.markQualityDependent ("KometConcordeStyleAmplifier");
    registry.registerType ("KometConcordeStyleAmplifier", [] { return trimmed (oversampled<KometConcordeStyleAmplifierProcessor> (Orders { 0, 0, 1 }), -20.31f); });

    // Same wrapper class, three chain roles -- only the .nam file loaded
    // into each instance determines whether it sounds like an amp, an
    // amp+cab, or a pedal. Amp and Amp+Cab process identically (see
    // NAMProcessor.h) and only exist as separate blocks so TONE3000 search
    // can be scoped to one gear at a time, per user request.
    registry.registerType ("NeuralAmp", [] { return std::make_unique<NAMProcessor> ("Neural Amp"); });
    registry.registerType ("NeuralAmpCab", [] { return std::make_unique<NAMProcessor> ("Neural Amp + Cab"); });
    registry.registerType ("NeuralPedal", [] { return std::make_unique<NAMProcessor> ("Neural Pedal"); });

    // Same story, one class, two roles -- IRLoaderProcessor convolves with
    // a WAV impulse response either way; "Cab" vs "Reverb" is just which
    // TONE3000 gear (cab vs space) the loaded IR came from. See
    // IRLoaderProcessor.h -- this is NOT the same technology as NAM above.
    registry.registerType ("Cab", [] { return std::make_unique<IRLoaderProcessor> ("Cab"); });
    registry.registerType ("Reverb", [] { return std::make_unique<IRLoaderProcessor> ("Reverb"); });

    // Two independently-loaded cab IRs crossfaded together -- static
    // multi-mic blending (Dynamics=0) and level-dependent blending
    // (Dynamics>0) in one processor, since they're the same DSP underneath
    // (two convolutions + a crossfade), not two separate features. See
    // DynamicCabProcessor.h.
    registry.registerType ("DynamicCab", [] { return std::make_unique<DynamicCabProcessor>(); });

    // Phase 2: Delay + Reverb. Working through the icon sheet's named
    // variants one at a time -- each remaining Delay/Reverb glyph stays
    // documented-but-unbuilt in docs/icons/AGENT-icon-notes.md until it
    // gets its own processor (only glyphs the user has already approved
    // get wired up; see that doc's rule on never guessing a new one).
    registry.registerType ("DigitalDelay", [] { return std::make_unique<DelayProcessor>(); });
    registry.registerType ("TapeDelay", [] { return std::make_unique<TapeDelayProcessor>(); });
    registry.registerType ("AnalogDelay", [] { return std::make_unique<AnalogDelayProcessor>(); });
    registry.registerType ("DualDelay", [] { return std::make_unique<DualDelayProcessor>(); });
    registry.registerType ("MultiTapDelay", [] { return std::make_unique<MultiTapDelayProcessor>(); });

    // Phase 3: Modulation.
    registry.registerType ("Tremolo", [] { return std::make_unique<TremoloProcessor>(); });
    registry.registerType ("Chorus", [] { return std::make_unique<ChorusProcessor>(); });
    registry.registerType ("Vibrato", [] { return std::make_unique<VibratoProcessor>(); });
    registry.registerType ("Flanger", [] { return std::make_unique<FlangerProcessor>(); });
    registry.registerType ("Phaser", [] { return std::make_unique<PhaserProcessor>(); });
    registry.registerType ("Rotary", [] { return std::make_unique<RotaryProcessor>(); });
    registry.registerType ("UniVibe", [] { return std::make_unique<UniVibeProcessor>(); });
    registry.registerType ("PitchMod", [] { return std::make_unique<PitchModProcessor>(); });

    // Phase 4: Pitch.
    registry.registerType ("PitchShift", [] { return std::make_unique<PitchShiftProcessor>(); });
    registry.registerType ("Octaver", [] { return std::make_unique<OctaverProcessor>(); });
    registry.registerType ("Harmonizer", [] { return std::make_unique<HarmonizerProcessor>(); });

    // Phase 5 (partial): Looper. Sits in the Delay category per the
    // reference sheet, not alongside the other Phase 4 Pitch effects.
    registry.registerType ("Looper", [] { return std::make_unique<LooperProcessor>(); });
    registry.registerType ("Ambient", [] { return std::make_unique<ReverbProcessor>(); });
    registry.registerType ("Spring", [] { return std::make_unique<SpringReverbProcessor>(); });
    registry.registerType ("Hall", [] { return std::make_unique<HallReverbProcessor>(); });
    registry.registerType ("Plate", [] { return std::make_unique<PlateReverbProcessor>(); });
    registry.registerType ("Room", [] { return std::make_unique<RoomReverbProcessor>(); });
    registry.registerType ("Shimmer", [] { return std::make_unique<ShimmerReverbProcessor>(); });
    registry.registerType ("GatedReverb", [] { return std::make_unique<GatedReverbProcessor>(); });
    registry.registerType ("PingPong", [] { return std::make_unique<PingPongDelayProcessor>(); });
    registry.registerType ("ReverseDelay", [] { return std::make_unique<ReverseDelayProcessor>(); });
    registry.registerType ("Hold", [] { return std::make_unique<HoldProcessor>(); });
}

void EffectRegistry::registerType (const juce::String& key, Creator creator)
{
    // Probing with a real (immediately discarded) instance instead of
    // asking the call site to also type the display name by hand: that
    // second string would have no way to stay in sync if a processor's
    // constructor default ever changes, and preset save/load's reverse
    // lookup (keyForDisplayName) needs this to always exactly match what
    // getName() really returns at runtime.
    if (auto probe = creator())
    {
        const juce::String displayName (probe->getName());
        displayNamesByKey[key] = displayName;
        keysByDisplayName[displayName] = key;
    }

    creators[key] = std::move (creator);
}

std::unique_ptr<EffectProcessor> EffectRegistry::create (const juce::String& key) const
{
    const auto it = creators.find (key);
    return it != creators.end() ? it->second() : nullptr;
}

juce::String EffectRegistry::displayNameForKey (const juce::String& key) const
{
    const auto it = displayNamesByKey.find (key);
    return it != displayNamesByKey.end() ? it->second : key;
}

juce::String EffectRegistry::keyForDisplayName (const juce::String& displayName) const
{
    const auto it = keysByDisplayName.find (displayName);
    return it != keysByDisplayName.end() ? it->second : juce::String();
}

juce::StringArray EffectRegistry::getRegisteredNames() const
{
    juce::StringArray names;
    for (const auto& [name, creator] : creators)
        names.add (name);
    return names;
}

} // namespace openguitarmultifx
