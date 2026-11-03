# UI redesign — three selectable themes

Status: in progress, delivered as six PRs (one per card on the Notion board "OpenGuitar MultiFx", titled `Visual novo N/6`).
Prototype: [`prototype.html`](./prototype.html). It is one self-contained file with Sora and the icons embedded, so open it straight in a browser. The top bar switches theme and page. Most controls are clickable.

## Goal

Replace the current flat dark-grey look ("muito amador" feedback, 2026-09-10) with a modern, stage-readable visual system for the ~10" touchscreen. The user picks one of **three themes** in Settings → Appearance. All three share **one layout**, so a theme is only a table of tokens (colours, radii, block style, knob style). It never changes layout, behaviour, presets, or sound.

## Market references (what each one contributes)

| Product | What we take |
|---|---|
| Neural DSP Quad Cortex | Touch grid, big tappable blocks, scenes vs. stomp mode, glass cards (Stage Glass) |
| Line 6 Helix / HX Edit | Category colour per block, snapshots, bottom inspector, scribble-strip naming (Console) |
| Fractal Axe-FX III / FM9 | Precise numeric readouts, deep grid |
| Boss GX-100, Hotone Ampero II, Headrush Prime | Large tiles and a performance (Live) screen |
| Kemper | LED states readable from far away |
| Positive Grid Spark / BIAS FX, UAFX | Modern cards and sheets |
| FabFilter Pro-Q 3 | Numbered EQ nodes plus a composite curve (Master EQ) |
| Teenage Engineering, Elektron | Colour-blocking and strong type (Bold Blocks) |

## Themes and tokens

Source of truth in code: `Source/UI/Theme.{h,cpp}` (`theme::Tokens`). This table mirrors the CSS variables in the prototype (`--bg --s1 --s2 --s3 --ln --ln2 --tx --mu --di --r --rs`). When a value changes, update both.

| Token | Role | Stage Glass | Console | Bold Blocks |
|---|---|---|---|---|
| `background` (`--bg`) | app / full-screen card | `#0a0b0e` | `#1a1b1e` | `#0d0d0d` |
| `surface` (`--s1`) | panels, overlay cards, footer, drawer | `#121419` | `#212226` | `#171717` |
| `raised` (`--s2`) | buttons, fields, idle cells, list rows | `#191c23` | `#2a2b30` | `#202020` |
| `raisedHigh` (`--s3`) | tracks, bypassed fills, pressed | `#242833` | `#36373d` | `#2c2c2c` |
| `line` (`--ln`) | hairlines | white 7 % | `#34353b` | `#262626` |
| `lineStrong` (`--ln2`) | widget borders | white 13 % | `#45464d` | `#3a3a3a` |
| `text` / `textMuted` / `textDim` | type ramp | `#f3f5f8` / `#8b92a0` / `#555c69` | `#e6e6e8` / `#9a9ba2` / `#64656c` | `#f5f2ea` / `#9b978d` / `#5d5a54` |
| `radius` / `smallRadius` | cards & blocks / buttons & cells | 18 / 12 | 5 / 4 | 14 / 10 |
| `uppercaseLabels` | section labels in caps | no | yes | yes |
| `blockStyle` | effect block drawing | `glow` | `topStripe` | `filled` |

### Per-theme rules

- **Stage Glass** (Quad Cortex / iOS): the block is a dark glass card with a light vertical gradient. The icon tile and LED are tinted with the category colour and glow when active. Knob is an arc with glow. Bypass: the icon is desaturated and the glow goes away.
- **Console** (Helix / Ableton): flat graphite with a 6–7 px category stripe on top of the block. Corners are small, labels are uppercase, and numbers use tabular/mono figures. Bypass: the stripe turns grey.
- **Bold Blocks** (Teenage Engineering / Boss): the whole block is filled with the category colour and the text is near-black. Bypass is an outline only, in the category colour, on `raised`. Knob is a solid disc with a pointer.

### Accent colours: three layers, never merged

1. **Theme tokens** (this file): surfaces, lines, text.
2. **Snapshot accent** (`OpenGuitarMultiFxLookAndFeel::getAppAccentColour()`, set by `MainComponent::applySnapshotAccent()`): everything interactive, such as knob fill, toggles, scrollbar, selected cell. It stays snapshot-driven in every theme.
3. **Category / effect accent** (`EffectProcessor::getAccentColour()`): block icon, stripe or fill. Card 2 introduces the more saturated category palette below.

### Category palette (card 2)

| Category | Colour | Category | Colour |
|---|---|---|---|
| Amp | `#FF6A3D` | Reverb | `#3DD6D0` |
| Amp + Cab | `#FF8A4C` | Pitch | `#3BD68A` |
| Drive/Boost | `#F6C343` | Filter/FX | `#9BE15D` |
| Distortion | `#FF9F1C` | Neural | `#E85CF0` |
| Fuzz | `#FF5C5C` | Cab | `#FFA45C` |
| Dynamics | `#FF4D6D` | Utility | `#9AA3B2` |
| Modulation | `#A77BFF` | Delay | `#4C8DFF` |

The prototype's `CAT` table is the reference if this list drifts.

## Global constraints (every card)

- `touch::minTapTarget` (48 px) stays the floor for every interactive element, in every theme. A tighter theme only shrinks radius and padding, never targets.
- Sora stays embedded, with no system font lookup. Theme names and labels drawn by JUCE are ASCII.
- Overlays go through `OverlayHost`, with no native windows. `Tone3000Panel` stays the only settings surface.
- No component paints a surface/line/text literal (`juce::Colour (0xff…)`) once its card has landed. Use `theme::current()`. Semantic colours (tuner in-tune green, EQ band colours, snapshot palette A–H, clip red) are not theme tokens and stay literal.
- After switching a theme the whole tree must repaint correctly with no restart: `OpenGuitarMultiFxLookAndFeel::applyTheme()`, then `sendLookAndFeelChange()`, then `repaint()`. Components that cache colours must re-pull them in `lookAndFeelChanged()`.
- Every visual PR carries screenshots of **all three themes** (before/after) and needs Luiza's visual OK before merge.

## Cards / PRs

| # | Scope | Main files | Acceptance |
|---|---|---|---|
| 1 | Token system, 3 themes, persistence, Settings → Appearance; surfaces of every component moved to tokens | `Theme.{h,cpp}`, `OpenGuitarMultiFxLookAndFeel`, `Tone3000Panel`, `MainComponent`, `Main.cpp`, every component's background/line literals | Theme persists in `<appdata>/OpenGuitarMultiFx/theme.txt`. Switching applies live. Default Stage Glass. `ThemeTests` green |
| 2 | Grid, effect blocks (3 `blockStyle`s), top bar, snapshot strip (name inside the cell, no tap/hold legend), footer; new category palette | `EffectBlockComponent`, `ChainContainer`, `RowEndpointBlock`, `PresetBadge`, `SnapshotBar`, `FooterBar`, `EffectCategory` colours | Bypass/selected readable in all 3 themes. Cell ≥ 48 px |
| 3 | Parameter drawer: page tabs, new knob per theme, numpad on value tap | `ParameterPanel`, `SelectorSwitch`, LookAndFeel `drawRotarySlider` | Knob arc/disc per theme. Value editable by numpad |
| 4 | Add effect as a **list** (one 64 px row per model: icon, name, short description, tag, CPU-cost bars from `docs/CpuCostMap.md`, `+` button; category rail with counts; subcategory chips; search) + Presets (All/Banks/Setlists) + Snapshot editor | `AddEffectBrowserDialog`, `PresetBrowserDialog`, `PromptCards` | Add effect is a list, not tiles. Search filters across categories |
| 5 | Tuner, Master EQ (numbered nodes, composite curve), Settings layout (side navigation, Appearance first), TONE3000 | `TunerOverlay`, `MasterEqDialog`, `Tone3000Panel`, `Tone3000SearchDialog`, `ModelListDialog`, `OAuthLoginDialog` | Settings has section navigation. TONE3000 is still optional |
| 6 | Live (stage) screen via `OverlayHost`: giant preset name, tuner/BPM, 8 footswitch tiles | new `LiveOverlay`, `MainComponent` | Readable from 2 m. Footswitch tiles mirror MIDI/footswitch state |

Cards 2–6 each depend on card 1. Card 4's list and card 5's settings navigation are independent of card 2/3.
