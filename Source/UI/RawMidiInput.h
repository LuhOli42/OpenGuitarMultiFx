#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <functional>
#include <map>

#if JUCE_LINUX
 #include <dirent.h>
 #include <fcntl.h>
 #include <poll.h>
 #include <unistd.h>
#endif

namespace openguitarmultifx
{

/**
    MIDI input straight from the ALSA rawmidi nodes (/dev/snd/midiC*D*), for kernels without the ALSA sequencer.

    juce::MidiInput on Linux only speaks to the sequencer (/dev/snd/seq), and the vendor kernel of the Cubie A7S image
    ships without it: no controller of any kind (USB, or a Bluetooth MIDI bridge exposed as a rawmidi device) reached the
    app, so MIDI Learn could never learn anything. Only started when /dev/snd/seq is missing, so a desktop keeps using
    the normal JUCE path.

    A thread rescans the nodes every couple of seconds (hot-plug) and polls the open ones. Running status, realtime
    bytes and SysEx are handled; only channel messages are forwarded, as juce::MidiMessage, to `onMessage` (called on
    this thread -- MainComponent::handleIncomingMidiMessage hops to the message thread itself).
*/
class RawMidiInput : private juce::Thread
{
public:
    using Callback = std::function<void (const juce::MidiMessage&)>;

    static bool isNeeded()
    {
#if JUCE_LINUX
        return ! juce::File ("/dev/snd/seq").exists();
#else
        return false;
#endif
    }

    explicit RawMidiInput (Callback callback) : juce::Thread ("RawMidiInput"), onMessage (std::move (callback))
    {
        startThread();
    }

    ~RawMidiInput() override { stopThread (2000); }

private:
    struct Parser
    {
        int status = 0;
        int data[2] = { 0, 0 };
        int have = 0;
        bool inSysex = false;

        static int dataBytesFor (int s)
        {
            const int type = s & 0xf0;
            return (type == 0xc0 || type == 0xd0) ? 1 : 2;
        }
    };

    void feed (Parser& p, const juce::uint8* bytes, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            const int b = bytes[i];

            if (b >= 0xf8)
                continue; // realtime (clock, active sensing): may appear anywhere, never changes the running status

            if (b & 0x80)
            {
                p.inSysex = b == 0xf0;
                p.status = (b >= 0x80 && b < 0xf0) ? b : 0; // system common messages cancel running status
                p.have = 0;
                continue;
            }

            if (p.inSysex || p.status == 0)
                continue;

            p.data[p.have++] = b;
            if (p.have == Parser::dataBytesFor (p.status))
            {
                const auto type = p.status & 0xf0;
                const auto channel = (p.status & 0x0f) + 1;
                if (type == 0xb0)
                    onMessage (juce::MidiMessage::controllerEvent (channel, p.data[0], p.data[1]));
                else if (type == 0xc0)
                    onMessage (juce::MidiMessage::programChange (channel, p.data[0]));
                else if (type == 0x90)
                    onMessage (juce::MidiMessage::noteOn (channel, p.data[0], (juce::uint8) p.data[1]));
                else if (type == 0x80)
                    onMessage (juce::MidiMessage::noteOff (channel, p.data[0], (juce::uint8) p.data[1]));
                p.have = 0;
            }
        }
    }

    void run() override
    {
#if JUCE_LINUX
        struct Open { int fd; Parser parser; };
        std::map<juce::String, Open> open;
        juce::uint32 lastScan = 0;

        while (! threadShouldExit())
        {
            const auto now = juce::Time::getMillisecondCounter();
            if (open.empty() || now - lastScan > 2000)
            {
                lastScan = now;
                for (const auto& file : juce::File ("/dev/snd").findChildFiles (juce::File::findFiles, false, "midiC*D*"))
                {
                    const auto path = file.getFullPathName();
                    if (open.count (path) != 0)
                        continue;
                    const int fd = ::open (path.toRawUTF8(), O_RDONLY | O_NONBLOCK);
                    if (fd >= 0)
                        open[path] = { fd, {} };
                }
            }

            std::vector<pollfd> fds;
            std::vector<juce::String> names;
            for (auto& [name, o] : open)
            {
                fds.push_back ({ o.fd, POLLIN, 0 });
                names.push_back (name);
            }

            if (fds.empty())
            {
                wait (500);
                continue;
            }

            if (::poll (fds.data(), (nfds_t) fds.size(), 250) <= 0)
                continue;

            for (size_t i = 0; i < fds.size(); ++i)
            {
                if ((fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
                {
                    ::close (open[names[i]].fd);
                    open.erase (names[i]); // unplugged
                    continue;
                }

                if ((fds[i].revents & POLLIN) != 0)
                {
                    juce::uint8 buffer[256];
                    const auto n = ::read (fds[i].fd, buffer, sizeof (buffer));
                    if (n > 0)
                        feed (open[names[i]].parser, buffer, (int) n);
                    else if (n < 0 && errno != EAGAIN)
                    {
                        ::close (open[names[i]].fd);
                        open.erase (names[i]);
                    }
                }
            }
        }

        for (auto& [name, o] : open)
            ::close (o.fd);
#endif
    }

    Callback onMessage;
};

} // namespace openguitarmultifx
