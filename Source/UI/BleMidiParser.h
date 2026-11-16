#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>

namespace openguitarmultifx
{

/**
    Decodes the "MIDI over Bluetooth Low Energy" packet format (the BLE MIDI characteristic's notifications) into
    plain channel messages.

    A packet is: a header byte (bit 7 set, bit 6 clear, low 6 bits = timestamp high), then MIDI messages, each status
    byte preceded by a timestamp byte (bit 7 set). Running status is allowed (a timestamp byte followed directly by data
    bytes, or more data bytes with no timestamp at all), realtime bytes may sit anywhere (also behind a timestamp), and a
    SysEx can span packets: a continuation packet carries the header and then the SysEx data bytes straight away, and
    the closing F7 has its own timestamp byte. Timestamps are not used -- a pedalboard reacts on arrival.

    Only channel messages reach `onMessage` (the same set RawMidiInput forwards); one parser per characteristic, as
    running status and an open SysEx are per connection.
*/
class BleMidiParser
{
public:
    using Callback = std::function<void (const juce::MidiMessage&)>;

    explicit BleMidiParser (Callback callback) : onMessage (std::move (callback)) {}

    void feedPacket (const juce::uint8* bytes, int count)
    {
        if (count < 2 || (bytes[0] & 0xc0) != 0x80)
            return; // not a BLE MIDI packet (a header byte is 10xxxxxx)

        pendingTimestamp = false;
        for (int i = 1; i < count; ++i)
        {
            const int b = bytes[i];

            if ((b & 0x80) == 0)
            {
                pendingTimestamp = false;
                if (! inSysex)
                    data (b);
                continue;
            }

            if (! pendingTimestamp)
            {
                pendingTimestamp = true; // a timestamp low byte: what follows it is a status byte (or running-status data)
                continue;
            }

            pendingTimestamp = false;
            status (b);
        }
    }

    void reset() noexcept
    {
        runningStatus = 0;
        have = 0;
        inSysex = false;
        pendingTimestamp = false;
    }

private:
    void status (int b)
    {
        if (b >= 0xf8)
            return; // realtime: never touches running status
        if (b == 0xf0) { inSysex = true; runningStatus = 0; have = 0; return; }
        if (b == 0xf7) { inSysex = false; return; }
        inSysex = false;
        runningStatus = b < 0xf0 ? b : 0; // system common cancels running status
        have = 0;
    }

    void data (int b)
    {
        if (runningStatus == 0)
            return;

        bytes[have++] = b;
        const int type = runningStatus & 0xf0;
        const int needed = (type == 0xc0 || type == 0xd0) ? 1 : 2;
        if (have < needed)
            return;

        const int channel = (runningStatus & 0x0f) + 1;
        if (type == 0xb0)
            onMessage (juce::MidiMessage::controllerEvent (channel, bytes[0], bytes[1]));
        else if (type == 0xc0)
            onMessage (juce::MidiMessage::programChange (channel, bytes[0]));
        else if (type == 0x90)
            onMessage (juce::MidiMessage::noteOn (channel, bytes[0], (juce::uint8) bytes[1]));
        else if (type == 0x80)
            onMessage (juce::MidiMessage::noteOff (channel, bytes[0], (juce::uint8) bytes[1]));
        else if (type == 0xe0)
            onMessage (juce::MidiMessage::pitchWheel (channel, bytes[0] | (bytes[1] << 7)));
        have = 0;
    }

    Callback onMessage;
    int runningStatus = 0;
    int bytes[2] { 0, 0 };
    int have = 0;
    bool inSysex = false;
    bool pendingTimestamp = false;
};

} // namespace openguitarmultifx
