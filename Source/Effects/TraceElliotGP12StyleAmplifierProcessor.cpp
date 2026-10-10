#include "TraceElliotGP12StyleAmplifierProcessor.h"
#include "PotTaper.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double vBias = 4.5;           // the same single-supply op-amp topology as the graphic EQs
    constexpr double sliderOhms = 10.0e3;   // GEQ slider track (linear, as on the Boss-style graphics)
    constexpr double sliderMinOhms = 10.0;
    const NodalCircuit::OpAmpMacro opAmp { 1.0e5, 3.0e6, 75.0, 2.0, 7.0, 0.0, 0.0 };

    /** One band's gyrator branch -- the same element the Boss graphic EQs use (an inductor
        L = C_gyr R_gyr R_loss in series with R_loss, resonating with C_series). Values are tuned to
        the GP12 SMX's published band centres; the real BOM is not published. */
    struct Gyrator { double cSeries, cGyr, rGyr, rLoss; };
    constexpr Gyrator gyrators[TraceElliotGP12StyleAmplifierProcessor::numBands] = {
        { 4.7e-6, 0.15e-6, 100.0e3, 400.0 },    // 30 Hz
        { 3.3e-6, 0.15e-6, 100.0e3, 320.0 },    // 40 Hz
        { 2.2e-6, 0.1e-6, 100.0e3, 320.0 },     // 60 Hz
        { 1.5e-6, 0.068e-6, 100.0e3, 248.0 },   // 100 Hz
        { 0.68e-6, 0.033e-6, 100.0e3, 348.0 },  // 180 Hz
        { 0.33e-6, 0.022e-6, 100.0e3, 302.0 },  // 340 Hz
        { 0.15e-6, 0.012e-6, 100.0e3, 323.0 },  // 660 Hz
        { 0.068e-6, 0.0068e-6, 100.0e3, 324.0 },// 1.3 kHz
        { 0.033e-6, 0.0033e-6, 100.0e3, 344.0 },// 2.6 kHz
        { 0.015e-6, 0.0022e-6, 100.0e3, 307.0 },// 5 kHz
        { 0.0068e-6, 0.0012e-6, 100.0e3, 310.0 },// 10 kHz
        { 0.0033e-6, 0.001e-6, 100.0e3, 341.0 },// 15 kHz
    };

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
        "tegp12", "Trace Elliot GP12-Style Amplifier", "|", std::move (gainParam));
    group->addChild (std::move (shapeParam));
    group->addChild (std::move (loCompParam));
    group->addChild (std::move (balanceParam));
    group->addChild (std::move (hiCompParam));
    group->addChild (std::move (graphicParam));
    group->addChild (std::move (gLevelParam));
    group->addChild (std::move (outputParam));

    // Page 2: the twelve graphic band sliders.
    auto eqPage = std::make_unique<juce::AudioProcessorParameterGroup> ("tegp12_geq", "Graphic EQ", "|");
    const char* bandNames[numBands] = { "30 Hz", "40 Hz", "60 Hz", "100 Hz", "180 Hz", "340 Hz",
                                        "660 Hz", "1.3 kHz", "2.6 kHz", "5 kHz", "10 kHz", "15 kHz" };
    for (int i = 0; i < numBands; ++i)
    {
        const juce::String id ("tegp12_eq_" + juce::String (i));
        auto p = makeSlider (id.toRawUTF8(), bandNames[i]);
        bands[(size_t) i] = p.get();
        eqPage->addChild (std::move (p));
    }
    group->addChild (std::move (eqPage));

    parameters = std::move (group);
}

void TraceElliotGP12StyleAmplifierProcessor::buildChannel (Channel& ch)
{
    // Each graphic stage is the same structure as the graphic EQs' equaliser block: an op-amp whose
    // (+) and (-) nodes are joined through each band's slider to a gyrator branch to the bias.
    // Bands 0..5 live on the first stage, 6..11 on the second.
    auto buildStage = [] (NodalCircuit& c, int& srcHandle, NodalCircuit::Node& outNode,
                          std::array<int, numBands>& rUp, std::array<int, numBands>& rDown, int firstBand)
    {
        const auto nb = c.addNode(), src = c.addNode(), nOut = c.addNode(), pNode = c.addNode(), nNode = c.addNode(), out = c.addNode();
        c.addSource (nb, vBias);
        srcHandle = c.addSource (src, vBias);

        c.addResistor (src, nOut, opAmp.outputOhms);
        c.addResistor (nOut, pNode, 3.3e3);
        c.addOpAmpMacro (pNode, nNode, out, opAmp);
        c.addResistor (nNode, out, 3.3e3);

        for (int k = 0; k < 6; ++k)
        {
            const auto wiper = c.addNode();
            rUp[(size_t) (firstBand + k)] = c.addResistor (wiper, pNode, 5.0e3);
            rDown[(size_t) (firstBand + k)] = c.addResistor (wiper, nNode, 5.0e3);

            const auto& g = gyrators[firstBand + k];
            const auto x = c.addNode(), y = c.addNode();
            c.addCapacitor (wiper, x, g.cSeries);
            c.addResistor (x, y, g.rLoss);
            c.addResistor (y, nb, g.rGyr);
            c.addCoupledInductors ({ { y, nb } }, { g.cGyr * g.rGyr * g.rLoss });
            for (auto n : { wiper, x, y })
                c.setInitialGuess (n, vBias);
        }

        outNode = out;
        for (auto n : { nOut, pNode, nNode, out })
            c.setInitialGuess (n, vBias);
    };

    buildStage (ch.eq1, ch.srcEq1, ch.nEq1Out, ch.rUp, ch.rDown, 0);
    buildStage (ch.eq2, ch.srcEq2, ch.nEq2Out, ch.rUp, ch.rDown, 6);
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
        // Shape 1: the classic Trace Elliot scoop -- lows and presence up, mids down.
        // Shape 2: a wider, shallower rock variant. Flat: all gains 0 dB.
        const double loDb = shape == 1 ? 6.0 : (shape == 2 ? 4.0 : 0.0);
        const double midDb = shape == 1 ? -6.0 : (shape == 2 ? -3.0 : 0.0);
        const double hiDb = shape == 1 ? 5.0 : (shape == 2 ? 2.0 : 0.0);
        ch.preLo.makeLowShelf (sampleRate, shape == 2 ? 80.0 : 60.0, loDb);
        ch.preMid.makePeak (sampleRate, 400.0, 0.8, midDb);
        ch.preHi.makeHighShelf (sampleRate, shape == 2 ? 4000.0 : 3000.0, hiDb);
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
            auto& c = k < 6 ? ch.eq1 : ch.eq2;
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
    appliedGraphic = false;
    updatePreShape();
    updateSliders();

    dcOk = true;
    for (auto& ch : channels)
    {
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

            // Pre-Shape: the two fixed curves (biquad shelves + a mid cut); shape 0 passes through.
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
            bool ok = ch.eq1.solveSample();
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

            data[i] = (float) (wet * gOut);
        }
    }

    shortcut.end (buffer);
}

void TraceElliotGP12StyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::pulse_svg, IconData::pulse_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
