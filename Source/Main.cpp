#include "UI/MainComponent.h"
#include "UI/OpenGuitarMultiFxLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace openguitarmultifx
{

class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow (const juce::String& name)
        : DocumentWindow (name,
                           juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                           DocumentWindow::allButtons)
    {
        // JUCE-drawn decorations, not the native/Wayland ones -- a window
        // exported from inside a distrobox container talking to a native
        // title bar is a plausible source of an unexpected close signal
        // (unconfirmed, but it's the natural thing to rule out first).
        setUsingNativeTitleBar (false);
        setContentOwned (new MainComponent(), true);
        setResizable (true, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::Logger::writeToLog ("MainWindow::closeButtonPressed() -- close (X) button was clicked");
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class OpenGuitarMultiFxApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "OpenGuitarMultiFx"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }

    void initialise (const juce::String&) override
    {
#if JUCE_LINUX
        // WebKitGTK (the embedded browser TONE3000 login uses, see
        // OAuthLoginDialog.h) is known to render blank under Wayland
        // compositors unless its own compositing mode is disabled --
        // must be set before the WebKit web-process subprocess is ever
        // spawned (it reads this from its inherited environment), so as
        // early as possible here, well before any WebBrowserComponent
        // gets constructed later. The `0` (don't overwrite) respects an
        // explicit value the user/environment may already have set.
        setenv ("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 0);
#endif

        // A real log file, independent of however stdout/stderr get
        // captured through distrobox/podman -- so "why did it close" is
        // answerable after the fact instead of guessed at.
        fileLogger.reset (juce::FileLogger::createDefaultAppLogger (
            "OpenGuitarMultiFx", "openguitarmultifx.log", "OpenGuitarMultiFx session started"));
        juce::Logger::setCurrentLogger (fileLogger.get());

        // Set before MainWindow is constructed -- its own constructor reads
        // the default LookAndFeel's background colour immediately.
        lookAndFeel = std::make_unique<OpenGuitarMultiFxLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        juce::Logger::writeToLog ("initialise() -- creating MainWindow");
        mainWindow = std::make_unique<MainWindow> (getApplicationName() + " -- cheapCortex");
        juce::Logger::writeToLog ("initialise() -- MainWindow created and visible");
    }

    void systemRequestedQuit() override
    {
        juce::Logger::writeToLog ("systemRequestedQuit() -- calling quit()");
        quit();
    }

    void shutdown() override
    {
        juce::Logger::writeToLog ("shutdown() -- destroying MainWindow");
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr); // before lookAndFeel is destroyed below -- no dangling default
        lookAndFeel = nullptr;
        juce::Logger::setCurrentLogger (nullptr);
        fileLogger = nullptr;
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<OpenGuitarMultiFxLookAndFeel> lookAndFeel;
    std::unique_ptr<juce::FileLogger> fileLogger;
};

} // namespace openguitarmultifx

START_JUCE_APPLICATION (openguitarmultifx::OpenGuitarMultiFxApplication)
