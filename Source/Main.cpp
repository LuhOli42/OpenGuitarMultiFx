#include "EffectRegistry.h"
#include "UI/MainComponent.h"
#include "UI/OpenGuitarMultiFxLookAndFeel.h"
#include "UI/ParameterPanel.h"

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
    // Baked in by CMake from `git describe --tags --match 'v*' --dirty` --
    // the same string scripts/package.sh writes into the release VERSION
    // file, so the Settings "Software update" section and the updater's
    // version comparison see what the release actually was.
    const juce::String getApplicationVersion() override { return OGMFX_VERSION; }

    /** `--panel-snapshot <dir> <key,key,...>`: render each effect's parameter drawer, every knob page, off-screen to
        <dir>/<key>-page<N>.png and quit -- no window is opened. For checking the drawer's layout without a screen grab. */
    static void writePanelSnapshots (const juce::String& dir, const juce::StringArray& keys)
    {
        EffectRegistry registry;
        registerBuiltInEffects (registry);
        for (auto& key : keys)
        {
            auto fx = registry.create (key);
            if (fx == nullptr)
                continue;
            ParameterPanel panel;
            panel.setProcessor (fx.get());
            const int pages = (int) fx->getParameterPages().size();
            for (int page = 0; page < pages; ++page)
            {
                panel.setBounds (0, 0, 1280, juce::jmax (240, panel.getPreferredContentHeight (1280)));
                const auto image = panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f);
                juce::File out (dir + "/" + key + "-page" + juce::String (page + 1) + ".png");
                out.deleteFile();
                juce::FileOutputStream stream (out);
                juce::PNGImageFormat().writeImageToStream (image, stream);
                std::function<void (juce::Component&)> clickNext = [&] (juce::Component& root)
                {
                    for (auto* child : root.getChildren())
                    {
                        if (auto* b = dynamic_cast<juce::Button*> (child))
                            if (b->isVisible() && b->getButtonText().startsWith ("Page " + juce::String (page + 2)) && b->onClick)
                            {
                                b->onClick();
                                return;
                            }
                        clickNext (*child);
                    }
                };
                clickNext (panel);
            }
        }
    }

    void initialise (const juce::String& commandLine) override
    {
        const auto args = juce::StringArray::fromTokens (commandLine, true);
        if (const int at = args.indexOf ("--panel-snapshot"); at >= 0 && at + 2 < args.size())
        {
            lookAndFeel = std::make_unique<OpenGuitarMultiFxLookAndFeel>();
            juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());
            writePanelSnapshots (args[at + 1].unquoted(), juce::StringArray::fromTokens (args[at + 2].unquoted(), ",", ""));
            quit();
            return;
        }

#if JUCE_LINUX
        // JUCE's WebBrowserComponent runs WebKitGTK in a child process
        // (this same executable, re-exec'd) that calls
        // gdk_set_allowed_backends("x11") and embeds via XEmbed into
        // JUCE's X11 window. If the session also exports
        // GDK_BACKEND=wayland (KDE Plasma Wayland does here), the two
        // contradict, gtk_init in the child dies with "cannot open
        // display", and the login pane stays blank -- reproduced with a
        // bare WebBrowserComponent app, fixed by GDK_BACKEND=x11 alone.
        // Overwrite (1), not don't-overwrite: the inherited value is
        // exactly the problem. Only this process's GTK use is affected,
        // and JUCE itself talks plain Xlib (XWayland), never GDK. See
        // Source/Tone3000/AGENTS.md's decision log.
        setenv ("GDK_BACKEND", "x11", 1);
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
