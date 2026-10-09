#pragma once

#include "TrainwreckExpressStyleAmplifierProcessor.h"

namespace openguitarmultifx
{

/**
    A Komet Concorde-style guitar amplifier (Ken Fischer's "Trainwreck Engineered" 50 W Komet, 2x EL34, solid-state
    rectified, single channel) -- docs/circuits/KometConcorde.md, read that file to understand this processor.

    Same family, solver and power-section machinery as the Trainwreck Express-style amplifier; the circuit differences are
    in TrainwreckExpressStyleAmplifierProcessor.cpp's Concorde spec: first gain stage -> anode-driven tone stack -> Volume
    -> recovery stage -> cold stage -> the extra V2B cathode follower into the phase inverter (.022 uF / 220k), a Hi-Cut
    across the phase inverter's outputs, a 4k8 output transformer and a higher solid-state B+.

    Controls, page 1: Volume, Treble, Middle, Bass, Presence, Hi-Cut, Touch (Fast / Gradual). Page 2: as the Express.
*/
class KometConcordeStyleAmplifierProcessor : public TrainwreckExpressStyleAmplifierProcessor
{
public:
    KometConcordeStyleAmplifierProcessor();

    const char* getName() const override { return "Komet Concorde-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffb8862b); }
};

} // namespace openguitarmultifx
