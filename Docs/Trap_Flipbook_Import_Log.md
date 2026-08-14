# Trap Flipbook Import Log

Full action log for converting 4 trap sprite sheets into working `PaperFlipbook` assets.
Import + flipbook assembly only -- nothing was placed in the level.

## Step 1 -- Located files

`RawAssets/Meshy_Traps/` contained exactly 4 files:
- `RawAssets\Meshy_Traps\electric_trap_flipbook.png`
- `RawAssets\Meshy_Traps\saw_trap_flip_book.png`
- `RawAssets\Meshy_Traps\spike_column_flip_book.png`
- `RawAssets\Meshy_Traps\spike_floor_flipbook.png`

All 4 confirmed as genuine PNG data via magic-byte check (`file` command), all four sheets are
1536x1024 canvases, 8-bit RGBA.

## Methodology used for all 4 sheets

Rather than trust filename hints ("flipbook") or assume a fixed frame count, each sheet's grid
was determined two ways and cross-checked against each other:

1. **Visual inspection** (read the actual image).
2. **Programmatic content-density analysis**: for each candidate equal-division (2, 3, 4, 5, 6, 8
   rows/columns), compute the per-row/per-column pixel standard deviation, then compare the
   average value at the candidate gutter positions (division boundaries) against the average
   value at candidate cell centers. A clean grid shows a large drop in variance exactly at the
   gutters (low-detail background strip) relative to the content-bearing cell centers -- this
   "separation score" was computed for every candidate division on every sheet.

**Convention decision (applies to all 4, logged once here rather than repeated per trap):**
inspecting this project's one real precedent for animation-frame sprites
(`FB_Enemy_DeathBotFlying_Idle` / `SP_Enemy_DeathBotFlying_Idle_01..05`) showed that every frame
sprite in that flipbook points at a different `source_uv`/`source_dimension` sub-region of the
**same single** `T_Enemy_DeathBotFlying` texture -- confirmed directly by querying those sprites'
properties, not assumed. Every trap below follows this exact pattern: one `T_Trap_<Name>` texture
holding the full, un-sliced sheet, and 8 `SP_Trap_<Name>_NN` sprites each with a different UV
rect. This is a deliberate, logged deviation from a literal "slice into individual frame *image
files*" reading of the brief -- it matches this project's actual established convention (which
the brief itself pointed me at) more closely than pre-cropping 8 separate PNGs would have, and
avoids duplicating pixel data across many small texture assets for no benefit.

`PaperFlipbook.key_frames` structure (confirmed via the same DeathBotFlying precedent) is a list
of `{sprite, frame_run}` structs, `frame_run=1` per entry, played at `frames_per_second`. All 4
new flipbooks use this identical structure.

---

## Trap 1 -- Electric

**File identified:** `electric_trap_flipbook.png` -- unambiguous, filename directly names the
trap type.

**Grid detection:** Visual read: a clearly repeating conveyor/rail unit, 4 rows tall, 2 columns
wide. Programmatic check confirmed this decisively: row-division separation score peaked at
**4 rows** (17.7, vs. 1.3 for 2 rows and 3.4 for 8 rows); column-division separation score peaked
overwhelmingly at **2 columns** (47.9, far above every other candidate). Grid = **4 rows x 2
columns = 8 frames**, cell size 768x256 (1536/2, 1024/4).

**Phase structure (flagged per Step 2's request, not specially handled in code):** this is NOT a
uniform loop -- it has a distinct arc: frame 1 is fully off (no electricity), frames 2-4 ramp up
to peak arc intensity, frames 5-6 hold near-peak, frames 7-8 fade back down to off. A real
activate-then-fade cycle, matching the brief's own description of the archetype.

**Frames extracted (all as `source_uv`/`source_dimension` sub-regions of one texture, per the
convention decision above -- no separate cropped image files):**

| # | Asset | source_uv | source_dimension |
|---|---|---|---|
| 1 | `SP_Trap_Electric_01` | (0, 0) | 768x256 |
| 2 | `SP_Trap_Electric_02` | (768, 0) | 768x256 |
| 3 | `SP_Trap_Electric_03` | (0, 256) | 768x256 |
| 4 | `SP_Trap_Electric_04` | (768, 256) | 768x256 |
| 5 | `SP_Trap_Electric_05` | (0, 512) | 768x256 |
| 6 | `SP_Trap_Electric_06` | (768, 512) | 768x256 |
| 7 | `SP_Trap_Electric_07` | (0, 768) | 768x256 |
| 8 | `SP_Trap_Electric_08` | (768, 768) | 768x256 |

Backing texture: `T_Trap_Electric` (`/Game/Environments/CityBiome/Traps/T_Trap_Electric`),
1536x1024, imported from the unsliced source PNG.

**FPS chosen: 15.** Reasoning: electricity/arc effects read best fast and crackly -- this is an
ambient hazard effect, not a slow mechanical telegraph the player needs a long visual warning for
in the same way a stabbing spike does. 8 frames at 15fps gives a ~0.53s full cycle, fast enough to
feel like genuine electrical arcing rather than a slow strobe.

**Final flipbook:** `FB_Trap_Electric`
(`/Game/Environments/CityBiome/Traps/FB_Trap_Electric`), 8 frames, 15.0 fps. Verified post-import:
`frames_per_second=15.0`, `key_frames` count=8, in the exact order above.

**Ambiguity/judgment calls:** none of real significance -- both row and column statistical
signals were strong and unambiguous, and matched the visual read exactly.

---

## Trap 2 -- Saw

**File identified:** `saw_trap_flip_book.png` -- unambiguous filename.

**Grid detection -- the one genuinely ambiguous case, logged explicitly per the brief's
instructions:**

Row analysis clearly confirmed **4 rows** (separation score 21.1, clearly the best candidate).

Column analysis was **not** as clean as the other 3 sheets: 2-column separation scored only 9.5,
while 8-column scored 11.8 -- a much smaller, less decisive gap than e.g. Electric's 47.9 for 2
columns. On the aggregate statistic alone, 2 columns is not obviously "the" answer here.

**Judgment call made, with reasoning:** I resolved this in favor of **2 columns** anyway, for two
concrete reasons rather than just picking the higher aggregate score:
1. A finer-grained row/column profile printout (not just the aggregate candidate-comparison
   score) showed one specific, precisely-located dip in column variance centered almost exactly
   at x=768 -- the exact midpoint of a 1536px canvas, i.e. exactly where a real 2-column boundary
   would fall. This is a real, localized signal, not an artifact of noise.
2. Content plausibility: 8 columns would mean a cell width of only 192px. The mechanical detail
   actually visible in the sheet (gear wheels, blade assemblies, chain links) is clearly wider
   than that in every frame -- an 8-column reading doesn't fit what's actually drawn.

The weaker aggregate signal (vs. Electric/SpikeFloor's very clean 2-column dominance) is most
likely because Saw's left and right halves are both similarly detailed/busy (mechanical parts on
both sides of the gutter), unlike Electric where one side is calmer -- so the content-vs-gutter
contrast is genuinely less extreme here even though the gutter position itself is real and
correctly located.

Grid = **4 rows x 2 columns = 8 frames** (same orientation as Electric), cell size 768x256.

**Phase structure:** frames 1-2 read as idle/retracted (blade tucked away), frames 3-4 show the
blade extending and beginning to spin (visibly growing in the frame as it extends toward the
"camera"), frames 5-6 show the blade at or near full extension/peak spin, frames 7-8 return to
idle/retracted. Unlike the spike traps below, the retract transition here reads as more abrupt
(no visible intermediate "retracting" frame the way the spike traps have) -- flagging this as an
observation, not something I altered or compensated for.

**Frames extracted:**

| # | Asset | source_uv | source_dimension |
|---|---|---|---|
| 1 | `SP_Trap_Saw_01` | (0, 0) | 768x256 |
| 2 | `SP_Trap_Saw_02` | (768, 0) | 768x256 |
| 3 | `SP_Trap_Saw_03` | (0, 256) | 768x256 |
| 4 | `SP_Trap_Saw_04` | (768, 256) | 768x256 |
| 5 | `SP_Trap_Saw_05` | (0, 512) | 768x256 |
| 6 | `SP_Trap_Saw_06` | (768, 512) | 768x256 |
| 7 | `SP_Trap_Saw_07` | (0, 768) | 768x256 |
| 8 | `SP_Trap_Saw_08` | (768, 768) | 768x256 |

Backing texture: `T_Trap_Saw` (`/Game/Environments/CityBiome/Traps/T_Trap_Saw`), 1536x1024.

**FPS chosen: 12.** Reasoning: a spinning saw blade should read as genuinely fast-moving/dangerous
(faster than the deliberate stabbing spikes below), but the mechanical extend-arm motion is more
substantial than the purely atmospheric electric arc, so it sits between Electric's 15 and the
spike traps' 10. 8 frames at 12fps gives a ~0.67s cycle.

**Final flipbook:** `FB_Trap_Saw` (`/Game/Environments/CityBiome/Traps/FB_Trap_Saw`), 8 frames,
12.0 fps. Verified post-import: `frames_per_second=12.0`, `key_frames` count=8, correct order.

---

## Trap 3 -- Spike Column

**File identified:** `spike_column_flip_book.png` -- unambiguous filename.

**Grid detection:** Visual read was unambiguous and clean from the start: 2 clear horizontal
bands, each containing 4 side-by-side column segments showing a smooth retract->extend->peak->
retract progression reading left-to-right across each band. Programmatic check confirmed this
decisively and from a *different* axis than the other 3 sheets: row separation peaked at
**2 rows** (25.5, clearly ahead of 4 rows' 16.3), column separation peaked at **4 columns** (29.5,
clearly ahead of every other candidate). Grid = **2 rows x 4 columns = 8 frames** -- the one sheet
with the opposite orientation from the other three -- cell size 384x512 (1536/4, 1024/2).

**Phase structure:** a clean, smooth cycle: frame 1 fully retracted, frames 2-4 extend
progressively, frame 5 holds at full extension/peak, frames 6-8 retract progressively back to
frame-1's state. The single cleanest, most symmetric progression of all 4 traps -- no ambiguity.

**Frames extracted:**

| # | Asset | source_uv | source_dimension |
|---|---|---|---|
| 1 | `SP_Trap_SpikeColumn_01` | (0, 0) | 384x512 |
| 2 | `SP_Trap_SpikeColumn_02` | (384, 0) | 384x512 |
| 3 | `SP_Trap_SpikeColumn_03` | (768, 0) | 384x512 |
| 4 | `SP_Trap_SpikeColumn_04` | (1152, 0) | 384x512 |
| 5 | `SP_Trap_SpikeColumn_05` | (0, 512) | 384x512 |
| 6 | `SP_Trap_SpikeColumn_06` | (384, 512) | 384x512 |
| 7 | `SP_Trap_SpikeColumn_07` | (768, 512) | 384x512 |
| 8 | `SP_Trap_SpikeColumn_08` | (1152, 512) | 384x512 |

Backing texture: `T_Trap_SpikeColumn` (`/Game/Environments/CityBiome/Traps/T_Trap_SpikeColumn`),
1536x1024.

**FPS chosen: 10.** Reasoning: a physical stabbing hazard needs a fair, readable telegraph before
it can hurt the player -- matches the existing `FB_Enemy_DeathBotFlying_Idle` precedent's own
10fps exactly, chosen deliberately rather than coincidentally, since both are "give the player
time to read this" animations rather than pure atmospheric effects. 8 frames at 10fps gives a
0.8s cycle, long enough to be dodge-able.

**Final flipbook:** `FB_Trap_SpikeColumn`
(`/Game/Environments/CityBiome/Traps/FB_Trap_SpikeColumn`), 8 frames, 10.0 fps. Verified
post-import: `frames_per_second=10.0`, `key_frames` count=8, correct order.

---

## Trap 4 -- Spike Floor

**File identified:** `spike_floor_flipbook.png` -- unambiguous filename.

**Grid detection:** Visual read: 4 rows, 2 columns, same orientation as Electric/Saw. Programmatic
check confirmed: row separation peaked at **4 rows** (15.3, ahead of 8 rows' 9.1); column
separation peaked decisively at **2 columns** (28.8, clearly the dominant candidate, same clean
pattern as Electric). Grid = **4 rows x 2 columns = 8 frames**, cell size 768x256.

**Phase structure:** frame 1 retracted (no visible spikes), frame 2 spikes just starting to show,
frames 3-4 spikes extending further (visibly longer, chain linkage becoming visible as the
mechanism descends), frames 5-6 spikes at or near maximum length/peak danger, frames 7-8 retract
back to frame-1's state. A clean, smooth extend-hold-retract arc, similar in spirit to Spike
Column but read in the 4x2 orientation instead of 2x4.

**Frames extracted:**

| # | Asset | source_uv | source_dimension |
|---|---|---|---|
| 1 | `SP_Trap_SpikeFloor_01` | (0, 0) | 768x256 |
| 2 | `SP_Trap_SpikeFloor_02` | (768, 0) | 768x256 |
| 3 | `SP_Trap_SpikeFloor_03` | (0, 256) | 768x256 |
| 4 | `SP_Trap_SpikeFloor_04` | (768, 256) | 768x256 |
| 5 | `SP_Trap_SpikeFloor_05` | (0, 512) | 768x256 |
| 6 | `SP_Trap_SpikeFloor_06` | (768, 512) | 768x256 |
| 7 | `SP_Trap_SpikeFloor_07` | (0, 768) | 768x256 |
| 8 | `SP_Trap_SpikeFloor_08` | (768, 768) | 768x256 |

Backing texture: `T_Trap_SpikeFloor` (`/Game/Environments/CityBiome/Traps/T_Trap_SpikeFloor`),
1536x1024.

**FPS chosen: 10.** Same reasoning as Spike Column -- a physical stabbing hazard needs a readable
telegraph, matching the DeathBotFlying precedent's 10fps.

**Final flipbook:** `FB_Trap_SpikeFloor`
(`/Game/Environments/CityBiome/Traps/FB_Trap_SpikeFloor`), 8 frames, 10.0 fps. Verified
post-import: `frames_per_second=10.0`, `key_frames` count=8, correct order.

---

## Overall notes

- **All 4 sheets turned out to have exactly 8 frames**, despite this being determined
  independently per sheet (never assumed uniform, per the brief) -- confirmed by two different
  methods (visual + statistical) landing on 8 for all four, on two different grid orientations
  (3 sheets at 4x2, 1 sheet at 2x4). This is a coincidental consistency in the source art, not an
  assumption carried in from the brief's "6-8 frames" description of the electric-floor archetype.
- **No pre-existing assets were touched.** `Content/Environments/CityBiome/Traps/` did not exist
  before this pass (confirmed via asset registry query before any import). The import script
  additionally refuses (logs an error, skips) rather than overwrites if any target name were ever
  found to already exist.
- **Nothing was placed in the level.** Import and flipbook assembly only, per the brief.
- **40 total new UE assets** (confirmed via a final asset-registry query): 4 textures + 32
  sprites + 4 flipbooks. This log file is a separate, non-UE deliverable. Nothing committed yet.

## Post-import fix -- left/right wobble

After the initial import, playback showed a visible left-right "wobble" -- reported directly by
the project owner as looking like the sheets "weren't cut the same way each time."

**Diagnosis (measured, not guessed):** used PIL to measure the actual non-transparent content
bounding box within each frame's raw grid cell, in every source PNG. This revealed a clean,
systematic pattern -- NOT random per-frame jitter: the object's horizontal center was consistent
*within* a given grid column, but shifted noticeably *between* columns:

| Trap | Column-to-column content-center-x spread | Within-column consistency |
|---|---|---|
| Electric | 16px (col1 avg 388, col2 avg 375) | tight (≤3px) |
| Saw | 4px | already tight -- negligible issue |
| SpikeColumn | 86px (four columns: 236, 228, 181, 152) | very tight (≤3px per column) |
| SpikeFloor | 53px (col1 avg 405, col2 avg 358) | fairly tight (≤7px) |

This means the grid *boundaries* themselves were correct (confirmed independently in Step 2 via
the gutter-detection method) -- the wobble comes from the source art's own content not being
drawn perfectly re-centered in every cell, which is a known characteristic of multi-panel
AI-generated sheets. The default `PaperSprite` pivot (`CENTER_CENTER`, i.e. the geometric center
of the raw UV rect) has no way to know this and anchors every frame at its cell's raw center
regardless -- so the visible object shifts by exactly the source art's own per-column drift.

**Fix applied:** for every one of the 32 sprites, set `pivot_mode = CUSTOM` and
`custom_pivot_point` to that *specific frame's own measured content-bbox center* on the X axis
(eliminating the column bias entirely, not just averaging it down), while leaving the Y pivot at
the raw cell's geometric center for every frame. Y was deliberately left alone: the vertical bbox
range per trap is mostly the effect's own height genuinely changing frame to frame (electric arcs
growing/shrinking, spikes extending/retracting) -- correcting Y the same way would have anchored
to a growing/shrinking bounding box and introduced a new, artificial vertical bob that doesn't
exist in the source art. The report was specifically about left-right movement, and the measured
data supported treating X and Y differently rather than applying the same correction to both.

Verified post-fix: `SP_Trap_Electric_02` now reports `pivot_mode=CUSTOM`,
`custom_pivot_point=(1142, 128)` (previously the uncorrected default center was `(1152, 128)`) --
a 10px correction, consistent with the diagnosed 13-16px column bias. Same fix applied and
verified across all 32 sprites in all 4 flipbooks.

## Second post-import fix -- up/down wobble with a neighboring frame visibly bleeding in

The project owner reported the pivot fix above did not fully resolve it -- Electric specifically
still wobbled vertically, and described seeing "a piece of another picture pop into the frame"
during playback. That specific symptom (not just position drift, but a *different frame's
content* becoming visible) ruled out a pivot/anchor problem, since a pivot only repositions
where a frame is drawn -- it can't make a different frame's pixels appear.

**Diagnosis:** cropped all 8 Electric frames out of the raw source PNG with PIL and inspected
each individually -- every single cropped frame was clean on its own, no defect baked into the
source pixels. Since the problem is invisible in a static crop but visible during actual
in-engine playback, the cause has to be in *how the GPU samples the texture at render time*, not
in the crop boundaries themselves. Checked `T_Trap_Electric`'s texture settings: `filter` was
`TF_DEFAULT` (bilinear filtering). Bilinear filtering blends a sampled pixel with its neighboring
texels -- and because none of the 4 trap sheets have any padding/gutter reserved between grid
cells, sampling near the edge of a frame's UV rect blends in a sliver of the texel data from the
*next cell over*. Since Electric's grid is 4 rows stacked vertically, this cross-cell bleed shows
up specifically as a vertical artifact -- exactly matching "up and down" with a fragment of a
different frame appearing.

**Fix applied:** set `filter = TF_NEAREST` (point/nearest sampling, no interpolation) on all 4
trap textures (`T_Trap_Electric`, `T_Trap_Saw`, `T_Trap_SpikeColumn`, `T_Trap_SpikeFloor`) --
applied to all 4 as a preventive/consistency measure, not just Electric, since all 4 have the
same unpadded-grid construction and are equally susceptible even if only Electric was reported so
far. Point sampling means every rendered pixel comes from exactly one source texel, never a blend
across a cell boundary, which eliminates this class of bleed entirely rather than reducing it.

**Known tradeoff, disclosed rather than hidden:** nearest/point filtering can look very slightly
more jagged on diagonal edges compared to bilinear's smooth blending, since there's no
interpolation at all now. Given these are detailed painted sprites (not pixel art), this is a
real visual tradeoff, not a free fix -- but it's the standard, correct fix for unpadded sprite
sheets in Unreal/Paper2D, and eliminating visible content-bleed takes priority over minor edge
smoothness. If the sharper edges read poorly in practice, the alternative fix is adding real
pixel padding between cells in the source sheets themselves (would require re-authoring the
source PNGs, not done here).

## Frame-gated contact damage -- Spike Column

The project owner asked whether Spike Column could damage the player only while the spikes are
actually extended, not during its idle/retracted frames. Two decisions were confirmed up front:

- **Scope:** build a reusable `ATrapHazard` C++ base class (not a one-off), since the same
  frame-gated-damage need applies to all 4 traps, not just Spike Column.
- **Dangerous range:** frames 4-6 in the 1-indexed asset naming (`SP_Trap_SpikeColumn_04..06`) --
  0-indexed as `DangerousFrameStart=3`, `DangerousFrameEnd=5` to match
  `UPaperFlipbookComponent::GetPlaybackPositionInFrames()`'s 0-based indexing.

**`ATrapHazard` (`Source/PythonTest/TrapHazard.h/.cpp`):** a `UBoxComponent` root
(`OverlapAllDynamic` profile, same as `ARoomExitTrigger`) drives a real BeginOverlap/EndOverlap
against the player -- deliberately NOT following `ADeathMetalCatEnemyBase`'s distance-based
pattern, since that class only exists because the enemy/player capsules share the "Pawn"
collision profile (Block/Block, can never overlap each other); a static trap box has no such
mismatch. A `UPaperFlipbookComponent` attached to the box is purely cosmetic. Every `Tick`, while
the player is overlapping, checks `GetPlaybackPositionInFrames()` against
`[DangerousFrameStart, DangerousFrameEnd]` and applies `ContactDamage` via the existing
`UGameplayStatics::ApplyDamage` path (same as every other damage source in the game), gated by
`ContactDamageCooldown` so continuous overlap doesn't spam damage every tick. The player's own
`TakeDamage`/`CanTakeDamage()` already handles i-frames -- this class doesn't duplicate that.

Compiled via a full engine rebuild (Live Coding can't safely register a brand-new `UCLASS`;
editor was closed, `Build.bat PythonTestEditor Win64 Development` run directly, DLL timestamp
verified newer than both the source and the prior DLL, then the editor was reopened).

**`BP_Trap_SpikeColumn`** (`/Game/Environments/CityBiome/Blueprints/BP_Trap_SpikeColumn`) created
as a Blueprint child of `ATrapHazard` via the Python Editor Scripting API
(`unreal.BlueprintFactory` with `parent_class` set, then `unreal.BlueprintEditorLibrary.compile_blueprint`),
with:
- `Flipbook` = `FB_Trap_SpikeColumn`
- `DangerousFrameStart` = 3, `DangerousFrameEnd` = 5
- `ContactDamage` = 15.0, `ContactDamageCooldown` = 1.0s
- `DamageBoxExtent` = (60, 60, 220) -- a placeholder sized to roughly cover the column + spike
  reach at full extension; tune in-editor once actually placed in a level, against the real
  visual silhouette.

**API pitfall hit and resolved (worth recording since it cost significant time):** verifying the
created Blueprint's parent class via `bp.generated_class().get_default_object()` followed by
`isinstance(...)` gave a false negative every time, making it look like the factory's
`parent_class` property wasn't being honored. It was actually being honored correctly the whole
time -- `get_default_object()` called as a *method* on the `unreal.Class` instance returned by
`generated_class()` does not return that class's CDO; it silently returns something whose
`get_class()` reports the generic `/Script/Engine.BlueprintGeneratedClass` regardless of the
Blueprint's real parent. The correct call is the free function
`unreal.get_default_object(gen_class)`, which returns the real CDO and confirms `isinstance`
correctly. Not yet placed in any level -- this was a create-and-configure-only task, matching
the original trap-import task's own "do not place in the level" boundary.

## Frame-gated contact damage -- remaining 3 traps (Electric, Saw, Spike Floor)

Extended the same `ATrapHazard` pattern to the other 3 traps, each as its own
`BP_Trap_<Name>` Blueprint child in `/Game/Environments/CityBiome/Blueprints/`, created via the
now-proven-correct factory + `compile_blueprint` + `unreal.get_default_object(gen_class)`
verification method above. No new C++ was needed -- this is exactly what the reusable-base-class
decision was for.

**Dangerous frame range -- reasoning applied per trap.** No explicit range was given for these
3, so each range was chosen the same way Spike Column's confirmed choice reads structurally
(last ramp/extend frame through the hold/peak frames through the first fade/retract frame),
mapped onto that trap's own phase structure as already documented above:

| Trap | Phase structure (from import log above) | Dangerous frames (1-idx) | 0-indexed (`Start`,`End`) | Reasoning |
|---|---|---|---|---|
| Electric | off(1), ramp(2-4), hold(5-6), fade(7-8) | 4-7 | (3, 6) | Frame 4 is late-ramp (arc nearly fully formed), 5-6 is the sustained hold, 7 is the first fade frame (arc still visibly present, just weakening) -- excludes 2-3 (too faint to read as "on") and 8 (already back to off). |
| Saw | idle(1-2), extending(3-4), peak(5-6), idle(7-8) | 4-6 | (3, 5) | Frame 4 is late extension, 5-6 is full extension/peak spin. Unlike the other 3 traps, the log noted the retract here is *abrupt* -- frames 7-8 are already back to idle/retracted with no visible intermediate "retracting" frame -- so there's no partial-fade frame to include the way Electric/Spike Floor get one. |
| Spike Floor | retracted(1), starting(2), extending(3-4), peak danger(5-6), retract(7-8) | 4-7 | (3, 6) | Frame 4 is late extension, 5-6 is explicitly described in the import log as "peak danger," 7 is the first retract frame (spikes still substantially extended, just beginning to withdraw) -- same structural mapping as Electric. |

**Damage/cooldown values:** kept identical to Spike Column's (`ContactDamage=15.0`,
`ContactDamageCooldown=1.0s`) as a consistent baseline across all 4 traps, rather than inventing
unrequested per-trap balance differences -- these are placeholders, same as Spike Column's,
meant to be tuned once actually placed and playtested.

**`DamageBoxExtent` placeholders** (all explicitly flagged as needing in-editor tuning against
the real visual reach, same disclaimer as Spike Column):
- `BP_Trap_Electric`: `(120, 60, 40)` -- wide, low box matching the 768x256 wide/flat sprite and
  the arc's modest height.
- `BP_Trap_Saw`: `(90, 60, 90)` -- taller than Electric's box since the blade visibly extends
  outward at peak, needs more reach than a purely ambient arc.
- `BP_Trap_SpikeFloor`: `(100, 60, 140)` -- floor-level spikes, tall enough to cover the extended
  spike reach but shorter than the dedicated `BP_Trap_SpikeColumn` (220), since this is a shallower
  floor mechanism rather than a full vertical column.

**Verified post-creation:** all 3 confirmed `isinstance(cdo, unreal.TrapHazard) == True`, correct
flipbook assignment, and correct property readback (`dangerous=[3,6]` for Electric and Spike
Floor, `dangerous=[3,5]` for Saw, matching the table above). None placed in any level.

Verified post-fix: all 4 textures now report `filter=TF_NEAREST` (previously `TF_DEFAULT`).
