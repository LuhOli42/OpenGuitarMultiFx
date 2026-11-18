#include "EffectRegistry.h"
#include "UI/MainComponent.h"
#include "UI/OpenGuitarMultiFxLookAndFeel.h"
#include "UI/Theme.h"
#include "Engine/CpuAffinity.h"
#include "UI/OnScreenKeyboard.h"
#include "UI/GpuRendering.h"
#include "UI/ParameterPanel.h"
#include "UI/TouchSizing.h"

#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>

namespace openguitarmultifx
{

/** Diagnostic (OGMFX_GL_PROBE=1): a black window with ONE small square moving -- same window, same GPU backend, nothing
    else. If the board's GPU is still busy with this, the per-frame cost is in the backend, not in the UI tree. */
class GlProbe : public juce::Component, private juce::Timer
{
public:
    GlProbe() { setOpaque (true); startTimerHz (60); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);
        g.setColour (juce::Colours::limegreen);
        g.fillRect (box);
    }
private:
    void timerCallback() override
    {
        const auto old = box;
        x = (x + 6) % juce::jmax (1, getWidth() - 64);
        box = juce::Rectangle<int> (x, getHeight() / 2, 64, 64);
        repaint (old.getUnion (box));
    }
    int x = 0;
    juce::Rectangle<int> box { 0, 0, 64, 64 };
};

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
        if (juce::SystemStats::getEnvironmentVariable ("OGMFX_GL_PROBE", {}).isNotEmpty())
            setContentOwned (new GlProbe(), true);
        else
            setContentOwned (new MainComponent(), true);
        setResizable (true, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        if (touch::kioskMode())
            goKiosk();
    }

    /** Device build: the app is the only X client and the panel is a 1920x1200 8" touchscreen, so the 1280x800 layout
        is scaled up to fill it (UI scale = panel width / 1280, override with OGMFX_UI_SCALE) and shown full-screen
        with no title bar. */
    void goKiosk()
    {
        auto& desktop = juce::Desktop::getInstance();
        float scale = juce::SystemStats::getEnvironmentVariable ("OGMFX_UI_SCALE", {}).getFloatValue();
        if (scale <= 0.0f)
            if (auto* display = desktop.getDisplays().getPrimaryDisplay())
                scale = (float) display->totalArea.getWidth() / 1280.0f; // logical px at scale 1.0 -> 1280 wide after scaling
        if (scale > 0.5f && scale < 4.0f)
            desktop.setGlobalScaleFactor (scale);

        setTitleBarHeight (0);
        setTitleBarButtonsRequired (0, false); // no minimise/maximise/close buttons on the panel
        setResizable (false, false);
        desktop.setKioskModeComponent (this, false);
        if (auto* content = getContentComponent())
            gpu.attachTo (*content);
    }

    GpuRendering gpu;

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
            panel.setEffectRegistry (registry); // category accent, subtitle and page names, as in the app
            panel.setProcessor (fx.get());
            for (int page = 0; page < panel.getPageCount(); ++page)
            {
                panel.selectPage (page);
                panel.setBounds (0, 0, 1280, juce::jmax (240, panel.getPreferredContentHeight (1280)));
                const auto image = panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f);
                juce::File out (dir + "/" + key + "-page" + juce::String (page + 1) + ".png");
                out.deleteFile();
                juce::FileOutputStream stream (out);
                juce::PNGImageFormat().writeImageToStream (image, stream);
            }
        }
    }

    void initialise (const juce::String& commandLine) override
    {
        cpuaffinity::init(); // device build: UI/message thread on the slow cores, audio on the fast ones

        const auto args = juce::StringArray::fromTokens (commandLine, true);
        if (const int at = args.indexOf ("--panel-snapshot"); at >= 0 && at + 2 < args.size())
        {
            theme::load();
            lookAndFeel = std::make_unique<OpenGuitarMultiFxLookAndFeel>();
            juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());
            writePanelSnapshots (args[at + 1].unquoted(), juce::StringArray::fromTokens (args[at + 2].unquoted(), ",", ""));
            quit();
            return;
        }

        // A real log file, independent of however stdout/stderr get
        // captured through distrobox/podman -- so "why did it close" is
        // answerable after the fact instead of guessed at.
        fileLogger.reset (juce::FileLogger::createDefaultAppLogger (
            "OpenGuitarMultiFx", "openguitarmultifx.log", "OpenGuitarMultiFx session started"));
        juce::Logger::setCurrentLogger (fileLogger.get());

        // Set before MainWindow is constructed -- its own constructor reads
        // the default LookAndFeel's background colour immediately.
        theme::load();
        lookAndFeel = std::make_unique<OpenGuitarMultiFxLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        juce::Logger::writeToLog ("initialise() -- creating MainWindow");
        // `--window-snapshot <file.png> [screen]`: build the main screen OFF-screen (no window), give its
        // async start-up ~3 s, render it at the target's 1280x800 to <file> and quit -- for comparing
        // against docs/ui/prototype.html without grabbing the desktop. The optional screen name opens
        // that overlay before the shot: presets | tuner | eq | settings.
        if (const int at = args.indexOf ("--window-snapshot"); at >= 0 && at + 1 < args.size())
        {
            const juce::File out (args[at + 1].unquoted());
            const juce::String screen = at + 2 < args.size() ? args[at + 2].unquoted() : juce::String();
            snapshotComponent = std::make_unique<MainComponent>();
            snapshotComponent->setSize (1280, 800);
            juce::Timer::callAfterDelay (2500, [this, screen]
            {
                if (auto* mc = dynamic_cast<MainComponent*> (snapshotComponent.get()))
                    mc->openOverlayForSnapshot (screen);
            });
            juce::Timer::callAfterDelay (3000, [this, out]
            {
                const auto image = snapshotComponent->createComponentSnapshot (snapshotComponent->getLocalBounds(), true, 1.0f);
                out.deleteFile();
                juce::FileOutputStream stream (out);
                juce::PNGImageFormat().writeImageToStream (image, stream);
                stream.flush();
                snapshotComponent = nullptr;
                quit();
            });
            return;
        }

        if (touch::kioskMode())
            keyboardManager = std::make_unique<OnScreenKeyboardManager>();
        mainWindow = std::make_unique<MainWindow> (getApplicationName() + " -- cheapCortex");
        juce::Logger::writeToLog ("initialise() -- MainWindow created and visible");

       #if JUCE_LINUX
        // Code is paged in from the SD card the first time a path runs (the first use of each effect). Read the whole
        // executable once, in the background and at idle priority, so every page sits in the page cache from then on.
        juce::Thread::launch (juce::Thread::Priority::background, []
        {
            juce::FileInputStream in (juce::File::getSpecialLocation (juce::File::currentExecutableFile));
            if (! in.openedOk())
                return;
            std::vector<char> chunk (1 << 20);
            while (in.read (chunk.data(), (int) chunk.size()) > 0)
                juce::Thread::sleep (2);
        });
       #endif
    }

    void systemRequestedQuit() override
    {
        juce::Logger::writeToLog ("systemRequestedQuit() -- calling quit()");
        quit();
    }

    void shutdown() override
    {
        juce::Logger::writeToLog ("shutdown() -- destroying MainWindow");
        keyboardManager = nullptr;
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr); // before lookAndFeel is destroyed below -- no dangling default
        lookAndFeel = nullptr;
        juce::Logger::setCurrentLogger (nullptr);
        fileLogger = nullptr;
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<OnScreenKeyboardManager> keyboardManager; // device (kiosk) build only
    std::unique_ptr<juce::Component> snapshotComponent; // --window-snapshot only
    std::unique_ptr<OpenGuitarMultiFxLookAndFeel> lookAndFeel;
    std::unique_ptr<juce::FileLogger> fileLogger;
};

} // namespace openguitarmultifx

START_JUCE_APPLICATION (openguitarmultifx::OpenGuitarMultiFxApplication)
