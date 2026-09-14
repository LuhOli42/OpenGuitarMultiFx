juce_add_console_app(OpenGuitarMultiFx_Tests
  PRODUCT_NAME "OpenGuitarMultiFx Tests"
  BUNDLE_ID "com.openguitarmultifx.OpenGuitarMultiFxTests"
)

target_sources(OpenGuitarMultiFx_Tests PRIVATE
  RunTests.cpp
  SignalGraphTests.cpp
  DeferredReclaimerTests.cpp
  ParameterManagerTests.cpp
  PitchDetectorTests.cpp
  PresetManagerTests.cpp
  GateProcessorTests.cpp
  CompressorProcessorTests.cpp
  OverdriveProcessorTests.cpp
  NAMProcessorTests.cpp
  DelayProcessorTests.cpp
  TapeDelayProcessorTests.cpp
  ReverbProcessorTests.cpp
  SpringReverbProcessorTests.cpp
  HallReverbProcessorTests.cpp
  PlateReverbProcessorTests.cpp
  RoomReverbProcessorTests.cpp
  ShimmerReverbProcessorTests.cpp
  GatedReverbProcessorTests.cpp
  AnalogDelayProcessorTests.cpp
  DualDelayProcessorTests.cpp
  MultiTapDelayProcessorTests.cpp
  TremoloProcessorTests.cpp
  ChorusProcessorTests.cpp
  VibratoProcessorTests.cpp
  FlangerProcessorTests.cpp
  PhaserProcessorTests.cpp
  RotaryProcessorTests.cpp
  UniVibeProcessorTests.cpp
  PitchModProcessorTests.cpp
  PitchShiftProcessorTests.cpp
  OctaverProcessorTests.cpp
  HarmonizerProcessorTests.cpp
  LooperProcessorTests.cpp
  PingPongDelayProcessorTests.cpp
  ReverseDelayProcessorTests.cpp
  HoldProcessorTests.cpp
  ../Source/EffectRegistry.cpp
  ../Source/Engine/SignalGraph.cpp
  ../Source/Engine/PitchDetector.cpp
  ../Source/Engine/PolyphonicPitchDetector.cpp
  ../Source/Engine/TuningSettings.cpp
  ../Source/Engine/ParameterManager.cpp
  ../Source/Presets/PresetManager.cpp
  ../Source/Effects/GateProcessor.cpp
  ../Source/Effects/CompressorProcessor.cpp
  ../Source/Effects/OverdriveProcessor.cpp
  ../Source/Effects/NAMProcessor.cpp
  ../Source/Effects/IRLoaderProcessor.cpp
  ../Source/Effects/DynamicCabProcessor.cpp
  ../Source/Effects/DelayProcessor.cpp
  ../Source/Effects/TapeDelayProcessor.cpp
  ../Source/Effects/ReverbProcessor.cpp
  ../Source/Effects/SpringReverbProcessor.cpp
  ../Source/Effects/HallReverbProcessor.cpp
  ../Source/Effects/PlateReverbProcessor.cpp
  ../Source/Effects/RoomReverbProcessor.cpp
  ../Source/Effects/ShimmerReverbProcessor.cpp
  ../Source/Effects/GatedReverbProcessor.cpp
  ../Source/Effects/AnalogDelayProcessor.cpp
  ../Source/Effects/DualDelayProcessor.cpp
  ../Source/Effects/MultiTapDelayProcessor.cpp
  ../Source/Effects/TremoloProcessor.cpp
  ../Source/Effects/ChorusProcessor.cpp
  ../Source/Effects/VibratoProcessor.cpp
  ../Source/Effects/FlangerProcessor.cpp
  ../Source/Effects/PhaserProcessor.cpp
  ../Source/Effects/RotaryProcessor.cpp
  ../Source/Effects/UniVibeProcessor.cpp
  ../Source/Effects/PitchModProcessor.cpp
  ../Source/Effects/PitchShiftProcessor.cpp
  ../Source/Effects/OctaverProcessor.cpp
  ../Source/Effects/HarmonizerProcessor.cpp
  ../Source/Effects/LooperProcessor.cpp
  ../Source/Effects/PingPongDelayProcessor.cpp
  ../Source/Effects/ReverseDelayProcessor.cpp
  ../Source/Effects/HoldProcessor.cpp
)

target_include_directories(OpenGuitarMultiFx_Tests PRIVATE ../Source)

target_compile_definitions(OpenGuitarMultiFx_Tests PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  OGMFX_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(OpenGuitarMultiFx_Tests PRIVATE
  juce::juce_audio_basics
  juce::juce_audio_processors
  juce::juce_core
  juce::juce_dsp
  juce::juce_events
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags
  nam_core
  chowdsp::chowdsp_wdf
  OpenGuitarMultiFx_Icons
)

add_test(NAME OpenGuitarMultiFx_UnitTests COMMAND OpenGuitarMultiFx_Tests)
