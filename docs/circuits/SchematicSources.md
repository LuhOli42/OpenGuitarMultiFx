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
| Ampeg B-15 | el34world `Ampeg/Ampeg_B-15N.pdf`, `Ampeg_B-15NF.pdf` (+ B-15NC, B-15S, B-15T, B-15R) | factory, verified | model rebuilt to rev C (591722 C); only the OT and 5AR4 resistance are estimates |
| Ampeg V-4 / V-4B | V-4B: factory drawing 4010267 (5-71), photos at ampegv4.com `images/schematics/V4B.jpg` and irationaudio.com (2016/02 "ampeg-v4b"); V-4 / VT-22: el34world `Ampeg/Ampegv4_v22.service_manual.pdf`, `Ampegvt22_v4_reva.pdf` | factory, verified | model rebuilt to 4010267; the P.E.C. module contents, L101 and the OT are estimates |
| Mesa Bass 400 | el34world `Mesa_Boogie/Boogie_bass400.pdf` (2 pages) | factory, verified | it is the original 1983 Bass 400; the model uses it for the 400+ (same architecture per the Plus manual, which adds a 7th EQ band) |
| Trace Elliot GP12 | el34world `Trace_elliot/Trace_Elliot_GP12_serie6-preamp.pdf` (11 pages), `Trace_Elliot_GP12_250Watt.pdf` | factory, verified | Series 6 (no SMX): the model's graphic EQ and Pre Shape 1 are now the drawing's circuits; the SMX compressor and Shape 2 stay behavioural |
| Dumble (ODS family) | el34world `Dumble/*.pdf` and schematicheaven.net `dumbleamps/*.pdf`: the 70s ODS `preamp.pdf`, `distorto.pdf`, `power.pdf`, `output.pdf`, the 1997 `97fullschem.pdf` (a 2 x EL34 late ODS), mods and notes | community traces | used for the ODS model (70s set). Not the Steel String Singer. The #124 schematic and layouts sit behind an Amp Garage login |

## Found, behind a login / captcha / bot block (fetch by hand)

| Circuit | Where | Notes |
|---|---|---|
| Acoustic 360 / 361 | ManualsLib "Acoustic 360 Service Manual" (manual 1003858); Elektrotanya `acoustic_360_361_sm.pdf`; Tapatalk "unofficial Acoustic Control Corp forum" schematic index; TalkBass "Links to Acoustic (ACC) schematics" | full factory service manual (voltages, parts, schematic) |
| Dumble Steel String Singer | AmpGarage `viewtopic.php?p=397996` (#001 / #002 / #004 layouts, #002 hand schematic + corrected PDF) | reverse-engineered |
| Komet 60 / Concorde | AmpGarage threads "Komet 60 Schematic...Accurate?", "Komet K60 or Concord Layout" | traces; accuracy disputed |
| Dr. Z Carmen Ghia | TDPRI "Carmen Ghia" thread (590924), a reverse-engineered schematic + a layout | Phoenix rev 1.3 redraw (attachment 305957, 700x543): model rebuilt to its preamp, NFB and output stage; the 5751 PI is not legible at that size |
| Bad Cat Hot Cat 30 | Music-Electronics-Forum threads point to a Schematic Heaven PDF (link moved) | Hot Cat 30 and 30R differ; the model follows the Bad Cat schematic used in #111 (EL34 pair, Edge, Brilliance) and its doc now says so |
| Boss ODB-3 | DIYStompboxes 2007 hand-drawn schematic; a clean full redraw on postimg (3465x1063, linked from DIYSB) | community | model rebuilt to the redraw (discrete stages, LED feedback, IC1b clean path, gyrator Low) |
| Boss BD-2 | factory board schematic (hobby-hour, Free Info Society); Gauss Markov redraw | the model already uses the factory board |
| Mu-Tron III | mu-tron.com owner's manual (schematic on p.14 per MEF); GGG "Neutron" replica schematic | the randtronics "complete manual" link has 3 pages only |
| A/DA Flanger | "adadepot" archive (per MEF) | SAD1024 (1977) vs MN3010 (from 5/78) revisions; model uses the Charlie Barth / moosapotamus SAD1024 trace (2007) + FlangerClone rev5 calibration |
| Fender Super-Sonic 22 | thetubestore.com `Fender-Super-Sonic-22-Schematic.pdf`: Fender service diagram 0078327000 rev A, 3 sheets, 600 dpi, printed rails and test voltages | factory | the model is built from it (docs/circuits/SuperSonic22.md) |
| SansAmp Bass Driver DI | DIYStompboxes thread (Russian archive, PIC switching omitted) | the model uses kanengomibako's V1 teardown |

## Not found (no public schematic; the model stays an estimate)

Diezel VH4, Fryette Deliverance D120 (only the Deliverance 60 circulates).

Found later as community work (2026-10-26):
- Divided by 13 FTR37: Edwin Thoen's 3-page trace with measured voltages (Das Musikding forum "Divided by 13 FTR 37
  Schematic!", file-download 8389; also on Scribd). The model is rebuilt to it.
- Boss VB-2: the BYOC Analog Vibrato (a VB-2 clone) schematic, rev 2.0, via the Wayback Machine
  (web.archive.org/web/20250327200355/http://byocelectronics.com/analogvibratoschematic.pdf). The model's filters
  and emphasis come from it; the delay timing is still calibrated.
- Carr Rambler: Bryce Amps' preamp trace (December 2011). The preamp, tone stack and reverb send are rebuilt to it;
  the power stage and tremolo stay estimated.
