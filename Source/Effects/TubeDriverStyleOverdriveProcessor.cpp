#include "TubeDriverStyleOverdriveProcessor.h"
#include "IconKit.h"
#include "PotTaper.h"
#include "TubeModels.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    // ---- the supply: a 12.6 V AC winding, half-wave rectified into 470 uF each way (D4/D3), R18 10 ohm, R19 470 ohm to V+,
    // R20 1K to V-, and R3 1K (with 22 uF) feeding the op-amps' positive pin. The rails are the DC solution of that with the
    // loads' currents (a NJM4558's ~3.5 mA quiescent, plus the tubes' ~0.65 mA), not a measurement: the 12.6 V is a rating
    // and the transformer's regulation moves it by a volt or two.
    constexpr double peakVolts = 12.6 * 1.41421356 - 0.7;         // 17.1 V on each filter capacitor
    constexpr double opAmpCurrent = 3.5e-3, tubeCurrent = 0.65e-3;
    constexpr double loadCurrent = opAmpCurrent + tubeCurrent;
    constexpr double vPlus = peakVolts - 0.010 * loadCurrent - 470.0 * loadCurrent;   // after R18 and R19
    constexpr double vMinus = -(peakVolts - 0.010 * loadCurrent - 1000.0 * loadCurrent); // after R18 and R20
    constexpr double vOpAmpPlus = vPlus - 1000.0 * opAmpCurrent;  // R3 in series with the op-amps' V+ pin
    constexpr double opAmpHeadroom = 1.5;

    constexpr double drivePotMax = 500.0e3;   // R5, log, wired as a rheostat
    constexpr double hiPotMax = 500.0e3;      // R13, linear
    constexpr double loPotMax = 100.0e3;      // R15, linear, wired as a rheostat
    constexpr double levelPotMax = 100.0e3;   // R17, log
    constexpr double outputLoadResistance = 1.0e6;

    // NJM4558-class op-amp on the drawing's +/- supply (the drawing does not name the part).
    const NodalCircuit::OpAmpMacro opAmp { 1.0e5, 3.0e6, 75.0, vMinus + opAmpHeadroom, vOpAmpPlus - opAmpHeadroom, 0.0, 0.0 };
}

double TubeDriverStyleOverdriveProcessor::railPositive() noexcept { return vPlus; }
double TubeDriverStyleOverdriveProcessor::railNegative() noexcept { return vMinus; }

TubeDriverStyleOverdriveProcessor::TubeDriverStyleOverdriveProcessor()
{
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };

    auto drive = make ("tubedriver_drive", "Tube Drive", 0.5f);
    auto hi = make ("tubedriver_hi", "Hi", 0.5f);
    auto lo = make ("tubedriver_lo", "Lo", 0.5f);
    auto level = make ("tubedriver_level", "Level", 0.5f);
    driveParam = drive.get();
    hiParam = hi.get();
    loParam = lo.get();
    levelParam = level.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "tubedriver", "Tube Driver-Style Overdrive", "|", std::move (drive));
    group->addChild (std::move (hi));
    group->addChild (std::move (lo));
    group->addChild (std::move (level));
    parameters = std::move (group);
}

void TubeDriverStyleOverdriveProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ block A: the input filter, IC1a (buffer), IC1b (gain)
    {
        auto& c = ch.a;
        const auto in = c.addNode();
        ch.srcIn = c.addSource (in, 0.0);

        // R1 10K, C1 0.033 uF, R2 1M and C2 47 pF into IC1a's (+). IC1a is a voltage follower at unity gain on +/-11 V: it never
        // clips for a guitar, so it is an ideal follower here (the D1/D2 1N4148s from its input to the rails never conduct either).
        const auto nR = c.addNode(), n3 = c.addNode(), nBuf = c.addNode();
        c.addResistor (in, nR, 10.0e3);
        c.addCapacitor (nR, n3, 0.033e-6);
        c.addResistor (n3, gnd, 1.0e6);
        c.addCapacitor (n3, gnd, 47.0e-12);
        c.addFollower (n3, nBuf, 0.0);

        // C4 5 uF and R4 1K5 into IC1b's (-) ((+) at ground); feedback = R5 (the Tube Drive rheostat) || C5 120 pF
        const auto nC4 = c.addNode(), m = c.addNode(), o = c.addNode();
        c.addCapacitor (nBuf, nC4, 5.0e-6);
        c.addResistor (nC4, m, 1.5e3);
        c.addOpAmpMacro (gnd, m, o, opAmp);
        ch.rDrive = c.addResistor (m, o, 1.0e3);
        c.addCapacitor (m, o, 120.0e-12);

        ch.nOp2 = o;
    }

    // ================================================================ block B: the 12AX7 and the tone network
    {
        auto& c = ch.b;
        const auto vp = c.addNode(), vn = c.addNode(), src = c.addNode();
        c.addSource (vp, vPlus);
        c.addSource (vn, vMinus);
        ch.srcOp = c.addSource (src, 0.0);

        // IC1b's output resistance, C6 0.01 uF, R6 10K into triode 1's grid (R7 10K to V-)
        const auto nOut1 = c.addNode(), nC6 = c.addNode(), g1 = c.addNode();
        c.addResistor (src, nOut1, opAmp.outputOhms);
        c.addCapacitor (nOut1, nC6, 0.01e-6);
        c.addResistor (nC6, g1, 10.0e3);
        c.addResistor (g1, vn, 10.0e3);

        // Triode 1 (pins 6/7/8): plate load R8 68K, cathode on V-. C7 0.047 uF and R9 10K into triode 2's grid (R10 10K to V-).
        const auto p1 = c.addNode(), nC7 = c.addNode(), g2 = c.addNode();
        c.addResistor (vp, p1, 68.0e3);
        c.addTriode (p1, g1, vn);
        c.addCapacitor (p1, nC7, 0.047e-6);
        c.addResistor (nC7, g2, 10.0e3);
        c.addResistor (g2, vn, 10.0e3);

        // Triode 2 (pins 1/2/3): R11 68K
        const auto p2 = c.addNode();
        c.addResistor (vp, p2, 68.0e3);
        c.addTriode (p2, g2, vn);

        // Tone: C8 330 pF into the Hi pot (R13 500K), R12 22K to N1, C10 0.047 uF N1-N2, C11 0.047 uF and R16 2K2 from N2 to
        // ground, C9 0.1 uF N1-M, the Lo pot (R15, 100K rheostat) from M to N2, R14 220K from the Hi wiper to M, and the
        // Level pot (R17) from the Hi wiper to ground.
        const auto nH = c.addNode(), nW = c.addNode(), nM = c.addNode(), nN1 = c.addNode(), nN2 = c.addNode(), nLw = c.addNode();
        c.addCapacitor (p2, nH, 330.0e-12);
        ch.rHiTop = c.addResistor (nH, nW, 1.0e3);
        ch.rHiBottom = c.addResistor (nW, nM, 1.0e3);
        c.addResistor (nW, nM, 220.0e3);
        c.addResistor (p2, nN1, 22.0e3);
        c.addCapacitor (nN1, nM, 0.1e-6);
        c.addCapacitor (nN1, nN2, 0.047e-6);
        c.addCapacitor (nN2, gnd, 0.047e-6);
        c.addResistor (nN2, gnd, 2.2e3);
        ch.rLo = c.addResistor (nM, nN2, 1.0e3);
        ch.rLevelTop = c.addResistor (nW, nLw, 1.0e3);
        ch.rLevelBottom = c.addResistor (nLw, gnd, 1.0e3);
        c.addResistor (nLw, gnd, outputLoadResistance);

        ch.nPlate1 = p1;
        ch.nPlate2 = p2;
        ch.nGrid1 = g1;
        ch.nGrid2 = g2;
        ch.nOut = nLw;

        // Bias guesses: plates about 10 V above the cathodes
        c.setInitialGuess (p1, vMinus + 10.0);
        c.setInitialGuess (p2, vMinus + 10.0);
        c.setInitialGuess (nC7, vMinus + 10.0);
        c.setInitialGuess (g1, vMinus + 0.1);
        c.setInitialGuess (g2, vMinus + 0.1);
        c.setInitialGuess (nC6, vMinus + 0.1);
    }
}

void TubeDriverStyleOverdriveProcessor::updatePots (double drive, double hi, double lo, double level)
{
    // Tube Drive: R5 500K log as a rheostat in the feedback. Clockwise = more resistance = more gain (assumed).
    const double rDrive = juce::jmax (1.0, drivePotMax * pots::audio (drive));

    // Hi: R13 500K linear, wiper toward the C8 (top) end = brighter (assumed clockwise); the wiper goes to R14 and the Level pot.
    const double hiBottom = juce::jmax (1.0, hiPotMax * hi);
    const double hiTop = juce::jmax (1.0, hiPotMax - hiBottom);

    // Lo: R15 100K linear rheostat between M and N2. MORE resistance = more low end (measured on the network: with 1 ohm
    // the bass drops 20 dB at 100 Hz), so clockwise = more resistance; the direction of the real knob is an assumption.
    const double rLo = juce::jmax (1.0, loPotMax * lo);

    // Level: R17 100K log, wiper-to-ground segment
    const double lBottom = juce::jmax (1.0, levelPotMax * pots::audio (level));
    const double lTop = juce::jmax (1.0, levelPotMax - lBottom);

    for (auto& ch : channels)
    {
        ch.a.setResistance (ch.rDrive, rDrive);
        ch.b.setResistance (ch.rHiTop, hiTop);
        ch.b.setResistance (ch.rHiBottom, hiBottom);
        ch.b.setResistance (ch.rLo, rLo);
        ch.b.setResistance (ch.rLevelTop, lTop);
        ch.b.setResistance (ch.rLevelBottom, lBottom);
    }
}

void TubeDriverStyleOverdriveProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    smoothedDrive.reset (newSampleRate, 0.02);
    smoothedDrive.setCurrentAndTargetValue (driveParam->get());
    smoothedHi.reset (newSampleRate, 0.02);
    smoothedHi.setCurrentAndTargetValue (hiParam->get());
    smoothedLo.reset (newSampleRate, 0.02);
    smoothedLo.setCurrentAndTargetValue (loParam->get());
    smoothedLevel.reset (newSampleRate, 0.02);
    smoothedLevel.setCurrentAndTargetValue (levelParam->get());

    updatePots (driveParam->get(), hiParam->get(), loParam->get(), levelParam->get());

    dcOk = true;
    for (auto& ch : channels)
    {
        dcOk = ch.a.prepare (newSampleRate) && dcOk;
        ch.b.setSource (ch.srcOp, ch.a.voltage (ch.nOp2));
        dcOk = ch.b.prepare (newSampleRate) && dcOk;
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void TubeDriverStyleOverdriveProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedDrive.setTargetValue (driveParam->get());
    smoothedHi.setTargetValue (hiParam->get());
    smoothedLo.setTargetValue (loParam->get());
    smoothedLevel.setTargetValue (levelParam->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float d = smoothedDrive.getNextValue();
        const float h = smoothedHi.getNextValue();
        const float lo = smoothedLo.getNextValue();
        const float lv = smoothedLevel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (d, h, lo, lv);
        }

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            ch.a.setSource (ch.srcIn, (double) data[i]);
            bool ok = ch.a.solveSample();

            ch.b.setSource (ch.srcOp, ch.a.voltage (ch.nOp2));
            ok = ch.b.solveSample() && ok;

            data[i] = (float) ch.b.voltage (ch.nOut);

            if (chIdx == 0)
            {
                ++sampleCount;
                if (! ok)
                    ++failureCount;
            }
        }
    }

    shortcut.end (buffer);
}

void TubeDriverStyleOverdriveProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
