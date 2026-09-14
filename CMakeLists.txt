cmake_minimum_required(VERSION 3.22)
project(OpenGuitarMultiFx VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
FetchContent_Declare(
  JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.9
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

include(cmake/NAMCore.cmake)
include(cmake/ChowdspWDF.cmake)

# Sora (SIL OFL, see Assets/Fonts/SORA-LICENSE.txt), embedded rather than
# relying on the OS having a decent font installed -- the final target is a
# from-scratch embedded Linux build with no guaranteed font at all. Static
# weights, not the variable font: JUCE's own font engine doesn't reliably
# expose a variable font's weight axis, but it renders a real bold/extrabold
# file exactly as designed. Switched from Inter to Sora per user request
# (2026-09-10) -- Inter read as generic/default, Sora's geometric shapes and
# distinctive rounded terminals read more as a purpose-built hardware/gear
# product than a stock UI font.
juce_add_binary_data(OpenGuitarMultiFx_Fonts
  SOURCES
    Assets/Fonts/Sora-Regular.ttf
    Assets/Fonts/Sora-Bold.ttf
    Assets/Fonts/Sora-ExtraBold.ttf
)

# Effect block icons -- embedded SVGs, not hand-transcribed juce::Path calls.
# The first attempt at drawIcon() reimplemented each glyph's geometry by eye
# from the approved reference-sheet prototype and drifted from it in several
# ways per icon (stroke width, inset ratios, pin lengths, filled vs. outline
# circles) -- see docs/icons/AGENT-icon-notes.md. Embedding the literal
# approved SVG and rendering it with juce::Drawable guarantees the on-screen
# glyph is pixel-identical to what got approved, the same reasoning as
# embedding Sora above instead of hoping the OS font looks similar enough.
juce_add_binary_data(OpenGuitarMultiFx_Icons
  NAMESPACE IconData
  HEADER_NAME IconData.h
  SOURCES
    Assets/Icons/gate.svg
    Assets/Icons/pulse.svg
    Assets/Icons/overdrive.svg
    Assets/Icons/boost.svg
    Assets/Icons/neura_chip.svg
    Assets/Icons/neura_chip_cab.svg
    Assets/Icons/cab.svg
    Assets/Icons/hall.svg
    Assets/Icons/digital_delay.svg
    Assets/Icons/tape_delay.svg
    Assets/Icons/ping_pong.svg
    Assets/Icons/reverse_delay.svg
    Assets/Icons/hold.svg
    Assets/Icons/ambient.svg
    Assets/Icons/spring.svg
    Assets/Icons/plate.svg
    Assets/Icons/room.svg
    Assets/Icons/analog_delay.svg
    Assets/Icons/dual_delay.svg
    Assets/Icons/shimmer.svg
    Assets/Icons/gated.svg
    Assets/Icons/multi_tap.svg
    Assets/Icons/tremolo.svg
    Assets/Icons/chorus.svg
    Assets/Icons/vibrato.svg
    Assets/Icons/flanger.svg
    Assets/Icons/phaser.svg
    Assets/Icons/rotary.svg
    Assets/Icons/uni_vibe.svg
    Assets/Icons/pitch_mod.svg
    Assets/Icons/octaver.svg
    Assets/Icons/pitch_shift.svg
    Assets/Icons/harmonizer.svg
    Assets/Icons/looper.svg
)

# A real (if dev-facing, pre-touch, pre-Phase-7) GUI, not a console app --
# see AGENT.md: the user wants to SEE the chain being built, not just trust
# test output. Still no presets/split-merge yet, on purpose.
juce_add_gui_app(OpenGuitarMultiFx
  PRODUCT_NAME "OpenGuitarMultiFx"
  COMPANY_NAME "OpenGuitarMultiFx"
  BUNDLE_ID "com.openguitarmultifx.OpenGuitarMultiFx"
)

target_sources(OpenGuitarMultiFx PRIVATE
  Source/Main.cpp
  Source/EffectRegistry.cpp
  Source/Engine/AudioEngine.cpp
  Source/Engine/PitchDetector.cpp
  Source/Engine/PolyphonicPitchDetector.cpp
  Source/Engine/TuningSettings.cpp
  Source/Engine/SignalGraph.cpp
  Source/Engine/ParameterManager.cpp
  Source/Effects/GateProcessor.cpp
  Source/Effects/CompressorProcessor.cpp
  Source/Effects/OverdriveProcessor.cpp
  Source/Effects/PositiveGroundBoosterProcessor.cpp
  Source/Effects/DS1StyleDistortionProcessor.cpp
  Source/Effects/NAMProcessor.cpp
  Source/Effects/IRLoaderProcessor.cpp
  Source/Effects/DynamicCabProcessor.cpp
  Source/Effects/DelayProcessor.cpp
  Source/Effects/TapeDelayProcessor.cpp
  Source/Effects/PingPongDelayProcessor.cpp
  Source/Effects/ReverseDelayProcessor.cpp
  Source/Effects/HoldProcessor.cpp
  Source/Effects/ReverbProcessor.cpp
  Source/Effects/SpringReverbProcessor.cpp
  Source/Effects/HallReverbProcessor.cpp
  Source/Effects/PlateReverbProcessor.cpp
  Source/Effects/RoomReverbProcessor.cpp
  Source/Effects/ShimmerReverbProcessor.cpp
  Source/Effects/GatedReverbProcessor.cpp
  Source/Effects/AnalogDelayProcessor.cpp
  Source/Effects/DualDelayProcessor.cpp
  Source/Effects/MultiTapDelayProcessor.cpp
  Source/Effects/TremoloProcessor.cpp
  Source/Effects/ChorusProcessor.cpp
  Source/Effects/VibratoProcessor.cpp
  Source/Effects/FlangerProcessor.cpp
  Source/Effects/PhaserProcessor.cpp
  Source/Effects/RotaryProcessor.cpp
  Source/Effects/UniVibeProcessor.cpp
  Source/Effects/PitchModProcessor.cpp
  Source/Effects/PitchShiftProcessor.cpp
  Source/Effects/OctaverProcessor.cpp
  Source/Effects/HarmonizerProcessor.cpp
  Source/Effects/LooperProcessor.cpp
  Source/UI/ChainContainer.cpp
  Source/UI/EffectBlockComponent.cpp
  Source/UI/FooterBar.cpp
  Source/UI/PolyphonicTunerOverlay.cpp
  Source/UI/RowEndpointBlock.cpp
  Source/UI/ModelListDialog.cpp
  Source/UI/OverlayHost.cpp
  Source/UI/ParameterPanel.cpp
  Source/UI/MainComponent.cpp
  Source/UI/OpenGuitarMultiFxLookAndFeel.cpp
  Source/UI/PresetBadge.cpp
  Source/UI/PresetListDialog.cpp
  Source/UI/Tone3000Panel.cpp
  Source/UI/Tone3000SearchDialog.cpp
  Source/UI/OAuthLoginDialog.cpp
  Source/Presets/PresetManager.cpp
  Source/Tone3000/Tone3000Manager.cpp
)

target_include_directories(OpenGuitarMultiFx PRIVATE Source)

# JUCE_WEB_BROWSER=1: TONE3000 login renders in an embedded WebBrowserComponent
# (WebKitGTK on Linux, already available -- webkit2gtk-4.1 was found during the
# juceaide configure step regardless), never a separate system browser process.
# Curl is needed for TONE3000's REST calls (Tone3000Manager).
target_compile_definitions(OpenGuitarMultiFx PRIVATE
  JUCE_WEB_BROWSER=1
  JUCE_USE_CURL=1
)

# JUCE_USE_CURL only makes juce_core.cpp call into libcurl -- it doesn't link
# it. Without this, the link fails with undefined references to curl_*.
find_package(CURL REQUIRED)

# juce_gui_extra.cpp needs GTK/WebKitGTK headers for WebBrowserComponent on
# Linux. JUCE's CMake resolves these for its own juceaide helper build, but
# not for consumer targets -- without this, compiling juce_gui_extra.cpp
# fails with "gtk/gtk.h: No such file or directory". Same pattern as the
# CURL::libcurl fix above: ask pkg-config directly rather than assume JUCE
# wires it up.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(WEBKIT2GTK REQUIRED IMPORTED_TARGET webkit2gtk-4.1)

# No LTO: this is a dev-iteration build, and link-time optimization roughly
# doubles peak memory during the final link (confirmed: it was spawning 20
# parallel LTRANS jobs). On a machine this size that's the difference
# between a slow build and a hard crash -- see scripts/build.sh for the
# incident this comment refers to. Revisit for an actual release build.
target_link_libraries(OpenGuitarMultiFx PRIVATE
  juce::juce_audio_basics
  juce::juce_audio_devices
  juce::juce_audio_processors
  juce::juce_core
  juce::juce_cryptography
  juce::juce_dsp
  juce::juce_events
  juce::juce_gui_basics
  juce::juce_gui_extra
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags
  nam_core
  chowdsp::chowdsp_wdf
  OpenGuitarMultiFx_Fonts
  OpenGuitarMultiFx_Icons
  CURL::libcurl
  PkgConfig::GTK3
  PkgConfig::WEBKIT2GTK
)

enable_testing()
add_subdirectory(Tests)
