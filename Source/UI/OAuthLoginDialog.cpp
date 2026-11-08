#include "OAuthLoginDialog.h"
#include "Theme.h"

#include "TouchSizing.h"

#include <juce_events/juce_events.h>

namespace openguitarmultifx
{

OAuthLoginDialog::OAuthLoginDialog (const juce::String& authorizeUrl, const juce::String& redirectUriPrefix)
    : redirectPrefix (redirectUriPrefix)
{
    addAndMakeVisible (titleLabel);
    titleLabel.setFont (juce::Font (19.0f, juce::Font::bold));

    addAndMakeVisible (cancelButton);
    cancelButton.onClick = [this] { if (onCancelled) onCancelled(); };

    browser.onPageAboutToLoad = [this] (const juce::String& url) -> bool
    {
        if (! url.startsWith (redirectPrefix))
            return true;

        juce::StringPairArray params;
        const auto query = url.fromFirstOccurrenceOf ("?", false, false);

        for (const auto& pair : juce::StringArray::fromTokens (query, "&", ""))
        {
            if (pair.isEmpty())
                continue;

            params.set (juce::URL::removeEscapeChars (pair.upToFirstOccurrenceOf ("=", false, false)),
                        juce::URL::removeEscapeChars (pair.fromFirstOccurrenceOf ("=", false, false)));
        }

        // Deferred: we're inside the webview's own navigation-decision
        // callback here, not a normal message-loop turn. Let that unwind
        // first before the caller potentially deletes this overlay.
        if (onRedirectReached)
        {
            auto callback = onRedirectReached;
            juce::MessageManager::callAsync ([callback, params] { callback (params); });
        }

        return false; // cancel navigation -- we never actually load the redirect URI
    };

    addAndMakeVisible (browser);
    setSize (480, 640);

    browser.goToURL (authorizeUrl);
}

void OAuthLoginDialog::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto top = area.removeFromTop (touch::minTapTarget + 8);
    cancelButton.setBounds (top.removeFromRight (110).withSizeKeepingCentre (110, touch::minTapTarget));
    titleLabel.setBounds (top);

    area.removeFromTop (6);
    browser.setBounds (area);
}

void OAuthLoginDialog::paint (juce::Graphics& g)
{
    g.fillAll (theme::current().surface);
}

} // namespace openguitarmultifx
