#include "LoopbackServer.h"

#include <cstring>

namespace openguitarmultifx
{

LoopbackServer::~LoopbackServer()
{
    stop();
}

bool LoopbackServer::start (int port, std::function<void (const juce::StringPairArray&)> onRequest)
{
    stop(); // restartable: a second "Log in" click replaces a still-pending one

    callback = std::move (onRequest);

    if (! listener.createListener (port, "127.0.0.1"))
        return false;

    shouldStop = false;
    worker = std::thread ([this] { run(); });
    return true;
}

void LoopbackServer::stop()
{
    shouldStop = true;
    listener.close();

    if (worker.joinable())
        worker.join();
}

void LoopbackServer::run()
{
    while (! shouldStop.load())
    {
        std::unique_ptr<juce::StreamingSocket> connection (listener.waitForNextConnection());

        if (connection == nullptr || shouldStop.load())
            return;

        // read(..., false) returns immediately with whatever has ALREADY
        // arrived -- 0 if the request line is still in flight right after
        // accept(), which dropped real callbacks. Wait for readability
        // first; a preconnect probe that never sends anything times out
        // here instead of blocking the one real request behind it.
        if (connection->waitUntilReady (true, 2000) <= 0)
            continue;

        char chunk[4096] = {};
        const int bytesRead = connection->read (chunk, (int) sizeof (chunk) - 1, false);

        if (bytesRead <= 0)
            continue;

        const auto request = juce::String::fromUTF8 (chunk, bytesRead);

        // Request line looks like: GET /callback?code=...&state=... HTTP/1.1
        const auto firstLine = request.upToFirstOccurrenceOf ("\r\n", false, false);
        const auto path = firstLine.fromFirstOccurrenceOf (" ", false, false)
                                    .upToFirstOccurrenceOf (" ", false, false);
        const auto query = path.fromFirstOccurrenceOf ("?", false, false);

        juce::StringPairArray params;

        for (const auto& pair : juce::StringArray::fromTokens (query, "&", ""))
        {
            if (pair.isEmpty())
                continue;

            const auto key = pair.upToFirstOccurrenceOf ("=", false, false);
            const auto value = pair.fromFirstOccurrenceOf ("=", false, false);
            params.set (juce::URL::removeEscapeChars (key), juce::URL::removeEscapeChars (value));
        }

        const bool isCallback = path.startsWith ("/callback")
                                 && (params.containsKey ("code") || params.containsKey ("error"));

        if (! isCallback)
        {
            static const char* const notFound = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            connection->write (notFound, (int) std::strlen (notFound));
            connection->close();
            continue;
        }

        static const char* const response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Connection: close\r\n\r\n"
            "<html><body style=\"font-family:sans-serif;background:#141414;color:#eee;padding:40px\">"
            "<h2>OpenGuitarMultiFx</h2><p>Login complete &mdash; you can close this tab.</p></body></html>";

        connection->write (response, (int) std::strlen (response));
        connection->close();

        if (callback)
            callback (params);

        return;
    }
}

} // namespace openguitarmultifx
