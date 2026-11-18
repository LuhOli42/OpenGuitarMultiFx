#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace openguitarmultifx
{

/**
    TONE3000 login (OAuth 2.0 device flow), tone search, and model download.

    Strictly a control-thread component: the audio thread never touches it,
    and per ARCHITECTURE.md the integration stays OPTIONAL -- the product
    works fully without it, loading .nam files from disk by hand.

    You need your own publishable client_id from tone3000.com -> Settings ->
    API Keys. There is no shared or demo client_id (verified against the
    official API docs and their example client repo), so this can't be
    hardcoded. Set it with setClientId(), or export TONE3000_CLIENT_ID.

    No client secret is used, and none is needed: the device flow below is
    meant for public clients that can't keep one. TONE3000's t3k_cs_...
    secret key is documented as server-only -- if you ever find yourself
    wanting to embed it here, that's a bug.

    Login is the device flow only (beginDeviceLogin/pollDeviceLogin): the
    UI shows a QR code + short code and the user signs in on a phone. The app
    never shows a web page itself -- there is no WebBrowserComponent and no
    local HTTP listener (both removed 2026-11-18, see Source/Tone3000/AGENTS.md).
*/
class Tone3000Manager
{
public:
    Tone3000Manager();
    ~Tone3000Manager();

    void setClientId (const juce::String& newClientId);
    bool hasClientId() const { return clientId.isNotEmpty(); }
    juce::String getClientId() const { return clientId; }

    bool isLoggedIn() const;
    void logOut();

    /** The only login (RFC 8628, TONE3000's "Device Authorization"): the app asks for a short code and shows it as a QR
        code of verificationUriComplete plus the code in text; the user signs in on a phone (or any browser) and the app
        polls until TONE3000 hands over the tokens. No browser in the app, no redirect, no PKCE: only the publishable key.
        Tokens come from the same /oauth/token endpoint, so refresh works as usual. */
    struct DeviceLogin
    {
        juce::String deviceCode;      // secret, never shown
        juce::String userCode;        // "BCDF-GHJK"
        juce::String verificationUri; // https://www.tone3000.com/activate
        juce::String verificationUriComplete; // the same page with the code filled in (what the QR code holds)
        int expiresInSec = 600;
        int intervalSec = 5;          // minimum seconds between polls
    };
    void beginDeviceLogin (std::function<void (bool ok, DeviceLogin login, juce::String error)> onResult);

    enum class DevicePoll { pending, slowDown, done, denied, expired, failed };
    /** One poll of the token endpoint (call every `intervalSec`, +5 s after slowDown). onResult on the message thread;
        `done` means the tokens are stored and isLoggedIn() is true. */
    void pollDeviceLogin (const DeviceLogin& login, std::function<void (DevicePoll result, juce::String error)> onResult);

    /** gear/format values are exactly the strings TONE3000's API uses (verified
        against their docs, not guessed): gear one of "amp", "amp-cab", "pedal",
        "outboard", "cab", "space", "experimental"; format one of "nam", "ir",
        "aida-x", "aa-snapshot", "proteus". Only "nam" and "ir" are formats this
        app's engine can actually load (NeuralAmpModelerCore / juce::dsp::Convolution
        respectively) -- aida-x/aa-snapshot/proteus are other tools' formats. */
    struct Tone
    {
        int id = 0;
        juce::String title;
        juce::String author;
        juce::String license;
        juce::String gear;
        juce::String format;
    };

    /** Search public tones by name, e.g. "JCM800" or "Tube Screamer".
        gearFilter is one of the Gear strings above, or empty for no filter.
        architectureFilter is "1" (A1), "2" (A2), "custom", or empty.
        IMPORTANT, per TONE3000's own docs: omitting architectureFilter does
        NOT mean "all architectures" -- it means "A1 + Custom, EXCLUDING A2".
        There is no confirmed single value that means "everything including
        A2"; getting A2 results at all requires passing "2" explicitly,
        which then excludes A1/Custom in turn (the API takes one value, not
        a combinable list the way gears does).
        onComplete fires on the message thread. */
    void searchTones (const juce::String& query, const juce::String& gearFilter, const juce::String& architectureFilter,
                       std::function<void (bool success, std::vector<Tone> results, juce::String error)> onComplete);

    /** Downloads a model file given the `model_url` returned by the API.
        onComplete fires on the message thread. */
    void downloadModel (const juce::String& modelUrl,
                         const juce::File& destination,
                         std::function<void (bool success, juce::String error)> onComplete);

    /** Lists a tone's models and downloads the first one to `destination`.
        onComplete fires on the message thread. */
    void downloadFirstModelForTone (int toneId,
                                     const juce::File& destination,
                                     std::function<void (bool success, juce::String error)> onComplete);

private:
    struct HttpResult
    {
        bool ok = false;
        int statusCode = 0;
        juce::String body;
    };

    HttpResult httpGet (const juce::String& url, bool withAuth) const;
    HttpResult httpPostForm (const juce::String& url, const juce::String& formBody) const;

    /** Streams an authenticated GET straight to disk -- model files are opaque bytes,
        not text, so they must never round-trip through a juce::String. */
    bool httpDownloadToFile (const juce::String& url, const juce::File& destination, juce::String& error);

    /** Blocking (called from a background thread, never the message thread).
        Exchanges refreshToken for a new access_token and persists it. Access
        tokens are short-lived (observed ~1 hour) and nothing was refreshing
        them before -- every call past that point failed with HTTP 401 and
        surfaced as an opaque "Search failed" error. */
    bool refreshAccessTokenBlocking();

    /** httpGet, but retries once after a blocking token refresh if the first
        attempt came back 401. Not const, since a successful refresh mutates
        accessToken/refreshToken. */
    HttpResult httpGetWithRefresh (const juce::String& url, bool withAuth);

    juce::File getAuthFile() const;
    void loadPersistedAuth();
    void savePersistedAuth() const;

    /** Runs work on a detached background thread, then delivers the result on the message thread.
        The alive flag means a callback that outlives this object simply does nothing. */
    template <typename Work>
    void runInBackground (Work&& work) const
    {
        std::thread ([work = std::forward<Work> (work), alive = aliveFlag]() mutable
        {
            work (alive);
        }).detach();
    }

    juce::String clientId;
    juce::String accessToken, refreshToken;


    std::shared_ptr<std::atomic<bool>> aliveFlag = std::make_shared<std::atomic<bool>> (true);
};

} // namespace openguitarmultifx
