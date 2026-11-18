#pragma once

#include <juce_core/juce_core.h>

namespace openguitarmultifx::midicc
{

/**
    A learned CC remembers where it came from: CC n from a cable/USB controller is key n (0-127), CC n from a Bluetooth
    LE MIDI controller is key 128 + n. So the same CC number on two controllers drives two different things, and every
    binding store (knobs, scene/on-off/tap/tuner pedals, expression pedals) keeps its plain int and its file format;
    bindings saved before Bluetooth worked are all 0-127, i.e. cable, which is what they were.
*/
constexpr int bluetoothOffset = 128;
constexpr int numKeys = 256;

/** The snapshot-select CC (snapshotxml::snapshotSelectCcNumber) is reserved on every source. */
constexpr int snapshotSelectCcNumber = 69;

inline int key (int ccNumber, bool bluetooth) noexcept { return ccNumber + (bluetooth ? bluetoothOffset : 0); }
inline int number (int key) noexcept { return key % bluetoothOffset; }
inline bool isBluetooth (int key) noexcept { return key >= bluetoothOffset; }
inline bool isValid (int key) noexcept { return key >= 0 && key < numKeys; }
inline bool isSnapshotSelect (int key) noexcept { return isValid (key) && number (key) == snapshotSelectCcNumber; }

/** "CC 20" for a cable controller, "BT CC 20" for a Bluetooth one. */
inline juce::String label (int key)
{
    return (isBluetooth (key) ? "BT CC " : "CC ") + juce::String (number (key));
}

} // namespace openguitarmultifx::midicc
