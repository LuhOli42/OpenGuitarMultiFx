#include "MainComponent.h"

#include "OpenGuitarMultiFxLookAndFeel.h"
#include "PresetListDialog.h"
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

    // Same categories as the icon reference sheet (see
    // docs/icons/AGENT-icon-notes.md, whose category names come from the
    // original PT-language reference sheet) -- the "Add effect" menu
    // groups into these submenus once there are enough effect types that a
    // flat list stops being manageable. Order here is the order submenus
    // appear in. Translated to English 2026-09-11 per user request (all
    // app-visible text in English) -- the icon doc keeps the original PT
    // labels since that's what the actual reference sheet uses, but
    // nothing user-facing in the running app should be in Portuguese.
    const juce::StringArray categoryOrder { "Amplifiers", "Dynamics", "Drive", "Modulation",
                                             "Delay", "Reverb", "Filter/FX", "Utility" };

    juce::String categoryForDisplayName (const juce::String& displayName)
    {
        if (displayName == "Noise Gate" || displayName == "Compressor")
            return "Dynamics";
        if (displayName == "Overdrive")
            return "Drive";
        if (displayName == "Neural Amp" || displayName == "Neural Amp + Cab"
            || displayName == "Neural Pedal" || displayName == "Cab")
            return "Amplifiers";
        if (displayName == "Reverb" || displayName == "Ambient" || displayName == "Spring" || displayName == "Hall"
            || displayName == "Plate" || displayName == "Room" || displayName == "Shimmer" || displayName == "Gated")
            return "Reverb";
        if (displayName == "Digital Delay" || displayName == "Tape Delay" || displayName == "Analog Delay"
            || displayName == "Dual Delay" || displayName == "Multi Tap" || displayName == "Ping Pong"
            || displayName == "Reverse Delay" || displayName == "Hold" || displayName == "Looper")
            return "Delay";
        if (displayName == "Tremolo" || displayName == "Chorus" || displayName == "Vibrato"
            || displayName == "Flanger" || displayName == "Phaser" || displayName == "Rotary"
            || displayName == "Uni-Vibe" || displayName == "Pitch Mod")
            return "Modulation";
        if (displayName == "Pitch Shift" || displayName == "Octaver" || displayName == "Harmonizer")
            return "Filter/FX";
        return "Other"; // shouldn't normally happen -- a new effect type that hasn't been categorised yet
    }

    const juce::StringArray noteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    /** Standard equal-temperament conversion, A4 = 440Hz. Returns the
        nearest note name and how far off it is in cents (-50..+50) --
        used to feed FooterBar's tuner gauge from PitchDetector's raw
        frequency estimate. `hz <= 0` (PitchDetector's "no clear pitch"
        sentinel) returns {"--", 0}. */
    std::pair<juce::String, float> frequencyToNoteAndCents (float hz)
    {
        if (hz <= 0.0f)
            return { "--", 0.0f };

        const float midiNote = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        const int nearestNote = (int) std::round (midiNote);
        const float cents = (midiNote - (float) nearestNote) * 100.0f;

        const int octave = nearestNote / 12 - 1;
        const int noteIndex = ((nearestNote % 12) + 12) % 12;
        return { noteNames[noteIndex] + juce::String (octave), cents };
    }
}

MainComponent::MainComponent()
{
    registerBuiltInEffects (registry);

    addAndMakeVisible (presetBadge);
    presetBadge.onClicked = [this] { showPresetsPanel(); };

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
        titleLabel.setFont (juce::Font (juce::FontOptions (42.0f).withTypeface (laf->getExtraBoldTypeface())));
    }

    addAndMakeVisible (cpuLabel);
    cpuLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (settingsButton);
    settingsButton.onClick = [this] { showSettingsPanel(); };

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
        // Already picking an effect? This click just closes that list --
        // opening a second one at the newly clicked cell reads as the menu
        // refusing to go away (per user request 2026-09-11).
        if (addEffectMenuOpen)
        {
            juce::PopupMenu::dismissAllActiveMenus();
            addEffectMenuOpen = false;
            return;
        }

        // Just dismissed by this very click -- see addEffectMenuClosedAtMs.
        if (juce::Time::getMillisecondCounter() - addEffectMenuClosedAtMs < 200)
            return;

        showAddEffectMenu (index);
    };

    parameterPanel.onRemoveRequested = [this] (EffectProcessor* p) { removeEffect (p); };
    parameterPanel.setModelsDirectory (getModelsDirectory());
    parameterPanel.setTone3000Manager (tone3000);
    parameterPanel.onPushOverlay = [this] (std::unique_ptr<juce::Component> c) { overlayHost.pushOverlay (std::move (c)); };
    parameterPanel.onPopOverlay  = [this] { overlayHost.popOverlay(); };

    parameterPanel.onMidiLearnRequested = [this] (juce::AudioParameterFloat* param)
    {
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

    if (! audioEngine.start())
        titleLabel.setText ("Audio device failed to open", juce::dontSendNotification);

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
}

MainComponent::~MainComponent()
{
    aliveFlag->store (false); // must be first -- an in-flight MIDI callAsync can still fire after this point

    for (auto& midiInput : juce::MidiInput::getAvailableDevices())
        audioEngine.getDeviceManager().removeMidiInputDeviceCallback (midiInput.identifier, this);

    audioEngine.stop(); // must happen before chain's processors are destroyed by the member destructors below
}

void MainComponent::addEffect (const juce::String& registryName, int targetGridSlot)
{
    auto processor = registry.create (registryName);
    if (processor == nullptr)
        return;

    auto* raw = processor.get();

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

    auto block = std::make_unique<EffectBlockComponent> (*raw);
    block->gridSlot = slot;
    // Tapping the already-open block closes the drawer instead of just
    // re-opening it on itself -- same block, second tap, gone.
    block->onClicked = [this, raw] { selectBlock (selectedProcessor == raw ? nullptr : raw); };
    block->onDragEnded = [this] (EffectBlockComponent& b) { handleBlockDragEnded (b); };
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

    juce::PopupMenu menu;
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

        blocks.clearQuick (false); // releases ownership without deleting -- newBlockOrder already holds every pointer
        for (auto* b : newBlockOrder)
            blocks.add (b);

        rebuildSignalGraph();
    }

    layoutChain(); // snaps every block, including the dragged one, back onto the clean grid
}

void MainComponent::showAddEffectMenu (int targetGridSlot)
{
    auto keys = registry.getRegisteredNames();

    std::map<juce::String, std::vector<juce::String>> keysByCategory;
    for (auto& key : keys)
        keysByCategory[categoryForDisplayName (registry.displayNameForKey (key))].push_back (key);

    // Item IDs are assigned sequentially across every submenu, and idToKey
    // is that same sequence -- juce::PopupMenu only gives an item's ID
    // back, never which submenu it came from, so this flat lookup is what
    // turns that ID back into a registry key.
    juce::PopupMenu menu;
    std::vector<juce::String> idToKey;

    auto addCategory = [&] (const juce::String& category)
    {
        const auto it = keysByCategory.find (category);
        if (it == keysByCategory.end())
            return;

        juce::PopupMenu submenu;
        for (auto& key : it->second)
        {
            idToKey.push_back (key);
            submenu.addItem ((int) idToKey.size(), registry.displayNameForKey (key));
        }
        menu.addSubMenu (category, submenu);
        keysByCategory.erase (it);
    };

    for (auto& category : categoryOrder)
        addCategory (category);

    // Anything left over (e.g. "Other") -- still shown, just last. Collected
    // into a separate list first: addCategory() erases from keysByCategory,
    // which can't happen safely while range-for is iterating that same map.
    juce::StringArray remainingCategories;
    for (auto& [category, categoryKeys] : keysByCategory)
        remainingCategories.add (category);
    for (auto& category : remainingCategories)
        addCategory (category);

    // Big is fine -- comfortable to tap on a 10" touchscreen matters more
    // than compactness (see AGENT.md's UI/UX Design Philosophy).
    addEffectMenuOpen = true;
    menu.showMenuAsync (juce::PopupMenu::Options().withStandardItemHeight (touch::minTapTarget),
        [this, idToKey, targetGridSlot] (int result)
        {
            addEffectMenuOpen = false;
            addEffectMenuClosedAtMs = juce::Time::getMillisecondCounter();
            if (result > 0 && result - 1 < (int) idToKey.size())
                addEffect (idToKey[(size_t) result - 1], targetGridSlot);
        });
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
    auto panel = std::make_unique<Tone3000Panel> (tone3000);
    panel->onPushOverlay = [this] (std::unique_ptr<juce::Component> c) { overlayHost.pushOverlay (std::move (c)); };
    panel->onPopOverlay  = [this] { overlayHost.popOverlay(); };

    overlayHost.pushOverlay (std::move (panel));
}

void MainComponent::showPresetsPanel()
{
    auto dialog = std::make_unique<PresetListDialog> (presets.listPresetNames());

    dialog->onSaveRequested = [this] (juce::String name)
    {
        savePresetAs (name);
        overlayHost.popOverlay();
    };

    dialog->onPresetChosen = [this] (juce::String name)
    {
        loadPresetByName (name);
        overlayHost.popOverlay();
    };

    dialog->onDeleteRequested = [this] (juce::String name)
    {
        presets.deletePreset (name);
        if (currentPresetName == name)
        {
            currentPresetName.clear();
            currentPresetNumber = 0;
            updatePresetDisplay();
        }
        overlayHost.popOverlay();
        showPresetsPanel(); // reopen with a refreshed list -- simplest way to reflect the deletion
    };

    dialog->onPopOverlay = [this] { overlayHost.popOverlay(); };

    overlayHost.pushOverlay (std::move (dialog));
}

void MainComponent::loadPresetByName (const juce::String& name)
{
    if (auto xml = presets.loadPreset (name))
    {
        applyPresetXml (*xml);
        currentPresetName = name;
        currentPresetNumber = xml->getIntAttribute ("number", 0);
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

void MainComponent::handleMidiCc (int ccNumber, int ccValue)
{
    // A knob armed for MIDI Learn takes priority over an existing binding
    // on the same CC -- the next CC message received while armed is what
    // gets learned, even if that CC number was already bound to something
    // else (re-learning a CC onto a new parameter implicitly un-learns it
    // from the old one, since a CC can only ever drive one parameter).
    if (midiLearnArmedParam != nullptr)
    {
        for (auto it = midiCcBindings.begin(); it != midiCcBindings.end(); )
            it = it->second == midiLearnArmedParam ? midiCcBindings.erase (it) : std::next (it);

        midiCcBindings[ccNumber] = midiLearnArmedParam;
        midiLearnArmedParam = nullptr;
        return;
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
    presets.savePreset (name, *xml);

    currentPresetName = name;
    currentPresetNumber = number;
    updatePresetDisplay();
}

void MainComponent::updatePresetDisplay()
{
    presetBadge.setNumber (currentPresetNumber);
    titleLabel.setText (currentPresetName.isNotEmpty() ? currentPresetName : "No preset loaded",
                         juce::dontSendNotification);
    quickSaveButton.setVisible (currentPresetName.isNotEmpty());
    resized(); // quickSaveButton's visibility changes how much room titleLabel gets
}

std::unique_ptr<juce::XmlElement> MainComponent::buildPresetXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("Preset");

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
    }

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
    // Tear the current chain down the same way removeEffect() would, one
    // block at a time, so nothing bypasses the graveyard/DeferredReclaimer
    // discipline (rebuildSignalGraph()/layoutChain() re-run every
    // iteration -- wasteful but this only ever happens from a menu tap,
    // never the audio thread).
    while (! blocks.isEmpty())
        removeEffect (&blocks[0]->processor);

    for (int i = 0; i < xml.getNumChildElements(); ++i)
    {
        auto* blockXml = xml.getChildElement (i);
        if (blockXml->getTagName() != "Block")
            continue;

        auto processor = registry.create (blockXml->getStringAttribute ("key"));
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

    refreshRowEndpoints();
    rebuildSignalGraph();
    layoutChain();
}

void MainComponent::timerCallback()
{
    cpuLabel.setText ("CPU " + juce::String (audioEngine.getCurrentCpuUsage() * 100.0, 1) + "%",
                       juce::dontSendNotification);

    footerBar.setLevels (audioEngine.getInputLevel(), audioEngine.getOutputLevel());
    const auto [noteName, cents] = frequencyToNoteAndCents (audioEngine.getDetectedFrequencyHz());
    footerBar.setTuning (noteName, cents / 50.0f); // +/-50 cents maps to the gauge's full deflection

    const auto now = juce::Time::getMillisecondCounter();
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                      [now] (const RetiredProcessor& r) { return now - r.retiredAtMs >= 600; }),
                      graveyard.end());

    parameterPanel.refresh();
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
    cpuLabel.setBounds (top.removeFromRight (70).withSizeKeepingCentre (70, 20));
    settingsButton.setBounds (top.removeFromRight (touch::minTapTarget)
                                   .withSizeKeepingCentre (touch::minTapTarget, touch::minTapTarget));
    top.removeFromRight (8);
    presetBadge.setBounds (top.removeFromLeft (80));
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
