#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <thread>

namespace openguitarmultifx
{

/**
    A one-shot local HTTP listener, just enough to catch an OAuth redirect
    coming back from the SYSTEM browser (desktop login path -- see
    Tone3000Panel::doLogin; the embedded WebBrowserComponent in
    OAuthLoginDialog remains the fallback for devices with no browser).

    TONE3000's docs say localhost origins are auto-allowed without
    pre-registering a redirect URI, which is what makes this the simplest
    viable flow for a desktop app.

    Serves connections until one carries a `code` or `error` query parameter
    on /callback, answers that one with a small "you can close this tab"
    page, and stops. Anything else a browser sends meanwhile (favicon,
    preconnect probes with no data) gets a 404 / is dropped without using up
    the one shot. The callback fires on this object's own background thread
    -- the caller is responsible for marshalling to the message thread.
*/
class LoopbackServer
{
public:
    ~LoopbackServer();

    bool start (int port, std::function<void (const juce::StringPairArray&)> onRequest);
    void stop();

private:
    void run();

    juce::StreamingSocket listener;
    std::thread worker;
    std::atomic<bool> shouldStop { false };
    std::function<void (const juce::StringPairArray&)> callback;
};

} // namespace openguitarmultifx
