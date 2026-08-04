#pragma once

// The bridge between the visualizer and the running game.
//
// Everything that touches the engine lives here, and all of it goes through
// reflection. Three jobs:
//
//   1. Discovery. Find the local PlayerController, its HUD and its camera
//      manager, and bind the UFunctions we need. What was and was not found is
//      recorded in Diagnostics so a failure is diagnosable from the panel and
//      the log instead of presenting as "nothing draws".
//
//   2. Rendering. A shipping build compiles out DrawDebugLine and every
//      UKismetSystemLibrary::DrawDebug* body along with it, because
//      ENABLE_DRAW_DEBUG is 0. AHUD::DrawLine is not debug code, so it
//      survives, and it is BlueprintCallable and therefore reachable through
//      reflection. We hook the HUD's per-frame draw event and issue our lines
//      from inside it, which is the only point in the frame where the HUD's
//      Canvas is valid.
//
//   3. Tracing. UKismetSystemLibrary's trace functions still work in shipping
//      (only their DrawDebugType visualisation is stripped), so we call them
//      through reflection and draw the result ourselves.

#include <cstdint>
#include <mutex>

#include <String/StringType.hpp>

#include "TVDrawList.hpp"
#include "TVMath.hpp"
#include "TVProject.hpp"
#include "TVReflect.hpp"

namespace TraceViz::Engine
{
    using RC::Unreal::UObject;

    // Result of a trace, filled from the reflected FHitResult.
    struct TraceResult
    {
        bool bBlockingHit{false};
        Vec3 ImpactPoint{};
        Vec3 ImpactNormal{};
        Vec3 Location{};
        double Distance{0.0};
        // Name of the actor that was hit, when the engine version exposes it.
        RC::StringType HitActorName{};
        bool bValid{false};
    };

    // What to draw for a trace, beyond the geometry itself.
    struct TraceDrawOptions
    {
        // Colour used when the trace hits nothing, and when it hits.
        Color MissColor{Colors::Red};
        Color HitColor{Colors::Green};
        // Seconds to keep the visualisation alive. 0 means one frame, which is
        // what you want when re-tracing every tick.
        float Duration{0.0f};
        CategoryId Category{kDefaultCategory};
        int SphereSegments{16};
        bool bDrawImpactNormal{true};
        double ImpactNormalLength{40.0};
        float Thickness{1.0f};
        // Draw the shape at both ends plus the connecting rails, rather than
        // just a line along the trace axis.
        bool bDrawSweptVolume{true};
    };

    struct Diagnostics
    {
        // Discovery
        bool bUnrealInitialised{false};
        bool bPlayerControllerFound{false};
        bool bHudFound{false};
        bool bCameraManagerFound{false};
        bool bHudHookInstalled{false};

        RC::StringType PlayerControllerClass{};
        RC::StringType HudClass{};
        RC::StringType HookedFunctionName{};

        // Binding: which UFunctions resolved, with their reflected signatures.
        RC::StringType BoundSignatures{};
        RC::StringType MissingFunctions{};

        // Liveness. If HudDrawCallbacks stops climbing, the HUD is not
        // rendering and nothing else matters.
        uint64_t GameTicks{0};
        uint64_t HudDrawCallbacks{0};
        uint64_t SegmentsDrawnLastFrame{0};
        uint64_t SegmentsClampedLastFrame{0};
        uint64_t DrawLineFailures{0};

        // Last sampled view.
        Vec3 CameraLocation{};
        Rotator CameraRotation{};
        double CameraFov{0.0};
        float ViewportWidth{0.0f};
        float ViewportHeight{0.0f};

        // Result of the last projection calibration: the pixel distance
        // between our own projection and the engine's own
        // ProjectWorldLocationToScreen for a probe point. Negative means the
        // check has not been run.
        double CalibrationErrorPixels{-1.0};
        RC::StringType CalibrationDetail{};

        RC::StringType LastError{};
    };

    // Tunables the panel can change at runtime.
    struct Settings
    {
        bool bEnabled{true};
        // Upper bound on lines issued to the HUD per frame. Each one is a
        // ProcessEvent call, so this is the main cost control.
        uint32_t MaxSegmentsPerFrame{6000};
        AspectConstraint Constraint{AspectConstraint::MaintainXFov};
        // Multiplier applied to the reported camera FOV. Only needed if a game
        // reports a FOV that does not match what it renders; calibration will
        // tell you whether it does.
        double FovScale{1.0};
        double NearClip{1.0};
        float GlobalThicknessScale{1.0f};
        float GlobalAlpha{1.0f};
    };

    class Bridge
    {
      public:
        // Bind UFunctions. Safe to call once the Unreal module is up.
        void OnUnrealInit();

        // Per game tick: refresh object handles, sample the camera, age the
        // draw list, and make sure the HUD hook is attached to the right
        // function for the current HUD class.
        void OnGameTick(float DeltaSeconds);

        // Issues the current draw list to the HUD canvas. Called only from the
        // HUD draw hook, where Canvas is valid.
        void RenderToHud(UObject* HudObject);

        // Compares our projection against the engine's own for a spread of
        // points, and records the worst pixel error. This is how the fast
        // projection path gets confirmed rather than assumed.
        //
        // Must run on the game thread, because it calls ProcessEvent. The UI
        // runs on UE4SS's render thread, so it calls RequestCalibration() and
        // the work happens on the next tick.
        void RunProjectionCalibration();
        void RequestCalibration();

        // ---- Traces. Each performs the trace through reflection and, unless
        // bVisualiseOnly is set, submits the visualisation to the draw list.

        bool LineTrace(const Vec3& Start, const Vec3& End, uint8_t TraceChannel, bool bTraceComplex, const TraceDrawOptions& Options, TraceResult& Out);
        bool SphereTrace(const Vec3& Start,
                         const Vec3& End,
                         double Radius,
                         uint8_t TraceChannel,
                         bool bTraceComplex,
                         const TraceDrawOptions& Options,
                         TraceResult& Out);
        bool CapsuleTrace(const Vec3& Start,
                          const Vec3& End,
                          double Radius,
                          double HalfHeight,
                          uint8_t TraceChannel,
                          bool bTraceComplex,
                          const TraceDrawOptions& Options,
                          TraceResult& Out);
        bool BoxTrace(const Vec3& Start,
                      const Vec3& End,
                      const Vec3& HalfSize,
                      const Rotator& Orientation,
                      uint8_t TraceChannel,
                      bool bTraceComplex,
                      const TraceDrawOptions& Options,
                      TraceResult& Out);

        // Camera POV, as sampled on the most recent game tick.
        bool GetViewInfo(ViewInfo& Out) const;

        Diagnostics GetDiagnostics() const;
        Settings& GetSettings()
        {
            return m_settings;
        }

        // True once the HUD hook has fired at least once, i.e. we are actually
        // drawing.
        bool IsRendering() const;

      private:
        void BindFunctions();
        void RefreshObjects();
        void SampleCamera();
        void EnsureHudHook();
        bool ReadViewportSize(UObject* HudObject, float& OutWidth, float& OutHeight);
        void FillTraceResult(const Reflect::StructReader& Hit, TraceResult& Out);

        // Traces are a three-step sequence because each shape has its own
        // extra parameters, and Reset() has to happen before any of them are
        // written. Prepare zeroes the buffer and fills the shared parameters;
        // the caller then adds the shape-specific ones; Finish invokes and
        // reads the hit back out.
        bool PrepareTrace(Reflect::FunctionCall& Call, const Vec3& Start, const Vec3& End, uint8_t TraceChannel, bool bTraceComplex, UObject* WorldContext);
        bool FinishTrace(Reflect::FunctionCall& Call, UObject* LibraryCdo, TraceResult& Out);

        // Adds the impact marker and normal that every trace kind shares.
        void SubmitImpactVisual(const TraceDrawOptions& Options, const TraceResult& Result);

        // Guards diagnostics, the sampled view, settings and cached object
        // pointers.
        mutable std::mutex m_mutex;
        // Guards the trace parameter buffers, which are reused between calls.
        // Kept separate from m_mutex so a trace never holds a lock across
        // ProcessEvent that the HUD draw path also wants.
        mutable std::mutex m_trace_mutex;

        Diagnostics m_diag{};
        Settings m_settings{};
        ViewInfo m_view{};
        bool m_view_valid{false};

        // Cached engine objects. Revalidated every tick because a map change
        // invalidates all of them.
        UObject* m_player_controller{};
        UObject* m_hud{};
        UObject* m_camera_manager{};
        UObject* m_kismet_system_library_cdo{};

        // The UFunction we hooked, and the hook ids needed to remove it.
        RC::Unreal::UFunction* m_hooked_draw_function{};
        int32_t m_hook_id_pre{};
        int32_t m_hook_id_post{};

        // Bound calls. Kept as members so binding happens once.
        Reflect::FunctionCall m_hud_draw_line;
        Reflect::FunctionCall m_get_camera_location;
        Reflect::FunctionCall m_get_camera_rotation;
        Reflect::FunctionCall m_get_fov_angle;
        Reflect::FunctionCall m_project_world_to_screen;
        Reflect::FunctionCall m_line_trace;
        Reflect::FunctionCall m_sphere_trace;
        Reflect::FunctionCall m_capsule_trace;
        Reflect::FunctionCall m_box_trace;

        // Hot-path parameter handles for the per-line HUD call.
        const Reflect::FunctionCall::Param* m_p_start_x{};
        const Reflect::FunctionCall::Param* m_p_start_y{};
        const Reflect::FunctionCall::Param* m_p_end_x{};
        const Reflect::FunctionCall::Param* m_p_end_y{};
        const Reflect::FunctionCall::Param* m_p_line_color{};
        const Reflect::FunctionCall::Param* m_p_line_thickness{};

        bool m_calibration_requested{false};
    };

    Bridge& Get();
} // namespace TraceViz::Engine
