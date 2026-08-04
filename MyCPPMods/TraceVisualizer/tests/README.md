# Geometry and projection tests

`TVMath`, `TVShapes`, `TVDrawList` and `TVProject` have no UE4SS or Unreal
dependency, which means they can be built and run on any machine with a C++20
compiler — no game, no engine, no Windows.

That matters because the maths is the part that is easy to get quietly wrong and
hard to eyeball in-game: a capsule whose `HalfHeight` convention is off by one
radius still looks like a capsule.

These check real properties rather than just "did it run":

- every sphere vertex lies on the sphere, to 1e-14
- every capsule vertex lies on the capsule surface (distance to the core
  segment equals the radius)
- an Unreal-convention capsule reaches exactly `±HalfHeight`, not
  `±(HalfHeight + Radius)`
- `MakeBasis` is orthonormal and right-handed across a spread of rotations, and
  matches `FRotationMatrix` for known angles
- a cone's base radius equals `Height * tan(Angle)`
- a 90° horizontal FOV puts a point 45° to the right exactly on the screen edge,
  and the derived vertical FOV lands the top edge correctly for the aspect ratio
- a segment straddling the camera clips to the near plane at the geometrically
  correct point instead of vanishing
- draw list lifetimes: immediate-mode entries survive at least one snapshot,
  timed entries expire on schedule, persistent ones do not, and overflow is
  clamped with the full shortfall counted

## Running

```sh
cd MyCPPMods/TraceVisualizer/src

g++ -std=c++20 -Wall -Wextra -I. ../tests/test_geo.cpp TVShapes.cpp TVDrawList.cpp -o /tmp/test_geo && /tmp/test_geo
g++ -std=c++20 -Wall -Wextra -I. ../tests/test_proj.cpp TVProject.cpp -o /tmp/test_proj && /tmp/test_proj
```

Both print `ALL PASS (0 failures)` and exit 0 on success.

On Windows with MSVC:

```bat
cd MyCPPMods\TraceVisualizer\src
cl /std:c++20 /EHsc /W4 /I. ..\tests\test_geo.cpp TVShapes.cpp TVDrawList.cpp /Fe:test_geo.exe && test_geo.exe
cl /std:c++20 /EHsc /W4 /I. ..\tests\test_proj.cpp TVProject.cpp /Fe:test_proj.exe && test_proj.exe
```
