#include <juce_core/juce_core.h>

#include "UI/BleMidiParser.h"

#include <vector>

namespace openguitarmultifx
{

class BleMidiParserTests : public juce::UnitTest
{
public:
    BleMidiParserTests() : juce::UnitTest ("BleMidiParser", "MIDI") {}

    void runTest() override
    {
        std::vector<juce::MidiMessage> got;
        BleMidiParser parser ([&got] (const juce::MidiMessage& m) { got.push_back (m); });
        auto feed = [&parser] (std::initializer_list<int> bytes)
        {
            std::vector<juce::uint8> b;
            for (auto v : bytes)
                b.push_back ((juce::uint8) v);
            parser.feedPacket (b.data(), (int) b.size());
        };

        beginTest ("one control change: header, timestamp, status, two data bytes");
        got.clear();
        feed ({ 0x80, 0x80, 0xb0, 7, 100 });
        expectEquals ((int) got.size(), 1);
        if (got.size() == 1)
        {
            expect (got[0].isControllerOfType (7));
            expectEquals (got[0].getControllerValue(), 100);
            expectEquals (got[0].getChannel(), 1);
        }

        beginTest ("two messages in one packet, each with its own timestamp");
        got.clear();
        feed ({ 0x81, 0x82, 0xc2, 5, 0x83, 0x90, 60, 90 });
        expectEquals ((int) got.size(), 2);
        if (got.size() == 2)
        {
            expect (got[0].isProgramChange());
            expectEquals (got[0].getProgramChangeNumber(), 5);
            expectEquals (got[0].getChannel(), 3);
            expect (got[1].isNoteOn());
            expectEquals (got[1].getNoteNumber(), 60);
        }

        beginTest ("running status: timestamp then data, and data with no timestamp");
        got.clear();
        feed ({ 0x80, 0x80, 0xb0, 1, 10, 0x81, 1, 20, 1, 30 });
        expectEquals ((int) got.size(), 3);
        if (got.size() == 3)
        {
            expectEquals (got[1].getControllerValue(), 20);
            expectEquals (got[2].getControllerValue(), 30);
        }

        beginTest ("running status carries over to the next packet");
        got.clear();
        feed ({ 0x80, 0x84, 4, 40 });
        expectEquals ((int) got.size(), 1);
        if (got.size() == 1)
            expectEquals (got[0].getControllerValue(), 40);

        beginTest ("realtime bytes behind a timestamp do not break running status");
        got.clear();
        feed ({ 0x80, 0x80, 0xb0, 11, 50, 0x81, 0xf8, 0x82, 11, 60 });
        expectEquals ((int) got.size(), 2);
        if (got.size() == 2)
            expectEquals (got[1].getControllerValue(), 60);

        beginTest ("a SysEx spanning packets is skipped, and what follows it still decodes");
        got.clear();
        feed ({ 0x80, 0x80, 0xf0, 0x7e, 0x01, 0x02 });
        feed ({ 0x80, 0x03, 0x04, 0x81, 0xf7, 0x82, 0xb0, 64, 127 });
        expectEquals ((int) got.size(), 1);
        if (got.size() == 1)
            expect (got[0].isControllerOfType (64));

        beginTest ("pitch bend is 14 bits, LSB first");
        got.clear();
        feed ({ 0x80, 0x80, 0xe0, 0x00, 0x40 });
        expectEquals ((int) got.size(), 1);
        if (got.size() == 1)
            expectEquals (got[0].getPitchWheelValue(), 8192);

        beginTest ("not a BLE MIDI packet: ignored");
        got.clear();
        feed ({ 0x40, 0xb0, 7, 100 });
        expectEquals ((int) got.size(), 0);
    }
};

static BleMidiParserTests bleMidiParserTests;

} // namespace openguitarmultifx
