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
| One-shot local HTTP listener catching the system-browser OAuth redirect | `LoopbackServer.{h,cpp}` (used by `Tone3000Panel::doLogin`) |

### Key Relationships
- `Tone3000/` → `Effects/` (one-directional): `GearRouting.h` names `EffectRegistry` roles (`"NeuralAmp"`, `"Cab"`, ...) but never includes/depends on the concrete processor classes.
- Strictly a control-thread component — the audio thread never touches `Tone3000Manager`.

## Public API
| Export | Used By | Change Impact |
|--------|---------|---------------|
| `Tone3000Manager::beginLogin/completeLogin` | `Tone3000Panel` | Manager itself neither launches a browser nor listens: the panel opens the system browser + `LoopbackServer` first, and falls back to `OAuthLoginDialog` (embedded `WebBrowserComponent`, intercepts navigation to `getRedirectUri()`) when that's unavailable |
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
| Embedded `WebBrowserComponent` login as the browserless-device path (now the FALLBACK, see the 2026-09-19 row below), instead of only system browser + loopback listener | Final target is a touchscreen device with no browser installed at all; this is also TONE3000's own documented recommendation for native apps | Dropping the system-browser path entirely (done in `52bb680`, reverted as the desktop default 2026-09-19) |
| No client secret anywhere, only PKCE | `t3k_cs_...` is documented server-only; embedding it here is a bug by definition. There is no shared/demo `client_id` — each deployment needs its own publishable `t3k_pub_...` (verified against official docs + example client) | Embedding a shared client secret |
| Two separate TONE3000 searches for "amp" vs "amp-cab" gear, on request | Different things to go looking for even though `NAMProcessor` loads either through the identical code path | One merged amp/amp-cab search |
| `architecture` filter passed explicitly when A2 results are wanted | Per TONE3000's docs, omitting it means "A1 + Custom, EXCLUDING A2" — there is no confirmed single value meaning "everything" | Assuming omitted = "all architectures" |
| **Desktop login goes through the SYSTEM browser + `LoopbackServer` first (2026-09-19); the embedded `WebBrowserComponent` is only the fallback** (`Tone3000Panel::doLogin` tries `loopback.start(17872)` + `launchInDefaultBrowser()`, falls back to `OAuthLoginDialog` if either is unavailable, i.e. on the browserless touchscreen target). `LoopbackServer` is the class deleted in `52bb680`, restored with one real bug fixed: `read(..., false)` returns 0 if the request is still in flight right after `accept()`, so callbacks were dropped -- it now waits with `waitUntilReady` first, ignores probes/favicon requests (404) and only consumes its one shot on a `/callback` request carrying `code` or `error`. Verified end to end on the host: real login completed, tokens saved | The embedded pane is STILL blank when the app runs directly on the host (Bazzite, WebKitGTK 2.52.5): the `--juce-gtkwebkitfork-child` process is simply gone with no stderr output, cause not found (a gdb follow-fork attempt only caught the unrelated startup `gsettings` fork). Reproducing needs the host, NOT the `juce-dev` container -- see the row below | Keeping the embedded pane as the only login path on a desktop that already has a browser signed in to TONE3000; the user also remembered (correctly) that login needed no such screen before `52bb680` |
| `GDK_BACKEND=x11` forced with overwrite (`setenv(..., 1)`, Linux only, `Source/Main.cpp`'s `initialise()`) | Inside the `juce-dev` container the session exports `GDK_BACKEND=wayland`, contradicting the `gdk_set_allowed_backends("x11")` JUCE's WebKit child calls; the child's `gtk_init` died with `cannot open display: :0`. Reproduced with a bare JUCE `WebBrowserComponent` app (blank inherited env, renders with `GDK_BACKEND=x11`) and confirmed in the real app **inside the container**. Only the child uses GDK; JUCE itself is plain Xlib via XWayland | Believing this fixed the user-facing bug: it did not fix the host case (the user still saw blank), an over-claim from testing in the wrong environment. Kept because it is correct for the container/`GDK_BACKEND=wayland` case and harmless otherwise |
| _Superseded history:_ `WEBKIT_DISABLE_COMPOSITING_MODE=1` was added first (commit `47cc2ef`) on the theory that WebKitGTK renders blank under Wayland; it did not help | Kept only as an audit trail of the wrong turn | — |

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
