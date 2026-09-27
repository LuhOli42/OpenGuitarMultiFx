#pragma once

#include "NodalCircuit.h"

namespace openguitarmultifx::nobels
{

/**
    What the Nobels DT-1 and ODR-1 share (their circuit diagrams, both from 2000, are drawn from the same template): the
    JFET input buffer with its electronic-bypass switch, and the output stage behind the Level pot. Only three values in
    the output stage differ between the two pedals, so it is a function with those as parameters
    (docs/circuits/DT1StyleDistortion.md, ODR1StyleOverdrive.md).

    Every node in these pedals is in the usual 0..9 V picture with the audio ground at 4.5 V (`vBias`); the diagram's "V-"
    is the battery negative, 0 V, and its flat-bar ground is the 4.5 V midpoint.
*/
constexpr double vBias = 4.5;

// 1N4148-class silicon (the project's standard) and a red LED (the Guv'nor's: ~1.75 V at 0.5 mA).
constexpr double siIs = 2.52e-9;
constexpr double siNVt = 1.752 * 25.85e-3;
constexpr double ledIs = 1.3e-19;
constexpr double ledNVt = 1.9 * 25.85e-3;

// A JFET used as a series switch (the electronic bypass) is a small resistance while it conducts.
constexpr double jfetSwitchOhms = 200.0;
constexpr double downstreamLoad = 1.0e6;

/** NJM4558 / RC4558-class dual op-amp on a 9 V supply: 100 dB, 3 MHz, ~75 ohm out, swing ~2 V short of each rail. */
inline const NodalCircuit::OpAmpMacro njm4558 { 1.0e5, 3.0e6, 75.0, 2.0, 7.0, 0.0, 0.0 };

/** J1 -> R1 33K -> Q1 (a JFET source follower, ideal here, Vgs ~ -1.5 V; R2 1M to the battery negative) -> C1 0.22 uF ->
    R4 330K -> Q5 (series switch) -> R5 330K. Returns the node after R5, where the pedal's own input network starts. */
inline NodalCircuit::Node addInputBuffer (NodalCircuit& c, NodalCircuit::Node nb, NodalCircuit::Node in)
{
    const auto gnd = NodalCircuit::ground;
    const auto nG = c.addNode(), nS = c.addNode(), nA = c.addNode(), nB = c.addNode();
    c.addResistor (in, nG, 33.0e3);
    c.addResistor (nG, gnd, 1.0e6);
    c.addFollower (nG, nS, -1.5);
    c.addCapacitor (nS, nA, 0.22e-6);
    c.addResistor (nA, nb, 330.0e3);
    c.addResistor (nA, nB, jfetSwitchOhms);
    c.addResistor (nB, nb, 330.0e3);
    c.setInitialGuess (nS, 1.5);
    c.setInitialGuess (nA, vBias);
    c.setInitialGuess (nB, vBias);
    return nB;
}

/** The output stage behind the Level wiper: 1 uF, R 330K, a series switch, R 330K, 1 uF into a buffer's base/(+) (with `rBase`
    to ground), the buffer (an ideal follower with `followerDrop`), 3.3 uF, and `rOut` to the jack with the amp's input across
    it. Returns the output node. DT-1: rBase 200K, an emitter follower (0.65 V drop), rOut 200K. ODR-1: 150K, an op-amp
    follower (no drop), 150K. */
inline NodalCircuit::Node addOutputTail (NodalCircuit& c, NodalCircuit::Node nb, NodalCircuit::Node fromLevelWiper,
                                         double rBase, double followerDrop, double rOut)
{
    const auto gnd = NodalCircuit::ground;
    const auto nQ7 = c.addNode(), nN = c.addNode(), nBase = c.addNode(), nBuf = c.addNode(), nOut = c.addNode();
    c.addCapacitor (fromLevelWiper, nQ7, 1.0e-6);
    c.addResistor (nQ7, nb, 330.0e3);
    c.addResistor (nQ7, nN, jfetSwitchOhms);
    c.addResistor (nN, nb, 330.0e3);
    c.addCapacitor (nN, nBase, 1.0e-6);
    c.addResistor (nBase, nb, rBase);
    c.addFollower (nBase, nBuf, followerDrop);
    c.addCapacitor (nBuf, nOut, 3.3e-6);
    c.addResistor (nOut, gnd, rOut);
    c.addResistor (nOut, gnd, downstreamLoad);
    for (auto n : { nQ7, nN, nBase })
        c.setInitialGuess (n, vBias);
    c.setInitialGuess (nBuf, vBias - followerDrop);
    return nOut;
}

} // namespace openguitarmultifx::nobels
