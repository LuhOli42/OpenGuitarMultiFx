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
  PositiveGroundBoosterProcessorTests.cpp
  DS1StyleDistortionProcessorTests.cpp
  OD1StyleOverdriveProcessorTests.cpp
  TubeScreamerStyleOverdriveProcessorTests.cpp
  NodalCircuitTests.cpp
  TubeCircuitTests.cpp
  ReductionProbe.cpp
  ReducedOrderEquivalenceTests.cpp
  EffectCostBench.cpp
  SagEmulatorTests.cpp
  BassmanStyleAmplifierProcessorTests.cpp
  SuperLeadStyleAmplifierProcessorTests.cpp
  TwinReverbStyleAmplifierProcessorTests.cpp
  DeluxeReverbStyleAmplifierProcessorTests.cpp
  JC120StyleAmplifierProcessorTests.cpp
  JTM45StyleAmplifierProcessorTests.cpp
  JCM800StyleAmplifierProcessorTests.cpp
  AC15StyleAmplifierProcessorTests.cpp
  AC30StyleAmplifierProcessorTests.cpp
  SLO100StyleAmplifierProcessorTests.cpp
  MarkIICPlusStyleAmplifierProcessorTests.cpp
  DualRectifierStyleAmplifierProcessorTests.cpp
  EVH5150StyleAmplifierProcessorTests.cpp
  ENGLPowerballStyleAmplifierProcessorTests.cpp
  RockerverbStyleAmplifierProcessorTests.cpp
  ChannelKnobMemoryTests.cpp
  PresetChainBench.cpp
  CentaurStyleOverdriveProcessorTests.cpp
  BD2StyleOverdriveProcessorTests.cpp
  BigMuffStyleFuzzProcessorTests.cpp
  DT1StyleDistortionProcessorTests.cpp
  ODR1StyleOverdriveProcessorTests.cpp
  OverdriverStyleOverdriveProcessorTests.cpp
  EPStyleBoosterProcessorTests.cpp
  TubeDriverStyleOverdriveProcessorTests.cpp
  MetalZoneStyleDistortionProcessorTests.cpp
  FuzzFaceStyleFuzzProcessorTests.cpp
  ToneBenderStyleFuzzProcessorTests.cpp
  HM2StyleDistortionProcessorTests.cpp
  OpAmpClipperDistortionProcessorTests.cpp
  GuvnorStyleDistortionProcessorTests.cpp
  BluesBreakerStyleOverdriveProcessorTests.cpp
  RatStyleDistortionProcessorTests.cpp
  GE7StyleEqualizerProcessorTests.cpp
  ParametricEQProcessorTests.cpp
  RingModProcessorTests.cpp
  NoiseGateTests.cpp
  DynaCompStyleCompressorProcessorTests.cpp
  SqueezerStyleCompressorProcessorTests.cpp
  StudioCompressorTests.cpp
  CrunchBoxStyleDistortionProcessorTests.cpp
  ZendriveStyleOverdriveProcessorTests.cpp
  OCDStyleOverdriveProcessorTests.cpp
  PedalDeathProbe.cpp
  ChainSpikeBench.cpp
  CallbackOverheadBench.cpp
  PitchDetectorRangeTests.cpp
  AmpDynamicsBench.cpp
  RePrepareTransparencyTests.cpp
  OversampledEffectTests.cpp
  PedalUnityLevelTests.cpp
  PedalDiagnostics.cpp
  NamPedalCompare.cpp
  MixLawTests.cpp
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
)

# Every production source the processors need -- shared by the unit-test runner and the
# offline RenderFx tool below.
set(OGMFX_SOURCES
  ../Source/EffectRegistry.cpp
  ../Source/Engine/SignalGraph.cpp
  ../Source/Engine/PitchDetector.cpp
  ../Source/Engine/ParameterManager.cpp
  ../Source/Presets/PresetManager.cpp
  ../Source/Effects/GateProcessor.cpp
  ../Source/Effects/CompressorProcessor.cpp
  ../Source/Effects/PositiveGroundBoosterProcessor.cpp
  ../Source/Effects/DS1StyleDistortionProcessor.cpp
  ../Source/Effects/OD1StyleOverdriveProcessor.cpp
  ../Source/Effects/TubeScreamerStyleOverdriveProcessor.cpp
  ../Source/Effects/CentaurStyleOverdriveProcessor.cpp
  ../Source/Effects/BD2StyleOverdriveProcessor.cpp
  ../Source/Effects/BigMuffStyleFuzzProcessor.cpp
  ../Source/Effects/DT1StyleDistortionProcessor.cpp
  ../Source/Effects/ODR1StyleOverdriveProcessor.cpp
  ../Source/Effects/OverdriverStyleOverdriveProcessor.cpp
  ../Source/Effects/EPStyleBoosterProcessor.cpp
  ../Source/Effects/TubeDriverStyleOverdriveProcessor.cpp
  ../Source/Effects/MetalZoneStyleDistortionProcessor.cpp
  ../Source/Effects/FuzzFaceStyleFuzzProcessor.cpp
  ../Source/Effects/ToneBenderStyleFuzzProcessor.cpp
  ../Source/Effects/HM2StyleDistortionProcessor.cpp
  ../Source/Effects/OpAmpClipperDistortionProcessor.cpp
  ../Source/Effects/GuvnorStyleDistortionProcessor.cpp
  ../Source/Effects/BluesBreakerStyleOverdriveProcessor.cpp
  ../Source/Effects/RatStyleDistortionProcessor.cpp
  ../Source/Effects/GE7StyleEqualizerProcessor.cpp
  ../Source/Effects/ParametricEQProcessor.cpp
  ../Source/Effects/RingModProcessor.cpp
  ../Source/Effects/DS201StyleNoiseGateProcessor.cpp
  ../Source/Effects/NS2StyleNoiseSuppressorProcessor.cpp
  ../Source/Effects/DynaCompStyleCompressorProcessor.cpp
  ../Source/Effects/SqueezerStyleCompressorProcessor.cpp
  ../Source/Effects/Dbx160StyleCompressorProcessor.cpp
  ../Source/Effects/GSeriesStyleBusCompressorProcessor.cpp
  ../Source/Effects/Urei1176StyleCompressorProcessor.cpp
  ../Source/Effects/La2aStyleCompressorProcessor.cpp
  ../Source/Effects/CrunchBoxStyleDistortionProcessor.cpp
  ../Source/Effects/ZendriveStyleOverdriveProcessor.cpp
  ../Source/Effects/OCDStyleOverdriveProcessor.cpp
  ../Source/Effects/BassmanStyleAmplifierProcessor.cpp
  ../Source/Effects/SuperLeadStyleAmplifierProcessor.cpp
  ../Source/Effects/TwinReverbStyleAmplifierProcessor.cpp
  ../Source/Effects/DeluxeReverbStyleAmplifierProcessor.cpp
  ../Source/Effects/JC120StyleAmplifierProcessor.cpp
  ../Source/Effects/JTM45StyleAmplifierProcessor.cpp
  ../Source/Effects/JCM800StyleAmplifierProcessor.cpp
  ../Source/Effects/AC15StyleAmplifierProcessor.cpp
  ../Source/Effects/AC30StyleAmplifierProcessor.cpp
  ../Source/Effects/SLO100StyleAmplifierProcessor.cpp
  ../Source/Effects/MarkIICPlusStyleAmplifierProcessor.cpp
  ../Source/Effects/DualRectifierStyleAmplifierProcessor.cpp
  ../Source/Effects/EVH5150StyleAmplifierProcessor.cpp
  ../Source/Effects/ENGLPowerballStyleAmplifierProcessor.cpp
  ../Source/Effects/RockerverbStyleAmplifierProcessor.cpp
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

target_sources(OpenGuitarMultiFx_Tests PRIVATE ${OGMFX_SOURCES})

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

# The full suite runs as ONE serial ctest entry on purpose: normal suites
# contain wall-clock perf asserts (realTimeFactor / %CPU bounds) that go
# flaky under parallel CPU contention — keep the native run uncontended.
add_test(NAME OpenGuitarMultiFx_UnitTests COMMAND OpenGuitarMultiFx_Tests)

# Everything except the wall-clock perf suites (categories "Bench" and
# "Probe"): they measure real-time bounds and cannot hold them under
# qemu-aarch64 emulation — the aarch64 CI job runs this set, and perf
# asserts elsewhere go log-only under emulation (Tests/TestEnvironment.h),
# which is what makes parallel sharding safe there.
#
# Registered as 16 shards because the runner is serial AND suite weights are
# lopsided (amp suites carry 10-20 sub-tests each): `ctest --parallel 4`
# backfills as short shards finish, so the makespan tracks total-work/4
# instead of the heaviest 1/N-th — 4 shards stalled on a >40 min straggler,
# 8 still left a ~39 min tail; finer packing shrinks the straggler bound.
foreach (shard RANGE 0 15)
  add_test(NAME OpenGuitarMultiFx_UnitTests_NoPerf.shard${shard}
           COMMAND OpenGuitarMultiFx_Tests --shard ${shard}/16
                   --skip-category Bench --skip-category Probe)
endforeach()

# Offline renderer: WAV in -> one registered effect -> WAV out, for listening sessions
# without launching the app (see RenderFx.cpp for usage).
juce_add_console_app(OpenGuitarMultiFx_RenderFx
  PRODUCT_NAME "OpenGuitarMultiFx RenderFx"
  BUNDLE_ID "com.openguitarmultifx.RenderFx"
)

target_sources(OpenGuitarMultiFx_RenderFx PRIVATE
  RenderFx.cpp
  ${OGMFX_SOURCES}
)

target_include_directories(OpenGuitarMultiFx_RenderFx PRIVATE ../Source)

target_compile_definitions(OpenGuitarMultiFx_RenderFx PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
)

target_link_libraries(OpenGuitarMultiFx_RenderFx PRIVATE
  juce::juce_audio_basics
  juce::juce_audio_formats
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
