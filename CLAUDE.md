# Important Rules

1. Only ever **edit or create files** under `MyCPPMods/`.
2. `RE-UE4SS/` is a build dependency (a git submodule providing the UE4SS
   engine and headers), not part of this project. Never edit it, and don't
   propose changes to it. Reading its headers/docs for API reference is fine
   and often necessary — see below.
3. Do not modify build artifacts (`build/`, `out/`, `.vs/`) — regenerate them
   instead.

# Scope

C++ mods for Palworld (Unreal Engine 5.1), built on the UE4SS modding
framework. See `MyCPPMods/TraceVisualizer/README.md` for the current mod.

# Working with RE-UE4SS

Building anything in `MyCPPMods/` compiles against RE-UE4SS's headers, and the
root `CMakeLists.txt` pulls it in via `add_subdirectory(RE-UE4SS)`. So while you
should never *edit* files under `RE-UE4SS/`, you will often need to *read* them:

- `RE-UE4SS/UE4SS/include/` — the UE4SS mod API (`CppUserModBase`, hooks, GUI).
- `RE-UE4SS/deps/first/Unreal/include/Unreal/` — the Unreal reflection API
  (`UObject`, `FProperty`, `UFunction`, etc.). This is itself a nested
  submodule (`UEPseudo`) and may not be cloned locally — see the Prerequisites
  section of the TraceVisualizer README if it's missing.
- `RE-UE4SS/docs/` — official guides and API reference.
- `RE-UE4SS/UE4SS/src/`, `RE-UE4SS/cppmods/` — UE4SS's own implementation and
  example mods, useful as reference for how an API is actually meant to be
  called (signatures alone don't always show usage).

When adding a new mod, resolve engine calls through reflection
(`StaticFindObject`, `FProperty` offsets/sizes) rather than hardcoding struct
layouts — see `MyCPPMods/TraceVisualizer/src/TVReflect.hpp` for the rationale
and the pattern to follow.
