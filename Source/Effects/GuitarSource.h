#pragma once

namespace openguitarmultifx::guitarSource
{

/**
    What a guitar looks like to a pedal whose input impedance is LOW enough for it to matter: a single coil's DC
    resistance, in series with the pedal's input. Every other circuit in this project reads its input as an ideal
    voltage (docs/circuits/GainStaging.md), which is fine for a 500k-1M input; the Fuzz Face and the Tone Bender
    load the guitar heavily, and that loading is a large part of what they sound like.
*/
constexpr double resistance = 6.0e3;

/**
    A real pickup is also an inductor (a single coil ~2.5 H): its impedance rises with frequency, so a LOW input impedance does
    not just attenuate the guitar, it takes the top end off it: one pole at (Zin + R) / 2 pi L, i.e. 1-3 kHz for a 15-40K
    input. That is why a Fuzz Face or a Big Muff plugged straight into a guitar sounds round, and a model fed by an ideal
    voltage sounds hard and bright. The recorded DI already carries the pickup's response into the interface's 1M input, so
    the DI is taken as the pickup's open-circuit voltage and PickupLoadEffect applies the pole for the pedal's input impedance.
    Not modelled: the cable's capacitance and the pickup/cable resonance under load (the DI has the unloaded one already).
*/
constexpr double inductance = 2.5;

/** A standard instrument cable's shunt capacitance (~30 pF/ft, a typical 15-18 ft board cable): the OTHER half of
    "the pickup reacting with the pedal" -- together with `inductance` and whatever the pedal's own input impedance
    is, it forms a real resonant LC network (a peak, then a rolloff), not just a divider. This is why a passive
    guitar sounds different plugged into a low-input-impedance fuzz than into a 1M op-amp pedal, REGARDLESS of the
    circuit inside the pedal, and it was missing for every pedal in this project except the three built with an
    explicit series resistor (FuzzFace/ToneBender/BigMuff, before 2026-09-27). See PickupLoadEffect.h. */
constexpr double cableCapacitance = 470.0e-12;

} // namespace openguitarmultifx::guitarSource
