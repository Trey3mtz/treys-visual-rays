/*
 * TraceVizAPI.h - public interface to the Palworld trace visualizer.
 *
 * Drop this single header into your own UE4SS C++ mod. It has no dependency on
 * UE4SS, on the Unreal headers, or on the visualizer's internals: it is a plain
 * C ABI resolved at runtime through GetProcAddress, so your mod and the
 * visualizer can be built independently and updated independently.
 *
 * Usage:
 *
 *     #include <TraceViz/TraceVizAPI.h>
 *
 *     static const TraceVizAPI* g_viz = nullptr;
 *
 *     void MyMod::on_unreal_init() {
 *         g_viz = TraceViz_Load();          // returns null if not installed
 *     }
 *
 *     void MyMod::OnFire(TraceVizVec3 muzzle, TraceVizVec3 target) {
 *         if (!g_viz) return;
 *         TraceVizHit hit{};
 *         g_viz->SphereTrace(muzzle, target, 25.0, TRACEVIZ_CHANNEL_VISIBILITY,
 *                            0, 2.0f, TRACEVIZ_DEFAULT_CATEGORY, &hit);
 *         // The sweep, the impact point and the surface normal are now on
 *         // screen for two seconds, and `hit` holds the result.
 *     }
 *
 * Coordinates are Unreal's: X forward, Y right, Z up, centimetres. Rotations
 * are degrees.
 *
 * Threading: every function here is safe to call from the game thread. The
 * Draw* functions are safe from any thread. The *Trace functions call into the
 * engine and must only be used on the game thread.
 */

#ifndef TRACEVIZ_API_H
#define TRACEVIZ_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Bumped only when the struct layout below changes incompatibly. */
#define TRACEVIZ_API_VERSION 1u

/* Name of the exported entry point that TraceViz_Load() searches for. There is
 * deliberately no "name of the providing DLL" constant: see TraceViz_Load()
 * below for why filename-based lookup does not work under UE4SS. */
#define TRACEVIZ_ENTRY_POINT_NAME "TraceViz_GetAPI"

/* Duration values with special meaning. */
#define TRACEVIZ_ONE_FRAME 0.0f
#define TRACEVIZ_PERSISTENT (-1.0f)

#define TRACEVIZ_DEFAULT_CATEGORY 0u

/*
 * Categories are just a uint32_t you pick, so two unrelated mods that both
 * hardcode e.g. `1` will toggle each other's visuals off when either one
 * disables "their" category. TraceViz_MakeCategory hashes a name (your mod's
 * name is a good choice) into a category id, which makes an accidental
 * collision between unrelated mods astronomically unlikely without either
 * one needing to know what id the other picked:
 *
 *     const uint32_t MyCategory = TraceViz_MakeCategory("MyAwesomeMod.Radar");
 */
#ifdef __cplusplus
static inline uint32_t TraceViz_MakeCategory(const char* Name)
#else
static uint32_t TraceViz_MakeCategory(const char* Name)
#endif
{
    /* FNV-1a. Not cryptographic, just needs to spread names apart well. */
    uint32_t Hash = 2166136261u;
    while (Name && *Name)
    {
        Hash ^= (uint32_t)(unsigned char)(*Name++);
        Hash *= 16777619u;
    }
    /* Never collide with the reserved default category. */
    return (Hash == TRACEVIZ_DEFAULT_CATEGORY) ? 1u : Hash;
}

/*
 * Trace channels. These are ETraceTypeQuery values, which are *not* the same
 * as ECollisionChannel: they index the project's trace channel table.
 * TraceTypeQuery1 is Visibility and TraceTypeQuery2 is Camera in a default
 * Unreal project, and Palworld does not appear to change those two. Anything
 * beyond them is game-specific, so pass the raw index.
 */
#define TRACEVIZ_CHANNEL_VISIBILITY 0u
#define TRACEVIZ_CHANNEL_CAMERA 1u

    typedef struct TraceVizVec3
    {
        double X;
        double Y;
        double Z;
    } TraceVizVec3;

    typedef struct TraceVizRotator
    {
        double Pitch;
        double Yaw;
        double Roll;
    } TraceVizRotator;

    /* Linear colour, 0..1 per channel, matching FLinearColor. */
    typedef struct TraceVizColor
    {
        float R;
        float G;
        float B;
        float A;
    } TraceVizColor;

    typedef struct TraceVizHit
    {
        int32_t bBlockingHit;
        TraceVizVec3 ImpactPoint;
        TraceVizVec3 ImpactNormal;
        /* Where the swept shape's origin ended up; equals ImpactPoint for a
           line trace. This is the one you want when placing a shape at the
           point of contact. */
        TraceVizVec3 Location;
        double Distance;
    } TraceVizHit;

    /*
     * The interface table. StructSize lets a newer visualizer add functions
     * without breaking a mod built against an older header: check
     * StructSize >= offsetof(...) before calling anything you are not sure of.
     */
    typedef struct TraceVizAPI
    {
        uint32_t StructSize;
        uint32_t Version;

        /* ---- Drawing. Duration 0 draws for one frame, TRACEVIZ_PERSISTENT
           draws until cleared, anything else is seconds. ---- */

        void (*DrawLine)(TraceVizVec3 Start, TraceVizVec3 End, TraceVizColor Color, float Thickness, float Duration, uint32_t Category);

        void (*DrawArrow)(TraceVizVec3 Start, TraceVizVec3 End, double HeadSize, TraceVizColor Color, float Thickness, float Duration, uint32_t Category);

        void (*DrawPoint)(TraceVizVec3 Center, double Size, TraceVizColor Color, float Thickness, float Duration, uint32_t Category);

        void (*DrawSphere)(TraceVizVec3 Center, double Radius, int32_t Segments, TraceVizColor Color, float Thickness, float Duration, uint32_t Category);

        /* Extent is the half-size on each axis, matching Unreal's box trace. */
        void (*DrawBox)(TraceVizVec3 Center,
                        TraceVizVec3 Extent,
                        TraceVizRotator Rotation,
                        TraceVizColor Color,
                        float Thickness,
                        float Duration,
                        uint32_t Category);

        /* HalfHeight is measured to the outside of the caps, as Unreal does. */
        void (*DrawCapsule)(TraceVizVec3 Center,
                            double HalfHeight,
                            double Radius,
                            TraceVizRotator Rotation,
                            int32_t Segments,
                            TraceVizColor Color,
                            float Thickness,
                            float Duration,
                            uint32_t Category);

        void (*DrawCone)(TraceVizVec3 Origin,
                         TraceVizVec3 Direction,
                         double Height,
                         double AngleDegrees,
                         int32_t Segments,
                         TraceVizColor Color,
                         float Thickness,
                         float Duration,
                         uint32_t Category);

        void (*DrawCoordinateSystem)(TraceVizVec3 Origin, TraceVizRotator Rotation, double Scale, float Thickness, float Duration, uint32_t Category);

        /* ---- Traces. These perform the trace *and* draw it. Return non-zero
           if the trace ran; OutHit may be null if you only want the picture.
           Game thread only. ---- */

        int32_t (*LineTrace)(TraceVizVec3 Start,
                             TraceVizVec3 End,
                             uint8_t TraceChannel,
                             int32_t bTraceComplex,
                             float Duration,
                             uint32_t Category,
                             TraceVizHit* OutHit);

        int32_t (*SphereTrace)(TraceVizVec3 Start,
                               TraceVizVec3 End,
                               double Radius,
                               uint8_t TraceChannel,
                               int32_t bTraceComplex,
                               float Duration,
                               uint32_t Category,
                               TraceVizHit* OutHit);

        int32_t (*CapsuleTrace)(TraceVizVec3 Start,
                                TraceVizVec3 End,
                                double Radius,
                                double HalfHeight,
                                uint8_t TraceChannel,
                                int32_t bTraceComplex,
                                float Duration,
                                uint32_t Category,
                                TraceVizHit* OutHit);

        int32_t (*BoxTrace)(TraceVizVec3 Start,
                            TraceVizVec3 End,
                            TraceVizVec3 HalfSize,
                            TraceVizRotator Orientation,
                            uint8_t TraceChannel,
                            int32_t bTraceComplex,
                            float Duration,
                            uint32_t Category,
                            TraceVizHit* OutHit);

        /* ---- Management ---- */

        void (*Clear)(void);
        void (*ClearPersistent)(void);

        /* Categories let you group visuals and toggle them as a set, both from
           code and from the in-game panel. */
        void (*SetCategoryEnabled)(uint32_t Category, int32_t bEnabled);

        /* Non-zero once the HUD draw hook has fired at least once. If this
           stays zero, the visualizer is loaded but nothing is reaching the
           screen; check the TraceViz tab in the UE4SS GUI. */
        int32_t (*IsRendering)(void);
    } TraceVizAPI;

    typedef const TraceVizAPI* (*TraceVizGetAPIFn)(uint32_t RequestedVersion);

#ifdef __cplusplus
} /* extern "C" */
#endif

/*
 * Convenience loader. Only compiled on Windows, and only if the consumer has
 * not asked to skip it. Returns null when the visualizer is not installed,
 * which your mod should treat as "visualisation disabled" rather than an error.
 *
 * This does NOT look up the visualizer's DLL by name. UE4SS's own C++ mod
 * installation convention renames every mod's compiled DLL to the literal
 * filename "main.dll" (see docs/guides/installing-a-c++-mod.md: the file
 * always ends up at Mods/<ModName>/dlls/main.dll). That means every loaded
 * C++ mod in the process is a module named main.dll, indistinguishable from
 * each other by name -- so a name-based GetModuleHandleW lookup for
 * "TraceVisualizer.dll" would never find anything, no matter how the project
 * was built, and there would be no reliable way to tell TraceVisualizer's
 * main.dll apart from any other mod's main.dll if you tried.
 *
 * Instead this walks every module currently loaded in the process and asks
 * each one for the TraceViz_GetAPI export by name. Exactly one mod can export
 * that symbol, so this finds the right module regardless of what its file is
 * named on disk.
 */
#if defined(_WIN32) && !defined(TRACEVIZ_NO_LOADER)
#include <windows.h>
#include <tlhelp32.h>

#ifdef __cplusplus
extern "C"
{
#endif

    static const TraceVizAPI* TraceViz_Load(void)
    {
        HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
        if (Snapshot == INVALID_HANDLE_VALUE)
        {
            return 0;
        }

        {
            const TraceVizAPI* Result = 0;
            MODULEENTRY32W Entry;
            Entry.dwSize = sizeof(Entry);

            if (Module32FirstW(Snapshot, &Entry))
            {
                do
                {
                    FARPROC Raw = GetProcAddress(Entry.hModule, TRACEVIZ_ENTRY_POINT_NAME);
                    if (Raw)
                    {
                        TraceVizGetAPIFn GetAPI = (TraceVizGetAPIFn)(void*)Raw;
                        const TraceVizAPI* Candidate = GetAPI(TRACEVIZ_API_VERSION);
                        if (Candidate)
                        {
                            Result = Candidate;
                            break;
                        }
                    }
                } while (Module32NextW(Snapshot, &Entry));
            }

            CloseHandle(Snapshot);
            return Result;
        }
    }

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* _WIN32 && !TRACEVIZ_NO_LOADER */

/* Handy literals so callers do not have to spell out struct initialisers. */
#ifdef __cplusplus
namespace TraceVizColors
{
    inline constexpr TraceVizColor Red{1.0f, 0.0f, 0.0f, 1.0f};
    inline constexpr TraceVizColor Green{0.0f, 1.0f, 0.0f, 1.0f};
    inline constexpr TraceVizColor Blue{0.0f, 0.35f, 1.0f, 1.0f};
    inline constexpr TraceVizColor Yellow{1.0f, 0.95f, 0.1f, 1.0f};
    inline constexpr TraceVizColor Cyan{0.0f, 1.0f, 1.0f, 1.0f};
    inline constexpr TraceVizColor Magenta{1.0f, 0.0f, 1.0f, 1.0f};
    inline constexpr TraceVizColor White{1.0f, 1.0f, 1.0f, 1.0f};
    inline constexpr TraceVizColor Orange{1.0f, 0.5f, 0.0f, 1.0f};
} // namespace TraceVizColors
#endif

#endif /* TRACEVIZ_API_H */
