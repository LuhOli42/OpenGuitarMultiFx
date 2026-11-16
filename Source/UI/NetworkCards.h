#pragma once

#include "OpenGuitarMultiFxLookAndFeel.h"
#include "OverlayChrome.h"
#include "Theme.h"
#include "TouchSizing.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace openguitarmultifx
{

namespace netcards
{
    /** The row of the network / device in use: an accent bar on the left, an accent outline and a "CONNECTED" pill
        on the right, so it reads at a glance instead of as one more grey caption. Returns the space left of the pill. */
    inline juce::Rectangle<float> paintConnectedRow (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        const auto& t = theme::current();
        const auto accent = OpenGuitarMultiFxLookAndFeel::getAppAccentColour();
        g.setColour (accent.withAlpha (0.14f));
        g.fillRoundedRectangle (bounds, t.smallRadius);
        g.setColour (accent);
        g.drawRoundedRectangle (bounds.reduced (1.0f), t.smallRadius, 2.0f);
        g.fillRoundedRectangle (bounds.withWidth (6.0f).reduced (0.0f, 6.0f), 3.0f);

        auto pill = bounds.withTrimmedRight (90.0f).removeFromRight (128.0f).withSizeKeepingCentre (124.0f, 30.0f);
        g.fillRoundedRectangle (pill, 15.0f);
        g.setColour (theme::inkOn (accent));
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText ("CONNECTED", pill.toNearestInt(), juce::Justification::centred);
        return bounds.withRight (pill.getX() - 8.0f);
    }

    /** Runs `args` (no shell), waits for it and returns stdout+stderr. Call from a worker thread only. */
    inline juce::String run (const juce::StringArray& args)
    {
        juce::ChildProcess process;
        if (! process.start (args))
            return {};
        auto output = process.readAllProcessOutput();
        process.waitForProcessToFinish (30000);
        return output;
    }

    /** One `nmcli -t` line -> fields. nmcli escapes ':' (and '\') inside a value with a backslash. */
    inline juce::StringArray splitTerse (const juce::String& line)
    {
        juce::StringArray fields;
        juce::String current;
        for (int i = 0; i < line.length(); ++i)
        {
            const auto ch = line[i];
            if (ch == '\\' && i + 1 < line.length())
                current << line[++i];
            else if (ch == ':')
            {
                fields.add (current);
                current.clear();
            }
            else
                current << ch;
        }
        fields.add (current);
        return fields;
    }

    /** Signal-strength bars (0-4), drawn instead of a font glyph (the app's font has no symbols). */
    inline void drawBars (juce::Graphics& g, juce::Rectangle<float> area, int bars, juce::Colour on, juce::Colour off)
    {
        const float w = area.getWidth() / 7.0f;
        for (int i = 0; i < 4; ++i)
        {
            const float h = area.getHeight() * (0.3f + 0.23f * (float) i);
            g.setColour (i < bars ? on : off);
            g.fillRoundedRectangle (area.getX() + (float) i * 2.0f * w, area.getBottom() - h, w, h, 1.5f);
        }
    }
}

/**
    Settings card for the Wi-Fi radio (nmcli): scan, pick a network, type the password with the on-screen keyboard
    (OnScreenKeyboard.h), connect. The device has no keyboard or desktop to do it with.
*/
class WifiCard : public juce::Component,
                 private juce::ListBoxModel
{
public:
    WifiCard()
    {
        addAndMakeVisible (titleLabel);
        titleLabel.setText ("Wi-Fi", juce::dontSendNotification);
        titleLabel.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));

        addAndMakeVisible (statusLabel);
        statusLabel.setFont (juce::Font (juce::FontOptions (16.0f)));
        statusLabel.setColour (juce::Label::textColourId, theme::current().textMuted);

        addAndMakeVisible (closeButton);
        closeButton.onClick = [this] { if (onPopOverlay) onPopOverlay(); };

        addAndMakeVisible (scanButton);
        scanButton.text = "Scan";
        scanButton.onClick = [this] { scan(); };

        addAndMakeVisible (list);
        list.setRowHeight (touch::minTapTarget + 16);
        list.setColour (juce::ListBox::backgroundColourId, theme::current().surface);
        touch::enableDragScroll (*list.getViewport());

        addChildComponent (passwordField);
        passwordField.setPasswordCharacter ((juce::juce_wchar) '*');
        passwordField.setTextToShowWhenEmpty ("Password", theme::current().textDim);
        passwordField.setFont (juce::Font (juce::FontOptions (22.0f)));
        passwordField.onReturnKey = [this] { connect(); };

        addChildComponent (connectButton);
        connectButton.text = "Connect";
        connectButton.kind = overlaychrome::ButtonKind::primary;
        connectButton.onClick = [this] { connect(); };

        setSize (900, 660);
        refreshCurrent();
        scan();
    }

    std::function<void()> onPopOverlay;

    void paint (juce::Graphics& g) override { g.fillAll (theme::current().background); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (24, 20);
        auto header = area.removeFromTop (64);
        closeButton.setBounds (header.removeFromRight (56).withSizeKeepingCentre (52, 52));
        scanButton.setBounds (header.removeFromRight (150).withSizeKeepingCentre (130, 56));
        titleLabel.setBounds (header);

        statusLabel.setBounds (area.removeFromTop (34));
        area.removeFromTop (6);

        auto bottom = area.removeFromBottom (selectedNeedsPassword() || connectButton.isVisible() ? 76 : 0);
        list.setBounds (area);
        connectButton.setBounds (bottom.removeFromRight (190).reduced (0, 8));
        bottom.removeFromRight (12);
        passwordField.setBounds (bottom.reduced (0, 8));
    }

private:
    struct Network
    {
        juce::String ssid;
        int signal = 0;
        bool secured = false;
        bool inUse = false;
    };

    bool selectedNeedsPassword() const
    {
        return juce::isPositiveAndBelow (selected, (int) networks.size()) && networks[(size_t) selected].secured;
    }

    void setStatus (const juce::String& text) { statusLabel.setText (text, juce::dontSendNotification); }

    void refreshCurrent()
    {
        juce::Component::SafePointer<WifiCard> safe (this);
        juce::Thread::launch ([safe]
        {
            const auto out = netcards::run ({ "nmcli", "-t", "-f", "ACTIVE,SSID", "dev", "wifi" });
            juce::String ssid;
            for (auto& line : juce::StringArray::fromLines (out))
                if (line.startsWith ("yes:"))
                    ssid = line.fromFirstOccurrenceOf (":", false, false);
            juce::MessageManager::callAsync ([safe, ssid]
            {
                if (safe != nullptr)
                    safe->setStatus (ssid.isNotEmpty() ? "Connected to " + ssid : "Not connected");
            });
        });
    }

    void scan()
    {
        setStatus ("Scanning...");
        juce::Component::SafePointer<WifiCard> safe (this);
        juce::Thread::launch ([safe]
        {
            const auto out = netcards::run ({ "nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "dev", "wifi", "list",
                                              "--rescan", "yes" });
            std::vector<Network> found;
            for (auto& line : juce::StringArray::fromLines (out))
            {
                const auto f = netcards::splitTerse (line);
                if (f.size() < 4 || f[1].isEmpty())
                    continue;
                bool duplicate = false;
                for (auto& n : found)
                    if (n.ssid == f[1])
                    {
                        n.signal = juce::jmax (n.signal, f[2].getIntValue());
                        n.inUse = n.inUse || f[0] == "*";
                        duplicate = true;
                    }
                if (! duplicate)
                    found.push_back ({ f[1], f[2].getIntValue(), f[3].isNotEmpty() && f[3] != "--", f[0] == "*" });
            }
            std::sort (found.begin(), found.end(), [] (const Network& a, const Network& b)
                       { return a.inUse != b.inUse ? a.inUse : a.signal > b.signal; }); // the one in use on top

            juce::MessageManager::callAsync ([safe, found = std::move (found)]() mutable
            {
                if (safe == nullptr)
                    return;
                safe->networks = std::move (found);
                safe->selected = -1;
                safe->passwordField.setVisible (false);
                safe->connectButton.setVisible (false);
                safe->list.updateContent();
                safe->list.repaint();
                safe->resized();
                safe->refreshCurrent();
            });
        });
    }

    void connect()
    {
        if (! juce::isPositiveAndBelow (selected, (int) networks.size()))
            return;

        const auto ssid = networks[(size_t) selected].ssid;
        const auto password = passwordField.getText();
        setStatus ("Connecting to " + ssid + "...");
        passwordField.unfocusAllComponents();

        juce::Component::SafePointer<WifiCard> safe (this);
        juce::Thread::launch ([safe, ssid, password]
        {
            juce::String out;
            if (password.isEmpty())
                out = netcards::run ({ "nmcli", "-w", "25", "connection", "up", "id", ssid }); // a saved network
            if (password.isNotEmpty() || out.containsIgnoreCase ("Error"))
            {
                juce::StringArray args { "nmcli", "-w", "25", "dev", "wifi", "connect", ssid };
                if (password.isNotEmpty())
                {
                    args.add ("password");
                    args.add (password);
                }
                out = netcards::run (args);
            }
            const bool ok = out.containsIgnoreCase ("successfully");
            juce::MessageManager::callAsync ([safe, ssid, ok, out]
            {
                if (safe == nullptr)
                    return;
                safe->setStatus (ok ? "Connected to " + ssid
                                    : "Could not connect: " + out.trim().fromLastOccurrenceOf ("Error:", false, false).trim());
                safe->refreshCurrent();
            });
        });
    }

    // ---- ListBoxModel
    int getNumRows() override { return (int) networks.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selectedRow) override
    {
        if (! juce::isPositiveAndBelow (row, (int) networks.size()))
            return;

        const auto& t = theme::current();
        const auto& n = networks[(size_t) row];
        auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (4.0f, 3.0f);

        g.setColour (selectedRow ? t.raisedHigh : t.raised);
        g.fillRoundedRectangle (bounds, t.smallRadius);
        if (selectedRow)
        {
            g.setColour (t.lineStrong);
            g.drawRoundedRectangle (bounds.reduced (0.5f), t.smallRadius, 1.5f);
        }

        auto textArea = bounds.withTrimmedRight (200.0f);
        if (n.inUse)
            textArea = netcards::paintConnectedRow (g, bounds);

        g.setColour (t.text);
        g.setFont (juce::Font (juce::FontOptions (22.0f, n.inUse ? juce::Font::bold : juce::Font::plain)));
        g.drawText (n.ssid, textArea.reduced (16.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);

        if (! n.inUse)
        {
            g.setColour (t.textMuted);
            g.setFont (juce::Font (juce::FontOptions (15.0f)));
            g.drawText (n.secured ? "secured" : "open", bounds.withTrimmedRight (86.0f).removeFromRight (150.0f).toNearestInt(),
                        juce::Justification::centredRight);
        }

        const int bars = n.signal >= 75 ? 4 : n.signal >= 50 ? 3 : n.signal >= 25 ? 2 : 1;
        netcards::drawBars (g, juce::Rectangle<float> (bounds.getRight() - 62.0f, bounds.getCentreY() - 14.0f, 34.0f, 28.0f),
                            bars, t.text, t.lineStrong);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        selected = row;
        const bool secured = selectedNeedsPassword();
        passwordField.clear();
        passwordField.setVisible (secured);
        connectButton.setVisible (true);
        resized();
        if (secured)
            passwordField.grabKeyboardFocus(); // pops the on-screen keyboard
        else
            connect();
    }

    juce::Label titleLabel, statusLabel;
    overlaychrome::CloseButton closeButton;
    overlaychrome::ActionButton scanButton, connectButton;
    juce::ListBox list { "wifiNetworks", this };
    juce::TextEditor passwordField;
    std::vector<Network> networks;
    int selected = -1;
};

/**
    Settings card for Bluetooth (bluetoothctl): scan, pick a device, pair + trust + connect, disconnect.
*/
class BluetoothCard : public juce::Component,
                      private juce::ListBoxModel
{
public:
    BluetoothCard()
    {
        addAndMakeVisible (titleLabel);
        titleLabel.setText ("Bluetooth", juce::dontSendNotification);
        titleLabel.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));

        addAndMakeVisible (statusLabel);
        statusLabel.setFont (juce::Font (juce::FontOptions (16.0f)));
        statusLabel.setColour (juce::Label::textColourId, theme::current().textMuted);

        addAndMakeVisible (closeButton);
        closeButton.onClick = [this] { if (onPopOverlay) onPopOverlay(); };

        addAndMakeVisible (scanButton);
        scanButton.text = "Scan";
        scanButton.onClick = [this] { scan(); };

        addAndMakeVisible (list);
        list.setRowHeight (touch::minTapTarget + 16);
        list.setColour (juce::ListBox::backgroundColourId, theme::current().surface);
        touch::enableDragScroll (*list.getViewport());

        setSize (900, 660);
        scan();
    }

    std::function<void()> onPopOverlay;

    void paint (juce::Graphics& g) override { g.fillAll (theme::current().background); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (24, 20);
        auto header = area.removeFromTop (64);
        closeButton.setBounds (header.removeFromRight (56).withSizeKeepingCentre (52, 52));
        scanButton.setBounds (header.removeFromRight (150).withSizeKeepingCentre (130, 56));
        titleLabel.setBounds (header);
        statusLabel.setBounds (area.removeFromTop (34));
        area.removeFromTop (6);
        list.setBounds (area);
    }

private:
    struct Device
    {
        juce::String mac, name;
        bool connected = false;
    };

    void setStatus (const juce::String& text) { statusLabel.setText (text, juce::dontSendNotification); }

    juce::String connectedSummary() const
    {
        juce::StringArray names;
        for (const auto& d : devices)
            if (d.connected)
                names.add (d.name);
        if (! names.isEmpty())
            return "Connected to " + names.joinIntoString (", ") + " (tap to disconnect)";
        return devices.empty() ? "No devices found" : "Nothing connected - tap a device to pair and connect";
    }

    void scan()
    {
        setStatus ("Scanning for 8 s... put the device in pairing mode");
        juce::Component::SafePointer<BluetoothCard> safe (this);
        juce::Thread::launch ([safe]
        {
            netcards::run ({ "bluetoothctl", "power", "on" });
            netcards::run ({ "bluetoothctl", "--timeout", "8", "scan", "on" });

            const auto connected = netcards::run ({ "bluetoothctl", "devices", "Connected" });
            std::vector<Device> found;
            for (auto& line : juce::StringArray::fromLines (netcards::run ({ "bluetoothctl", "devices" })))
            {
                // "Device AA:BB:CC:DD:EE:FF Name"
                if (! line.startsWith ("Device "))
                    continue;
                const auto rest = line.fromFirstOccurrenceOf ("Device ", false, false);
                const auto mac = rest.upToFirstOccurrenceOf (" ", false, false);
                const auto name = rest.fromFirstOccurrenceOf (" ", false, false).trim();
                if (mac.length() == 17 && name.isNotEmpty() && name != mac.replaceCharacter (':', '-'))
                    found.push_back ({ mac, name, connected.contains (mac) });
            }

            juce::MessageManager::callAsync ([safe, found = std::move (found)]() mutable
            {
                if (safe == nullptr)
                    return;
                std::stable_sort (found.begin(), found.end(), [] (const Device& a, const Device& b) { return a.connected && ! b.connected; });
                safe->devices = std::move (found);
                safe->list.updateContent();
                safe->list.repaint();
                safe->setStatus (safe->connectedSummary());
            });
        });
    }

    void toggleConnection (int row)
    {
        if (! juce::isPositiveAndBelow (row, (int) devices.size()))
            return;

        const auto device = devices[(size_t) row];
        setStatus ((device.connected ? "Disconnecting " : "Connecting to ") + device.name + "...");
        juce::Component::SafePointer<BluetoothCard> safe (this);
        juce::Thread::launch ([safe, device]
        {
            juce::String out;
            if (device.connected)
                out = netcards::run ({ "bluetoothctl", "disconnect", device.mac });
            else
            {
                netcards::run ({ "bluetoothctl", "--timeout", "20", "pair", device.mac });
                netcards::run ({ "bluetoothctl", "trust", device.mac });
                out = netcards::run ({ "bluetoothctl", "--timeout", "20", "connect", device.mac });
            }
            const bool ok = out.containsIgnoreCase ("successful");
            juce::MessageManager::callAsync ([safe, device, ok, out]
            {
                if (safe == nullptr)
                    return;
                for (auto& d : safe->devices)
                    if (d.mac == device.mac && ok)
                        d.connected = ! device.connected;
                std::stable_sort (safe->devices.begin(), safe->devices.end(),
                                  [] (const Device& a, const Device& b) { return a.connected && ! b.connected; });
                safe->setStatus (ok ? safe->connectedSummary()
                                    : "Failed: " + out.trim().fromLastOccurrenceOf ("\n", false, false).trim());
                safe->list.repaint();
            });
        });
    }

    int getNumRows() override { return (int) devices.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selectedRow) override
    {
        if (! juce::isPositiveAndBelow (row, (int) devices.size()))
            return;

        const auto& t = theme::current();
        const auto& d = devices[(size_t) row];
        auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (4.0f, 3.0f);
        g.setColour (selectedRow ? t.raisedHigh : t.raised);
        g.fillRoundedRectangle (bounds, t.smallRadius);

        auto textArea = bounds.withTrimmedRight (240.0f);
        if (d.connected)
            textArea = netcards::paintConnectedRow (g, bounds);

        g.setColour (t.text);
        g.setFont (juce::Font (juce::FontOptions (22.0f, d.connected ? juce::Font::bold : juce::Font::plain)));
        g.drawText (d.name, textArea.reduced (16.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);
        g.setColour (t.textMuted);
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
        if (! d.connected)
            g.drawText (d.mac, bounds.reduced (16.0f, 0.0f).toNearestInt(), juce::Justification::centredRight);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override { toggleConnection (row); }

    juce::Label titleLabel, statusLabel;
    overlaychrome::CloseButton closeButton;
    overlaychrome::ActionButton scanButton;
    juce::ListBox list { "btDevices", this };
    std::vector<Device> devices;
};

} // namespace openguitarmultifx
