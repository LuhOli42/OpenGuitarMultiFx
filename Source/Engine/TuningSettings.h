#pragma once

#include "TuningProfile.h"

namespace openguitarmultifx::tunings
{

/** Where the user's last-chosen tuning is remembered across launches --
    deliberately separate from PresetManager's preset files (a tuning
    describes the physical instrument plugged in, not a sound you dial
    up, so it has no business living inside a guitar preset). Plain
    comma-separated note names in a small text file, not a new
    PropertiesFile/ValueTree subsystem -- there's exactly one value to
    remember. */
TuningProfile loadSavedTuning();
void saveTuning (const TuningProfile& tuning);

} // namespace openguitarmultifx::tunings
