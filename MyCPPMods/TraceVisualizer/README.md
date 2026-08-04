# TraceVisualizer

Runtime visualisation of rays, sweeps and shape traces for Palworld (Unreal
Engine 5.1), built as a UE4SS C++ mod.

The point is to stop guessing. You draw the trace you are about to rely on,
look at it in-game, and see where it actually goes.

---

## Why the obvious approach does not work

Unreal's debug drawing is compiled out of shipping builds. `EngineDefines.h`
gates it on `ENABLE_DRAW_DEBUG`, which is 0 when `UE_BUILD_SHIPPING` is set, and
every `DrawDebugLine` / `DrawDebugSphere` / `DrawDebugCapsule` body sits inside
that `#if`.

This catches people out because the *reflection data survives*.
`UKismetSystemLibrary::DrawDebugLine` is still a real `UFunction`, you can still
find it, you can still call it — and it does nothing at all, because its body
compiled to empty. The same applies to the `DrawDebugType` parameter on
`LineTraceSingle` and friends: the trace runs, the drawing does not.

So a working solution needs a drawing path that is **not** debug code.

## How this works

Three observations, in order:

1. **`AHUD::DrawLine` is gameplay code, not debug code.** It is
   `UFUNCTION(BlueprintCallable)` on `AHUD`, it survives shipping, and it is
   reachable through reflection. Same for `AHUD::Project`, `DrawText`,
   `DrawRect`.

2. **It only works inside the HUD render pass.** `AHUD::DrawLine` dereferences
   `AHUD::Canvas`, which is only non-null while the HUD is rendering. Calling it
   from anywhere else dereferences null. So we hook `ReceiveDrawHUD`, the
   Blueprint event the engine fires at the end of `AHUD::DrawHUD()`, and issue
   our lines from inside it.

   The hook resolves the function on the *live HUD instance's* class chain
   rather than on `AHUD`. If the game's HUD Blueprint implements
   `ReceiveDrawHUD`, that override is a different `UFunction` and it is the one
   the engine actually calls — hooking the base class function would silently
   never fire.

3. **Traces themselves still work.** `UKismetSystemLibrary::LineTraceSingle`,
   `SphereTraceSingle`, `CapsuleTraceSingle` and `BoxTraceSingle` do their real
   work in shipping; only their visualisation is stripped. We call them through
   reflection and draw the result ourselves.

Everything 3D is projected to screen space by this mod, then drawn as 2D lines
on the canvas.

### Nothing depends on a hardcoded struct layout

The tempting way to call an engine function from a mod is to declare a matching
`struct Params { ... }` and hand it to `ProcessEvent`. That only works if your
layout exactly matches the game's, which depends on the engine version, on
whether `FVector` is float or double, on packing, and on which modules were
compiled in. Get it wrong and you corrupt memory in a way that surfaces as a
crash somewhere unrelated.

Instead, `TVReflect` builds every parameter buffer from the reflection data the
game already carries: offsets from `FProperty::GetOffset_Internal()`, sizes from
`FProperty::GetSize()`, struct fields looked up by name. `FVector` being 12
bytes (UE4) or 24 bytes (UE5) is discovered rather than assumed. A signature
that does not match produces a logged failure and a `false` return, not a
crash — and the panel prints every resolved signature so you can compare against
the engine header directly.

### Projection is verified, not assumed

Routing every vertex through `ProjectWorldLocationToScreen` would cost one
`ProcessEvent` per point, and a single sphere wireframe is ~150 points. So the
projection is done locally in `TVProject`.

Doing your own projection means you can get it wrong. The panel therefore has a
**Run projection calibration** button that compares this mod's projection
against the engine's own `ProjectWorldLocationToScreen` at five points spread
across the view, and reports the worst pixel error. Under a pixel means the fast
path is correct. A large error at the edges but not the centre means the aspect
constraint or FOV scale is wrong, and both are adjustable in the panel.

---

## Building

The mod is part of the existing `MyCPPMods` CMake tree.

```bat
git submodule update --init --recursive
generate_project_files.bat
build_mods_shipping.bat
```

**If `git submodule update --init --recursive` fails on `deps/first/Unreal`:**
that nested submodule (`UEPseudo`) is declared with an SSH URL
(`git@github.com:...`), which fails without SSH keys on GitHub. Either set up an
SSH key, or rewrite it to HTTPS:

```bat
cd RE-UE4SS
git config --file .gitmodules submodule.deps/first/Unreal.url https://github.com/Re-UE4SS/UEPseudo.git
git submodule sync deps/first/Unreal
git submodule update --init deps/first/Unreal
```

Install the resulting `TraceVisualizer.dll` as a UE4SS C++ mod in the usual
layout:

```
Palworld/Binaries/Win64/ue4ss/Mods/TraceVisualizer/dlls/main.dll
```

(UE4SS loads `dlls/main.dll`; rename the built DLL accordingly. Add
`TraceVisualizer : 1` to `mods.txt`.)

---

## First run

Open the UE4SS GUI and select the **TraceViz** tab. Work down the Status
section:

| Row | Meaning if red |
|---|---|
| PlayerController | No controller instance found yet — you are probably still in a menu. |
| HUD | The controller's `MyHUD` is null. The game may not spawn a HUD actor. |
| Camera manager | `PlayerCameraManager` is null; no view to project from. |
| Draw hook installed | `ReceiveDrawHUD` was not found on the HUD class. |
| HUD draw callbacks firing | The hook is attached but never fires — the HUD is hidden, or the game renders its HUD purely through UMG. |

Then press **Place test pattern**. A coordinate triad, sphere, box, capsule,
arrow and cone appear 4 m in front of the camera. If those do not show up, the
problem is in rendering, not in whatever you were trying to visualise — and the
Status rows above tell you which stage failed.

The trace buttons (Line / Sphere / Capsule / Box) fire a real trace out of the
camera and draw the swept volume plus the impact point and surface normal.

Hotkeys: **F9** toggles drawing, **F10** clears.

---

## Using it from your own mod

Copy `include/TraceViz/TraceVizAPI.h` into your project. It is a plain C ABI
resolved at runtime, with no dependency on this mod's internals, so the two can
be built and updated independently.

```cpp
#include <TraceViz/TraceVizAPI.h>

static const TraceVizAPI* g_viz = nullptr;

void MyMod::on_unreal_init()
{
    g_viz = TraceViz_Load();   // null if the visualizer is not installed
}

void MyMod::OnSomethingInteresting(TraceVizVec3 from, TraceVizVec3 to)
{
    if (!g_viz) return;

    TraceVizHit hit{};
    g_viz->SphereTrace(from, to, /*radius*/ 25.0,
                       TRACEVIZ_CHANNEL_VISIBILITY, /*complex*/ 0,
                       /*duration*/ 2.0f, TRACEVIZ_DEFAULT_CATEGORY, &hit);

    if (hit.bBlockingHit)
    {
        g_viz->DrawSphere(hit.Location, 25.0, 24,
                          TraceVizColors::Cyan, 2.0f, 2.0f,
                          TRACEVIZ_DEFAULT_CATEGORY);
    }
}
```

Treat a null return as "visualisation disabled" rather than an error, so your
mod still works when the visualizer is not installed.

**Durations:** `0` draws for one frame (submit every tick — this is the normal
mode), a positive value is seconds, `TRACEVIZ_PERSISTENT` lasts until cleared.

**Categories** group visuals so they can be toggled as a set, from code via
`SetCategoryEnabled` or from the panel.

**Threading:** `Draw*` is safe from any thread. The `*Trace` functions call into
the engine and are game-thread only.

---

## Layout

| File | Role |
|---|---|
| `TVMath.hpp` | Vectors, rotators, colours. No UE dependency. |
| `TVShapes.*` | Tessellates every shape into line segments, including swept volumes. |
| `TVDrawList.*` | Thread-safe segment store with lifetimes and category filtering. |
| `TVProject.*` | World-to-screen projection with near-plane clipping. |
| `TVReflect.*` | Reflection-driven `UFunction` calls and struct reads. |
| `TVEngine.*` | Object discovery, camera sampling, HUD rendering, traces. |
| `TVApiImpl.cpp` | The exported C ABI. |
| `dllmain.cpp` | Mod lifecycle and the diagnostics panel. |

`TVMath`, `TVShapes`, `TVDrawList` and `TVProject` have no engine dependency and
are covered by a standalone test harness — the sphere/capsule/box tessellation
and the projection maths are verified numerically rather than by eye.

---

## Known limits

- **No depth testing.** Canvas lines draw over the world, so a trace behind a
  wall still shows. That matches how `DrawDebug*` behaves with
  `SDPG_Foreground`, and is usually what you want for debugging, but it is not
  occlusion-correct.
- **Cost scales with segment count.** Each segment is one reflected
  `AHUD::DrawLine`, i.e. one `ProcessEvent`. The per-frame budget in the panel
  is the control; the panel reports when it clamps.
- **Text labels are not rendered yet.** The draw list carries them, but
  `AHUD::DrawText` takes an `FString`, and constructing one across the mod
  boundary needs the game's allocator. Deliberately left out rather than done
  on a guess.
- **`ActorsToIgnore` is always empty.** `bIgnoreSelf` is set, which covers the
  common case. Passing a populated array needs a game-allocated `TArray`.
- **Hit actor name is best-effort.** UE4 exposes `FHitResult::Actor`; UE5
  replaced it with `HitObjectHandle`, which is not a plain pointer.
- **Only the `*Single` trace variants are wrapped.** The `*Multi`, `ByProfile`
  and `ForObjects` variants follow the same pattern and are straightforward to
  add.
- **Blueprint-issued traces are not captured automatically.** Hooking
  `UKismetSystemLibrary`'s trace `UFunction`s would visualise every trace the
  game's Blueprints perform, and is a natural next step. Traces the game
  performs in native C++ would still not be visible without signature scanning.
