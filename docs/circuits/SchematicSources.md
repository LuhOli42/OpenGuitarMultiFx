# Schematic sources for the models marked "estimated"

Survey of 2026-10-25: for every amp / pedal whose doc says its values are estimated or that no schematic is public,
where a drawing can actually be found. **Verified** = downloaded and opened. Use this before re-deriving a model
(docs/circuits/CircuitFamilies.md's rule: real schematic first).

## Found and downloadable

| Circuit | Source | Kind | Notes |
|---|---|---|---|
| Matchless DC-30 / HC-30 | el34world `Matchless/Matchless_dc30_old.pdf` (4 pages: Ch1, Ch2, PI/power, supply) | hand trace, verified | Elektrotanya's "factory" DC-30 is the same trace. The model differs from it (Ch1 tone stack, CF load, EF86 plate / screen, TONE selector topology), see the audit |
| Soldano Astroverb 16 | el34world `Soldano/Soldano_astroverb_16.pdf` | redraw, verified | used in PR #136; some values are drawing errors |
| Gibson EH-150 | el34world `Gibson/Gibson_eh150.pdf`, `Gibson_eh_150_later.pdf`, `Gibson_eh_150_older.pdf` | factory-style, verified | three revisions; the model's doc says none could be obtained |
| Ampeg B-15 | el34world `Ampeg/Ampeg_B-15N.pdf`, `Ampeg_B-15NF.pdf` (+ B-15NC, B-15S, B-15T, B-15R) | factory, verified | the model targets B-15N rev C; its OT / NFB values are marked estimates |
| Ampeg V-4 / V-4B | V-4B: factory drawing 4010267 (5-71), photos at ampegv4.com `images/schematics/V4B.jpg` and irationaudio.com (2016/02 "ampeg-v4b"); V-4 / VT-22: el34world `Ampeg/Ampegv4_v22.service_manual.pdf`, `Ampegvt22_v4_reva.pdf` | factory, verified | model rebuilt to 4010267; the P.E.C. module contents, L101 and the OT are estimates |
| Mesa Bass 400 | el34world `Mesa_Boogie/Boogie_bass400.pdf` (2 pages) | factory, verified | check whether it is the 400 or the 400+ |
| Trace Elliot GP12 | el34world `Trace_elliot/Trace_Elliot_GP12_serie6-preamp.pdf` (11 pages), `Trace_Elliot_GP12_250Watt.pdf` | factory, verified | the model's pre-shape curve is marked estimated |
| Dumble (ODS family) | el34world `Dumble/*.pdf` (full schematic, pre / power / mods) | community traces | not the Steel String Singer |

## Found, behind a login / captcha / bot block (fetch by hand)

| Circuit | Where | Notes |
|---|---|---|
| Acoustic 360 / 361 | ManualsLib "Acoustic 360 Service Manual" (manual 1003858); Elektrotanya `acoustic_360_361_sm.pdf`; Tapatalk "unofficial Acoustic Control Corp forum" schematic index; TalkBass "Links to Acoustic (ACC) schematics" | full factory service manual (voltages, parts, schematic) |
| Dumble Steel String Singer | AmpGarage `viewtopic.php?p=397996` (#001 / #002 / #004 layouts, #002 hand schematic + corrected PDF) | reverse-engineered |
| Komet 60 / Concorde | AmpGarage threads "Komet 60 Schematic...Accurate?", "Komet K60 or Concord Layout" | traces; accuracy disputed |
| Dr. Z Carmen Ghia | TDPRI "Carmen Ghia" thread (590924), a reverse-engineered schematic + a layout | 150R cathode (layout says 130R) |
| Bad Cat Hot Cat 30 | Music-Electronics-Forum threads point to a Schematic Heaven PDF (link moved) | Hot Cat 30 and 30R differ |
| Boss ODB-3 | DIYStompboxes 2007 hand-drawn schematic | community |
| Boss BD-2 | factory board schematic (hobby-hour, Free Info Society); Gauss Markov redraw | the model already uses the factory board |
| Mu-Tron III | mu-tron.com owner's manual (schematic on p.14 per MEF); GGG "Neutron" replica schematic | the randtronics "complete manual" link has 3 pages only |
| A/DA Flanger | "adadepot" archive (per MEF) | SAD1024 (1977) vs MN3010 (from 5/78) revisions |
| SansAmp Bass Driver DI | DIYStompboxes thread (Russian archive, PIC switching omitted) | the model uses kanengomibako's V1 teardown |

## Not found (no public schematic; the model stays an estimate)

Divided by 13 FTR37, Diezel VH4, Carr Rambler, Fryette Deliverance D120 (only the Deliverance 60 circulates),
Boss VB-2 (MN3207 / MN3102 BBD, ~4 ms).
