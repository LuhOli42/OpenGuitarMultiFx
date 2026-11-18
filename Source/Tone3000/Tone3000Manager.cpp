#include "Tone3000Manager.h"

#include <juce_events/juce_events.h> // MessageManager::callAsync

namespace openguitarmultifx
{

namespace
{
    const juce::String apiBase = "https://www.tone3000.com/api/v1";
    constexpr int httpTimeoutMs = 15000;
}

Tone3000Manager::Tone3000Manager()
{
    clientId = juce::SystemStats::getEnvironmentVariable ("TONE3000_CLIENT_ID", {}).trim();
    loadPersistedAuth();
}

Tone3000Manager::~Tone3000Manager()
{
    aliveFlag->store (false);
}

void Tone3000Manager::setClientId (const juce::String& newClientId)
{
    clientId = newClientId.trim();
    savePersistedAuth();
}

bool Tone3000Manager::isLoggedIn() const
{
    return accessToken.isNotEmpty();
}

void Tone3000Manager::logOut()
{
    accessToken.clear();
    refreshToken.clear();
    savePersistedAuth();
}

juce::File Tone3000Manager::getAuthFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("OpenGuitarMultiFx")
               .getChildFile ("tone3000_auth.json");
}

void Tone3000Manager::loadPersistedAuth()
{
    const auto file = getAuthFile();

    if (! file.existsAsFile())
        return;

    const auto parsed = juce::JSON::parse (file.loadFileAsString());

    if (auto* obj = parsed.getDynamicObject())
    {
        if (clientId.isEmpty())
            clientId = obj->getProperty ("client_id").toString();

        accessToken = obj->getProperty ("access_token").toString();
        refreshToken = obj->getProperty ("refresh_token").toString();
    }
}

void Tone3000Manager::savePersistedAuth() const
{
    // Tokens land in a plain file under the user's app-data dir. Good enough
    // for a dev build on a single-user machine; a shipping product should
    // move these into the OS keychain / secret service instead.
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("client_id", clientId);
    obj->setProperty ("access_token", accessToken);
    obj->setProperty ("refresh_token", refreshToken);

    const auto file = getAuthFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::JSON::toString (juce::var (obj)));
}

Tone3000Manager::HttpResult Tone3000Manager::httpGet (const juce::String& url, bool withAuth) const
{
    HttpResult result;

    // InputStreamOptions has a const member, so it can't be reassigned --
    // every option has to be chained in one expression.
    const juce::String headers = (withAuth && accessToken.isNotEmpty())
                                      ? "Authorization: Bearer " + accessToken
                                      : juce::String();

    const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                             .withConnectionTimeoutMs (httpTimeoutMs)
                             .withStatusCode (&result.statusCode)
                             .withExtraHeaders (headers);

    if (auto stream = juce::URL (url).createInputStream (options))
    {
        result.body = stream->readEntireStreamAsString();
        result.ok = result.statusCode >= 200 && result.statusCode < 300;
    }

    return result;
}

Tone3000Manager::HttpResult Tone3000Manager::httpPostForm (const juce::String& url,
                                                            const juce::String& formBody) const
{
    HttpResult result;

    const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                             .withConnectionTimeoutMs (httpTimeoutMs)
                             .withStatusCode (&result.statusCode)
                             .withHttpRequestCmd ("POST")
                             .withExtraHeaders ("Content-Type: application/x-www-form-urlencoded");

    if (auto stream = juce::URL (url).withPOSTData (formBody).createInputStream (options))
    {
        result.body = stream->readEntireStreamAsString();
        result.ok = result.statusCode >= 200 && result.statusCode < 300;
    }

    return result;
}

bool Tone3000Manager::refreshAccessTokenBlocking()
{
    if (refreshToken.isEmpty())
        return false;

    const auto form = "grant_type=refresh_token"
                       "&refresh_token=" + juce::URL::addEscapeChars (refreshToken, true)
                       + "&client_id=" + juce::URL::addEscapeChars (clientId, true);

    const auto result = httpPostForm (apiBase + "/oauth/token", form);
    if (! result.ok)
        return false;

    const auto parsed = juce::JSON::parse (result.body);
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
        return false;

    const auto newAccess = obj->getProperty ("access_token").toString();
    if (newAccess.isEmpty())
        return false;

    accessToken = newAccess;

    const auto newRefresh = obj->getProperty ("refresh_token").toString();
    if (newRefresh.isNotEmpty())
        refreshToken = newRefresh;

    savePersistedAuth();
    return true;
}

Tone3000Manager::HttpResult Tone3000Manager::httpGetWithRefresh (const juce::String& url, bool withAuth)
{
    auto result = httpGet (url, withAuth);

    if (result.statusCode == 401 && withAuth && refreshAccessTokenBlocking())
        result = httpGet (url, withAuth); // one retry with the freshly refreshed token

    return result;
}

bool Tone3000Manager::httpDownloadToFile (const juce::String& url,
                                           const juce::File& destination,
                                           juce::String& error)
{
    int statusCode = 0;
    std::unique_ptr<juce::InputStream> stream;

    // Same 401 -> refresh -> retry-once dance as httpGetWithRefresh. Model
    // downloads are just as likely to land after the access token expired
    // as a search is.
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                 .withConnectionTimeoutMs (httpTimeoutMs)
                                 .withStatusCode (&statusCode)
                                 .withExtraHeaders (accessToken.isNotEmpty()
                                                         ? "Authorization: Bearer " + accessToken
                                                         : juce::String());

        stream = juce::URL (url).createInputStream (options);

        if (stream != nullptr && statusCode == 401 && attempt == 0 && refreshAccessTokenBlocking())
        {
            stream.reset();
            continue;
        }

        break;
    }

    if (stream == nullptr)
    {
        error = "Could not open a connection to " + url;
        return false;
    }

    if (statusCode < 200 || statusCode >= 300)
    {
        error = "Download failed (HTTP " + juce::String (statusCode) + ").";
        return false;
    }

    destination.getParentDirectory().createDirectory();
    destination.deleteFile();

    juce::FileOutputStream out (destination);

    if (out.failedToOpen())
    {
        error = "Could not write " + destination.getFullPathName();
        return false;
    }

    out.writeFromInputStream (*stream, -1);
    out.flush();
    return true;
}

void Tone3000Manager::beginDeviceLogin (std::function<void (bool, DeviceLogin, juce::String)> onResult)
{
    if (clientId.isEmpty())
    {
        onResult (false, {}, "Set your client_id first.");
        return;
    }

    const auto form = "client_id=" + juce::URL::addEscapeChars (clientId, true);

    runInBackground ([this, form, onResult] (std::shared_ptr<std::atomic<bool>> alive)
    {
        const auto result = httpPostForm (apiBase + "/oauth/device_authorization", form);

        if (! alive->load())
            return;

        DeviceLogin login;
        juce::String error;
        const auto parsed = juce::JSON::parse (result.body);

        if (! result.ok)
        {
            const auto code = parsed.getProperty ("error", {}).toString();
            error = "TONE3000 refused the login request (HTTP " + juce::String (result.statusCode)
                    + (code.isNotEmpty() ? ", " + code : juce::String()) + ").";
        }
        else if (auto* obj = parsed.getDynamicObject())
        {
            login.deviceCode = obj->getProperty ("device_code").toString();
            login.userCode = obj->getProperty ("user_code").toString();
            login.verificationUri = obj->getProperty ("verification_uri").toString();
            login.verificationUriComplete = obj->getProperty ("verification_uri_complete").toString();
            if (login.verificationUri.isEmpty())
                login.verificationUri = "https://www.tone3000.com/activate";
            if (login.verificationUriComplete.isEmpty()) // the QR then opens the page and the code is typed by hand
                login.verificationUriComplete = login.verificationUri;
            if (obj->hasProperty ("expires_in"))
                login.expiresInSec = juce::jmax (30, (int) obj->getProperty ("expires_in"));
            if (obj->hasProperty ("interval"))
                login.intervalSec = juce::jlimit (1, 60, (int) obj->getProperty ("interval"));
            if (login.deviceCode.isEmpty() || login.userCode.isEmpty())
                error = "TONE3000 sent no login code.";
        }
        else
            error = "TONE3000 sent an unreadable answer.";

        juce::MessageManager::callAsync ([alive, onResult, login, error]
        {
            if (alive->load())
                onResult (error.isEmpty(), login, error);
        });
    });
}

void Tone3000Manager::pollDeviceLogin (const DeviceLogin& login, std::function<void (DevicePoll, juce::String)> onResult)
{
    const auto form = "grant_type=" + juce::URL::addEscapeChars ("urn:ietf:params:oauth:grant-type:device_code", true)
                      + "&device_code=" + juce::URL::addEscapeChars (login.deviceCode, true)
                      + "&client_id=" + juce::URL::addEscapeChars (clientId, true);

    runInBackground ([this, form, onResult] (std::shared_ptr<std::atomic<bool>> alive)
    {
        const auto result = httpPostForm (apiBase + "/oauth/token", form);

        if (! alive->load())
            return;

        DevicePoll outcome = DevicePoll::failed;
        juce::String error, newAccess, newRefresh;
        const auto parsed = juce::JSON::parse (result.body);

        if (result.ok)
        {
            newAccess = parsed.getProperty ("access_token", {}).toString();
            newRefresh = parsed.getProperty ("refresh_token", {}).toString();
            if (newAccess.isNotEmpty())
                outcome = DevicePoll::done;
            else
                error = "Token response had no access_token.";
        }
        else
        {
            const auto code = parsed.getProperty ("error", {}).toString();
            if (code == "authorization_pending")      outcome = DevicePoll::pending;
            else if (code == "slow_down")             outcome = DevicePoll::slowDown;
            else if (code == "access_denied")         outcome = DevicePoll::denied;
            else if (code == "expired_token")         outcome = DevicePoll::expired;
            else if (result.statusCode == 0)          { outcome = DevicePoll::pending; } // no connection right now: keep trying until the code expires
            else error = "Login failed (HTTP " + juce::String (result.statusCode) + (code.isNotEmpty() ? ", " + code : juce::String()) + ").";
        }

        juce::MessageManager::callAsync ([this, alive, onResult, outcome, error, newAccess, newRefresh]
        {
            if (! alive->load())
                return;
            if (outcome == DevicePoll::done)
            {
                accessToken = newAccess;
                refreshToken = newRefresh;
                savePersistedAuth();
            }
            onResult (outcome, error);
        });
    });
}

void Tone3000Manager::searchTones (const juce::String& query, const juce::String& gearFilter,
                                    const juce::String& architectureFilter,
                                    std::function<void (bool, std::vector<Tone>, juce::String)> onComplete)
{
    if (! isLoggedIn())
    {
        onComplete (false, {}, "Not logged in to TONE3000.");
        return;
    }

    auto url = apiBase + "/tones/search?query=" + juce::URL::addEscapeChars (query, true);
    if (gearFilter.isNotEmpty())
        url += "&gears=" + juce::URL::addEscapeChars (gearFilter, true);
    if (architectureFilter.isNotEmpty())
        url += "&architecture=" + juce::URL::addEscapeChars (architectureFilter, true);

    runInBackground ([this, url, onComplete] (std::shared_ptr<std::atomic<bool>> alive)
    {
        const auto result = httpGetWithRefresh (url, true);

        if (! alive->load())
            return;

        std::vector<Tone> tones;
        juce::String error;

        if (! result.ok)
        {
            error = "Search failed (HTTP " + juce::String (result.statusCode) + ").";
        }
        else
        {
            const auto parsed = juce::JSON::parse (result.body);

            // Paginated envelope: { data: [...], page, page_size, total, total_pages }
            const auto data = parsed.getProperty ("data", juce::var());

            if (auto* array = data.getArray())
            {
                for (const auto& item : *array)
                {
                    Tone tone;
                    tone.id = (int) item.getProperty ("id", 0);
                    tone.title = item.getProperty ("title", {}).toString();
                    tone.license = item.getProperty ("license", {}).toString();
                    tone.gear = item.getProperty ("gear", {}).toString();
                    tone.format = item.getProperty ("format", {}).toString();

                    const auto user = item.getProperty ("user", juce::var());
                    tone.author = user.getProperty ("username", {}).toString();

                    tones.push_back (tone);
                }
            }
            else
            {
                error = "Unexpected search response shape.";
            }
        }

        juce::MessageManager::callAsync ([alive, onComplete, tones, error]
        {
            if (alive->load())
                onComplete (error.isEmpty(), tones, error);
        });
    });
}

void Tone3000Manager::downloadModel (const juce::String& modelUrl,
                                      const juce::File& destination,
                                      std::function<void (bool, juce::String)> onComplete)
{
    if (! isLoggedIn())
    {
        onComplete (false, "Not logged in to TONE3000.");
        return;
    }

    runInBackground ([this, modelUrl, destination, onComplete] (std::shared_ptr<std::atomic<bool>> alive)
    {
        // model_url is a pre-built download URL that still needs the bearer token.
        juce::String error;
        httpDownloadToFile (modelUrl, destination, error);

        if (! alive->load())
            return;

        juce::MessageManager::callAsync ([alive, onComplete, error]
        {
            if (alive->load())
                onComplete (error.isEmpty(), error);
        });
    });
}

void Tone3000Manager::downloadFirstModelForTone (int toneId,
                                                  const juce::File& destination,
                                                  std::function<void (bool, juce::String)> onComplete)
{
    if (! isLoggedIn())
    {
        onComplete (false, "Not logged in to TONE3000.");
        return;
    }

    // NOTE: the tone_id filter on /models is the one piece of this file not
    // verified against the live API (the docs confirm /models lists models
    // and that each carries model_url + tone_id, but not the exact filter
    // param). If listing comes back empty for a tone that clearly has
    // models, this query string is the first thing to check.
    const auto url = apiBase + "/models?tone_id=" + juce::String (toneId);

    runInBackground ([this, url, destination, onComplete] (std::shared_ptr<std::atomic<bool>> alive)
    {
        const auto listResult = httpGetWithRefresh (url, true);

        if (! alive->load())
            return;

        juce::String modelUrl, error;

        if (! listResult.ok)
        {
            error = "Model list failed (HTTP " + juce::String (listResult.statusCode) + ").";
        }
        else
        {
            const auto parsed = juce::JSON::parse (listResult.body);
            const auto data = parsed.getProperty ("data", juce::var());

            if (auto* array = data.getArray(); array != nullptr && ! array->isEmpty())
            {
                // Prefer an A2 model when the tone ships several: it is the cheaper architecture on the CPU and
                // NAM core v0.6.0 loads it. The field is read defensively ("2", 2 or "a2"); anything else falls
                // back to the first model, as before.
                modelUrl = array->getReference (0).getProperty ("model_url", {}).toString();
                for (const auto& model : *array)
                {
                    const auto arch = model.getProperty ("architecture", {}).toString().trim().toLowerCase();
                    if ((arch == "2" || arch == "a2") && model.getProperty ("model_url", {}).toString().isNotEmpty())
                    {
                        modelUrl = model.getProperty ("model_url", {}).toString();
                        break;
                    }
                }
            }

            if (modelUrl.isEmpty())
                error = "No downloadable model found for this tone.";
        }

        if (error.isNotEmpty())
        {
            juce::MessageManager::callAsync ([alive, onComplete, error]
            {
                if (alive->load())
                    onComplete (false, error);
            });
            return;
        }

        juce::MessageManager::callAsync ([this, alive, modelUrl, destination, onComplete]
        {
            if (alive->load())
                downloadModel (modelUrl, destination, onComplete);
        });
    });
}

} // namespace openguitarmultifx
