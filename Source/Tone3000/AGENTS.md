# Tone3000

## Purpose
Owns: TONE3000 integration — OAuth2+PKCE login, tone search, model download (`Tone3000Manager`), and the routing table from a TONE3000 (gear, format) pair to a local folder + `EffectRegistry` role (`GearRouting.h`).
Does not own: the UI around it (`Source/UI/Tone3000Panel`, `Tone3000SearchDialog`, `OAuthLoginDialog`) or where downloaded files get loaded into the chain (`Source/Effects/NAMProcessor`, `IRLoaderProcessor`).

This entire module is **optional** — the product works fully with models loaded manually from disk. Never couple a core product feature to it (see root AGENTS.md).

## Code Map

### Find It Fast
| Looking for... | Go to |
|----------------|-------|
| (gear, format) → folder/registry-role/extension mapping | `GearRouting.h::routeFor` |
| Which TONE3000 `gears` value a block's contextual search should use | `GearRouting.h::gearFilterForProcessorName` |
| Login/search/download implementation | `Tone3000Manager.{h,cpp}` |
| PKCE verifier/challenge/state generation | `Pkce.h` |

### Key Relationships
- `Tone3000/` → `Effects/` (one-directional): `GearRouting.h` names `EffectRegistry` roles (`"NeuralAmp"`, `"Cab"`, ...) but never includes/depends on the concrete processor classes.
- Strictly a control-thread component — the audio thread never touches `Tone3000Manager`.

## Public API
| Export | Used By | Change Impact |
|--------|---------|---------------|
| `Tone3000Manager::beginLogin/completeLogin` | `OAuthLoginDialog` | No system-browser launch, no local HTTP listener — UI renders the authorize page in an embedded `WebBrowserComponent` and intercepts navigation to `getRedirectUri()` itself |
| `Tone3000Manager::searchTones/downloadModel/downloadFirstModelForTone` | `Tone3000SearchDialog`, `ParameterPanel` | All callbacks fire on the message thread |
| `GearRouting::routeFor(gear, format)` | Download flow, deciding save folder + auto-created block type | `format` is load-bearing: `"nam"`→NAM engine only, `"ir"`→convolution only; other formats (`aida-x`/`aa-snapshot`/`proteus`) return `supported=false` — UI must refuse the download, not hand the engine an unparseable file |

## External Dependencies
| Service | Used For | Failure Mode |
|---------|----------|--------------|
| tone3000.com API (OAuth2+PKCE, endpoints tones/models/makes/tags, 100 req/min) | Tone search + model download | Access tokens are short-lived (~1hr observed); every call after expiry used to fail with an opaque "Search failed" (HTTP 401) until `refreshAccessTokenBlocking()` and `httpGetWithRefresh()` were added (commit `bf32ee8`) |

## Entry Points
| Task | Start Here |
|------|------------|
| Add a new gear/format route | `GearRouting.h::routeFor` |
| Change login/search/download flow | `Tone3000Manager.{h,cpp}` |
| Debug a search/download API failure | Check `Tone3000Manager::httpGetWithRefresh` (401 → refresh → retry) first |

## Decisions
| Decision | Why | Rejected |
|----------|-----|----------|
| Login via embedded `WebBrowserComponent`, not system browser + loopback HTTP listener | Final target is a touchscreen device with no browser installed at all; this is also TONE3000's own documented recommendation for native apps | System-browser + `LoopbackServer` (built first, then deleted — see commit `52bb680`) |
| No client secret anywhere, only PKCE | `t3k_cs_...` is documented server-only; embedding it here is a bug by definition. There is no shared/demo `client_id` — each deployment needs its own publishable `t3k_pub_...` (verified against official docs + example client) | Embedding a shared client secret |
| Two separate TONE3000 searches for "amp" vs "amp-cab" gear, on request | Different things to go looking for even though `NAMProcessor` loads either through the identical code path | One merged amp/amp-cab search |
| `architecture` filter passed explicitly when A2 results are wanted | Per TONE3000's docs, omitting it means "A1 + Custom, EXCLUDING A2" — there is no confirmed single value meaning "everything" | Assuming omitted = "all architectures" |
| `WEBKIT_DISABLE_COMPOSITING_MODE=1` set (Linux only, `Source/Main.cpp`'s `initialise()`, before anything that could spawn the WebKit subprocess) via `setenv` with don't-overwrite | User-reported: the embedded login browser rendered blank. WebKitGTK is documented to render blank under Wayland compositors (this dev machine runs KDE Plasma Wayland) unless its own compositing is disabled — a known category of issue, applied here as the standard mitigation. **Not independently confirmed against a live TONE3000 login** (needs real credentials this session doesn't have) — flagged for the user to verify, not asserted as definitely fixed | Assuming it was a code bug in `OAuthLoginDialog`/`OverlayHost` — read through both; the overlay push/pop and redirect-interception logic look correct, making an environment-level WebKitGTK rendering issue the more likely explanation |

## Patterns

### Adding a new downloadable gear/format pair
1. Add a case in `GearRouting.h::routeFor` — set `supported=true`, a save `subfolder`, a `registryRole` (or empty if no auto chain-block fits), and `fileExtension`.
2. If it should auto-create a chain block, add the matching entry to `subfolderForProcessorName`/`gearFilterForProcessorName` so contextual search and the file picker stay in sync.
3. Confirm the engine can actually load the format (`Source/Effects/AGENTS.md`) before setting `supported=true` — `aida-x`/`aa-snapshot`/`proteus` deliberately stay `supported=false`.

## Contracts
- `gear`/`format` values passed to the API are the *exact* enum strings TONE3000 uses (verified against their docs, not guessed) — see the full list in `Tone3000Manager::Tone`'s doc comment. Don't invent new gear/format strings.
- Model files are opaque bytes — `httpDownloadToFile` streams straight to disk and must never round-trip through a `juce::String`.
- The `tone_id` filter on `GET /models` was *not* confirmed against the live API at integration time (only that the endpoint lists models with `model_url`+`tone_id`) — if a tone with known models comes back empty, check this first (flagged in source, commit `20100ff`).

## Pitfalls
- Omitting the `architecture` search parameter does **not** mean "all architectures" — it silently excludes A2 results. Passing `"2"` explicitly then excludes A1/Custom in turn (one value, not a combinable list like `gears`).
- `redirectUri` (`http://127.0.0.1:17872/callback`) is never actually connected to — it's purely the string matched against embedded-browser navigation events, a holdover naming from the earlier loopback-server design. Don't try to stand up a listener on that port.
- A stale-search race existed where an out-of-order API response could overwrite newer search results — `Tone3000SearchDialog` now clears results immediately on a new search and guards against overwriting with a stale response (commit `0050b30`); any new search UI must preserve this guard.

## Boundaries

### Never
- Add a client secret to this module — PKCE means it should never need one.
- Call anything in `Tone3000Manager` from the audio thread.
- Couple a core (non-optional) product feature to this module's availability.
