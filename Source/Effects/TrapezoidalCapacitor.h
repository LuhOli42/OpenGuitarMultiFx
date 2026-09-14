#pragma once

namespace openguitarmultifx
{

/**
    Trapezoidal (bilinear/Tustin) discretization of an ideal capacitor --
    a reusable building block for circuit-modeling processors, not
    specific to any one pedal (see Source/Effects/AGENTS.md's decision log
    for why: every future circuit-modeling processor should reuse this
    instead of re-deriving the same companion model per circuit).

    A capacitor's continuous-time law i = C dv/dt discretizes, via the
    trapezoidal rule, to a simple companion model -- a fixed equivalent
    resistance plus a one-sample memory ("history") term:

        v[n] = Req * i[n] + vHist[n]
        vHist[n] = v[n-1] + Req * i[n-1]
        Req = T / (2C)

    This is exactly the same discretization a WDF capacitor uses
    internally (its port resistance Rp = T/(2C) is the identical
    formula) -- solving a circuit via direct nodal analysis with this
    companion model is equally accurate to routing it through a WDF tree,
    just organized differently (see the Ebers-Moll BJT booster's design
    notes for why direct nodal analysis was chosen there over the
    generic WDF tree).

    Current/voltage sign convention: i is the current flowing INTO the
    capacitor's positive terminal (the terminal v is measured at, taking
    the other terminal as the reference/0V node) -- standard passive sign
    convention. Callers are responsible for using this consistently when
    folding getEquivalentResistance()/getHistoryVoltage() into their own
    node equations.
*/
class TrapezoidalCapacitor
{
public:
    void prepare (double sampleRate, float capacitanceFarads) noexcept
    {
        sampleRate_ = sampleRate;
        setCapacitance (capacitanceFarads);
        reset();
    }

    void setCapacitance (float capacitanceFarads) noexcept
    {
        capacitance = capacitanceFarads;
        equivalentResistance = (float) (1.0 / sampleRate_) / (2.0f * capacitance);
    }

    void reset() noexcept
    {
        voltage = 0.0f;
        current = 0.0f;
    }

    float getEquivalentResistance() const noexcept { return equivalentResistance; }

    /** This sample's history/memory voltage, computed from last sample's
        converged state -- add this as a fixed offset (an independent
        voltage source in series with getEquivalentResistance()) into the
        surrounding circuit's node equations before solving. */
    float getHistoryVoltage() const noexcept { return voltage + equivalentResistance * current; }

    /** Called once the surrounding circuit's solve has determined this
        sample's actual voltage/current for this capacitor -- updates the
        state getHistoryVoltage() will use next sample. */
    void updateState (float newVoltage, float newCurrent) noexcept
    {
        voltage = newVoltage;
        current = newCurrent;
    }

private:
    double sampleRate_ = 48000.0;
    float capacitance = 1.0e-6f;
    float equivalentResistance = 1.0f;
    float voltage = 0.0f;
    float current = 0.0f;
};

} // namespace openguitarmultifx
