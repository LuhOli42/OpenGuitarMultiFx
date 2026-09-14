# Vendors chowdsp_wdf (Chowdhury-DSP/chowdsp_wdf) -- a header-only Wave
# Digital Filter toolkit used for the physically-modelled (non-neural) amp
# stage, complementing NAMProcessor's neural captures. Chosen specifically
# for its licence: BSD 3-Clause (verified via the GitHub API's license
# field and the repo's own LICENSE file, not guessed) -- unlike the actual
# finished pedal plugins that use it (e.g. BYOD, GPL-3.0), this toolkit
# itself is embeddable in this closed-source product, same bar NAM's
# engine was chosen against (see root AGENTS.md's Global Decisions).
#
# Pinned to a specific commit rather than the sole v1.0.0 tag (Dec 2022) --
# that tag is ~3.5 years stale against a still-actively-maintained repo
# (latest commit as of this pin: Jul 2026); a moving `main` branch would
# make the build non-reproducible, so an exact commit is the middle
# ground, same reasoning as this project's own Eigen pin.
#
# Unlike nam_core, no SOURCE_SUBDIR workaround needed: chowdsp_wdf's own
# top-level CMakeLists.txt already guards its tests/bench subdirectories
# behind an `is_toplevel` check (get_directory_property(... PARENT_DIRECTORY)),
# which is false when pulled in via FetchContent -- so configuring it
# normally defines exactly one INTERFACE target (chowdsp_wdf /
# chowdsp::chowdsp_wdf) and nothing else, no extra executables to skip.
FetchContent_Declare(
  chowdsp_wdf_src
  GIT_REPOSITORY https://github.com/Chowdhury-DSP/chowdsp_wdf.git
  GIT_TAG 43bcd295e8403de913f9966f4fd7ff9d17c9f1a5
)
FetchContent_MakeAvailable(chowdsp_wdf_src)
