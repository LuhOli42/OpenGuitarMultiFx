#include "MainComponent.h"
#include "MidiLearnPresetXml.h"
#include "PromptCards.h"
#include "SnapshotPresetXml.h"

#include "AddEffectBrowserDialog.h"
#include "MasterEqDialog.h"
#include "OpenGuitarMultiFxLookAndFeel.h"
#include "TunerOverlay.h"
#include "../Engine/InputGainSettings.h"
#include "../Engine/MasterBusSettings.h"
#include "PresetBrowserDialog.h"
#include "Tone3000Panel.h"
#include "TouchSizing.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <utility>

namespace openguitarmultifx
{

namespace
{
    constexpr int blockGap = 8; // fixed horizontal gap between columns -- see MainComponent::rowGap for the (dynamic) vertical one

    // I/O selectors are a fraction of a block's footprint -- they hold two
    // short lines of text, not an icon, and don't need a block-sized tile.
    constexpr int ioWidth = 64;
    constexpr int ioHeight = 58;

    // Extra width in each gutter, OUTSIDE the endpoint tile, purely for the
    // vertical part of a row-to-row connector to run down. Without it a link
    // that skips a row has nowhere to go but straight through that row's
    // endpoint tile (per user report 2026-09-11, "ele fica sobreposto").
    constexpr int cableLane = 20;

    // Taller than touch::minTapTarget -- this is where the preset
    // number/name live, and per the user's explicit ask they need to be
    // readable from a few feet away on a stage, not just tappable.
    constexpr int topBarHeight = 92;

    // Tuner/BPM-tap/meters bar pinned at the bottom -- see FooterBar.h.
    // Bumped alongside topBarHeight per user request 2026-09-10 ("a parte
    // em vermelho maior e a parte em roxo tbm" -- top bar and footer).
    constexpr int footerHeight = 92;

    /** <paramID>_ch<N>: ChannelKnobMemory's serialized per-channel slots.
        Derived mirror state -- the snapshot format deliberately stores
        only live float params, so hidden-channel values can't persist
        through it and a pure channel switch must not flag dirty. */
    bool isChannelMemoryAttribute (const juce::String& name, const juce::StringArray& paramIds)
    {
        const auto sep = name.lastIndexOf ("_ch");
        return sep > 0 && name.substring (sep + 3).containsOnly ("0123456789")
               && paramIds.contains (name.substring (0, sep));
    }

    /** The dirty-check view of a preset XML: everything EXCEPT the parts a
        pure snapshot switch legitimately moves. The top-level <Block>
        elements are a live-state mirror of the current snapshot by
        construction, so their snapshot-controlled fields are stripped --
        the `bypassed` flag, and every EffectState attribute whose name is
        a captured paramID or channel-memory slot (`*_ch<N>`). Everything
        else stays compared: a model/IR swap (modelPath/irPath), a
        tempo-sync toggle (*_synced/*_subdiv), routing, MIDI Learn, and
        the structural Block skeleton (key/gridSlot, count, order) -- all
        real edits, all still flag dirty, alongside any change inside
        <Snapshots> itself (which buildPresetXml() refreshes from live). */
    std::unique_ptr<juce::XmlElement> dirtyCompareForm (const juce::XmlElement& presetXml)
    {
        auto copy = std::make_unique<juce::XmlElement> (presetXml);

        // Captured param IDs per block position, read from <Snapshots>
        // itself: whatever a snapshot switch can write is not
        // dirty-check content on the live-state mirror.
        std::vector<juce::StringArray> paramIds;
        if (auto* snapshotsXml = copy->getChildByName ("Snapshots"))
        {
            snapshotsXml->removeAttribute ("current");
            for (auto* snapshotXml : snapshotsXml->getChildWithTagNameIterator ("Snapshot"))
            {
                int blockIndex = 0;
                for (auto* blockXml : snapshotXml->getChildWithTagNameIterator ("Block"))
                {
                    if (blockIndex >= (int) paramIds.size())
                        paramIds.emplace_back();
                    for (int a = 0; a < blockXml->getNumAttributes(); ++a)
                    {
                        const auto name = blockXml->getAttributeName (a);
                        if (name != "bypassed")
                            paramIds[(size_t) blockIndex].addIfNotAlreadyThere (name);
                    }
                    ++blockIndex;
                }
            }
        }

        int blockIndex = 0;
        for (auto* blockXml : copy->getChildWithTagNameIterator ("Block"))
        {
            blockXml->removeAttribute ("bypassed");
            if (auto* stateXml = blockXml->getChildByName ("EffectState"))
            {
                static const juce::StringArray none;
                const auto& ids = blockIndex < (int) paramIds.size() ? paramIds[(size_t) blockIndex] : none;
                for (int a = stateXml->getNumAttributes() - 1; a >= 0; --a)
                {
                    const auto name = stateXml->getAttributeName (a);
                    if (ids.contains (name) || isChannelMemoryAttribute (name, ids))
                        stateXml->removeAttribute (name);
                }
            }
            ++blockIndex;
        }
        return copy;
    }

}

MainComponent::MainComponent()
{
    registerBuiltInEffects (registry);

    addAndMakeVisible (presetBadge);
    presetBadge.onClicked = [this] { showPresetsPanel(); };

    // The A-H snapshot strip: tap a letter to recall that snapshot (see
    // selectSnapshot()). Shown from the start -- an unsaved chain still has
    // an active snapshot, same as a hardware pedalboard before you save.
    addAndMakeVisible (snapshotBar);
    snapshotBar.setCurrent (currentSnapshot);
    snapshotBar.onSnapshotSelected = [this] (int index) { selectSnapshot (index); };
    snapshotBar.onSnapshotLongPressed = [this] (int index) { showSnapshotMenu (index); };
    parameterPanel.getSnapshotMarkerColour = [this] (EffectProcessor* processor, juce::AudioParameterFloat* param)
    {
        return snapshotVaries (processor, param) ? colourOfSnapshot (currentSnapshot) : juce::Colours::transparentBlack;
    };
    presetBadge.setSnapshotIndex (currentSnapshot);

    addChildComponent (quickSaveButton); // only shown once a preset is actually loaded -- see updatePresetDisplay()
    quickSaveButton.onClick = [this] { if (currentPresetName.isNotEmpty()) savePresetAs (currentPresetName); };

    addAndMakeVisible (titleLabel);
    titleLabel.setFont (juce::Font (juce::FontOptions (42.0f, juce::Font::bold)));

    // The heavier display weight (Sora ExtraBold), not just "bold" --
    // readable from a few feet away is the actual requirement here (this
    // is a stage instrument), see PresetBadge.h and AGENT.md's UI/UX
    // Design Philosophy. Falls back to ordinary bold if the LookAndFeel
    // set up in Main.cpp somehow isn't a OpenGuitarMultiFxLookAndFeel.
    if (auto* laf = dynamic_cast<OpenGuitarMultiFxLookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()))
    {
        presetBadge.setDisplayTypeface (laf->getExtraBoldTypeface());
        snapshotBar.setDisplayTypeface (laf->getExtraBoldTypeface());
        titleLabel.setFont (juce::Font (juce::FontOptions (42.0f).withTypeface (laf->getExtraBoldTypeface())));
    }

    addAndMakeVisible (cpuLabel);
    cpuLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (settingsButton);
    settingsButton.onClick = [this] { showSettingsPanel(); };

    addChildComponent (retryAudioButton);
    retryAudioButton.onClick = [this]
    {
        // Idempotent: a no-op while a device is open, a re-run of the
        // whole open sequence otherwise (see AudioEngine::retryDevice).
        // The next timer tick re-derives audioDeviceOk either way, so
        // nothing here needs to read the result.
        audioEngine.retryDevice();
    };

    // Not made visible -- OverlayHost manages its own visibility as cards
    // get pushed/popped (see AGENT.md's UI/UX Design Philosophy).
    addChildComponent (overlayHost);

    // Wraps into rows instead of growing sideways now (see layoutChain()),
    // so it's the vertical scrollbar that's the overflow fallback.
    chainViewport.setViewedComponent (&chainContainer, false);
    chainViewport.setScrollBarsShown (false, false); // driven by chainScrollBar instead -- see its member comment
    chainViewport.onScrolled = [this] { syncChainScrollBar(); layoutChain(); repaint(); };
    addAndMakeVisible (chainViewport);

    chainScrollBar.setAutoHide (false);
    chainScrollBar.addListener (this);
    addAndMakeVisible (chainScrollBar);
    chainContainer.setMaxVisibleRows (numRows);

    // The only way to add a block now -- hover the grid, a "+" appears
    // exactly under the cursor, click it. No permanent dashed-box tile
    // sitting there all the time any more -- see ChainContainer.h.
    chainContainer.onSlotClicked = [this] (int index)
    {
        // No "was a picker just dismissed by this very click?" guard is
        // needed here: the Add-Effect browser is an OverlayHost card that
        // covers the whole window, so its dismissal click is eaten by the
        // overlay and never reaches this grid (unlike the old PopupMenu,
        // whose dismiss click fell through -- see git history).
        showAddEffectMenu (index);
    };

    parameterPanel.onRemoveRequested = [this] (EffectProcessor* p) { removeEffect (p); };
    parameterPanel.onPreferredHeightChanged = [this] { resized(); };
    parameterPanel.setModelsDirectory (getModelsDirectory());
    parameterPanel.setTone3000Manager (tone3000);
    parameterPanel.onPushOverlay = [this] (std::unique_ptr<juce::Component> c) { overlayHost.pushOverlay (std::move (c)); };
    parameterPanel.onPopOverlay  = [this] { overlayHost.popOverlay(); };

    parameterPanel.onMidiLearnRequested = [this] (juce::AudioParameterFloat* param)
    {
        bypassPedalArmed = nullptr;
        if (sceneMidiArmed != scenemidi::noAction)
        {
            sceneMidiArmed = scenemidi::noAction;
            refreshScenePedalDisplay();
        }

        // Already bound -> tapping it clears the binding outright.
        for (auto it = midiCcBindings.begin(); it != midiCcBindings.end(); ++it)
        {
            if (it->second == param)
            {
                midiCcBindings.erase (it);
                midiLearnArmedParam = nullptr;
                return;
            }
        }

        // Tapping the already-armed knob again cancels the arm; tapping a
        // different (unbound) knob arms that one instead -- only one knob
        // can be listening for the next CC at a time.
        midiLearnArmedParam = (midiLearnArmedParam == param) ? nullptr : param;
    };
    parameterPanel.getMidiCcForParam = [this] (juce::AudioParameterFloat* param) -> int
    {
        for (auto& [cc, boundParam] : midiCcBindings)
            if (boundParam == param)
                return cc;
        return -1;
    };
    addAndMakeVisible (parameterPanel);

    addAndMakeVisible (footerBar);
    footerBar.onTunerTapped = [this] { showTuner(); };
    footerBar.onMasterEqTapped = [this] { showMasterEq(); };
    footerBar.onMasterGainChanged = [this] (float db)
    {
        audioEngine.setMasterGainDb (db);
        masterBusSaveCountdown = 10; // trailing-edge save in timerCallback (~500 ms)
    };

    audioEngine.start();
    // start()'s return reflects the requested setup, not the final device state --
    // its USB-switch path can fail on both legs and still return true. The only
    // honest liveness signal is whether a device is open right now.
    audioDeviceOk = audioEngine.getDeviceManager().getCurrentAudioDevice() != nullptr;
    if (! audioDeviceOk)
    {
        titleLabel.setText ("Audio device failed to open", juce::dontSendNotification);
        // cpuLabel carries the persistent warning (timerCallback rewrites it every tick anyway,
        // and updatePresetDisplay() overwrites titleLabel on the first preset refresh).
        cpuLabel.setColour (juce::Label::textColourId, juce::Colours::orange);
    }
    // The warning used to be all it offered; now there's an action too
    // (audit #93 item 2). Set here, not only on timer transitions -- a
    // failed first open means the transition check never fires.
    retryAudioButton.setVisible (! audioDeviceOk);

    // Remembered across launches -- the interface's gain staging, like the tuning before it, describes the hardware
    // plugged in rather than a sound, so it is not part of any guitar preset. See AudioEngine::setInputGainDb().
    audioEngine.setInputGainDb (inputgain::loadSavedGainDb());

    // The master section (output gain + free-form EQ bands) is app-level for the same reason: it trims the
    // monitoring chain the whole rig plays through, not a per-preset sound. See MasterBusSettings.h.
    {
        const auto master = masterbus::loadState();
        audioEngine.setMasterGainDb (master.gainDb);
        footerBar.setMasterGainDb (master.gainDb);
        audioEngine.setMasterEqBands (master.bands);
    }

    // Row 0 starts wired device-in -> device-out so the app still makes
    // sound out of the box; rows 1-3 start unrouted, showing a "+" at both
    // ends. Rows are independent until you say otherwise (user decision
    // 2026-09-11).
    rowRouting[0].inputChannel = audioEngine.getInputChannel();
    rowRouting[0].toDevice = true;
    rowRouting[0].deviceOutputPair = audioEngine.getOutputChannelPair();

    for (int row = 0; row < numRows; ++row)
    {
        addAndMakeVisible (rowInputBlocks[(size_t) row]);
        rowInputBlocks[(size_t) row].onClicked = [this, row] { showRowInputMenu (row); };

        addAndMakeVisible (rowOutputBlocks[(size_t) row]);
        rowOutputBlocks[(size_t) row].onClicked = [this, row] { showRowOutputMenu (row); };
    }
    refreshRowEndpoints();

    layoutChain();
    // ~1280x850: close to a realistic 10" touch panel candidate resolution
    // (see ARCHITECTURE.md's display section -- exact panel TBD, this is
    // our best stand-in), bumped from a plain 800 to fit the footer bar
    // added 2026-09-10 without re-opening the original clipping bug: the
    // chain area reserves a fixed height for numRows regardless of
    // how many blocks exist, so anything else added below it (the footer)
    // has to come out of the SAME fixed budget the parameter drawer draws
    // from -- see AGENTS.md's decision entries on both the original
    // 1000x660-was-too-short bug and the knob-geometry/footer follow-ups
    // for the exact numbers this size is chosen to keep working.
    setSize (1280, 850);
    // 50ms (20Hz) rather than the original 200ms -- the footer's tuner
    // needle and level meters need to read as live/responsive, not
    // laggy; the timer's other jobs (CPU label text, graveyard sweep,
    // parameterPanel.refresh()) are all cheap enough not to mind running
    // 4x more often.
    startTimer (50);

    // Program Change -> preset switching (Phase 5). Every currently
    // available MIDI input, not just one picked in a settings screen --
    // there's no MIDI device picker yet, and a pedalboard's foot
    // controller is normally the only MIDI device plugged in anyway.
    for (auto& midiInput : juce::MidiInput::getAvailableDevices())
    {
        audioEngine.getDeviceManager().setMidiInputDeviceEnabled (midiInput.identifier, true);
        audioEngine.getDeviceManager().addMidiInputDeviceCallback (midiInput.identifier, this);
    }

    scenemidi::load (sceneMidi);
    refreshScenePedalDisplay();

    // Snapshot A's colour owns the accent from launch -- no updatePresetDisplay()
    // runs this early (it would also clobber the audio-device warning above).
    applySnapshotAccent();
}

MainComponent::~MainComponent()
{
    aliveFlag->store (false); // must be first -- an in-flight MIDI callAsync can still fire after this point

    for (auto& midiInput : juce::MidiInput::getAvailableDevices())
        audioEngine.getDeviceManager().removeMidiInputDeviceCallback (midiInput.identifier, this);

    audioEngine.stop(); // must happen before chain's processors are destroyed by the member destructors below
}

void MainComponent::addEffect (const juce::String& registryName, int targetGridSlot, int cabSelection)
{
    auto processor = registry.create (registryName);
    if (processor == nullptr)
        return;

    auto* raw = processor.get();

    // The browser's two insertion variants for amps ('Amp' / 'Amp + Cab'): a
    // cabSelection >= 0 writes the page-2 Cab selector before the block goes
    // live, so the first prepared sample already carries the chosen voicing.
    // Processors without a `*_cab` param simply find nothing.
    if (cabSelection >= 0)
        if (auto* params = raw->getParameters())
            for (auto* p : params->getParameters (true))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p);
                    f != nullptr && juce::String (f->paramID).endsWith ("_cab"))
                    *f = (float) cabSelection;

    // < 0 means "no preference" -- append right after the highest occupied
    // slot, same as what clicking the "+" past the current content used to
    // mean before slots could be sparse.
    int slot = targetGridSlot;
    if (slot < 0)
    {
        int maxSlot = -1;
        for (auto* b : blocks)
            maxSlot = juce::jmax (maxSlot, b->gridSlot);
        slot = maxSlot + 1;
    }

    // `blocks`/`chain` stay sorted ascending by gridSlot -- that sort order
    // IS the signal processing order SignalGraph reads directly (see
    // rebuildSignalGraph()), so a block landing at any clicked grid cell
    // (not just the next sequential one, per user request 2026-09-10) just
    // means finding where it falls in that order, not literally inserting
    // at its own slot number as an array index.
    int arrayIndex = 0;
    while (arrayIndex < blocks.size() && blocks[arrayIndex]->gridSlot < slot)
        ++arrayIndex;

    chain.insert (chain.begin() + arrayIndex, std::move (processor));

    // Every snapshot gains a slot for the new block at the same position --
    // its default state, in every snapshot (a snapshot can never change
    // which blocks exist; they all hold the same chain). See
    // normaliseSnapshots().
    for (auto& snapshot : snapshots)
        snapshot.blocks.insert (snapshot.blocks.begin() + arrayIndex, snapshotxml::captureBlock (*raw));

    auto block = std::make_unique<EffectBlockComponent> (*raw);
    block->gridSlot = slot;
    // Tapping the already-open block closes the drawer instead of just
    // re-opening it on itself -- same block, second tap, gone.
    block->onClicked = [this, raw] { selectBlock (selectedProcessor == raw ? nullptr : raw); };
    block->onDragEnded = [this] (EffectBlockComponent& b) { handleBlockDragEnded (b); };
    block->onLongPressed = [this, raw] { showBypassPedalMenu (raw); };
    chainContainer.addAndMakeVisible (*block);
    blocks.insert (arrayIndex, block.release());

    rebuildSignalGraph();
    layoutChain();
    selectBlock (raw);
}

void MainComponent::removeEffect (EffectProcessor* processor)
{
    clearMidiBindingsFor (processor);

    for (int i = 0; i < blocks.size(); ++i)
    {
        if (&blocks[i]->processor == processor)
        {
            blocks.remove (i);
            break;
        }
    }

    for (auto it = chain.begin(); it != chain.end(); ++it)
    {
        if (it->get() == processor)
        {
            const int index = (int) std::distance (chain.begin(), it);
            for (auto& snapshot : snapshots)
                snapshot.blocks.erase (snapshot.blocks.begin() + index);

            graveyard.push_back ({ std::move (*it), juce::Time::getMillisecondCounter() });
            chain.erase (it);
            break;
        }
    }

    if (selectedProcessor == processor)
        selectBlock (nullptr);

    rebuildSignalGraph();
    layoutChain();
}

void MainComponent::selectBlock (EffectProcessor* processor)
{
    selectedProcessor = processor;

    for (auto* block : blocks)
        block->setSelected (&block->processor == processor);

    parameterPanel.setProcessor (processor);
    resized(); // the detail drawer only exists (and only takes up space) once something is selected
}

std::vector<int> MainComponent::feedersFor (int row) const
{
    std::vector<int> feeders;
    for (int candidate = 0; candidate < numRows; ++candidate)
        if (rowRouting[(size_t) candidate].toRows[(size_t) row])
            feeders.push_back (candidate);
    return feeders;
}

juce::String MainComponent::describeRowDestinations (int row) const
{
    const auto& routing = rowRouting[(size_t) row];
    juce::StringArray parts;

    if (routing.toDevice)
    {
        const auto outputNames = audioEngine.getAvailableOutputPairNames();
        const int pairIndex = routing.deviceOutputPair / 2;
        parts.add (pairIndex < outputNames.size() ? outputNames[pairIndex] : juce::String ("Out"));
    }

    for (int target = 0; target < numRows; ++target)
        if (routing.toRows[(size_t) target])
            parts.add ("Line " + juce::String (target + 1));

    return parts.joinIntoString (" + ");
}

juce::String MainComponent::describeRowFeeders (const std::vector<int>& feeders) const
{
    juce::StringArray parts;
    for (int feeder : feeders)
        parts.add ("Line " + juce::String (feeder + 1));
    return parts.joinIntoString (" + ");
}

void MainComponent::refreshRowEndpoints()
{
    const auto inputNames = audioEngine.getAvailableInputChannelNames();

    for (int row = 0; row < numRows; ++row)
    {
        const auto& routing = rowRouting[(size_t) row];

        // Left: whatever already feeds this row -- possibly several rows at
        // once (a merge). A row fed by ANOTHER row shows that link rather
        // than a "+" -- the "+" invites adding a device input, and one on
        // top of the incoming row(s) would be exactly the contradiction the
        // routing model avoids (per user request 2026-09-11: "quando a
        // linha tiver sendo usada por um output ela pode sumir o input +").
        const auto feeders = feedersFor (row);
        if (! feeders.empty())
            rowInputBlocks[(size_t) row].setDisplay ("FROM", describeRowFeeders (feeders));
        else if (routing.inputChannel < 0)
            rowInputBlocks[(size_t) row].setDisplay ({}, {});
        else
            rowInputBlocks[(size_t) row].setDisplay ("IN",
                routing.inputChannel < inputNames.size() ? inputNames[routing.inputChannel] : juce::String ("Default"));

        // Right: everything this row feeds -- possibly several at once, so
        // this summarises rather than naming one destination.
        if (! routing.feedsAnything())
            rowOutputBlocks[(size_t) row].setDisplay ({}, {});
        else
            rowOutputBlocks[(size_t) row].setDisplay ("TO", describeRowDestinations (row));
    }
}

void MainComponent::showRowInputMenu (int row)
{
    // Physical inputs only. Receiving FROM another row is expressed on that
    // other row's output tile instead, so one link is never two contradictory
    // settings (user decision 2026-09-11).
    auto inputNames = audioEngine.getAvailableInputChannelNames();
    if (inputNames.isEmpty())
        inputNames.add ("Default");

    const auto feeders = feedersFor (row);

    // One "Disconnect from Line N" item per CURRENT feeder (there can be
    // several now -- a merge), each removable independently, plus the
    // physical-input list below. Feeder item IDs start at 1000 so they
    // never collide with the input-list IDs (2..inputNames.size()+1) --
    // there's no realistic input count anywhere near that.
    constexpr int feederItemBase = 1000;

    // Input sensitivity lives here because this is the tile that says where the signal comes from, and the setting is
    // about that signal's level -- see AudioEngine::setInputGainDb(). IDs 2000+ so they miss both other ranges.
    constexpr int gainItemBase = 2000;
    juce::PopupMenu gainMenu;
    const float currentGainDb = audioEngine.getInputGainDb();
    for (int i = 0; i < 11; ++i)
    {
        const float db = -24.0f + 3.0f * (float) i; // -24 .. +6 dB in 3 dB steps
        gainMenu.addItem (gainItemBase + i, (db > 0.0f ? "+" : "") + juce::String (db, 0) + " dB", true,
                          std::abs (db - currentGainDb) < 0.01f);
    }

    juce::PopupMenu menu;
    menu.addSubMenu ("Input gain", gainMenu);
    menu.addSeparator();
    if (feeders.empty())
        menu.addItem (1, "Not connected", true, rowRouting[(size_t) row].inputChannel < 0);
    else
        for (int feeder : feeders)
            menu.addItem (feederItemBase + feeder, "Disconnect from Line " + juce::String (feeder + 1), true, false);

    menu.addSeparator();
    for (int i = 0; i < inputNames.size(); ++i)
        menu.addItem (i + 2, inputNames[i], true, feeders.empty() && rowRouting[(size_t) row].inputChannel == i);

    menu.showMenuAsync (juce::PopupMenu::Options().withStandardItemHeight (touch::minTapTarget),
        [this, row] (int result)
        {
            if (result <= 0)
                return;

            if (result >= gainItemBase)
            {
                const float db = -24.0f + 3.0f * (float) (result - gainItemBase);
                audioEngine.setInputGainDb (db);
                inputgain::saveGainDb (db);
                return;
            }

            if (result >= feederItemBase)
            {
                // Disconnect just this one feeder -- the others (if any)
                // and the row's own device input (if any) are untouched.
                const int feeder = result - feederItemBase;
                rowRouting[(size_t) feeder].toRows[(size_t) row] = false;
            }
            else
            {
                // Picking a physical input replaces every row-based feeder
                // outright (device input and routed-row input stay
                // mutually exclusive -- see refreshRowEndpoints()'s
                // comment on why a row never shows both at once).
                for (int candidate = 0; candidate < numRows; ++candidate)
                    rowRouting[(size_t) candidate].toRows[(size_t) row] = false;

                rowRouting[(size_t) row].inputChannel = result == 1 ? -1 : result - 2;

                // Row 0's input is also the device's -- the engine only opens one
                // input channel today, so a second fed row shares it. Selecting
                // per-row physical inputs properly is an engine change (multiple
                // input channels), not a UI one; see AGENTS.md.
                if (rowRouting[(size_t) row].inputChannel >= 0)
                    audioEngine.setInputChannel (rowRouting[(size_t) row].inputChannel);
            }

            refreshRowEndpoints();
            rebuildSignalGraph();
            layoutChain();
        });
}

void MainComponent::showRowOutputMenu (int row)
{
    auto outputNames = audioEngine.getAvailableOutputPairNames();
    if (outputNames.isEmpty())
        outputNames.add ("Default");

    const auto& routing = rowRouting[(size_t) row];

    // Every entry is a TOGGLE, not a choice: a row can feed the device and
    // several other rows at once (a split). The menu closes after each
    // toggle -- tap the tile again to add or remove another destination.
    juce::PopupMenu menu;
    menu.addItem (1, "Not connected", routing.feedsAnything(), false);
    menu.addSeparator();

    for (int i = 0; i < outputNames.size(); ++i)
        menu.addItem (i + 2, "Output " + outputNames[i], true,
                       routing.toDevice && routing.deviceOutputPair / 2 == i);

    menu.addSeparator();
    constexpr int rowItemBase = 100;
    for (int target = 0; target < numRows; ++target)
    {
        // Only forward -- a lower-numbered line can feed a higher one, never
        // the other way. This also makes a loop structurally impossible (no
        // link can ever point back at an equal-or-lower number), so the
        // separate cycle check this menu used to need is gone along with
        // it. Per user request 2026-09-12: "linkar 3 -> 2 n faz sentido...
        // qualquer opcao de numero 4>2 4>3 etc" -- not offered at all, not
        // just greyed.
        if (target <= row)
            continue;

        // Always offered, even if another row already feeds this target --
        // that's a merge (SignalGraph sums every row that feeds a given
        // one), not a conflict. No longer restricted to "whichever row got
        // there first" now that the engine can actually mix them.
        const bool alreadyOn = routing.toRows[(size_t) target];
        menu.addItem (rowItemBase + target, "Line " + juce::String (target + 1), true, alreadyOn);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withStandardItemHeight (touch::minTapTarget),
        [this, row, outputCount = outputNames.size()] (int result)
        {
            if (result <= 0)
                return;

            auto& r = rowRouting[(size_t) row];

            if (result == 1)
            {
                r.toDevice = false;
                r.toRows.fill (false);
            }
            else if (result >= rowItemBase)
            {
                const int target = result - rowItemBase;
                const bool nowOn = ! r.toRows[(size_t) target];
                r.toRows[(size_t) target] = nowOn;

                // The target now has a source; a device input on top of it
                // would be a second one (see showRowInputMenu()).
                if (nowOn)
                    rowRouting[(size_t) target].inputChannel = -1;
            }
            else if (result - 2 < outputCount)
            {
                const int pair = (result - 2) * 2;
                // Same output twice = turn it off; a different one = move it.
                r.toDevice = ! (r.toDevice && r.deviceOutputPair == pair);
                r.deviceOutputPair = pair;
                if (r.toDevice)
                    audioEngine.setOutputChannelPair (pair);
            }

            refreshRowEndpoints();
            rebuildSignalGraph();
            layoutChain();
        });
}

void MainComponent::rebuildSignalGraph()
{
    // Order follows the ROW LINKS, not the flat block array: a row's blocks
    // maps 1:1 onto SignalGraph's lanes now -- each row IS a lane, in the
    // same order, so a row's routing translates directly into lane
    // configuration with no flattening/approximation needed. Real splits
    // and merges are the engine's own job now (Source/Engine/SignalGraph.h),
    // not something this function has to fake by reordering blocks.
    auto signalGraph = std::make_unique<SignalGraph>();

    for (int row = 0; row < numRows; ++row)
    {
        const auto& routing = rowRouting[(size_t) row];

        // `blocks` is sorted by gridSlot and gridSlot == row * chainColumns +
        // col, so a row's blocks are already contiguous and in column order.
        std::vector<EffectProcessor*> rowProcessors;
        for (auto* block : blocks)
            if (block->gridSlot / chainColumns == row)
                rowProcessors.push_back (&block->processor);
        signalGraph->setLaneProcessors (row, std::move (rowProcessors));

        // Every row with a device input reads the SAME live input -- the
        // engine only opens one input channel today (see showRowInputMenu()'s
        // comment), so several fed rows just share it.
        signalGraph->setLaneReadsDeviceInput (row, routing.inputChannel >= 0);
        signalGraph->setLaneWritesDeviceOutput (row, routing.toDevice);

        for (int target = 0; target < numRows; ++target)
            if (routing.toRows[(size_t) target])
                signalGraph->addLaneConnection (row, target); // target > row always -- forward-only, enforced in showRowOutputMenu()
    }

    audioEngine.setSignalGraph (std::move (signalGraph));
}

void MainComponent::layoutChain()
{
    // chainColumns is a fixed policy (8) now -- see the member's comment --
    // and blockWidth/blockHeight/rowGap are derived in resized() from the
    // available space before this runs. Wraps into rows once a row fills
    // up, instead of growing sideways forever -- see AGENT.md's UI/UX
    // Design Philosophy (the Quad Cortex Grid reference this is modelled
    // on is a fixed grid, not a horizontally-scrolling strip).
    const int viewportWidth = juce::jmax (blockWidth, chainViewport.getWidth());

    // Each block is positioned by its OWN gridSlot now, not by its index in
    // `blocks` -- a block can sit at any grid cell you clicked (per user
    // request 2026-09-10, "ta adicionando ainda sequencialmente em vez
    // daonde eu clico"), while `blocks`/`chain` themselves stay sorted
    // ascending by gridSlot (see addEffect()/handleBlockDragEnded()), which
    // is what makes that sort order double as the actual signal processing
    // order SignalGraph reads (rebuildSignalGraph()) with no extra
    // bookkeeping.
    std::vector<juce::Rectangle<float>> bounds;
    int maxSlot = -1;

    for (auto* block : blocks)
    {
        const int row = block->gridSlot / chainColumns;
        const int col = block->gridSlot % chainColumns;
        block->setBounds (col * (blockWidth + blockGap), row * (blockHeight + rowGap), blockWidth, blockHeight);
        bounds.emplace_back (block->getBounds().toFloat());
        maxSlot = juce::jmax (maxSlot, block->gridSlot);
    }

    const int totalRows = juce::jmax (1, maxSlot / chainColumns + 1);
    // At least numRows tall even when real content doesn't fill that
    // many rows yet -- otherwise the grid's dim placeholder rows
    // (ChainContainer::paint()) would be clipped off (JUCE clips paint()
    // to the component's own bounds). Still grows past that if there's
    // genuinely more content (the scrollbar fallback).
    const int drawnRows = juce::jmax (totalRows, numRows);
    // + drawerScrollPadding: whatever the open drawer currently overlays
    // needs to stay reachable by scrolling -- see the member's comment for
    // why this has to be reapplied HERE rather than added on afterward.
    chainContainer.setSize (viewportWidth, drawnRows * (blockHeight + rowGap) - rowGap + drawerScrollPadding);
    chainContainer.setRowMetrics (chainColumns, blockWidth, blockHeight, blockGap, rowGap);
    chainContainer.setBlockBounds (std::move (bounds));

    // IN always sits at row 0; OUT sits at whichever row is currently the
    // LAST occupied one (by gridSlot, which can be sparse -- an empty row
    // below some real content still counts as "not the last occupied one"
    // even if it has nothing in it), not a single slot centred across
    // every possible row -- rows in between get a "continues" connector
    // glyph instead, drawn in paint(). Reuses
    // leftGutterColumn/rightGutterColumn/chainRowTop, which only change on
    // an actual window resize (see resized()), so this stays correct when
    // called from addEffect()/removeEffect()/handleBlockDragEnded() too --
    // none of which trigger a full resized() pass. Per user request
    // 2026-09-10 (Quad Cortex-style multi-row IN/OUT routing).
    chainUsedRows = juce::jlimit (1, numRows, totalRows);
    chainContainer.setUsedRows (chainUsedRows);

    // The rows live inside the scrolling viewport, so their endpoint tiles --
    // which sit in the gutters OUTSIDE it -- have to be offset by the same
    // scroll amount or they'd drift away from the row they belong to (the
    // connectors in paint() already do this). Per user report 2026-09-11:
    // "quando scrolamos as 4 linhas esses + e inputs tao se mexendo com o
    // scroll".
    const int scrollOffset = chainViewport.getViewPositionY();

    auto gutterRowSlot = [&] (juce::Rectangle<int> gutterColumn, int row)
    {
        return juce::Rectangle<int> (gutterColumn.getX(), chainRowTop + row * (blockHeight + rowGap) - scrollOffset,
                                      gutterColumn.getWidth(), blockHeight);
    };

    // Every row gets its own pair, not just row 0 and the last occupied one
    // -- each row is independently routable now, so each needs somewhere to
    // say so (per user request 2026-09-11, "+ no início e final de cada
    // linha").
    for (int row = 0; row < numRows; ++row)
    {
        // Tiles hug the grid side of their gutter; the cable lane is the
        // strip left over on the outer side.
        const auto leftSlot = gutterRowSlot (leftGutterColumn, row);
        const auto rightSlot = gutterRowSlot (rightGutterColumn, row);
        const auto inBounds = leftSlot.withTrimmedLeft (cableLane).withSizeKeepingCentre (ioWidth, ioHeight);
        const auto outBounds = rightSlot.withTrimmedRight (cableLane).withSizeKeepingCentre (ioWidth, ioHeight);

        rowInputBlocks[(size_t) row].setBounds (inBounds);
        rowOutputBlocks[(size_t) row].setBounds (outBounds);

        // Scrolled past the viewport's band: the row itself is clipped away
        // in there, so its endpoints would otherwise float over the top bar
        // or the footer with no row to belong to.
        const bool rowVisible = inBounds.getCentreY() > chainViewport.getY()
                                 && inBounds.getCentreY() < chainViewport.getBottom();
        rowInputBlocks[(size_t) row].setVisible (rowVisible);
        rowOutputBlocks[(size_t) row].setVisible (rowVisible);
    }

    // Where a row-to-row link crosses the grid: midway between the two rows'
    // own lines, in ChainContainer's coordinates (it draws the crossing; the
    // gutters either side are outside its bounds -- see paint()).
    std::vector<int> crossings;
    for (int row = 0; row < numRows; ++row)
    {
        const auto& routing = rowRouting[(size_t) row];

        for (int target = 0; target < numRows; ++target)
        {
            if (! routing.toRows[(size_t) target])
                continue;

            const int fromY = row * (blockHeight + rowGap) + blockHeight / 2;
            const int toY = target * (blockHeight + rowGap) + blockHeight / 2;
            const int dir = toY > fromY ? 1 : -1;
            crossings.push_back (fromY + dir * (blockHeight / 2 + rowGap / 2)); // same gap paint() uses
        }
    }
    chainContainer.setCrossings (std::move (crossings));

    repaint(); // the inter-row connector glyphs paint() draws depend on chainUsedRows
}

void MainComponent::handleBlockDragEnded (EffectBlockComponent& blockComp)
{
    if (blocks.indexOf (&blockComp) < 0)
    {
        layoutChain();
        return;
    }

    // Where it was dropped, translated back into a grid cell -- row from Y
    // now that dragging isn't locked to one row anymore, column from X,
    // same as layoutChain()'s own row/col math. Not clamped to existing
    // content -- you can drag a block onto any empty cell, same as adding
    // one there (see EffectBlockComponent::gridSlot's comment).
    const auto centre = blockComp.getBounds().getCentre();
    const int row = juce::jlimit (0, numRows - 1, centre.y / (blockHeight + rowGap));
    const int col = juce::jlimit (0, chainColumns - 1, centre.x / (blockWidth + blockGap));
    const int targetSlot = row * chainColumns + col;

    if (targetSlot != blockComp.gridSlot)
    {
        // Dropped onto an already-occupied cell -- swap slots with whatever
        // was there instead of silently refusing the drop.
        for (auto* other : blocks)
        {
            if (other != &blockComp && other->gridSlot == targetSlot)
            {
                other->gridSlot = blockComp.gridSlot;
                break;
            }
        }
        blockComp.gridSlot = targetSlot;

        // `blocks`/`chain` must stay sorted ascending by gridSlot -- that
        // sort order IS the signal processing order (see
        // rebuildSignalGraph()) -- so re-sort both, in lockstep, by the
        // same permutation rather than the old std::rotate (which only
        // made sense when "move to array index N" and "move to grid slot
        // N" were the same thing).
        std::vector<int> order ((size_t) blocks.size());
        std::iota (order.begin(), order.end(), 0);
        std::sort (order.begin(), order.end(),
                    [this] (int a, int b) { return blocks[a]->gridSlot < blocks[b]->gridSlot; });

        std::vector<std::unique_ptr<EffectProcessor>> newChain;
        newChain.reserve (chain.size());
        juce::Array<EffectBlockComponent*> newBlockOrder;
        for (int i : order)
        {
            newChain.push_back (std::move (chain[(size_t) i]));
            newBlockOrder.add (blocks[i]);
        }
        chain = std::move (newChain);

        // Snapshots store block state by chain POSITION, so the same
        // permutation has to be applied to every snapshot's block list or a
        // reorder would silently move each block's saved values onto the
        // wrong block.
        for (auto& snapshot : snapshots)
        {
            std::vector<snapshotxml::BlockSnapshot> reordered;
            reordered.reserve (snapshot.blocks.size());
            for (int i : order)
                if (i < (int) snapshot.blocks.size())
                    reordered.push_back (std::move (snapshot.blocks[(size_t) i]));
            snapshot.blocks = std::move (reordered);
        }

        blocks.clearQuick (false); // releases ownership without deleting -- newBlockOrder already holds every pointer
        for (auto* b : newBlockOrder)
            blocks.add (b);

        rebuildSignalGraph();
    }

    layoutChain(); // snaps every block, including the dragged one, back onto the clean grid
}

void MainComponent::showAddEffectMenu (int targetGridSlot)
{
    // The real picker is an OverlayHost card now (category rail + tile grid
    // + live search) -- the nested juce::PopupMenu it replaced couldn't fit
    // ~120 effects on a touchscreen. Categories are registry metadata
    // (EffectRegistry::categoryForKey), so this dialog needs no name table.
    auto dialog = std::make_unique<AddEffectBrowserDialog> (registry);

    juce::Component::SafePointer<AddEffectBrowserDialog> browser (dialog.get());
    auto choiceMade = std::make_shared<std::atomic<bool>> (false);

    // The pop is deferred one message-loop tick: popOverlay() destroys the
    // card, and freeing a std::function's storage while it is still running
    // (tile -> grid -> dialog -> this callback) is UB. The deferred side
    // guards on three things: aliveFlag (window may close before the tick
    // runs -- same pattern as the MIDI callbacks below), `browser` (null
    // once the card is dismissed by an outside tap, which invalidates the
    // pending choice), and topLayer() (popOverlay() removes the TOP layer,
    // so it only runs while this browser is still the top card).
    dialog->onEffectChosen = [this, targetGridSlot, browser, choiceMade] (juce::String key, bool cabOn)
    {
        // The card stays on screen until the deferred pop -- a second tile
        // tap in the same tick must not schedule a second addEffect.
        if (choiceMade->exchange (true))
            return;

        juce::MessageManager::callAsync ([this, key = std::move (key), targetGridSlot, cabOn,
                                        browser, alive = aliveFlag]
        {
            if (! alive->load() || browser == nullptr)
                return;

            if (overlayHost.topLayer() == browser.getComponent())
                overlayHost.popOverlay();

            addEffect (key, targetGridSlot, cabOn ? 1 : 0);
        });
    };
    dialog->onPopOverlay = [this, browser]
    {
        juce::MessageManager::callAsync ([this, browser, alive = aliveFlag]
        {
            if (alive->load() && browser != nullptr
                && overlayHost.topLayer() == browser.getComponent())
                overlayHost.popOverlay();
        });
    };
    overlayHost.pushOverlay (std::move (dialog));
}

juce::File MainComponent::getModelsDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("OpenGuitarMultiFx")
               .getChildFile ("models");
}

juce::File MainComponent::getPresetsDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("OpenGuitarMultiFx")
               .getChildFile ("presets");
}

void MainComponent::showSettingsPanel()
{
    // The app's one Settings screen, reached through the "..." button --
    // just login/key right now, search and download live in ParameterPanel,
    // contextual to whichever block is selected. See Tone3000Panel.h and
    // AGENT.md's UI/UX Design Philosophy for why this is a card pushed onto
    // overlayHost rather than a separate OS window.
    auto panel = std::make_unique<Tone3000Panel> (tone3000, appUpdater);
    panel->onPushOverlay = [this] (std::unique_ptr<juce::Component> c) { overlayHost.pushOverlay (std::move (c)); };
    panel->onPopOverlay  = [this] { overlayHost.popOverlay(); };
    panel->onQualityChanged = [this] (EffectRegistry::OversamplingQuality q) { applyRenderQuality (q); };
    // The return value only reports whether the REQUESTED rate applied (Settings
    // shows it) -- a failed switch can still leave a device open, and timerCallback
    // re-derives audioDeviceOk from the device itself every tick regardless.
    auto* rawPanel = panel.get();
    panel->onSampleRateChanged = [this, rawPanel] (double rate)
    {
        const auto err = audioEngine.setSampleRate (rate);
        // The ms readout of each buffer size depends on the rate.
        rawPanel->showBufferSizes (audioEngine.getAvailableBufferSizes(), audioEngine.getCurrentBufferSize(), audioEngine.getCurrentSampleRate());
        return err;
    };
    panel->onBufferSizeChanged = [this] (int size) { return audioEngine.setBufferSize (size); };
    panel->onRetryDevice = [this, rawPanel]
    {
        const auto err = audioEngine.retryDevice();
        if (err.isEmpty() && rawPanel != nullptr)
            // Re-derive the section from the now-open device: rate
            // buttons appear and the retry action hides itself.
        {
            rawPanel->showSampleRates (audioEngine.getAvailableSampleRates(), audioEngine.getCurrentSampleRate());
            rawPanel->showBufferSizes (audioEngine.getAvailableBufferSizes(), audioEngine.getCurrentBufferSize(), audioEngine.getCurrentSampleRate());
            rawPanel->showInterface (audioEngine.describeInterface());
        }
        return err;
    };
    panel->showSampleRates (audioEngine.getAvailableSampleRates(), audioEngine.getCurrentSampleRate());
    panel->showBufferSizes (audioEngine.getAvailableBufferSizes(), audioEngine.getCurrentBufferSize(), audioEngine.getCurrentSampleRate());
    panel->showInterface (audioEngine.describeInterface());

    overlayHost.pushOverlay (std::move (panel));
}

void MainComponent::applyRenderQuality (EffectRegistry::OversamplingQuality quality)
{
    EffectRegistry::setOversamplingQuality (quality);
    EffectRegistry::saveOversamplingQuality();

    // Same discipline as removeEffect(): the old processor is retired (the audio thread may still be running the old
    // SignalGraph on it), the new one takes its place in the same chain position and grid cell.
    EffectProcessor* newSelection = nullptr;
    bool rebuilt = false;

    for (int i = 0; i < (int) chain.size(); ++i)
    {
        const auto key = registry.keyForDisplayName (chain[(size_t) i]->getName());
        if (! registry.dependsOnOversamplingQuality (key))
            continue;

        auto fresh = registry.create (key);
        if (fresh == nullptr)
            continue;

        auto& old = chain[(size_t) i];
        if (auto state = old->getState())
            fresh->setState (*state);
        fresh->setBypassed (old->isBypassed());

        auto* raw = fresh.get();
        const int slot = blocks[i]->gridSlot;
        if (selectedProcessor == old.get())
            newSelection = raw;

        clearMidiBindingsFor (old.get());
        graveyard.push_back ({ std::move (old), juce::Time::getMillisecondCounter() });
        chain[(size_t) i] = std::move (fresh);

        auto block = std::make_unique<EffectBlockComponent> (*raw);
        block->gridSlot = slot;
        block->onClicked = [this, raw] { selectBlock (selectedProcessor == raw ? nullptr : raw); };
        block->onDragEnded = [this] (EffectBlockComponent& b) { handleBlockDragEnded (b); };
    block->onLongPressed = [this, raw] { showBypassPedalMenu (raw); };
        chainContainer.addAndMakeVisible (*block);
        blocks.set (i, block.release()); // deletes the old block component; its processor is alive in the graveyard
        rebuilt = true;
    }

    if (! rebuilt)
        return;

    if (newSelection != nullptr)
        selectBlock (newSelection);
    else if (selectedProcessor != nullptr)
        selectBlock (selectedProcessor); // keeps the highlight on the untouched selection

    rebuildSignalGraph();
    layoutChain();
}

void MainComponent::showTuner()
{
    auto overlay = std::make_unique<TunerOverlay>();
    overlay->getFrequencyHz = [this] { return audioEngine.getDetectedFrequencyHz(); };
    overlay->onPopOverlay = [this] { overlayHost.popOverlay(); };

    // The pitch analysis only runs while this overlay is up (AudioEngine::setTunerActive()): YIN's pass is the most
    // expensive single thing the app does per unit of audio, and nothing reads it otherwise.
    audioEngine.setTunerActive (true);
    overlay->onDestroyed = [this] { audioEngine.setTunerActive (false); };

    overlayHost.pushOverlay (std::move (overlay));
}

void MainComponent::showMasterEq()
{
    auto dialog = std::make_unique<MasterEqDialog>();
    dialog->setBands (audioEngine.getMasterEqBands());
    dialog->getSampleRate = [this] { return audioEngine.getCurrentSampleRate(); };
    dialog->onPopOverlay = [this] { overlayHost.popOverlay(); };
    dialog->onBandsChanged = [this] (const std::vector<MasterEqBand>& bands)
    {
        audioEngine.setMasterEqBands (bands);
        masterBusSaveCountdown = 10; // debounced save in timerCallback
    };

    overlayHost.pushOverlay (std::move (dialog));
}

void MainComponent::showPresetsPanel()
{
    auto dialog = std::make_unique<PresetBrowserDialog> (presetLibrary);
    auto* browser = dialog.get(); // for callbacks that reopen the card's own dialogs

    dialog->getCurrentPresetName = [this] { return currentPresetName; };

    dialog->onSaveRequested = [this] (juce::String name)
    {
        if (! presets.presetExists (name) || name == currentPresetName)
        {
            savePresetAs (name);
            overlayHost.popOverlay();
            return;
        }

        overlayHost.pushOverlay (std::make_unique<ChoiceCard> (
            "Replace preset?", "\"" + name + "\" already exists. Saving will overwrite it with the current sound.",
            juce::StringArray { "Replace", "Cancel" },
            [this, name] (int choice)
            {
                overlayHost.popOverlay(); // this card
                if (choice != 0)
                    return;
                savePresetAs (name);
                overlayHost.popOverlay(); // the list
            }, 1));
    };

    dialog->onPresetChosen = [this] (juce::String name)
    {
        overlayHost.popOverlay();
        loadPresetAskingToSave (name);
    };

    dialog->onNewRequested = [this, browser]
    {
        // The save-or-discard prompt stacks on top of the browser like
        // every other card; the refresh rides newPresetAskingToSave's
        // onReset so it runs only once the board is ACTUALLY cleared
        // (after Save/Discard too), leaving no stale highlighted tile --
        // and not at all on Cancel.
        juce::Component::SafePointer<PresetBrowserDialog> safeBrowser (browser);
        newPresetAskingToSave ([safeBrowser]
        {
            if (safeBrowser != nullptr)
                safeBrowser->refresh();
        });
    };

    dialog->onDeleteRequested = [this] (juce::String name)
    {
        overlayHost.pushOverlay (std::make_unique<ChoiceCard> (
            "Delete preset?", "\"" + name + "\" will be deleted. This can't be undone.",
            juce::StringArray { "Delete", "Cancel" },
            [this, name] (int choice)
            {
                if (choice != 0)
                {
                    overlayHost.popOverlay();
                    return;
                }

                presets.deletePreset (name);
                if (currentPresetName == name)
                {
                    currentPresetName.clear();
                    currentPresetNumber = 0;
                    presetBaseline.reset();
                    updatePresetDisplay();
                }
                closePresetCardsAndReopenList();
            }, 1));
    };

    dialog->onRenameRequested = [this] (juce::String name)
    {
        overlayHost.pushOverlay (std::make_unique<TextPromptCard> (
            "Rename preset", name,
            [this, name] (juce::String newName)
            {
                if (newName == name)
                {
                    overlayHost.popOverlay();
                    return;
                }
                if (! presets.renamePreset (name, newName))
                {
                    overlayHost.pushOverlay (std::make_unique<ChoiceCard> (
                        "Can't rename", "A preset called \"" + newName + "\" already exists.",
                        juce::StringArray { "OK" }, [this] (int) { overlayHost.popOverlay(); }));
                    return;
                }
                if (currentPresetName == name)
                {
                    currentPresetName = newName;
                    updatePresetDisplay();
                }
                closePresetCardsAndReopenList();
            },
            [this] { overlayHost.popOverlay(); }));
    };

    dialog->onDuplicateRequested = [this] (juce::String name)
    {
        overlayHost.pushOverlay (std::make_unique<TextPromptCard> (
            "Duplicate preset", name + " copy",
            [this, name] (juce::String newName)
            {
                if (! presets.duplicatePreset (name, newName))
                {
                    overlayHost.pushOverlay (std::make_unique<ChoiceCard> (
                        "Can't duplicate", "A preset called \"" + newName + "\" already exists.",
                        juce::StringArray { "OK" }, [this] (int) { overlayHost.popOverlay(); }));
                    return;
                }
                // The copy is a different preset: it keeps the tags but must
                // not inherit the original's bank slot or setlist seats.
                presetLibrary.clearMemberships (newName);
                closePresetCardsAndReopenList();
            },
            [this] { overlayHost.popOverlay(); }));
    };

    dialog->onEditTagsRequested = [this] (juce::String name)
    {
        juce::StringArray current;
        for (auto& e : presetLibrary.entries())
            if (e.name == name)
                current = e.tags;

        overlayHost.pushOverlay (std::make_unique<TextPromptCard> (
            "Tags for \"" + name + "\" (comma separated)", current.joinIntoString (", "),
            [this, name] (juce::String text)
            {
                juce::StringArray tags;
                tags.addTokens (text, ",", "");
                tags.trim();
                tags.removeEmptyStrings();
                presetLibrary.setTags (name, tags);
                closePresetCardsAndReopenList();
            },
            [this] { overlayHost.popOverlay(); }));
    };

    dialog->onNewSetlistRequested = [this] (juce::String presetName)
    {
        overlayHost.pushOverlay (std::make_unique<TextPromptCard> (
            "New setlist", "",
            [this, presetName] (juce::String setlistName)
            {
                presetLibrary.addToSetlist (presetName, setlistName);
                closePresetCardsAndReopenList();
            },
            [this] { overlayHost.popOverlay(); }));
    };

    dialog->onRenameSetlistRequested = [this] (juce::String setlistName)
    {
        overlayHost.pushOverlay (std::make_unique<TextPromptCard> (
            "Rename setlist", setlistName,
            [this, setlistName] (juce::String newName)
            {
                if (newName == setlistName)
                {
                    overlayHost.popOverlay();
                    return;
                }
                if (! presetLibrary.renameSetlist (setlistName, newName))
                {
                    overlayHost.pushOverlay (std::make_unique<ChoiceCard> (
                        "Can't rename", "A setlist called \"" + newName + "\" already exists.",
                        juce::StringArray { "OK" }, [this] (int) { overlayHost.popOverlay(); }));
                    return;
                }
                closePresetCardsAndReopenList();
            },
            [this] { overlayHost.popOverlay(); }));
    };

    dialog->onDeleteSetlistRequested = [this] (juce::String setlistName)
    {
        overlayHost.pushOverlay (std::make_unique<ChoiceCard> (
            "Delete setlist?", "\"" + setlistName + "\" will be removed. The presets in it stay saved.",
            juce::StringArray { "Delete", "Cancel" },
            [this, setlistName] (int choice)
            {
                overlayHost.popOverlay();
                if (choice != 0)
                    return;
                presetLibrary.deleteSetlist (setlistName);
                closePresetCardsAndReopenList();
            }, 1));
    };

    // Stepping keeps the browser open -- it's the performance action the
    // whole setlist mode exists for (the foot-switch up/down equivalent). The
    // refresh has to wait for the load to actually happen: with unsaved edits
    // the target only loads once the Save/Discard prompt resolves, and the
    // browser can meanwhile be closed -- hence the SafePointer, not raw `browser`.
    dialog->onStepSetlist = [this, browser] (juce::String setlistName, int delta)
    {
        const auto target = presetLibrary.stepSetlist (setlistName, currentPresetName, delta);
        if (target.isEmpty())
            return;
        juce::Component::SafePointer<PresetBrowserDialog> safeBrowser (browser);
        loadPresetAskingToSave (target, [safeBrowser]
        {
            if (safeBrowser != nullptr)
                safeBrowser->refresh();
        });
    };

    dialog->onPopOverlay = [this] { overlayHost.popOverlay(); };

    // The constructor's refresh() ran before getCurrentPresetName was wired,
    // so rebuild once more now that every callback exists -- otherwise the
    // loaded preset's tile opens unhighlighted.
    dialog->refresh();
    overlayHost.pushOverlay (std::move (dialog));
}

void MainComponent::closePresetCardsAndReopenList()
{
    // Pops the card(s) on top AND the list under them, then shows a fresh list -- the simplest way to reflect a
    // delete/rename/duplicate. The list is the bottom of this stack: presets are only reached from the badge.
    while (! overlayHost.isEmpty())
        overlayHost.popOverlay();
    showPresetsPanel();
}

void MainComponent::loadPresetAskingToSave (const juce::String& name, std::function<void()> onLoaded)
{
    if (! isPresetDirty())
    {
        loadPresetByName (name);
        if (onLoaded)
            onLoaded();
        return;
    }

    overlayHost.pushOverlay (std::make_unique<ChoiceCard> (
        "Unsaved changes", "\"" + currentPresetName + "\" has changes that aren't saved.",
        juce::StringArray { "Save", "Discard", "Cancel" },
        [this, name, onLoaded = std::move (onLoaded)] (int choice)
        {
            overlayHost.popOverlay();
            if (choice == 2)
                return;
            if (choice == 0)
                savePresetAs (currentPresetName);
            loadPresetByName (name);
            if (onLoaded)
                onLoaded();
        }, 2));
}

void MainComponent::newPreset()
{
    // Back to the state the app launches in: an empty <Preset/> runs the same
    // teardown path a preset load does -- the chain retires through the
    // graveyard/spillover discipline, snapshots and MIDI bindings reset.
    applyPresetXml (juce::XmlElement ("Preset"));
    // ...except routing: applyPresetXml leaves every row unfed (RowRouting{})
    // when the XML has no <Routing>, while a fresh board wires row 0
    // device-in -> device-out like the constructor does.
    rowRouting[0].inputChannel = audioEngine.getInputChannel();
    rowRouting[0].toDevice = true;
    rowRouting[0].deviceOutputPair = audioEngine.getOutputChannelPair();
    refreshRowEndpoints();
    // applyPresetXml already rebuilt the graph on the empty routing -- rebuild
    // again or the fresh board's row-0 device in->out link stays UI-only and
    // the board passes silence until some other action rebuilds it.
    rebuildSignalGraph();
    currentPresetName.clear();
    currentPresetNumber = 0;
    // Empty name -> capturePresetBaseline() stores nullptr, so an unnamed
    // chain is never "dirty" -- exactly like the launch state.
    capturePresetBaseline();
    updatePresetDisplay();
}

void MainComponent::newPresetAskingToSave (std::function<void()> onReset)
{
    if (! isPresetDirty())
    {
        newPreset();
        if (onReset)
            onReset();
        return;
    }

    overlayHost.pushOverlay (std::make_unique<ChoiceCard> (
        "Unsaved changes", "\"" + currentPresetName + "\" has changes that aren't saved.",
        juce::StringArray { "Save", "Discard", "Cancel" },
        [this, onReset = std::move (onReset)] (int choice)
        {
            overlayHost.popOverlay();
            if (choice == 2)
                return;
            if (choice == 0)
                savePresetAs (currentPresetName);
            newPreset();
            if (onReset)
                onReset();
        }, 2));
}

void MainComponent::capturePresetBaseline()
{
    presetBaseline = currentPresetName.isNotEmpty() ? buildPresetXml() : nullptr;
    presetDirty = false;
}

bool MainComponent::isPresetDirty() const
{
    return presetBaseline != nullptr
           && ! dirtyCompareForm (*buildPresetXml())->isEquivalentTo (dirtyCompareForm (*presetBaseline).get(), false);
}

void MainComponent::loadPresetByName (const juce::String& name)
{
    if (auto xml = presets.loadPreset (name))
    {
        applyPresetXml (*xml);
        currentPresetName = name;
        currentPresetNumber = xml->getIntAttribute ("number", 0);
        capturePresetBaseline();
        updatePresetDisplay();
    }
}

void MainComponent::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    // Both branches below hop to the message thread via callAsync before
    // touching anything -- this callback runs on JUCE's own MIDI thread,
    // and both loadPresetByName() (file I/O, chain/UI mutation) and
    // handleMidiCc() (parameter/binding-map mutation) need the message
    // thread. `alive` guards against either firing after MainComponent has
    // already been destroyed.
    if (message.isProgramChange())
    {
        // MIDI Program Change is 0-127; preset numbers start at 1 (see
        // PresetManager::nextAvailableNumber()'s "1 if there are none
        // yet"), so PC 0 maps to preset 1 -- "Patch 1" on a foot
        // controller landing on the first preset is the intuitive
        // mapping, not an off-by-one.
        const int presetNumber = message.getProgramChangeNumber() + 1;

        juce::MessageManager::callAsync ([this, presetNumber, alive = aliveFlag]
        {
            if (! alive->load())
                return;

            const auto name = presets.nameForNumber (presetNumber);
            if (name.isNotEmpty())
                loadPresetByName (name);
        });
    }
    else if (message.isController())
    {
        const int ccNumber = message.getControllerNumber();
        const int ccValue = message.getControllerValue();

        juce::MessageManager::callAsync ([this, ccNumber, ccValue, alive = aliveFlag]
        {
            if (alive->load())
                handleMidiCc (ccNumber, ccValue);
        });
    }
}

void MainComponent::selectSnapshot (int index)
{
    index = juce::jlimit (0, snapshotxml::maxSnapshots - 1, index);
    if (index == currentSnapshot)
        return;

    // Recall semantics (docs/PresetsAndSnapshots.md): edits made in the
    // outgoing snapshot stay stored in it -- write the live state back
    // BEFORE loading the incoming one, so a knob tweak survives the
    // round-trip away and back even if the preset is never saved.
    storeCurrentSnapshot();

    // Params + bypass flags only, written into the SAME processors -- the
    // graph is never rebuilt, so delay/reverb tails keep ringing through
    // the switch and every enable/disable rides processWithBypass()'s
    // crossfade + trails exactly like a hand-toggled bypass.
    const auto& target = snapshots[(size_t) index];
    for (int i = 0; i < (int) chain.size() && i < (int) target.blocks.size(); ++i)
        snapshotxml::applyBlock (*chain[(size_t) i], target.blocks[(size_t) i]);

    currentSnapshot = index;

    // A pure snapshot switch isn't an unsaved preset change, and none is
    // flagged: isPresetDirty()'s compare form excludes exactly the parts
    // a switch moves (`current` + the live <Block> mirror), while real
    // edits still differ inside <Snapshots> -- see dirtyCompareForm().
    updatePresetDisplay();
}

void MainComponent::showSnapshotMenu (int index)
{
    index = juce::jlimit (0, snapshotxml::maxSnapshots - 1, index);
    const auto label = [this] (int i)
    {
        const auto& name = snapshots[(size_t) i].name;
        return snapshotxml::letterFor (i) + (name.isNotEmpty() ? " - " + name : juce::String());
    };

    juce::PopupMenu colours;
    colours.addItem (100, "Default", true, snapshots[(size_t) index].colour.isEmpty());
    static const char* colourNames[] = { "Green", "Blue", "Orange", "Purple", "Yellow", "Red", "Teal", "Magenta" }; // defaultColourFor()'s order
    for (int c = 0; c < snapshotxml::maxSnapshots; ++c)
    {
        const auto colour = snapshotxml::defaultColourFor (c);
        colours.addColouredItem (101 + c, colourNames[c], colour, true, snapshots[(size_t) index].colour == colour.toString());
    }
    // Any colour, not just the eight defaults ("seria legal dar pra
    // personalizar qual cor de cada snapshot"): a ColourSelector card like
    // every other popup in the app.
    colours.addItem (150, "Custom colour...");

    juce::PopupMenu copyTo;
    for (int t = 0; t < snapshotxml::maxSnapshots; ++t)
        if (t != index)
            copyTo.addItem (200 + t, label (t));

    juce::PopupMenu menu;
    menu.addSectionHeader ("Snapshot " + label (index));
    menu.addItem (1, "Rename...");
    menu.addSubMenu ("Colour", colours);
    menu.addSubMenu ("Copy to", copyTo);

    // Footswitch -> scene. The pedal sends one fixed CC per switch, so each
    // action learns its own (CC 69 with a value per scene still works for
    // controllers that can send one).
    const auto pedalItem = [this] (juce::PopupMenu& m, int id, int action, const juce::String& what)
    {
        const int cc = sceneMidi.ccFor (action);
        m.addItem (id, cc >= 0 ? "Clear pedal: " + what + " (CC " + juce::String (cc) + ")"
                               : "Learn pedal: " + what,
                   true, sceneMidiArmed == action);
    };
    juce::PopupMenu pedal;
    pedalItem (pedal, 300, index, "this scene (" + snapshotxml::letterFor (index) + ")");
    pedalItem (pedal, 301, scenemidi::nextScene, "next scene");
    pedalItem (pedal, 302, scenemidi::previousScene, "previous scene");
    if (sceneMidiArmed != scenemidi::noAction)
        pedal.addItem (303, "Cancel learning");
    menu.addSubMenu ("Footswitch (MIDI)", pedal);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&snapshotBar)
                                                   .withStandardItemHeight (touch::minTapTarget),
                        [this, index, alive = aliveFlag] (int result)
    {
        if (! alive->load() || result == 0)
            return;

        if (result == 1)
        {
            overlayHost.pushOverlay (std::make_unique<TextPromptCard> (
                "Rename snapshot " + snapshotxml::letterFor (index), snapshots[(size_t) index].name,
                [this, index] (juce::String newName)
                {
                    snapshots[(size_t) index].name = newName;
                    overlayHost.popOverlay();
                    updatePresetDisplay();
                },
                [this] { overlayHost.popOverlay(); }));
        }
        else if (result == 150)
        {
            overlayHost.pushOverlay (std::make_unique<ColourPickerCard> (
                "Colour for snapshot " + snapshotxml::letterFor (index), colourOfSnapshot (index),
                [this, index] (juce::Colour colour)
                {
                    snapshots[(size_t) index].colour = colour.toString();
                    overlayHost.popOverlay();
                    updatePresetDisplay();
                },
                [this] { overlayHost.popOverlay(); }));
        }
        else if (result >= 100 && result < 100 + 1 + snapshotxml::maxSnapshots)
        {
            snapshots[(size_t) index].colour = result == 100 ? juce::String()
                                                             : snapshotxml::defaultColourFor (result - 101).toString();
            updatePresetDisplay();
        }
        else if (result >= 200 && result < 200 + snapshotxml::maxSnapshots)
        {
            copySnapshot (index, result - 200);
        }
        else if (result == 300)
            toggleScenePedal (index);
        else if (result == 301)
            toggleScenePedal (scenemidi::nextScene);
        else if (result == 302)
            toggleScenePedal (scenemidi::previousScene);
        else if (result == 303 && sceneMidiArmed != scenemidi::noAction)
            toggleScenePedal (sceneMidiArmed);
    });
}

void MainComponent::toggleScenePedal (int action)
{
    if (sceneMidi.ccFor (action) >= 0)
    {
        sceneMidi.clearAction (action);
        scenemidi::save (sceneMidi);
        sceneMidiArmed = scenemidi::noAction;
    }
    else if (sceneMidiArmed == action)
    {
        sceneMidiArmed = scenemidi::noAction;
    }
    else
    {
        sceneMidiArmed = action;
        midiLearnArmedParam = nullptr; // one learn at a time
        bypassPedalArmed = nullptr;
    }

    refreshScenePedalDisplay();
}

void MainComponent::refreshScenePedalDisplay()
{
    std::array<int, snapshotxml::maxSnapshots> ccs;
    for (int i = 0; i < snapshotxml::maxSnapshots; ++i)
        ccs[(size_t) i] = sceneMidi.ccFor (i);
    snapshotBar.setPedalCcs (ccs);

    juce::String caption;
    if (sceneMidiArmed != scenemidi::noAction)
    {
        const auto what = sceneMidiArmed == scenemidi::nextScene     ? juce::String ("NEXT scene")
                        : sceneMidiArmed == scenemidi::previousScene ? juce::String ("PREVIOUS scene")
                                                                     : "scene " + snapshotxml::letterFor (sceneMidiArmed);
        caption = "press the footswitch for " + what + " ...";
    }
    snapshotBar.setCaptionOverride (caption);
}

void MainComponent::freeCcForLearning (int cc)
{
    midiCcBindings.erase (cc);
    bypassPedals.erase (cc);
    if (sceneMidi.hasCc (cc))
    {
        sceneMidi.clearCc (cc);
        scenemidi::save (sceneMidi);
        refreshScenePedalDisplay();
    }
}

void MainComponent::showBypassPedalMenu (EffectProcessor* processor)
{
    int boundCc = -1;
    for (auto& [cc, bound] : bypassPedals)
        if (bound == processor)
            boundCc = cc;

    juce::PopupMenu menu;
    menu.addSectionHeader (processor->getName());
    menu.addItem (1, boundCc >= 0 ? "Clear pedal (CC " + juce::String (boundCc) + ")" : "Learn pedal (on/off)",
                  true, bypassPedalArmed == processor);
    if (bypassPedalArmed != nullptr && bypassPedalArmed != processor)
        menu.addItem (2, "Cancel learning");

    juce::Component* target = nullptr;
    for (auto* block : blocks)
        if (&block->processor == processor)
            target = block;

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target)
                                                   .withStandardItemHeight (touch::minTapTarget),
                        [this, processor, boundCc, alive = aliveFlag] (int result)
    {
        if (! alive->load() || result == 0)
            return;

        // The processor can be removed while the menu is open; only act if it is still in the chain.
        const bool stillThere = std::any_of (chain.begin(), chain.end(),
                                             [processor] (const auto& p) { return p.get() == processor; });
        if (! stillThere)
            return;

        if (result == 2)
        {
            bypassPedalArmed = nullptr;
        }
        else if (boundCc >= 0)
        {
            bypassPedals.erase (boundCc);
            bypassPedalArmed = nullptr;
        }
        else if (bypassPedalArmed == processor)
        {
            bypassPedalArmed = nullptr;
        }
        else
        {
            bypassPedalArmed = processor;
            midiLearnArmedParam = nullptr; // one learn at a time
            if (sceneMidiArmed != scenemidi::noAction)
            {
                sceneMidiArmed = scenemidi::noAction;
                refreshScenePedalDisplay();
            }
        }
    });
}

void MainComponent::toggleBypassPedal (EffectProcessor* processor)
{
    // Never touch a pointer that has left the chain (removed/replaced blocks are dropped from the map, this is the
    // belt to that pair of braces).
    for (auto* block : blocks)
        if (&block->processor == processor)
        {
            processor->setBypassed (! processor->isBypassed());
            block->repaint();
            return;
        }
}

void MainComponent::refreshBypassPedalDisplay()
{
    for (auto* block : blocks)
    {
        int cc = -1;
        for (auto& [boundCc, bound] : bypassPedals)
            if (bound == &block->processor)
                cc = boundCc;
        block->setPedal (cc, bypassPedalArmed == &block->processor);
    }
}

juce::Colour MainComponent::colourOfSnapshot (int index) const
{
    const auto& hex = snapshots[(size_t) index].colour;
    return hex.isNotEmpty() ? juce::Colour::fromString (hex) : snapshotxml::defaultColourFor (index);
}

void MainComponent::applySnapshotAccent()
{
    const auto colour = colourOfSnapshot (currentSnapshot);

    // The app's primary colour tracks the live snapshot ("a cor principal
    // tbm pode estar atrelada ao snapshot q estarmos"): knob fill, scrollbar
    // thumbs, button borders that have no per-effect override, popup
    // highlight, meter fill -- every accent read happens inside paint() or
    // findColour(), so one repaint is all it takes to switch themes.
    if (auto* laf = dynamic_cast<OpenGuitarMultiFxLookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()))
        laf->setAppAccentColour (colour);

    const auto& name = snapshots[(size_t) currentSnapshot].name;
    footerBar.setActiveSnapshot (snapshotxml::letterFor (currentSnapshot)
                                     + (name.isNotEmpty() ? juce::String (" ") + juce::String::fromUTF8 ("\xc2\xb7") + " " + name
                                                          : juce::String()),
                                 colour);

    repaint();
}

bool MainComponent::snapshotVaries (EffectProcessor* processor, juce::AudioParameterFloat* param) const
{
    int block = -1;
    for (int i = 0; i < (int) chain.size(); ++i)
        if (chain[(size_t) i].get() == processor)
            block = i;
    if (block < 0 || param == nullptr)
        return false;

    // The live value stands in for the current snapshot (it may hold an edit not stored yet -- Recall).
    const float live = param->get();
    for (int s = 0; s < snapshotxml::maxSnapshots; ++s)
    {
        if (s == currentSnapshot || block >= (int) snapshots[(size_t) s].blocks.size())
            continue;
        for (auto& [id, value] : snapshots[(size_t) s].blocks[(size_t) block].params)
            if (id == param->paramID && std::abs (value - live) > 1.0e-4f * juce::jmax (1.0f, std::abs (live)))
                return true;
    }
    return false;
}

void MainComponent::copySnapshot (int from, int to)
{
    if (from == to)
        return;

    if (from == currentSnapshot)
        storeCurrentSnapshot(); // the live chain IS the source's latest state (Recall)

    snapshots[(size_t) to].blocks = snapshots[(size_t) from].blocks;

    // Copying onto the live snapshot: make the chain sound like it now, through the same gapless path as a recall.
    if (to == currentSnapshot)
        for (int i = 0; i < (int) chain.size() && i < (int) snapshots[(size_t) to].blocks.size(); ++i)
            snapshotxml::applyBlock (*chain[(size_t) i], snapshots[(size_t) to].blocks[(size_t) i]);

    updatePresetDisplay();
}

void MainComponent::storeCurrentSnapshot()
{
    auto& snapshot = snapshots[(size_t) currentSnapshot];
    snapshot.blocks.resize (chain.size());
    for (int i = 0; i < (int) chain.size(); ++i)
        snapshot.blocks[(size_t) i] = snapshotxml::captureBlock (*chain[(size_t) i]);
}

void MainComponent::normaliseSnapshots()
{
    for (auto& snapshot : snapshots)
    {
        const int stored = (int) snapshot.blocks.size();
        snapshot.blocks.resize (chain.size());
        // Slots the XML never stored (an old preset, or blocks added since)
        // get the live state as their default -- switching onto them then
        // changes nothing, which is exactly what "no snapshot data" means.
        for (int i = stored; i < (int) chain.size(); ++i)
            snapshot.blocks[(size_t) i] = snapshotxml::captureBlock (*chain[(size_t) i]);
    }
}

void MainComponent::handleMidiCc (int ccNumber, int ccValue)
{
    // CC 69 is reserved for snapshot selection (docs/PresetsAndSnapshots.md
    // -- the Line 6 convention: value 0-7 = A-H). It always switches
    // snapshots, is never learnable onto a parameter, and ignores
    // out-of-range values.
    if (ccNumber == snapshotxml::snapshotSelectCcNumber)
    {
        const int index = snapshotxml::indexForCcValue (ccValue);
        if (index >= 0)
            selectSnapshot (index);
        return;
    }

    // A scene armed for pedal learning takes the next stomp. Only a press is
    // learned: the footswitch's release (value 0) arrives right after and must
    // not bind a second, useless CC.
    if (sceneMidiArmed != scenemidi::noAction)
    {
        if (ccValue >= scenemidi::pressThreshold)
        {
            freeCcForLearning (ccNumber);
            if (sceneMidi.bind (ccNumber, sceneMidiArmed))
            {
                sceneMidi.isPress (ccNumber, ccValue, juce::Time::getMillisecondCounter()); // the learning stomp must not also switch scene
                scenemidi::save (sceneMidi);
            }
            sceneMidiArmed = scenemidi::noAction;
            refreshScenePedalDisplay();
        }
        return;
    }

    // Same for a block armed for its on/off pedal.
    if (bypassPedalArmed != nullptr)
    {
        if (ccValue >= scenemidi::pressThreshold)
        {
            freeCcForLearning (ccNumber);
            for (auto it = bypassPedals.begin(); it != bypassPedals.end();)
                it = it->second == bypassPedalArmed ? bypassPedals.erase (it) : std::next (it);
            bypassPedals[ccNumber] = bypassPedalArmed;
            bypassStomps.isPress (ccNumber, ccValue, juce::Time::getMillisecondCounter()); // the learning stomp must not also toggle
            bypassPedalArmed = nullptr;
        }
        return;
    }

    // A knob armed for MIDI Learn takes priority over an existing binding
    // on the same CC -- the next CC message received while armed is what
    // gets learned, even if that CC number was already bound to something
    // else (re-learning a CC onto a new parameter implicitly un-learns it
    // from the old one, since a CC can only ever drive one parameter).
    if (midiLearnArmedParam != nullptr)
    {
        freeCcForLearning (ccNumber);

        for (auto it = midiCcBindings.begin(); it != midiCcBindings.end(); )
            it = it->second == midiLearnArmedParam ? midiCcBindings.erase (it) : std::next (it);

        midiCcBindings[ccNumber] = midiLearnArmedParam;
        midiLearnArmedParam = nullptr;
        return;
    }

    if (const int scene = sceneMidi.targetFor (ccNumber, ccValue, currentSnapshot); scene >= 0)
    {
        selectSnapshot (scene);
        return;
    }
    if (sceneMidi.hasCc (ccNumber))
        return; // a scene pedal's release/repeat never drives a knob

    if (const auto pedal = bypassPedals.find (ccNumber); pedal != bypassPedals.end())
    {
        if (bypassStomps.isPress (ccNumber, ccValue, juce::Time::getMillisecondCounter()))
            toggleBypassPedal (pedal->second);
        return; // likewise for an on/off pedal
    }

    const auto it = midiCcBindings.find (ccNumber);
    if (it == midiCcBindings.end())
        return;

    // CC values are 0-127; convert through the parameter's own
    // NormalisableRange (not a plain linear map) so a log-skewed
    // parameter like a delay Time knob still sweeps musically under CC
    // control instead of spending most of the CC's range on one extreme.
    auto* param = it->second;
    const float normalised = (float) ccValue / 127.0f;
    *param = param->getNormalisableRange().convertFrom0to1 (normalised);
}

void MainComponent::clearMidiBindingsFor (EffectProcessor* processor)
{
    if (processor == nullptr)
        return;

    if (bypassPedalArmed == processor)
        bypassPedalArmed = nullptr;
    for (auto it = bypassPedals.begin(); it != bypassPedals.end();)
        it = it->second == processor ? bypassPedals.erase (it) : std::next (it);

    auto* group = processor->getParameters();
    if (group == nullptr)
        return;

    for (auto* p : group->getParameters (true))
    {
        auto* floatParam = dynamic_cast<juce::AudioParameterFloat*> (p);
        if (floatParam == nullptr)
            continue;

        if (midiLearnArmedParam == floatParam)
            midiLearnArmedParam = nullptr;

        for (auto it = midiCcBindings.begin(); it != midiCcBindings.end(); )
            it = it->second == floatParam ? midiCcBindings.erase (it) : std::next (it);
    }
}

void MainComponent::savePresetAs (const juce::String& name)
{
    // Reuse the existing number on a resave (same name = same slot, not a
    // new one) -- see PresetManager::numberForExistingPreset()'s comment.
    const int existingNumber = presets.numberForExistingPreset (name);
    const int number = existingNumber > 0 ? existingNumber : presets.nextAvailableNumber();

    auto xml = buildPresetXml();
    xml->setAttribute ("number", number);
    // buildPresetXml() only captures the sound -- carry the organisational
    // metadata (tags, bank slot, setlists) forward from the file being
    // overwritten, or a resave silently drops everything the browser set up.
    // Check presetExists, not the number: an unnumbered preset file (e.g.
    // one saved before numbering existed) still holds metadata worth keeping.
    if (presets.presetExists (name))
        if (auto existing = presets.loadPreset (name))
            presetmeta::copyOrganisation (*existing, *xml);
    presets.savePreset (name, *xml);

    currentPresetName = name;
    currentPresetNumber = number;
    capturePresetBaseline();
    updatePresetDisplay();
}

void MainComponent::updatePresetDisplay()
{
    presetBadge.setNumber (currentPresetNumber);
    presetBadge.setSnapshotIndex (currentSnapshot);
    snapshotBar.setCurrent (currentSnapshot);

    std::array<juce::String, snapshotxml::maxSnapshots> names;
    std::array<juce::Colour, snapshotxml::maxSnapshots> colours;
    for (int i = 0; i < snapshotxml::maxSnapshots; ++i)
    {
        names[(size_t) i] = snapshots[(size_t) i].name;
        colours[(size_t) i] = snapshots[(size_t) i].colour.isNotEmpty() ? juce::Colour::fromString (snapshots[(size_t) i].colour)
                                                                       : juce::Colours::transparentBlack;
    }
    snapshotBar.setLabels (names, colours);

    // "Lead Show · Solo": the live snapshot's name, when it has one, so the player can read which part is on.
    const auto& snapshotName = snapshots[(size_t) currentSnapshot].name;
    titleLabel.setText (currentPresetName.isNotEmpty()
                            ? currentPresetName + (snapshotName.isNotEmpty() ? juce::String::fromUTF8 (" \xc2\xb7 ") + snapshotName : juce::String())
                                  + (presetDirty ? " *" : "")
                            : "No preset loaded",
                         juce::dontSendNotification);
    quickSaveButton.setVisible (currentPresetName.isNotEmpty());
    applySnapshotAccent();
    resized(); // quickSaveButton's visibility changes how much room titleLabel gets
}

std::unique_ptr<juce::XmlElement> MainComponent::buildPresetXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("Preset");
    midilearnxml::BlockGroups savedBlockGroups; // one per <Block> written, in order -- what a binding's "block" indexes
    bypasspedal::Blocks savedBlocks;

    for (int i = 0; i < (int) chain.size(); ++i)
    {
        auto& p = chain[(size_t) i];
        const auto key = registry.keyForDisplayName (p->getName());
        if (key.isEmpty())
            continue; // shouldn't happen -- every chain block was created via the registry

        auto* blockXml = xml->createNewChildElement ("Block");
        blockXml->setAttribute ("key", key);
        blockXml->setAttribute ("bypassed", p->isBypassed());
        // `blocks[i]` is `chain[i]`'s UI counterpart -- always true, they're
        // kept in lockstep everywhere (see AGENTS.md). gridSlot is its grid
        // cell, which can be sparse now (not necessarily == i) -- see
        // EffectBlockComponent::gridSlot's comment.
        blockXml->setAttribute ("gridSlot", blocks[i]->gridSlot);
        blockXml->addChildElement (p->getState().release());
        savedBlockGroups.push_back (p->getParameters());
        savedBlocks.push_back (p.get());
    }

    midilearnxml::write (*xml, midiCcBindings, savedBlockGroups);
    bypasspedal::write (*xml, bypassPedals, savedBlocks);

    // Snapshots (docs/PresetsAndSnapshots.md, stage 3): the current slot's
    // stored copy is refreshed from the LIVE chain here -- a knob edit only
    // lives in the current snapshot until a switch or a save, so capturing
    // it inline is what lets a save persist the latest edits AND keeps
    // isPresetDirty() seeing them against the baseline.
    auto snapshotsToWrite = snapshots;
    auto& currentToWrite = snapshotsToWrite[(size_t) currentSnapshot];
    currentToWrite.blocks.resize (chain.size());
    for (int i = 0; i < (int) chain.size(); ++i)
        currentToWrite.blocks[(size_t) i] = snapshotxml::captureBlock (*chain[(size_t) i]);
    snapshotxml::write (*xml, snapshotsToWrite, currentSnapshot);

    // The FULL per-row routing (device I/O, splits, merges), not just a
    // single global in/out pair -- see the class-level bug this fixes:
    // presets used to only restore row 0's device input/output, silently
    // discarding every other row's routing entirely, INCLUDING the split/
    // merge configurations Phase 5's SignalGraph rewrite exists to run
    // (see Source/Engine/AGENTS.md). A saved 3-row merge preset used to
    // reload as three disconnected, silent rows.
    auto* routingXml = xml->createNewChildElement ("Routing");
    for (int row = 0; row < numRows; ++row)
    {
        const auto& r = rowRouting[(size_t) row];
        auto* rowXml = routingXml->createNewChildElement ("Row");
        rowXml->setAttribute ("index", row);
        rowXml->setAttribute ("inputChannel", r.inputChannel);
        rowXml->setAttribute ("toDevice", r.toDevice);
        rowXml->setAttribute ("deviceOutputPair", r.deviceOutputPair);

        int toRowsMask = 0;
        for (int target = 0; target < numRows; ++target)
            if (r.toRows[(size_t) target])
                toRowsMask |= (1 << target);
        rowXml->setAttribute ("toRowsMask", toRowsMask);
    }

    return xml;
}

void MainComponent::applyPresetXml (const juce::XmlElement& xml)
{
    // Preset spillover (docs/PresetsAndSnapshots.md, stage 5): capture the
    // outgoing chain's processors per lane and its routing BEFORE tearing
    // down -- the trail-capable ones get a Spillover::Unit below and keep
    // ringing on silence, the rest die with the switch as always.
    const auto outgoingRouting = rowRouting;
    std::array<Spillover::LaneSpec, SignalGraph::numLanes> spillLanes;
    std::vector<std::pair<int, int>> spillConnections;
    const auto graveyardMark = graveyard.size();

    // Same lane grouping rebuildSignalGraph() uses: blocks are sorted by
    // gridSlot and row == gridSlot / chainColumns, so an out-of-range slot
    // simply lands on no lane.
    for (auto* block : blocks)
    {
        const int row = block->gridSlot / chainColumns;
        if (row >= 0 && row < numRows)
            spillLanes[(size_t) row].processors.push_back (&block->processor);
    }

    for (int lane = 0; lane < numRows; ++lane)
    {
        spillLanes[(size_t) lane].writesDeviceOutput = outgoingRouting[(size_t) lane].toDevice;
        for (int target = lane + 1; target < numRows; ++target)
            if (outgoingRouting[(size_t) lane].toRows[(size_t) target])
                spillConnections.push_back ({ lane, target });
    }

    // Tear the current chain down the same way removeEffect() would, one
    // block at a time, so nothing bypasses the graveyard/DeferredReclaimer
    // discipline (rebuildSignalGraph()/layoutChain() re-run every
    // iteration -- wasteful but this only ever happens from a menu tap,
    // never the audio thread).
    while (! blocks.isEmpty())
        removeEffect (&blocks[0]->processor);

    // The teardown retired every outgoing processor into the graveyard,
    // which frees them ~600 ms from now -- nowhere near a reverb tail's
    // lifetime. The trail-capable ones move on into a spill unit (handed
    // to the audio engine, which rings them out on silence in parallel
    // with the new chain); the rest go back to the normal graveyard
    // lifecycle -- retired graphs from the teardown may still point at
    // them for up to the DeferredReclaimer margin.
    std::vector<std::unique_ptr<EffectProcessor>> outgoing;
    for (auto it = graveyard.begin() + (std::ptrdiff_t) graveyardMark; it != graveyard.end(); ++it)
        outgoing.push_back (std::move (it->processor));

    if (auto unit = Spillover::build (spillLanes, spillConnections, outgoing))
        audioEngine.addSpillover (std::move (unit));

    for (auto& processor : outgoing)
        if (processor != nullptr)
            graveyard.push_back ({ std::move (processor), juce::Time::getMillisecondCounter() });

    midilearnxml::BlockGroups loadedBlockGroups; // one per <Block> element, nullptr where it was skipped
    bypasspedal::Blocks loadedBlocks;
    bypassPedalArmed = nullptr; // the blocks it pointed at are going away

    for (int i = 0; i < xml.getNumChildElements(); ++i)
    {
        auto* blockXml = xml.getChildElement (i);
        if (blockXml->getTagName() != "Block")
            continue;

        auto processor = registry.create (blockXml->getStringAttribute ("key"));
        loadedBlockGroups.push_back (processor != nullptr ? processor->getParameters() : nullptr);
        loadedBlocks.push_back (processor.get());
        if (processor == nullptr)
            continue; // an unknown key (e.g. a preset from a future build) -- skip, don't fail the whole load

        if (auto* stateXml = blockXml->getChildByName ("EffectState"))
            processor->setState (*stateXml);
        processor->setBypassed (blockXml->getBoolAttribute ("bypassed", false));

        auto* raw = processor.get();
        chain.push_back (std::move (processor));

        auto block = std::make_unique<EffectBlockComponent> (*raw);
        // Falls back to `blocks.size()` (this block's about-to-be array
        // index) for presets saved before gridSlot existed -- same
        // contiguous layout they always had.
        block->gridSlot = blockXml->getIntAttribute ("gridSlot", blocks.size());
        block->onClicked = [this, raw] { selectBlock (selectedProcessor == raw ? nullptr : raw); };
        block->onDragEnded = [this] (EffectBlockComponent& b) { handleBlockDragEnded (b); };
    block->onLongPressed = [this, raw] { showBypassPedalMenu (raw); };
        chainContainer.addAndMakeVisible (*block);
        blocks.add (block.release());
    }

    for (auto& r : rowRouting)
        r = RowRouting {};

    if (auto* routingXml = xml.getChildByName ("Routing"))
    {
        // Current format: every row's full routing, splits/merges included.
        for (auto* rowXml : routingXml->getChildIterator())
        {
            const int row = rowXml->getIntAttribute ("index", -1);
            if (row < 0 || row >= numRows)
                continue; // a future build with more rows than this one understands -- skip, don't fail the load

            auto& r = rowRouting[(size_t) row];
            r.inputChannel = rowXml->getIntAttribute ("inputChannel", -1);
            r.toDevice = rowXml->getBoolAttribute ("toDevice", false);
            r.deviceOutputPair = rowXml->getIntAttribute ("deviceOutputPair", 0);

            const int toRowsMask = rowXml->getIntAttribute ("toRowsMask", 0);
            for (int target = 0; target < numRows; ++target)
                r.toRows[(size_t) target] = (toRowsMask & (1 << target)) != 0;

            if (r.inputChannel >= 0)
                audioEngine.setInputChannel (r.inputChannel);
            if (r.toDevice)
                audioEngine.setOutputChannelPair (r.deviceOutputPair);
        }
    }
    else if (auto* ioXml = xml.getChildByName ("IO"))
    {
        // Older preset format (saved before per-row routing existed): just
        // row 0 wired straight device-in to device-out, same behaviour
        // those presets always had.
        const int inCh = ioXml->getIntAttribute ("inputChannel", 0);
        const int outPair = ioXml->getIntAttribute ("outputPairStart", 0);
        audioEngine.setInputChannel (inCh);
        audioEngine.setOutputChannelPair (outPair);
        rowRouting[0].inputChannel = inCh;
        rowRouting[0].toDevice = true;
        rowRouting[0].deviceOutputPair = outPair;
    }

    // Per-preset, like a hardware pedalboard's controller assignments: a preset saved without any leaves none active.
    midiCcBindings = midilearnxml::read (xml, loadedBlockGroups);
    bypassPedals = bypasspedal::read (xml, loadedBlocks);

    // Snapshots: read them if the file has them (current = whichever was
    // live at save time), else default every slot to the just-loaded state
    // -- a preset saved before this feature loads identically to how it
    // always did, as a single-state preset.
    int savedCurrent = 0;
    if (! snapshotxml::read (xml, snapshots, savedCurrent))
        snapshots = {};

    // Snapshot block entries are indexed by the SAVED <Block> order, so
    // any blocks this build couldn't create (unknown keys skipped above,
    // nullptr in loadedBlockGroups) must be dropped from every snapshot
    // first -- otherwise each entry after a skipped block would apply its
    // bypass/params to the wrong live block.
    std::vector<size_t> loadedSavedIndices;
    for (size_t i = 0; i < loadedBlockGroups.size(); ++i)
        if (loadedBlockGroups[i] != nullptr)
            loadedSavedIndices.push_back (i);

    for (auto& snapshot : snapshots)
    {
        std::vector<snapshotxml::BlockSnapshot> aligned;
        aligned.reserve (loadedSavedIndices.size());
        for (const size_t saved : loadedSavedIndices)
        {
            if (saved < snapshot.blocks.size())
            {
                aligned.push_back (std::move (snapshot.blocks[saved]));
            }
            else
            {
                // The XML never stored this position -- an old preset with
                // no <Snapshots> at all (every entry lands here) or a slot
                // saved with fewer blocks. Seed it from the live chain:
                // the "single-state" default where switching onto it
                // changes nothing.
                aligned.push_back (snapshotxml::captureBlock (*chain[aligned.size()]));
            }
        }
        snapshot.blocks = std::move (aligned);
    }

    normaliseSnapshots();
    currentSnapshot = savedCurrent;

    refreshRowEndpoints();
    rebuildSignalGraph();
    layoutChain();
}

void MainComponent::timerCallback()
{
    refreshBypassPedalDisplay();

    // Re-derive device liveness every tick rather than latching it from
    // start()/rate-switch results -- a mid-session unplug, a start() that
    // returned true with no device left open, or a failed rate switch that
    // then recovered all reduce to the same question: is a device open now.
    const bool deviceOk = audioEngine.getDeviceManager().getCurrentAudioDevice() != nullptr;
    if (deviceOk != audioDeviceOk)
    {
        audioDeviceOk = deviceOk;
        cpuLabel.setColour (juce::Label::textColourId, audioDeviceOk ? juce::Colours::white : juce::Colours::orange);
        retryAudioButton.setVisible (! audioDeviceOk); // Retry only exists while there's nothing to retry against
        resized();
    }

    // Average and worst-block, not one number: see AudioEngine::getPeakCpuUsage().
    // roundToInt, not String(double, 0): JUCE reads 0 decimal places as "shortest representation" and prints every digit.
    if (audioDeviceOk)
        cpuLabel.setText ("CPU " + juce::String (juce::roundToInt (audioEngine.getCurrentCpuUsage() * 100.0)) + "%  peak "
                              + juce::String (juce::roundToInt (audioEngine.getPeakCpuUsage() * 100.0)) + "%",
                          juce::dontSendNotification);
    else
        cpuLabel.setText ("NO AUDIO DEVICE", juce::dontSendNotification);

    footerBar.setLevels (audioEngine.getInputLevel(), audioEngine.getOutputLevel());

    // BPM-synced time parameters (ms/BPM-subdivision toggle -- see
    // EffectProcessor::updateTempoSyncedParams()) recomputed here, at the
    // same 50ms rate as everything else on this timer, whether or not
    // tap-tempo just changed -- cheap (a no-op unless a processor actually
    // has a synced binding) and keeps every synced knob tracking BPM live.
    const auto bpm = footerBar.getBpm();
    for (auto* block : blocks)
        block->processor.updateTempoSyncedParams (bpm);

    const auto now = juce::Time::getMillisecondCounter();
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                      [now] (const RetiredProcessor& r) { return now - r.retiredAtMs >= 600; }),
                      graveyard.end());

    parameterPanel.refresh();

    // Debounced master-bus save: knob drags and node drags fire dozens of edits a second -- one write, ~500 ms
    // after the last change, is plenty for a file read once per launch.
    if (masterBusSaveCountdown > 0 && --masterBusSaveCountdown == 0)
        masterbus::saveState ({ audioEngine.getMasterGainDb(), audioEngine.getMasterEqBands() });

    if (--ticksUntilDirtyCheck <= 0)
    {
        ticksUntilDirtyCheck = dirtyCheckIntervalTicks;
        if (const bool dirty = isPresetDirty(); dirty != presetDirty)
        {
            presetDirty = dirty;
            updatePresetDisplay();
        }
    }
}

void MainComponent::GearButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    // Same chrome as every other button (fill + accent border), then the
    // cog mark on top -- a ring, a hub, and 8 short teeth rotated around
    // it. No font involved anywhere: the device build ships no system
    // fonts (see OpenGuitarMultiFxLookAndFeel), which the "..." glyph this
    // replaces silently needed.
    getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), highlighted, down);

    const auto centre = getLocalBounds().toFloat().getCentre();
    const float ringR = (float) getWidth() * 0.21f;
    const float toothLen = (float) getWidth() * 0.13f;
    const float stroke = (float) getWidth() * 0.055f;
    g.setColour (findColour (juce::TextButton::textColourOffId).withAlpha (down ? 1.0f : 0.85f));

    for (int i = 0; i < 8; ++i)
    {
        const float angle = (float) i * juce::MathConstants<float>::pi / 4.0f;
        const auto dir = juce::Point<float> (std::cos (angle), std::sin (angle));
        g.drawLine ({ centre + dir * ringR, centre + dir * (ringR + toothLen) }, stroke);
    }
    g.drawEllipse (juce::Rectangle<float> (ringR * 2.0f, ringR * 2.0f).withCentre (centre), stroke);
    g.fillEllipse (juce::Rectangle<float> (stroke * 1.4f, stroke * 1.4f).withCentre (centre));
}

void MainComponent::resized()
{
    overlayHost.setBounds (getLocalBounds());

    auto area = getLocalBounds().reduced (12);

    // Footer (tuner/BPM-tap/meters) is pinned at the very bottom, reserved
    // BEFORE the drawer's cap math below so the drawer correctly sees less
    // leftover space with it there -- see FooterBar.h.
    footerBar.setBounds (area.removeFromBottom (footerHeight));
    area.removeFromBottom (8);

    // Tall enough for a stage-readable preset number/name (see
    // topBarHeight's comment) -- cpuLabel/settingsButton/quickSaveButton
    // don't need to be that tall themselves, just centred within it.
    auto top = area.removeFromTop (topBarHeight);
    // 150, not 70: it carries the average AND the worst-block peak now (see timerCallback()).
    cpuLabel.setBounds (top.removeFromRight (150).withSizeKeepingCentre (150, 20));
    // Sits between the CPU readout and Settings when the device is dead --
    // the actionable half of the NO AUDIO DEVICE warning (audit #93).
    if (retryAudioButton.isVisible())
        retryAudioButton.setBounds (top.removeFromRight (80).withSizeKeepingCentre (80, touch::minTapTarget));
    settingsButton.setBounds (top.removeFromRight (touch::minTapTarget)
                                   .withSizeKeepingCentre (touch::minTapTarget, touch::minTapTarget));
    top.removeFromRight (8);
    // A little wider than it used to be -- the badge draws number+letter
    // ("12B") now, not just a bare number.
    presetBadge.setBounds (top.removeFromLeft (88));
    top.removeFromLeft (8);
    // The A-H snapshot strip, one minTapTarget cell per snapshot, plus
    // the little "tap -- hold" caption the bar draws on top (see
    // SnapshotBar::hintHeight -- audit #93 item 3).
    snapshotBar.setBounds (top.removeFromLeft (snapshotxml::maxSnapshots * touch::minTapTarget)
                             .withSizeKeepingCentre (snapshotxml::maxSnapshots * touch::minTapTarget,
                                                     touch::minTapTarget + SnapshotBar::hintHeight));
    top.removeFromLeft (8);
    if (quickSaveButton.isVisible())
    {
        quickSaveButton.setBounds (top.removeFromRight (80).withSizeKeepingCentre (80, touch::minTapTarget));
        top.removeFromRight (8);
    }
    titleLabel.setBounds (top);

    area.removeFromTop (8);

    // `area` now spans the FULL space between the top bar and the footer.
    // The chain always fills ALL of it -- the parameter drawer, when open,
    // OVERLAYS on top of the bottom portion instead of shrinking this area
    // any more, per user request 2026-09-10 ("a aba que tem os knobs...
    // vai sobrepor as linhas").
    //
    // Tiles are SQUARE (blockWidth == blockHeight) sized so exactly
    // chainColumns (8) fit the available width, per user request
    // 2026-09-10 ("ele tem q ser quadrados n retangulos"). Almost always
    // more vertical room than 4 square tiles need, though -- rather than
    // stretch them into rectangles again, that leftover becomes rowGap
    // between rows instead ("n tem problema se tiver espaço entre eles"),
    // so the 4-row BLOCK as a whole still spans the full height even
    // though the individual tiles stay a sensible size.
    // A thin desktop-style strip, not touch::minTapTarget -- that made it
    // comically thick (user report 2026-09-12, "bizarramente grossa"). The
    // real reason it looked unusable before wasn't its width at all:
    // layoutChain() was erasing its own scroll range out from under it (see
    // drawerScrollPadding's comment) -- fixed separately, so this can be a
    // sane width. Declared here (before chainContentWidth) rather than down
    // by its own setBounds() call so the two can't drift out of sync again
    // the way they did when this used to be a separate literal there --
    // that's the exact bug behind "o 8 bloco ta cortando".
    const int scrollBarWidth = 14;

    // Every strip that comes out of `area` before the 8-column grid gets
    // whatever's left: the scrollbar (+ its 6px gap), then the two gutters
    // (each ioWidth + cableLane, the cable lane added to keep row-to-row
    // connectors off the endpoint tiles -- see its member comment) plus
    // their two 8px gaps. This has to stay in sync with every actual
    // removeFrom*() below it.
    const int chainContentWidth = area.getWidth() - (scrollBarWidth + 6) - 2 * (ioWidth + cableLane) - 16;
    blockWidth = juce::jmax (60, (chainContentWidth - (chainColumns - 1) * blockGap) / chainColumns);
    blockHeight = blockWidth;

    const int rowsContentHeight = numRows * blockHeight;
    const int leftoverHeight = area.getHeight() - 12 - rowsContentHeight; // the -12 mirrors the old fudge-padding
    rowGap = leftoverHeight > (numRows - 1) * blockGap ? leftoverHeight / (numRows - 1) : blockGap;

    auto chainRow = area;
    chainRowTop = chainRow.getY();

    // Scrollbar first, off the far right of the window -- everything else
    // (gutters, viewport) lays out inside what's left.
    chainScrollBar.setBounds (chainRow.removeFromRight (scrollBarWidth));
    chainRow.removeFromRight (6);

    leftGutterColumn = chainRow.removeFromLeft (ioWidth + cableLane);
    chainRow.removeFromLeft (8);
    rightGutterColumn = chainRow.removeFromRight (ioWidth + cableLane);
    chainRow.removeFromRight (8);
    chainViewport.setBounds (chainRow);
    layoutChain();

    // The detail drawer only exists once a block is selected. Its height
    // fits whatever the current processor actually needs (so a 1-2 knob
    // pedal never scrolls), floored at enough for one knob row and capped
    // at ~45% of the window -- it used to just always fill a quarter
    // of the window regardless of content, which either clipped a
    // knob-heavy processor or left a tiny 1-2 knob one scrolling for no
    // reason. See AGENT.md's UI/UX Design Philosophy: a drawer, never a
    // permanent desktop-style panel -- and now, per the OVERLAY change
    // above, positioned over the bottom of the SAME area the chain just
    // used rather than carved out of it beforehand.
    int panelHeight = 0;
    if (selectedProcessor != nullptr)
    {
        constexpr int floorForOneKnobRow = 240; // one unified button row + exactly one row of knobs
        const int cap = juce::jmax ((int) (getHeight() * 0.45f), floorForOneKnobRow);
        const int preferred = parameterPanel.getPreferredContentHeight (area.getWidth());
        panelHeight = juce::jmin (area.getHeight(), juce::jmin (preferred, cap));
    }
    parameterPanel.setBounds (area.removeFromBottom (panelHeight));
    parameterPanel.setVisible (panelHeight > 0);

    // The drawer covers the bottom panelHeight px of the SAME chain area it
    // overlays (see above) without shrinking chainViewport's own bounds --
    // so without this, any row underneath it would just be permanently
    // hidden with no way to reach it. Recording it here (ALWAYS, not just
    // when panelHeight > 0 -- closing the drawer has to clear it back to 0
    // just as reliably) and re-running layoutChain() lets it bake the
    // padding into chainContainer's height itself, so the padding survives
    // every later re-layout, including the ones triggered BY scrolling
    // (chainViewport.onScrolled -> layoutChain()) -- see the member's
    // comment for the loop that was silently erasing it before. Per user
    // request 2026-09-11 ("quando abrir tipo o tab por cima... coloca em
    // ver um scrollbar").
    drawerScrollPadding = panelHeight;
    layoutChain();

    if (panelHeight > 0)
    {
        // Overlays the chain -- must paint after it, see chainViewport's add
        // order. chainScrollBar deliberately stays BEHIND this: the drawer
        // covering its lower portion (down where "Remove" etc. sit) is
        // correct z-order for an overlay, not a bug -- the part of the bar
        // above the drawer is still there to drag. An earlier pass brought
        // the scrollbar in FRONT of the drawer instead, which visibly
        // covered the drawer's own buttons (user report 2026-09-12, "ela ta
        // por cima do botao remove").
        parameterPanel.toFront (false);

        // Auto-scroll the just-selected block into the part of the
        // viewport the drawer DOESN'T cover -- selecting a block on row 3
        // or 4 is exactly what opens the drawer that can then hide it, so
        // making the user manually scroll to see what they just selected
        // would be circular. Per user request 2026-09-12 ("se for pra
        // editar um efeito dessas linhas, tem que ser automatico o
        // scroll").
        for (auto* block : blocks)
        {
            if (&block->processor != selectedProcessor)
                continue;

            const int visibleHeight = chainViewport.getHeight() - panelHeight;
            const int currentTop = chainViewport.getViewPositionY();
            int target = currentTop;

            if (block->getBottom() > currentTop + visibleHeight)
                target = block->getBottom() - visibleHeight;
            if (block->getY() < target)
                target = block->getY();

            target = juce::jlimit (0, juce::jmax (0, chainContainer.getHeight() - chainViewport.getHeight()), target);
            chainViewport.setViewPosition (chainViewport.getViewPositionX(), target);
            break;
        }
    }

    // Closing the drawer shrinks chainContainer back down (the padding
    // above only applies while panelHeight > 0), but the viewport's
    // current scroll position doesn't necessarily snap back to 0 on its
    // own in the same call -- left stale for one frame, getViewPositionY()
    // in paint()'s connector code would read a leftover non-zero offset
    // against content that no longer has anywhere for it to point,
    // visibly detaching the connector from the rows it's supposed to
    // touch. Explicitly clamped here so it's never stale. Fixed 2026-09-11
    // ("lado direito ali ainda ta bugado").
    chainViewport.setViewPosition (chainViewport.getViewPositionX(),
                                    juce::jlimit (0, juce::jmax (0, chainContainer.getHeight() - chainViewport.getHeight()),
                                                  chainViewport.getViewPositionY()));
    syncChainScrollBar();
}

void MainComponent::syncChainScrollBar()
{
    const int contentHeight = chainContainer.getHeight();
    const int visibleHeight = juce::jmax (1, chainViewport.getMaximumVisibleHeight());

    chainScrollBar.setRangeLimits (0.0, (double) juce::jmax (contentHeight, visibleHeight), juce::dontSendNotification);
    chainScrollBar.setCurrentRange ((double) chainViewport.getViewPositionY(), (double) visibleHeight,
                                     juce::dontSendNotification); // never notify back -- this IS the response to a move
    chainScrollBar.setVisible (contentHeight > visibleHeight);
}

void MainComponent::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
{
    if (bar == &chainScrollBar)
        chainViewport.setViewPosition (chainViewport.getViewPositionX(), (int) newRangeStart);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff141414));

    // Inter-row connectors: every row before the last occupied one connects
    // to the start of the next with ONE continuous line -- down through the
    // right gutter, across through ChainContainer's own seam line (drawn in
    // its paint(), same Y, spanning its full width so the two halves meet
    // exactly at its edges), then down into the left gutter -- rather than
    // two disconnected glyphs. Per user correction 2026-09-10 ("as linhas
    // se conectarem do final com o início da próxima"). Built as ONE Path
    // per side with a rounded corner at the bend (PathStrokeType::curved
    // plus an explicit quadraticTo) rather than two separate drawLine()
    // calls -- those left a visible notch at the joint (flat line caps
    // don't overlap cleanly at a right angle) and read as sharp/square
    // rather than the rounded look asked for -- per user correction
    // 2026-09-11 ("falta fechar a linha, tamos com um gap ali... deixa
    // mais redondo"). Same white-at-30% line language as ChainContainer's
    // own lines throughout.
    g.setColour (juce::Colours::white.withAlpha (0.3f));

    // No inset needed any more: the scrollbar lives at the window's right
    // edge (chainScrollBar), not on top of the viewport's own, so nothing
    // overlaps the connector's rightmost reach.
    const float rightEdge = (float) chainViewport.getRight();
    const float leftEdge = (float) chainViewport.getX();
    constexpr float cornerRadius = 10.0f;

    // The rows themselves live inside chainViewport and move when it's
    // scrolled (see resized()'s comment on the drawer-open scroll
    // padding); these stub coordinates are absolute/unscrolled, so the
    // current scroll position has to be subtracted to stay lined up with
    // wherever the content actually is right now -- kept in sync via
    // visibleAreaChanged() triggering a repaint(). Fixed 2026-09-11 (was
    // the cause of a small but real gap between the stub and the row it's
    // supposed to touch whenever the chain had scrolled at all).
    const int scrollOffset = chainViewport.getViewPositionY();

    // One connector per row that feeds another row -- NOT one per pair of
    // consecutive rows. Rows are independent until explicitly linked (user
    // decision 2026-09-11: "some -- só conecta o que eu escolher"), so a
    // link can also skip rows or run upwards, and two adjacent rows with no
    // link between them correctly show nothing.
    for (int row = 0; row < numRows; ++row)
    {
        const auto& routing = rowRouting[(size_t) row];

        for (int target = 0; target < numRows; ++target)
        {
        if (! routing.toRows[(size_t) target])
            continue;

        const auto rowCentreY = [this, scrollOffset] (int r)
        {
            return (float) chainRowTop + (float) r * (float) (blockHeight + rowGap)
                    + (float) blockHeight * 0.5f - (float) scrollOffset;
        };

        const float fromY = rowCentreY (row);
        const float toY = rowCentreY (target);
        const float bendDir = toY > fromY ? 1.0f : -1.0f; // links can run upwards too

        // Cross in the GAP immediately beyond the source row, never at the
        // midpoint between the two rows -- for a link that skips a row the
        // midpoint lands exactly on the skipped row's own signal line, which
        // read as the connector being superimposed on it (user report
        // 2026-09-11: "ele fica sobreposto com a linha normal").
        const float crossY = fromY + bendDir * ((float) blockHeight * 0.5f + (float) rowGap * 0.5f);

        // Out of the source row's right end, around through the right
        // gutter, across (ChainContainer draws that middle span), then down
        // the left gutter and into the target row's left end.
        const float rightX = (float) rightGutterColumn.getRight() - cableLane * 0.5f;
        juce::Path rightSide;
        rightSide.startNewSubPath (rightEdge, fromY);
        rightSide.lineTo (rightX - cornerRadius, fromY);
        rightSide.quadraticTo (rightX, fromY, rightX, fromY + cornerRadius * bendDir);
        rightSide.lineTo (rightX, crossY - cornerRadius * bendDir);
        rightSide.quadraticTo (rightX, crossY, rightX - cornerRadius, crossY);
        rightSide.lineTo (rightEdge, crossY);
        g.strokePath (rightSide, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float leftX = (float) leftGutterColumn.getX() + cableLane * 0.5f;
        juce::Path leftSide;
        leftSide.startNewSubPath (leftEdge, crossY);
        leftSide.lineTo (leftX + cornerRadius, crossY);
        leftSide.quadraticTo (leftX, crossY, leftX, crossY + cornerRadius * bendDir);
        leftSide.lineTo (leftX, toY - cornerRadius * bendDir);
        leftSide.quadraticTo (leftX, toY, leftX + cornerRadius, toY);
        leftSide.lineTo (leftEdge, toY);
        g.strokePath (leftSide, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
}

} // namespace openguitarmultifx
