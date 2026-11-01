#include "TraceElliotGP12StyleAmplifierProcessor.h"
#include "PotTaper.h"
#include "IconKit.h"
#include "TubeAmpCommon.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double vBias = 4.5;           // the same single-supply op-amp topology as the graphic EQs
    constexpr double sliderOhms = 100.0e3;  // SL1-SL12, 100k linear (Trace Elliot Series 6 GP12 drawing, 3-1-89)
    constexpr double sliderMinOhms = 10.0;
    constexpr double legOhms = 100.0e3;     // R48 / R45 into each stage's (+), R47 / R46 feedback to its (-)
    const NodalCircuit::OpAmpMacro opAmp { 1.0e5, 3.0e6, 75.0, 2.0, 7.0, 0.0, 0.0 }; // TL072

    /** One band's gyrator, as drawn: the slider wiper -> C series -> the base of an emitter follower whose base sits on
        4k7 + 4k7 to ground with C mid from their joint to the emitter. With the follower ideal that is a series
        9k4 + L, L = C mid x 4k7 x 4k7 -- every band of the drawing lands on its printed centre with it. Where two
        capacitors sit in parallel on the board (the 30 Hz, 180 Hz, 1k3 and 2k6 bands) their sum is used. */
    struct Gyrator { double cSeries, cMid; };
    constexpr double gyrR = 4.7e3;
    constexpr Gyrator gyrators[TraceElliotGP12StyleAmplifierProcessor::numBands] = {
        { 0.352e-6, 3.7e-6 },   // 30 Hz: C29 0.33u + C28 22n; C27 1u5 + C26 2u2
        { 0.33e-6, 2.2e-6 },    // 40 Hz: C32, C31
        { 0.22e-6, 1.5e-6 },    // 60 Hz: C35, C34
        { 0.1e-6, 1.0e-6 },     // 100 Hz: C38, C37
        { 57.0e-9, 0.57e-6 },   // 180 Hz: C43 47n + C42 10n; C40 0.47u + C41 100n
        { 33.0e-9, 0.33e-6 },   // 340 Hz: C46, C45
        { 15.0e-9, 0.22e-6 },   // 660 Hz: C49, C48
        { 7.27e-9, 0.1e-6 },    // 1.3 kHz: C52 6n8 + C86 470p; C51
        { 3.3e-9, 37.7e-9 },    // 2.6 kHz: C56; C55 33n + C54 4n7
        { 2.2e-9, 22.0e-9 },    // 5 kHz: C59, C58
        { 1.0e-9, 10.0e-9 },    // 10 kHz
        { 680.0e-12, 6.8e-9 },  // 15 kHz
    };

    /** IC5's two halves carry alternate bands on the board: 30 / 60 / 180 / 660 / 2k6 / 10k, then 40 / 100 / 340 /
        1k3 / 5k / 15k. */
    constexpr bool onFirstStage (int band) noexcept { return band % 2 == 0; }

    double sliderPosition (float nominalDb) noexcept { return juce::jlimit (0.0, 1.0, 0.5 + (double) nominalDb / 30.0); }

    // The SMX's two compressors: a fast attack on the high band and a slower one on the low band
    // (the low band is what keeps sustained bass notes even). Threshold/ratio are the model's own
    // curve; the knobs set how far past 1:1 each band goes (up to ~4:1 at full).
    constexpr double loAtkSec = 25.0e-3, loRelSec = 300.0e-3;
    constexpr double hiAtkSec = 1.5e-3, hiRelSec = 80.0e-3;
    constexpr double compThreshold = 0.06;

    inline double compGain (double env, double amount) noexcept
    {
        if (env <= compThreshold)
            return 1.0;
        const double ratio = 1.0 + 3.0 * amount;
        const double outEnv = compThreshold * std::pow (env / compThreshold, 1.0 / ratio);
        return outEnv / env;
    }
}

TraceElliotGP12StyleAmplifierProcessor::TraceElliotGP12StyleAmplifierProcessor()
{
    auto makeParam = [] (const char* id, const char* name)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    };
    auto makeSlider = [] (const char* id, const char* name)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            id, name, juce::NormalisableRange<float> (-15.0f, 15.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            {
                return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
            }));
    };

    auto gainParam = makeParam ("tegp12_input_gain", "Input Gain");
    auto shapeParam = std::make_unique<juce::AudioParameterFloat> (
        "tegp12_pre_shape", "Pre Shape", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return v < 0.5f ? juce::String ("Flat") : (v < 1.5f ? juce::String ("Shape 1") : juce::String ("Shape 2"));
        }));
    auto loCompParam = makeParam ("tegp12_low_compression", "Low Compression");
    auto balanceParam = makeParam ("tegp12_eq_balance", "EQ Balance");
    auto hiCompParam = makeParam ("tegp12_high_compression", "High Compression");
    auto graphicParam = std::make_unique<juce::AudioParameterFloat> (
        "tegp12_graphic", "Graphic", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return v < 0.5f ? juce::String ("Out") : juce::String ("In");
        }));
    auto gLevelParam = makeSlider ("tegp12_graphic_level", "Graphic Level");
    auto outputParam = makeParam ("tegp12_output", "Output");

    inputGain = gainParam.get();
    preShape = shapeParam.get();
    lowCompression = loCompParam.get();
    eqBalance = balanceParam.get();
    highCompression = hiCompParam.get();
    graphic = graphicParam.get();
    graphicLevel = gLevelParam.get();
    output = outputParam.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "tegp12", "Trace Elliot GP12", "|", std::move (gainParam));
    group->addChild (std::move (shapeParam));
    group->addChild (std::move (loCompParam));
    group->addChild (std::move (balanceParam));
    group->addChild (std::move (hiCompParam));
    group->addChild (std::move (graphicParam));
    group->addChild (std::move (gLevelParam));
    group->addChild (std::move (outputParam));

    // The 12-band graphic EQ is a front-panel section on the real GP12 -> page 1 with the rest of
    // the panel controls, not a sub-page.
    const char* bandNames[numBands] = { "30 Hz", "40 Hz", "60 Hz", "100 Hz", "180 Hz", "340 Hz",
                                        "660 Hz", "1.3 kHz", "2.6 kHz", "5 kHz", "10 kHz", "15 kHz" };
    for (int i = 0; i < numBands; ++i)
    {
        const juce::String id ("tegp12_eq_" + juce::String (i));
        auto p = makeSlider (id.toRawUTF8(), bandNames[i]);
        bands[(size_t) i] = p.get();
        group->addChild (std::move (p));
    }

    // Page 2 (synthetic, like every amp's): a Cab selector. Off by default -- the GP12 is a DI preamp with no speaker.
    auto cab = std::make_unique<juce::AudioParameterFloat> (
        "tegp12_cab", "Cab", juce::NormalisableRange<float> (0.0f, 3.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            switch (juce::roundToInt (v)) { case 1: return juce::String ("1x15 Bass"); case 2: return juce::String ("8x10 Fridge");
                                            case 3: return juce::String ("4x10 Tweed"); default: return juce::String ("Off"); }
        }));
    cabParam = cab.get();
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("tegp12_page2", "Page 2", "|", std::move (cab));
    group->addChild (std::move (page2));

    parameters = std::move (group);
}

void TraceElliotGP12StyleAmplifierProcessor::updateCab()
{
    const int sel = juce::roundToInt (cabParam->get());
    if (sel == appliedCab)
        return;
    const tubeamp::CabOption* options[] = { &tubeamp::cabs::bass115, &tubeamp::cabs::bass810, &tubeamp::cabs::tweed410 };
    if (sel > 0)
    {
        const auto& c = options[(size_t) juce::jlimit (0, 2, sel - 1)]->profile;
        for (auto& ch : channels)
            ch.cab.prepare (sampleRate, c.resHz, c.resDb, c.loHz, c.lpHz, c.airHz, c.notchHz, c.notchDb);
    }
    appliedCab = sel;
}

void TraceElliotGP12StyleAmplifierProcessor::buildChannel (Channel& ch)
{
    // Each graphic stage is the same structure as the graphic EQs' equaliser block: an op-amp whose
    // (+) and (-) nodes are joined through each band's slider to a gyrator branch to the bias.
    // Even bands live on the first stage, odd bands on the second (onFirstStage()).
    auto buildStage = [] (NodalCircuit& c, int& srcHandle, NodalCircuit::Node& outNode,
                          std::array<int, numBands>& rUp, std::array<int, numBands>& rDown, int firstBand)
    {
        const auto nb = c.addNode(), src = c.addNode(), nOut = c.addNode(), pNode = c.addNode(), nNode = c.addNode(), out = c.addNode();
        c.addSource (nb, vBias);
        srcHandle = c.addSource (src, vBias);

        c.addResistor (src, nOut, opAmp.outputOhms);
        c.addResistor (nOut, pNode, legOhms);
        c.addOpAmpMacro (pNode, nNode, out, opAmp);
        c.addResistor (nNode, out, legOhms);

        for (int k = 0; k < 6; ++k)
        {
            const int band = firstBand + 2 * k;
            const auto wiper = c.addNode();
            rUp[(size_t) band] = c.addResistor (wiper, pNode, 0.5 * sliderOhms);
            rDown[(size_t) band] = c.addResistor (wiper, nNode, 0.5 * sliderOhms);

            const auto& g = gyrators[band];
            const auto x = c.addNode(), y = c.addNode();
            c.addCapacitor (wiper, x, g.cSeries);
            c.addResistor (x, y, 2.0 * gyrR);
            c.addCoupledInductors ({ { y, nb } }, { g.cMid * gyrR * gyrR });
            for (auto n : { wiper, x, y })
                c.setInitialGuess (n, vBias);
        }

        outNode = out;
        for (auto n : { nOut, pNode, nNode, out })
            c.setInitialGuess (n, vBias);
    };

    buildStage (ch.eq1, ch.srcEq1, ch.nEq1Out, ch.rUp, ch.rDown, 0);
    buildStage (ch.eq2, ch.srcEq2, ch.nEq2Out, ch.rUp, ch.rDown, 1);

    // Mid Pre Shape (C5-C7, C96, R13, R14, R16 into IC1a; IC2 picks R15 10k "straight" or R17 2k2 "shaped" into
    // IC1b's C8 1u5 / R18 33k inverting stage). Driven from TR2's emitter (low impedance), so straight from the source.
    {
        auto& c = ch.pre;
        const auto gnd = NodalCircuit::ground;
        const auto a = c.addNode(), b = c.addNode(), cc = c.addNode(), d = c.addNode(), o1 = c.addNode(),
                   sw = c.addNode(), n = c.addNode();
        ch.srcPre = c.addSource (a, 0.0);
        c.addCapacitor (a, b, 10.0e-9);                  // C6
        c.addCapacitor (b, cc, 10.0e-9);                 // C7
        c.addResistor (a, d, 33.0e3);                    // R13
        c.addResistor (d, cc, 33.0e3);                   // R16
        c.addCapacitor (d, gnd, 100.0e-9);               // C5
        c.addResistor (b, gnd, 6.8e3);                   // R14
        c.addCapacitor (b, gnd, 4.7e-9);                 // C96
        c.addOpAmp (cc, o1, o1);                         // IC1a follower
        ch.rShaped = c.addResistor (o1, sw, 1.0e12);     // R17 2k2 through IC2
        ch.rStraight = c.addResistor (a, sw, 10.0e3);    // R15 10k through IC2
        c.addCapacitor (sw, n, 1.5e-6);                  // C8
        ch.nPreOut = c.addNode();
        c.addOpAmp (gnd, n, ch.nPreOut);                 // IC1b
        c.addResistor (n, ch.nPreOut, 33.0e3);           // R18
    }
}

void TraceElliotGP12StyleAmplifierProcessor::updatePreShape()
{
    const int shape = (int) (preShape->get() + 0.5f);
    const bool graphicOn = graphic->get() >= 0.5f;
    if (shape == appliedShape && graphicOn == appliedGraphic)
        return;
    appliedShape = shape;
    appliedGraphic = graphicOn;

    for (auto& ch : channels)
    {
        // Shape 1 is the drawing's Mid Pre Shape network (ch.pre, IC2 switched to R17). Shape 2 -- the SMX's second
        // curve, not on the Series 6 drawing -- stays the estimated shelf / cut / shelf set on the straight path.
        ch.pre.setResistance (ch.rShaped, shape == 1 ? 2.2e3 : 1.0e12);
        ch.pre.setResistance (ch.rStraight, shape == 1 ? 1.0e12 : 10.0e3);
        const double loDb = shape == 2 ? 4.0 : 0.0;
        const double midDb = shape == 2 ? -3.0 : 0.0;
        const double hiDb = shape == 2 ? 2.0 : 0.0;
        ch.preLo.makeLowShelf (sampleRate, 80.0, loDb);
        ch.preMid.makePeak (sampleRate, 400.0, 0.8, midDb);
        ch.preHi.makeHighShelf (sampleRate, 4000.0, hiDb);
    }
}

void TraceElliotGP12StyleAmplifierProcessor::updateSliders()
{
    bool changed = false;
    for (int k = 0; k < numBands; ++k)
    {
        const float v = bands[(size_t) k]->get();
        if (v != appliedSliders[(size_t) k])
        {
            appliedSliders[(size_t) k] = v;
            changed = true;
        }
    }
    if (! changed)
        return;

    for (auto& ch : channels)
    {
        for (int k = 0; k < numBands; ++k)
        {
            const double u = sliderPosition (appliedSliders[(size_t) k]);
            auto& c = onFirstStage (k) ? ch.eq1 : ch.eq2;
            c.setResistance (ch.rUp[(size_t) k], juce::jmax (sliderMinOhms, sliderOhms * u));
            c.setResistance (ch.rDown[(size_t) k], juce::jmax (sliderMinOhms, sliderOhms * (1.0 - u)));
        }
    }
}

void TraceElliotGP12StyleAmplifierProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    smoothedInputGain.reset (newSampleRate, 0.02);
    smoothedInputGain.setCurrentAndTargetValue ((float) (0.25 + 7.75 * pots::audio (inputGain->get())));
    smoothedBalance.reset (newSampleRate, 0.02);
    smoothedBalance.setCurrentAndTargetValue (eqBalance->get());
    smoothedOutput.reset (newSampleRate, 0.02);
    smoothedOutput.setCurrentAndTargetValue ((float) (4.0 * pots::audio (output->get())));

    for (auto& ch : channels)
    {
        ch = Channel {};   // zeroes the pre-shape biquads' z state too; updatePreShape() rewrites the coefficients below
        buildChannel (ch);
    }

    appliedSliders.fill (1.0e9f);
    appliedShape = -1;
    appliedCab = -1;
    updateCab();
    for (auto& ch : channels)
        ch.cabMix = appliedCab > 0 ? 1.0 : 0.0;
    appliedGraphic = false;
    updatePreShape();
    updateSliders();

    dcOk = true;
    for (auto& ch : channels)
    {
        dcOk = ch.pre.prepare (newSampleRate) && dcOk;
        dcOk = ch.eq1.prepare (newSampleRate) && dcOk;
        ch.eq2.setSource (ch.srcEq2, ch.eq1.voltage (ch.nEq1Out));
        dcOk = ch.eq2.prepare (newSampleRate) && dcOk;
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void TraceElliotGP12StyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedInputGain.setTargetValue ((float) (0.25 + 7.75 * pots::audio (inputGain->get())));
    smoothedBalance.setTargetValue (eqBalance->get());
    smoothedOutput.setTargetValue ((float) (4.0 * pots::audio (output->get())));

    const double ts = 1.0 / sampleRate;
    updateCab();
    const double cabTarget = appliedCab > 0 ? 1.0 : 0.0;
    const double cabFade = 1.0 - std::exp (-1.0 / (0.004 * sampleRate)); // ~4 ms crossfade on a Cab change
    const double kCross = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 250.0 * ts);
    const double kLoAtk = 1.0 - std::exp (-ts / loAtkSec);
    const double kLoRel = 1.0 - std::exp (-ts / loRelSec);
    const double kHiAtk = 1.0 - std::exp (-ts / hiAtkSec);
    const double kHiRel = 1.0 - std::exp (-ts / hiRelSec);

    const double loComp = (double) lowCompression->get();
    const double hiComp = (double) highCompression->get();
    const double gLevelDb = (double) graphicLevel->get();
    const bool graphicOn = graphic->get() >= 0.5f;
    const double gLevel = std::pow (10.0, gLevelDb / 20.0);

    for (int i = 0; i < numSamples; ++i)
    {
        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePreShape();
            updateSliders();
        }

        const double gIn = (double) smoothedInputGain.getNextValue();
        const double bal = juce::jlimit (0.0, 1.0, (double) smoothedBalance.getNextValue());
        const double gOut = (double) smoothedOutput.getNextValue();

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            double x = gIn * (double) data[i];

            // Pre-Shape: the Mid Pre Shape circuit. IC1b inverts with 33k / 10k on the straight path; its output is
            // normalised by that so Flat stays unity and Shape 1 keeps the circuit's real level step (33k / 2k2).
            ch.pre.setSource (ch.srcPre, x);
            bool okPre = ch.pre.solveSample();
            x = -ch.pre.voltage (ch.nPreOut) * (10.0e3 / 33.0e3);
            x = ch.preHi.process (ch.preMid.process (ch.preLo.process (x)));

            // SMX crossover: complementary first-order split, so the two bands recombine exactly.
            ch.lpX += kCross * (x - ch.lpX);
            const double lo = ch.lpX;
            const double hi = x - lo;

            // The two compressors, each with its own detector speed.
            const double aLo = std::abs (lo);
            ch.envLo += (aLo - ch.envLo) * (aLo > ch.envLo ? kLoAtk : kLoRel);
            const double aHi = std::abs (hi);
            ch.envHi += (aHi - ch.envHi) * (aHi > ch.envHi ? kHiAtk : kHiRel);

            // The knob adds both squeeze and level on the real unit -- the makeup is part of the
            // control's documented behaviour, not a calibration trim.
            const double loOut = lo * compGain (ch.envLo, loComp) * (1.0 + loComp);
            const double hiOut = hi * compGain (ch.envHi, hiComp) * (1.0 + hiComp);

            // EQ BALANCE: flat at centre (the complementary split sums back to x), favouring one band at the ends.
            double wet = 2.0 * ((1.0 - bal) * loOut + bal * hiOut);

            // The EQ stages always solve, Graphic out included: otherwise their capacitor and
            // inductor histories go stale and re-engaging replays whatever was last in them.
            ch.eq1.setSource (ch.srcEq1, vBias + wet);
            bool ok = ch.eq1.solveSample() && okPre;
            ch.eq2.setSource (ch.srcEq2, ch.eq1.voltage (ch.nEq1Out));
            ok = ch.eq2.solveSample() && ok;
            if (chIdx == 0)
            {
                ++sampleCount;
                if (! ok)
                    ++failureCount;
            }

            if (graphicOn)
                wet = ch.eq2.voltage (ch.nEq2Out) - vBias;
            wet *= gLevel;

            double out = wet * gOut;
            if (ch.cabMix > 1.0e-6 || cabTarget > 0.0)
            {
                const double cabbed = ch.cab.process (out);
                out += ch.cabMix * (cabbed - out);
            }
            ch.cabMix += cabFade * (cabTarget - ch.cabMix);
            data[i] = (float) out;
        }
    }

    shortcut.end (buffer);
}

void TraceElliotGP12StyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::amp_svg, IconData::amp_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
