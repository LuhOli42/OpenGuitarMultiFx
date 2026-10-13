#pragma once

#include <algorithm>
#include <cmath>

namespace openguitarmultifx::phaser
{

/**
    One first-order all-pass phase-shift stage, the building block of every phaser: an RC network driven from both ends
    by an op-amp or a transistor phase splitter, H(s) = (1 - sRC) / (1 + sRC) -- flat magnitude, phase 0 -> -180 deg
    through f = 1 / (2 pi R C). R is the voltage-controlled element (JFET, OTA, LDR), so it changes every sample:
    implemented in topology-preserving (TPT / zero-delay-feedback) form, which stays exact and stable while it moves.
*/
class AllpassStage
{
public:
    void reset() noexcept { s = 0.0; }

    /** R in ohms, C in farads. */
    double process (double x, double r, double c, double sampleRate) noexcept
    {
        const double fc = 1.0 / (2.0 * 3.14159265358979323846 * std::max (1.0, r) * c);
        const double g = std::tan (3.14159265358979323846 * std::min (fc, 0.49 * sampleRate) / sampleRate);
        const double v = (x - s) * g / (1.0 + g);
        const double lp = v + s;
        s = lp + v;
        return 2.0 * lp - x;
    }

    double state() const noexcept { return s; }

private:
    double s = 0.0;
};

/**
    A JFET used as a voltage-controlled resistor (the MXR / Ibanez / Univox / Maestro way): in its ohmic region
    Ids = 2 beta ((Vgs - Vp) Vds - Vds^2 / 2), so its conductance is 2 beta (Vgs - Vp - Vds / 2) -- the Vds term is why
    these phasers distort a little on loud notes. Square-law parameters: Idss, Vp (beta = Idss / Vp^2).
*/
struct Jfet
{
    double idss = 6.0e-3, vp = -2.5;

    double ohms (double vgs, double vds) const noexcept
    {
        const double beta = idss / (vp * vp);
        const double drive = vgs - vp - 0.5 * vds;
        if (drive <= 1.0e-4)
            return 1.0e9; // pinched off
        return 1.0 / (2.0 * beta * drive);
    }
};

/**
    A CdS photocell lit by a lamp or an LED (Uni-Vibe, Mu-Tron Bi-Phase). Two lags make the sound: the light source
    (an incandescent filament's thermal time constant, ~40 ms; an LED is instant) and the cell, which brightens fast
    and darkens slowly (~10 ms vs ~80 ms here) -- the sweep lingers at the bottom and snaps through the top, the
    "throb". Resistance follows a power law of illumination between its lit and dark values.
*/
class LdrCell
{
public:
    void prepare (double sampleRate, double lampSeconds, double riseSeconds, double fallSeconds) noexcept
    {
        lampA = lampSeconds > 0.0 ? 1.0 - std::exp (-1.0 / (lampSeconds * sampleRate)) : 1.0;
        riseA = 1.0 - std::exp (-1.0 / (riseSeconds * sampleRate));
        fallA = 1.0 - std::exp (-1.0 / (fallSeconds * sampleRate));
    }
    void reset (double drive = 0.0) noexcept { light = drive; cond = drive; }

    /** drive 0..1 = the lamp/LED current as a fraction of full. Returns the cell's resistance. */
    double process (double drive, double litOhms, double darkOhms) noexcept
    {
        // A filament's light output rises steeply with current (radiated power ~ I^3.5 over its working range).
        const double target = std::pow (std::clamp (drive, 0.0, 1.0), lampA < 1.0 ? 3.5 : 1.0);
        light += lampA * (target - light);
        cond += (light > cond ? riseA : fallA) * (light - cond);
        // Conductance ~ illumination^0.8 between the dark and lit values.
        const double gDark = 1.0 / darkOhms, gLit = 1.0 / litOhms;
        return 1.0 / (gDark + (gLit - gDark) * std::pow (std::max (0.0, cond), 0.8));
    }

private:
    double lampA = 1.0, riseA = 1.0, fallA = 1.0, light = 0.0, cond = 0.0;
};

} // namespace openguitarmultifx::phaser
