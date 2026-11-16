#pragma once

#include "BleMidiParser.h"

#include <juce_core/juce_core.h>

#include <map>
#include <memory>
#include <set>
#include <string>

#if JUCE_LINUX
 #include <gio/gio.h>
#endif

namespace openguitarmultifx
{

/**
    Bluetooth LE MIDI controllers, read straight from BlueZ over D-Bus, for kernels without the ALSA sequencer.

    On a normal Linux, BlueZ's MIDI plugin turns a BLE MIDI controller into an ALSA sequencer port and juce::MidiInput
    sees it. The Cubie A7S kernel has no sequencer (see RawMidiInput), so a paired controller connected but its notes
    went nowhere. This class does the plugin's job itself: every few seconds it lists BlueZ's objects, (re)connects
    paired devices that offer the BLE MIDI service, subscribes to the MIDI I/O characteristic's notifications
    (StartNotify) and decodes them with BleMidiParser. Devices are paired from the app's Bluetooth screen; nothing
    here pairs or trusts anything.

    Runs its own GLib main context on its own thread (the D-Bus signals are dispatched there), so it never touches the
    message thread or the audio thread; `onMessage` is called on this thread, like RawMidiInput's.
*/
class BleMidiInput : private juce::Thread
{
public:
    using Callback = BleMidiParser::Callback;

    explicit BleMidiInput (Callback callback) : juce::Thread ("BleMidiInput"), onMessage (std::move (callback))
    {
        startThread();
    }

    ~BleMidiInput() override { stopThread (4000); }

private:
#if JUCE_LINUX
    static constexpr const char* midiServiceUuid = "03b80e5a-ede8-4b33-a751-6ce34ec4c700";
    static constexpr const char* midiIoCharacteristicUuid = "7772e5db-3868-4112-a1a9-f2669d106bf3";

    struct Subscription
    {
        BleMidiInput* owner = nullptr;
        guint signalId = 0;
        std::unique_ptr<BleMidiParser> parser;
    };

    static void onPropertiesChanged (GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                                     GVariant* parameters, gpointer userData)
    {
        auto* sub = static_cast<Subscription*> (userData);
        GVariant* changed = g_variant_get_child_value (parameters, 1); // (s a{sv} as)
        if (GVariant* value = g_variant_lookup_value (changed, "Value", G_VARIANT_TYPE ("ay")))
        {
            gsize size = 0;
            const auto* bytes = static_cast<const juce::uint8*> (g_variant_get_fixed_array (value, &size, 1));
            if (bytes != nullptr && size > 0)
                sub->parser->feedPacket (bytes, (int) size);
            g_variant_unref (value);
        }
        g_variant_unref (changed);
    }

    static bool hasMidiService (GVariant* device)
    {
        bool found = false;
        if (GVariant* uuids = g_variant_lookup_value (device, "UUIDs", G_VARIANT_TYPE ("as")))
        {
            GVariantIter it;
            const gchar* uuid = nullptr;
            g_variant_iter_init (&it, uuids);
            while (g_variant_iter_next (&it, "&s", &uuid))
                found = found || g_ascii_strcasecmp (uuid, midiServiceUuid) == 0;
            g_variant_unref (uuids);
        }
        return found;
    }

    void callAsync (GDBusConnection* bus, const gchar* path, const gchar* iface, const gchar* method)
    {
        g_dbus_connection_call (bus, "org.bluez", path, iface, method, nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE,
                                15000, nullptr, nullptr, nullptr);
    }

    void scan (GDBusConnection* bus)
    {
        GError* error = nullptr;
        GVariant* reply = g_dbus_connection_call_sync (bus, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager",
                                                       "GetManagedObjects", nullptr, G_VARIANT_TYPE ("(a{oa{sa{sv}}})"),
                                                       G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &error);
        if (reply == nullptr)
        {
            g_clear_error (&error); // BlueZ not running (yet): try again on the next scan
            return;
        }

        const auto now = juce::Time::getMillisecondCounter();
        std::set<std::string> characteristicsSeen;
        GVariant* objects = g_variant_get_child_value (reply, 0);
        GVariantIter it;
        const gchar* path = nullptr;
        GVariant* interfaces = nullptr;
        g_variant_iter_init (&it, objects);
        while (g_variant_iter_loop (&it, "{&o@a{sa{sv}}}", &path, &interfaces))
        {
            if (GVariant* device = g_variant_lookup_value (interfaces, "org.bluez.Device1", G_VARIANT_TYPE ("a{sv}")))
            {
                gboolean paired = FALSE, connected = FALSE;
                g_variant_lookup (device, "Paired", "b", &paired);
                g_variant_lookup (device, "Connected", "b", &connected);
                // A paired MIDI controller that is switched on but not connected: ask BlueZ to connect it (a BLE
                // device does not reconnect by itself). One attempt per device every 10 s at most.
                if (paired && ! connected && hasMidiService (device))
                {
                    auto& last = lastConnectAttempt[path];
                    if (last == 0 || now - last > 10000)
                    {
                        last = now;
                        callAsync (bus, path, "org.bluez.Device1", "Connect");
                    }
                }
                g_variant_unref (device);
            }

            if (GVariant* chr = g_variant_lookup_value (interfaces, "org.bluez.GattCharacteristic1", G_VARIANT_TYPE ("a{sv}")))
            {
                const gchar* uuid = nullptr;
                if (g_variant_lookup (chr, "UUID", "&s", &uuid) && uuid != nullptr
                    && g_ascii_strcasecmp (uuid, midiIoCharacteristicUuid) == 0)
                {
                    characteristicsSeen.insert (path);
                    if (subscriptions.count (path) == 0)
                    {
                        auto sub = std::make_unique<Subscription>();
                        sub->owner = this;
                        sub->parser = std::make_unique<BleMidiParser> (onMessage);
                        sub->signalId = g_dbus_connection_signal_subscribe (bus, "org.bluez", "org.freedesktop.DBus.Properties",
                                                                            "PropertiesChanged", path,
                                                                            "org.bluez.GattCharacteristic1",
                                                                            G_DBUS_SIGNAL_FLAGS_NONE, onPropertiesChanged,
                                                                            sub.get(), nullptr);
                        callAsync (bus, path, "org.bluez.GattCharacteristic1", "StartNotify");
                        juce::Logger::writeToLog (juce::String ("BLE MIDI: listening to ") + path);
                        subscriptions[path] = std::move (sub);
                    }
                }
                g_variant_unref (chr);
            }
        }
        g_variant_unref (objects);
        g_variant_unref (reply);

        // The controller went away (switched off, out of range): its characteristic object is gone. Drop the
        // subscription; a fresh one is made when it comes back.
        for (auto i = subscriptions.begin(); i != subscriptions.end();)
        {
            if (characteristicsSeen.count (i->first) == 0)
            {
                g_dbus_connection_signal_unsubscribe (bus, i->second->signalId);
                juce::Logger::writeToLog ("BLE MIDI: lost " + juce::String (i->first));
                i = subscriptions.erase (i);
            }
            else
                ++i;
        }
    }

    void run() override
    {
        GMainContext* context = g_main_context_new();
        g_main_context_push_thread_default (context);

        GError* error = nullptr;
        GDBusConnection* bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, nullptr, &error);
        if (bus == nullptr)
        {
            juce::Logger::writeToLog ("BLE MIDI: no system D-Bus (" + juce::String (error != nullptr ? error->message : "?") + ")");
            g_clear_error (&error);
        }
        else
        {
            juce::uint32 lastScan = 0;
            while (! threadShouldExit())
            {
                const auto now = juce::Time::getMillisecondCounter();
                if (lastScan == 0 || now - lastScan > 3000)
                {
                    lastScan = now;
                    scan (bus);
                }
                while (g_main_context_iteration (context, FALSE)) {}
                wait (5);
            }

            for (auto& [path, sub] : subscriptions)
                g_dbus_connection_signal_unsubscribe (bus, sub->signalId);
            subscriptions.clear();
            g_object_unref (bus);
        }

        g_main_context_pop_thread_default (context);
        g_main_context_unref (context);
    }

    std::map<std::string, std::unique_ptr<Subscription>> subscriptions;
    std::map<std::string, juce::uint32> lastConnectAttempt;
#else
    void run() override {}
#endif

    Callback onMessage;
};

} // namespace openguitarmultifx
