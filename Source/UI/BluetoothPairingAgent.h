#pragma once

#include <juce_core/juce_core.h>

#if JUCE_LINUX
 #include <gio/gio.h>
#endif

namespace openguitarmultifx
{

/**
    A BlueZ pairing agent that exists only while one of these objects is alive: the pairing window the Bluetooth
    screen opens when the user taps a device.

    Some BLE MIDI controllers (M-Vave Chocolate) ask the board to pair as soon as they connect ("Just Works" with a
    yes/no confirmation). With no agent registered BlueZ refuses ("No agent available for request type 2"), the
    controller never bonds, and BlueZ never resolves its GATT services, so BleMidiInput has no MIDI characteristic to
    listen to. A permanent auto-accepting agent would let anyone in the audience pair with the board, so this one
    accepts everything for the few seconds the user's own pairing takes and is unregistered right after; outside that
    window nothing answers and every incoming pairing is refused. Bonded devices reconnect with their stored keys and
    need no agent.

    Runs its own GLib main context on its own thread with a private system-bus connection (BlueZ calls the agent's
    methods there), so it never touches the message thread, the audio thread or BleMidiInput's connection.
*/
class BluetoothPairingAgent : private juce::Thread
{
public:
    BluetoothPairingAgent() : juce::Thread ("BluetoothPairingAgent")
    {
        startThread();
        registered.wait (3000);
    }

    ~BluetoothPairingAgent() override { stopThread (4000); }

private:
#if JUCE_LINUX
    static constexpr const char* agentPath = "/org/openguitarmultifx/pairing_agent";

    static constexpr const char* introspection =
        "<node><interface name='org.bluez.Agent1'>"
        "<method name='Release'/>"
        "<method name='RequestPinCode'><arg type='o' direction='in'/><arg type='s' direction='out'/></method>"
        "<method name='DisplayPinCode'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>"
        "<method name='RequestPasskey'><arg type='o' direction='in'/><arg type='u' direction='out'/></method>"
        "<method name='DisplayPasskey'><arg type='o' direction='in'/><arg type='u' direction='in'/>"
        "<arg type='q' direction='in'/></method>"
        "<method name='RequestConfirmation'><arg type='o' direction='in'/><arg type='u' direction='in'/></method>"
        "<method name='RequestAuthorization'><arg type='o' direction='in'/></method>"
        "<method name='AuthorizeService'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>"
        "<method name='Cancel'/>"
        "</interface></node>";

    // The board has no keypad or display to compare codes on, so every request is accepted (legacy PIN 0000).
    static void onMethodCall (GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
                              GVariant*, GDBusMethodInvocation* invocation, gpointer)
    {
        if (g_strcmp0 (method, "RequestPinCode") == 0)
            g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", "0000"));
        else if (g_strcmp0 (method, "RequestPasskey") == 0)
            g_dbus_method_invocation_return_value (invocation, g_variant_new ("(u)", 0u));
        else
            g_dbus_method_invocation_return_value (invocation, nullptr);
    }

    static bool callAgentManager (GDBusConnection* bus, const char* method, GVariant* args)
    {
        GError* error = nullptr;
        GVariant* reply = g_dbus_connection_call_sync (bus, "org.bluez", "/org/bluez", "org.bluez.AgentManager1", method,
                                                       args, nullptr, G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &error);
        if (reply == nullptr)
        {
            juce::Logger::writeToLog (juce::String ("Bluetooth pairing agent: ") + method + " failed ("
                                      + (error != nullptr ? error->message : "?") + ")");
            g_clear_error (&error);
            return false;
        }
        g_variant_unref (reply);
        return true;
    }

    void run() override
    {
        GMainContext* context = g_main_context_new();
        g_main_context_push_thread_default (context);

        GDBusConnection* bus = nullptr;
        if (gchar* address = g_dbus_address_get_for_bus_sync (G_BUS_TYPE_SYSTEM, nullptr, nullptr))
        {
            bus = g_dbus_connection_new_for_address_sync (address,
                                                          (GDBusConnectionFlags) (G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT
                                                                                  | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
                                                          nullptr, nullptr, nullptr);
            g_free (address);
        }

        guint objectId = 0;
        GDBusNodeInfo* node = g_dbus_node_info_new_for_xml (introspection, nullptr);
        if (bus != nullptr && node != nullptr)
        {
            static const GDBusInterfaceVTable vtable { onMethodCall, nullptr, nullptr, {} };
            objectId = g_dbus_connection_register_object (bus, agentPath, node->interfaces[0], &vtable,
                                                          nullptr, nullptr, nullptr);
        }

        bool agentRegistered = false;
        if (objectId != 0 && callAgentManager (bus, "RegisterAgent", g_variant_new ("(os)", agentPath, "NoInputNoOutput")))
        {
            agentRegistered = true;
            callAgentManager (bus, "RequestDefaultAgent", g_variant_new ("(o)", agentPath));
        }
        registered.signal();

        while (agentRegistered && ! threadShouldExit())
        {
            while (g_main_context_iteration (context, FALSE)) {}
            wait (5);
        }

        if (agentRegistered)
            callAgentManager (bus, "UnregisterAgent", g_variant_new ("(o)", agentPath));
        if (objectId != 0)
            g_dbus_connection_unregister_object (bus, objectId);
        if (node != nullptr)
            g_dbus_node_info_unref (node);
        if (bus != nullptr)
        {
            g_dbus_connection_close_sync (bus, nullptr, nullptr);
            g_object_unref (bus);
        }

        g_main_context_pop_thread_default (context);
        g_main_context_unref (context);
    }
#else
    void run() override { registered.signal(); }
#endif

    juce::WaitableEvent registered;
};

} // namespace openguitarmultifx
