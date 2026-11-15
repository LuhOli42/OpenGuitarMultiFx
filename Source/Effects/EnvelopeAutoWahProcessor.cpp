#include "EnvelopeAutoWahProcessor.h"
#include "IconKit.h"
#include "PotTaper.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    // ================= MXR Envelope Filter (schematic 120-4001) =================
    constexpr double rIntegrator = 47.0e3;   // R-18 / R-25, in series with the CD4016 switches
    constexpr double rSwitchOn = 400.0;      // CD4016 on-resistance at the 4.4 V supply
    constexpr double cIntegratorGuitar = 1.0e-9;   // C-7 / C-6
    constexpr double cIntegratorBass = 3.3e-9;     // bass voicing: 3.3x the caps -> sweep 3.3x lower
    constexpr double summerGain = 1.2;       // R-13 240K feedback / R-7, R-12 200K inputs

    // Detector: input inverter 5.1M/1M (gain 5.1), then the Threshold stage 470K / (10K + pot 500K).
    constexpr double detectorStage1 = 5.1;
    constexpr double rThresholdFeedback = 470.0e3, rThresholdFixed = 10.0e3, rThresholdPot = 500.0e3;

    // CMOS inverter stages swing ~2.1 V either side of mid-rail on the 4.4 V supply.
    constexpr double mxrRailV = 2.1;
    inline double mxrRail (double v) noexcept { return mxrRailV * std::tanh (v / mxrRailV); }
    constexpr double envFullScaleV = 1.6;    // detector volts that fully open the PWM comparator

    // ================= Electro-Harmonix Doctor Q =================
    constexpr double dqR47 = 47.0e3;         // input / earth resistor on the tuning node
    constexpr double dqRf = 470.0e3;         // feedback resistor
    constexpr double dqC = 5.0e-9;           // the two .005 caps
    constexpr double dqCin = 10.0e-9;        // .01 input cap (Bass voicing path)
    constexpr double dqRinBass = 470.0e3;
    constexpr double dqRailV = 4.2;
    inline double dqRail (double v) noexcept { return dqRailV * std::tanh (v / dqRailV); }

    // Detector: Sensitivity pot -> 47K -> .05 -> inverting amp 2.2M/47K -> 1N914 -> 100R/10uF -> 1N914 -> 25K trim
    // + 22K -> BC239C base.
    constexpr double dqDetectorGain = 2.2e6 / 47.0e3;
    constexpr double dqStorageC = 10.0e-6, dqChargeR = 120.0; // 100R + diode
    constexpr double dqDiodeDrop = 0.55, dqBaseThresholdV = 1.25; // charge diode / (diode + Vbe) in the discharge path
    constexpr double dqTrimMax = 25.0e3, dqBaseR = 22.0e3;
    constexpr double dqBetaF = 500.0, vThermal = 0.02585;
    // Transistor as a resistor with ~zero collector-emitter volts: Rce ~ VT / (betaF * Ib), floored by its bulk
    // resistance (estimated), capped by leakage.
    constexpr double dqRvMin = 120.0, dqRvMax = 2.0e6;
    // The Bass voicing (input through 470K into the virtual earth) is 7.2 dB quieter than Norm at the same
    // settings (measured on the reference signal); levelled here so the two voicings match.
    constexpr double dqBassMakeup = 2.30;

    constexpr int kControlBlock = 16;
    constexpr double coupleHz = 16.0;        // .05 uF coupling into 200K at both ends (MXR)
}

double EnvelopeAutoWahProcessor::mxrCentreFrequencyHz (double duty, bool bass) noexcept
{
    const double c = bass ? cIntegratorBass : cIntegratorGuitar;
    return std::sqrt (summerGain) * duty / ((rIntegrator + rSwitchOn) * c) / juce::MathConstants<double>::twoPi;
}

double EnvelopeAutoWahProcessor::doctorQCentreFrequencyHz (double rv) noexcept
{
    // w0^2 = (1/47K + 1/Rv) / (Rf C^2)
    return std::sqrt ((1.0 / dqR47 + 1.0 / rv) / dqRf) / dqC / juce::MathConstants<double>::twoPi;
}

double EnvelopeAutoWahProcessor::integratorCap() const noexcept
{
    return model == Model::mxrBassEnvelopeFilter ? cIntegratorBass : cIntegratorGuitar;
}

const char* EnvelopeAutoWahProcessor::getName() const
{
    switch (model)
    {
        case Model::mxrEnvelopeFilter:     return "MXR Envelope Filter-Style Auto-Wah";
        case Model::mxrBassEnvelopeFilter: return "MXR Bass Envelope Filter-Style Auto-Wah";
        case Model::doctorQ:               return "Doctor Q-Style Auto-Wah";
    }
    return "Auto-Wah";
}

EnvelopeAutoWahProcessor::EnvelopeAutoWahProcessor (Model m) : model (m)
{
    auto makeParam = [] (const juce::String& id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto makeStepped = [] (const juce::String& id, const char* name, float max, float def,
                           std::function<juce::String (float)> label)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            id, name, juce::NormalisableRange<float> (0.0f, max, 1.0f), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([label] (float v, int) { return label (v); }));
    };
    const juce::String p = model == Model::mxrEnvelopeFilter ? "mxrwah_" : model == Model::mxrBassEnvelopeFilter ? "mxrbasswah_" : "doctorqwah_";
    const juce::String title = getName();

    auto modeParam = makeStepped (p + "mode", "Mode", 2.0f, 0.0f,
                                  [] (float v) { return v < 0.5f ? juce::String ("Up") : v < 1.5f ? juce::String ("Down") : juce::String ("LFO"); });
    auto rateParam = makeParam (p + "rate", "Rate", 0.5f);
    mode = modeParam.get();
    rate = rateParam.get();

    std::vector<std::unique_ptr<juce::AudioProcessorParameter>> params;
    if (isMxr())
    {
        auto t = makeParam (p + "threshold", "Threshold", 0.5f);
        auto a = makeParam (p + "attack", "Attack", 0.3f);
        auto d = makeParam (p + "decay", "Decay", 0.4f);
        auto q = makeParam (p + "peak", "Peak", 0.46f);
        threshold = t.get(); attack = a.get(); decay = d.get(); peak = q.get();
        params.push_back (std::move (t));
        params.push_back (std::move (a));
        params.push_back (std::move (d));
        params.push_back (std::move (q));
        if (model == Model::mxrBassEnvelopeFilter)
        {
            auto b = makeParam (p + "blend", "Dry/FX", 0.6f);
            blend = b.get();
            params.push_back (std::move (b));
        }
    }
    else
    {
        auto s = makeParam (p + "sensitivity", "Sensitivity", 0.6f);
        auto r = makeParam (p + "range", "Range", 0.5f);
        auto v = makeStepped (p + "voicing", "Voicing", 1.0f, 0.0f,
                              [] (float x) { return x < 0.5f ? juce::String ("Norm") : juce::String ("Bass"); });
        threshold = s.get(); rangeTrim = r.get(); voicing = v.get();
        params.push_back (std::move (s));
        params.push_back (std::move (r));
        params.push_back (std::move (v));
    }
    params.push_back (std::move (modeParam));
    params.push_back (std::move (rateParam));

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (p + "group", title, "|");
    for (auto& prm : params)
        group->addChild (std::move (prm));
    parameters = std::move (group);
}

void EnvelopeAutoWahProcessor::updateMxrConstants (Channel& ch, double r19) const noexcept
{
    const double tau = 0.5 / sampleRate;
    const double c = integratorCap();
    const double a = ch.duty / ((rIntegrator + rSwitchOn) * c); // a1 == a2 (R-18 == R-25, C-7 == C-6)
    const double gamma = 1.0 / (r19 * c);                        // R-19 leak across the band-pass cap
    const double denB = 1.0 + tau * gamma;
    ch.c1 = tau * a;
    ch.c2 = denB;
    ch.c3 = 1.0 + summerGain * tau * tau * a * a / denB;
    ch.c4 = gamma;
}

void EnvelopeAutoWahProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;
    lfoPhase = 0.0;
    smoothedBlend.reset (newSampleRate, 0.02);
    smoothedBlend.setCurrentAndTargetValue (blend != nullptr ? blend->get() : 1.0f);

    const bool down = mode->get() >= 0.5f && mode->get() < 1.5f;
    const double r19 = peak != nullptr ? 50.0e3 * std::pow (20.0, (double) peak->get()) : 200.0e3;
    for (auto& ch : channels)
    {
        ch = Channel {};
        ch.duty = down ? dutyMax : dutyMin;
        ch.tuning = down ? 1.0 : 0.0;
        if (isMxr())
            updateMxrConstants (ch, r19);
        ch.countdown = kControlBlock;
    }
}

void EnvelopeAutoWahProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;
    if (isMxr())
        processMxr (buffer);
    else
        processDoctorQ (buffer);
}

void EnvelopeAutoWahProcessor::processMxr (juce::AudioBuffer<float>& buffer)
{
    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int numSamples = buffer.getNumSamples();

    const double ts = 1.0 / sampleRate;
    const double tau = 0.5 * ts;
    const double attackSec = 0.001 * std::pow (60.0, (double) attack->get());   // 1 ms .. 60 ms
    const double releaseSec = 0.04 * std::pow (15.0, (double) decay->get());    // 40 ms .. 0.6 s
    const double kAtk = 1.0 - std::exp (-ts / attackSec);
    const double kRel = 1.0 - std::exp (-ts / releaseSec);
    const double kDuty = 1.0 - std::exp (-ts * kControlBlock / 0.001);
    const double kHp = std::exp (-juce::MathConstants<double>::twoPi * coupleHz * ts);

    const double thresholdOhms = rThresholdFixed + rThresholdPot * pots::audio ((double) threshold->get());
    const double detectorGain2 = rThresholdFeedback / thresholdOhms;
    const double r19 = 50.0e3 * std::pow (20.0, (double) peak->get());          // 50K .. 1M, ~200K at the default
    const int modeSel = (int) (mode->get() + 0.5f);
    const double lfoInc = 0.2 * std::pow (40.0, (double) rate->get()) * ts;     // 0.2 Hz .. 8 Hz
    const bool hasBlend = blend != nullptr;
    smoothedBlend.setTargetValue (hasBlend ? blend->get() : 1.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        lfoPhase += lfoInc;
        lfoPhase -= std::floor (lfoPhase);
        const double lfoU = 1.0 - std::abs (2.0 * lfoPhase - 1.0);
        const double fx = (double) smoothedBlend.getNextValue();

        for (int chIdx = 0; chIdx < numChannels; ++chIdx)
        {
            auto& s = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);
            const double x = (double) data[i];

            // Detector: two inverting stages, diode peak detector.
            const double v = mxrRail (mxrRail (x * detectorStage1) * detectorGain2);
            const double rect = std::abs (v);
            s.env += (rect - s.env) * (rect > s.env ? kAtk : kRel);

            if (--s.countdown <= 0)
            {
                s.countdown = kControlBlock;
                double u = modeSel == 2 ? lfoU : juce::jlimit (0.0, 1.0, s.env / envFullScaleV);
                if (modeSel == 1)
                    u = 1.0 - u;
                // The PWM comparator makes duty ~ control voltage (envelope); the LFO sweeps exponentially.
                const double target = modeSel == 2 ? dutyMin * std::pow (dutyMax / dutyMin, u)
                                                   : dutyMin + (dutyMax - dutyMin) * u;
                s.duty += (target - s.duty) * kDuty;
                s.tuning = (s.duty - dutyMin) / (dutyMax - dutyMin);
                updateMxrConstants (s, r19);
            }

            const double xin = kHp * (s.inHpY + x - s.inHpX);
            s.inHpX = x;
            s.inHpY = xin;

            // SVF: HP = -1.2 (x + LP); BP' = -a HP - gamma BP; LP' = -a BP -- closed-form trapezoidal step.
            const double a = s.c1 / tau;
            const double pp = s.lp + tau * s.dLp;
            const double qb = s.bp + tau * s.dBp;
            double hp = (-summerGain * (xin + pp) + summerGain * s.c1 * qb / s.c2) / s.c3;
            hp = mxrRail (hp);
            const double bp = mxrRail ((qb - s.c1 * hp) / s.c2);
            const double lp = mxrRail (pp - s.c1 * bp);
            s.dBp = -a * hp - s.c4 * bp;
            s.dLp = -a * bp;
            s.bp = bp;
            s.lp = lp;

            const double wet = kHp * (s.outHpY + bp - s.outHpX);
            s.outHpX = bp;
            s.outHpY = wet;
            data[i] = (float) (hasBlend ? x * (1.0 - fx) + wet * fx : wet);
        }
    }
}

void EnvelopeAutoWahProcessor::processDoctorQ (juce::AudioBuffer<float>& buffer)
{
    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int numSamples = buffer.getNumSamples();

    const double h = 1.0 / sampleRate;
    const double gc = 2.0 * dqC / h;
    const double kAtk = 1.0 - std::exp (-h / dqStorageC / dqChargeR);
    const double gain = dqDetectorGain * pots::audio ((double) threshold->get());
    const double rBase = dqBaseR + dqTrimMax * (double) rangeTrim->get();
    const bool bass = voicing->get() >= 0.5f;
    const int modeSel = (int) (mode->get() + 0.5f);
    const double lfoInc = 0.2 * std::pow (40.0, (double) rate->get()) * h;
    const double rIn = dqRinBass + h / (2.0 * dqCin);
    constexpr double ibMin = 2.0e-10, ibMax = 1.0e-6;                // LFO sweep of the base current, log
    const double vcMax = 3.7;
    const double logSpan = std::log (dqRvMax / dqRvMin);

    for (int i = 0; i < numSamples; ++i)
    {
        lfoPhase += lfoInc;
        lfoPhase -= std::floor (lfoPhase);
        const double lfoU = 1.0 - std::abs (2.0 * lfoPhase - 1.0);

        for (int chIdx = 0; chIdx < numChannels; ++chIdx)
        {
            auto& s = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);
            const double x = (double) data[i];

            // ---- Detector: inverting amp, half-wave charge into 10 uF, discharge through the base network ----
            const double vamp = dqRail (-gain * x);
            const double charge = vamp - dqDiodeDrop;
            if (charge > s.env)
                s.env += (charge - s.env) * kAtk;
            else
            {
                const double ib = juce::jmax (0.0, s.env - dqBaseThresholdV) / rBase;
                s.env = juce::jmax (0.0, s.env - ib * h / dqStorageC);
            }

            if (--s.countdown <= 0)
            {
                s.countdown = kControlBlock;
                double ibNow;
                if (modeSel == 2)
                    ibNow = ibMin * std::pow (ibMax / ibMin, lfoU);
                else
                {
                    // Down inverts the control: the base is held on at rest and the stored volts (x2) pull it off.
                    const double vc = modeSel == 1 ? juce::jmax (0.0, vcMax - 2.0 * s.env) : s.env;
                    ibNow = juce::jmax (0.0, vc - dqBaseThresholdV) / rBase;
                }
                s.rv = juce::jlimit (dqRvMin, dqRvMax, vThermal / (dqBetaF * ibNow + 1.0e-12));
                s.tuning = juce::jlimit (0.0, 1.0, std::log (dqRvMax / s.rv) / logSpan);
            }

            // ---- MFB band-pass, trapezoidal companion models; the op-amp's inverting input is a virtual earth ----
            const double gx = 1.0 / dqR47 + 1.0 / s.rv;
            const double a = gx + gc + gc;
            const double ih1 = -gc * s.v1 - s.i1;
            const double ih2 = -gc * s.v2 - s.i2;
            const double gin = bass ? 0.0 : 1.0 / dqR47;
            const double is = bass ? (x - s.vIn - h / (2.0 * dqCin) * s.iIn) / rIn : 0.0;

            const double rhsN = gin * x - ih1 - ih2;
            const double rhsM = -ih1 - is;
            const double det = a / dqRf + gc * gc;
            const double n = (rhsN / dqRf + gc * rhsM) / det;
            const double vo = (a * rhsM - gc * rhsN) / det;

            s.i1 = gc * n + ih1;
            s.v1 = n;
            s.i2 = gc * (n - vo) + ih2;
            s.v2 = n - vo;
            if (bass)
            {
                s.vIn += h / (2.0 * dqCin) * (s.iIn + is);
                s.iIn = is;
            }
            data[i] = (float) (dqRail (vo) * (bass ? dqBassMakeup : 1.0));
        }
    }
}

void EnvelopeAutoWahProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // No wah glyph on the sheet: the phaser's resonant-filter glyph is the closest category mate (same reasoning as
    // the Mu-Tron III filter).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::phaser_svg, IconData::phaser_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
