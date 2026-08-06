#include "TVEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UnrealFlags.hpp>

#include "TVShapes.hpp"

namespace TraceViz::Engine
{
    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        // Object paths for everything we call. Kept together so a game that
        // renames or removes one produces an obvious, greppable log line
        // rather than a silent no-op.
        constexpr StringViewType kPathHudDrawLine = STR("/Script/Engine.HUD:DrawLine");
        constexpr StringViewType kPathGetCameraLocation = STR("/Script/Engine.PlayerCameraManager:GetCameraLocation");
        constexpr StringViewType kPathGetCameraRotation = STR("/Script/Engine.PlayerCameraManager:GetCameraRotation");
        constexpr StringViewType kPathGetFovAngle = STR("/Script/Engine.PlayerCameraManager:GetFOVAngle");
        constexpr StringViewType kPathProjectWorldToScreen = STR("/Script/Engine.PlayerController:ProjectWorldLocationToScreen");
        constexpr StringViewType kPathLineTrace = STR("/Script/Engine.KismetSystemLibrary:LineTraceSingle");
        constexpr StringViewType kPathSphereTrace = STR("/Script/Engine.KismetSystemLibrary:SphereTraceSingle");
        constexpr StringViewType kPathCapsuleTrace = STR("/Script/Engine.KismetSystemLibrary:CapsuleTraceSingle");
        constexpr StringViewType kPathBoxTrace = STR("/Script/Engine.KismetSystemLibrary:BoxTraceSingle");
        constexpr StringViewType kPathKismetSystemLibrary = STR("/Script/Engine.KismetSystemLibrary");
        // Palworld-specific: see EnsurePlayerControllerHook in TVEngine.hpp
        // for why this is hooked directly rather than discovered generically.
        constexpr StringViewType kPathPalControllerTick =
                STR("/Game/Pal/Blueprint/Controller/BP_PalPlayerController.BP_PalPlayerController_C:ReceiveTick");

        // EDrawDebugTrace::None. We draw the visualisation ourselves; asking
        // the engine to draw it would be a no-op in a shipping build anyway.
        constexpr uint8_t kDrawDebugTypeNone = 0;

        UObject* ReadObjectProperty(UObject* Owner, StringViewType PropertyName)
        {
            if (!Owner)
            {
                return nullptr;
            }
            const StringType Name{PropertyName};
            auto* Slot = Owner->GetValuePtrByPropertyNameInChain<UObject*>(Name.c_str());
            return Slot ? *Slot : nullptr;
        }

        bool ReadIntProperty(UObject* Owner, StringViewType PropertyName, int32_t& Out)
        {
            if (!Owner)
            {
                return false;
            }
            const StringType Name{PropertyName};
            auto* Slot = Owner->GetValuePtrByPropertyNameInChain<int32_t>(Name.c_str());
            if (!Slot)
            {
                return false;
            }
            Out = *Slot;
            return true;
        }

        StringType SafeObjectClassName(UObject* Object)
        {
            if (!Object)
            {
                return StringType{STR("<null>")};
            }
            auto* Class = Object->GetClassPrivate();
            return Class ? Class->GetName() : StringType{STR("<no class>")};
        }

        // Reads an FVector2D out parameter. FVector2D is two floats in UE4 and
        // two doubles in UE5, so the width comes from the reflected size.
        bool ReadVector2D(const void* Src, int32_t SizeInBytes, double& OutX, double& OutY)
        {
            if (SizeInBytes == 16)
            {
                double Tmp[2]{};
                std::memcpy(Tmp, Src, sizeof(Tmp));
                OutX = Tmp[0];
                OutY = Tmp[1];
                return true;
            }
            if (SizeInBytes == 8)
            {
                float Tmp[2]{};
                std::memcpy(Tmp, Src, sizeof(Tmp));
                OutX = Tmp[0];
                OutY = Tmp[1];
                return true;
            }
            return false;
        }
    } // namespace

    Bridge& Get()
    {
        static Bridge Instance{};
        return Instance;
    }

    // ---- Binding -----------------------------------------------------------

    void Bridge::OnUnrealInit()
    {
        std::lock_guard<std::mutex> Lock{m_mutex};
        BindFunctions();
        m_diag.bUnrealInitialised = true;
    }

    void Bridge::BindFunctions()
    {
        StringType Bound{};
        StringType Missing{};

        const auto TryBind = [&](Reflect::FunctionCall& Call, StringViewType Path) {
            if (Call.Bind(Path))
            {
                Bound += Call.DescribeSignature();
                Bound += STR("\n");
            }
            else
            {
                Missing += StringType{Path};
                Missing += STR("\n");
            }
        };

        TryBind(m_hud_draw_line, kPathHudDrawLine);
        TryBind(m_get_camera_location, kPathGetCameraLocation);
        TryBind(m_get_camera_rotation, kPathGetCameraRotation);
        TryBind(m_get_fov_angle, kPathGetFovAngle);
        TryBind(m_project_world_to_screen, kPathProjectWorldToScreen);
        TryBind(m_line_trace, kPathLineTrace);
        TryBind(m_sphere_trace, kPathSphereTrace);
        TryBind(m_capsule_trace, kPathCapsuleTrace);
        TryBind(m_box_trace, kPathBoxTrace);

        // Resolve the per-line parameters once. Every drawn segment writes
        // through these, so a name lookup per line would dominate the cost of
        // drawing.
        if (m_hud_draw_line.IsValid())
        {
            m_p_start_x = m_hud_draw_line.GetParam(STR("StartScreenX"));
            m_p_start_y = m_hud_draw_line.GetParam(STR("StartScreenY"));
            m_p_end_x = m_hud_draw_line.GetParam(STR("EndScreenX"));
            m_p_end_y = m_hud_draw_line.GetParam(STR("EndScreenY"));
            m_p_line_color = m_hud_draw_line.GetParam(STR("LineColor"));
            m_p_line_thickness = m_hud_draw_line.GetParam(STR("LineThickness"));

            if (!m_p_start_x || !m_p_start_y || !m_p_end_x || !m_p_end_y || !m_p_line_color)
            {
                m_diag.LastError = StringType{STR("AHUD::DrawLine parameter names did not match. Signature: ")} + m_hud_draw_line.DescribeSignature();
                Output::send<LogLevel::Error>(STR("[TraceViz] {}\n"), m_diag.LastError);
            }
        }

        m_kismet_system_library_cdo = Reflect::FindClassDefaultObject(kPathKismetSystemLibrary);

        m_diag.BoundSignatures = Bound;
        m_diag.MissingFunctions = Missing;

        Output::send<LogLevel::Default>(STR("[TraceViz] Bound engine functions:\n{}"), Bound);
        if (!Missing.empty())
        {
            Output::send<LogLevel::Warning>(STR("[TraceViz] Missing engine functions:\n{}"), Missing);
        }
    }

    // ---- Per-tick ----------------------------------------------------------

    void Bridge::OnGameTick(float DeltaSeconds)
    {
        bool bDoCalibration = false;
        {
            std::lock_guard<std::mutex> Lock{m_mutex};
            ++m_diag.GameTicks;

            RefreshObjects();
            SampleCamera();
            EnsureHudHook();

            bDoCalibration = m_calibration_requested;
            m_calibration_requested = false;
        }

        if (bDoCalibration)
        {
            RunProjectionCalibration();
        }

        // Ageing the draw list happens outside our lock: it takes the draw
        // list's own lock, and mods may be submitting concurrently.
        GetDrawList().Tick(DeltaSeconds);
    }

    void Bridge::RefreshObjects()
    {
        // Installs the controller-tick hook if it is not already installed.
        // The hook itself keeps m_player_controller current every frame from
        // then on, via NotifyControllerTick; this call does not touch
        // m_player_controller directly.
        EnsurePlayerControllerHook();

        m_diag.bPlayerControllerFound = (m_player_controller != nullptr);
        if (!m_player_controller)
        {
            UnhookHud();
            m_hud = nullptr;
            m_camera_manager = nullptr;
            m_diag.bHudFound = false;
            m_diag.bCameraManagerFound = false;
            return;
        }

        // Both hang off the controller as BlueprintReadOnly properties, so no
        // scanning is needed once we have it.
        UObject* Hud = ReadObjectProperty(m_player_controller, STR("MyHUD"));
        if (Hud != m_hud)
        {
            // Unregister before switching, not just forget: RegisterHook was
            // already called for the previous HUD's draw function, and that
            // registration keeps firing (and keeps calling RenderToHud) even
            // after we stop tracking it. Every HUD change without this would
            // stack another hook on whatever function was hooked before,
            // multiplying draw calls per frame across map loads.
            UnhookHud();
            m_hud = Hud;
            m_diag.HudClass = SafeObjectClassName(m_hud);
        }

        m_camera_manager = ReadObjectProperty(m_player_controller, STR("PlayerCameraManager"));

        m_diag.bHudFound = (m_hud != nullptr);
        m_diag.bCameraManagerFound = (m_camera_manager != nullptr);
    }

    void Bridge::SampleCamera()
    {
        if (!m_camera_manager)
        {
            return;
        }

        // ProcessEvent is only safe on the game thread, which is why the view
        // is sampled here and merely read during rendering.
        Vec3 Location{};
        Rotator Rotation{};
        double Fov = 90.0;

        if (!m_get_camera_location.IsValid() || !m_get_camera_rotation.IsValid())
        {
            return;
        }

        m_get_camera_location.Reset();
        m_get_camera_location.Invoke(m_camera_manager);
        if (!m_get_camera_location.GetVector(STR("ReturnValue"), Location))
        {
            return;
        }

        m_get_camera_rotation.Reset();
        m_get_camera_rotation.Invoke(m_camera_manager);
        {
            // FRotator's components are laid out Pitch, Yaw, Roll, which is
            // the same three-scalar read as a vector.
            Vec3 AsVector{};
            if (!m_get_camera_rotation.GetVector(STR("ReturnValue"), AsVector))
            {
                return;
            }
            Rotation = Rotator{AsVector.X, AsVector.Y, AsVector.Z};
        }

        if (m_get_fov_angle.IsValid())
        {
            m_get_fov_angle.Reset();
            m_get_fov_angle.Invoke(m_camera_manager);
            m_get_fov_angle.GetFloat(STR("ReturnValue"), Fov);
        }

        m_view.Location = Location;
        m_view.Rotation = Rotation;
        m_view.FovDegrees = Fov * m_settings.FovScale;
        m_view.Constraint = m_settings.Constraint;
        m_view.NearClip = m_settings.NearClip;
        m_view_valid = true;

        m_diag.CameraLocation = Location;
        m_diag.CameraRotation = Rotation;
        m_diag.CameraFov = Fov;
    }

    void Bridge::UnhookHud()
    {
        if (!m_hooked_draw_function)
        {
            return;
        }

        // A HUD class unloaded across a level transition (Blueprint classes
        // can be garbage-collected when nothing references them any more)
        // would leave this pointer dangling; UE4SS's own LiveView watch system
        // carries the same assumption when it unregisters a hook by stored
        // UFunction pointer, so this is not a new category of risk, but it is
        // one this mod cannot fully rule out without engine-side validation.
        UObjectGlobals::UnregisterHook(m_hooked_draw_function, {m_hook_id_pre, m_hook_id_post});
        m_hooked_draw_function = nullptr;
        m_diag.bHudHookInstalled = false;
    }

    void Bridge::EnsureHudHook()
    {
        if (!m_hud || m_hooked_draw_function)
        {
            return;
        }

        // Resolve the draw event on the live HUD's own class chain rather than
        // on AHUD. If the game's HUD Blueprint implements ReceiveDrawHUD, that
        // override is a different UFunction and it is the one the engine
        // actually calls, so hooking the base class function would never fire.
        UFunction* DrawFunction = m_hud->GetFunctionByNameInChain(STR("ReceiveDrawHUD"));
        if (!DrawFunction)
        {
            m_diag.LastError = StringType{STR("HUD has no ReceiveDrawHUD function; cannot install the draw hook.")};
            return;
        }

        const std::pair<int, int> Ids = UObjectGlobals::RegisterHook(
                DrawFunction,
                [](UnrealScriptFunctionCallableContext&, void*) {
                    // Nothing to do before the HUD's own drawing.
                },
                [](UnrealScriptFunctionCallableContext& Context, void*) {
                    // Drawing after the game's own HUD keeps our lines on top.
                    Get().RenderToHud(Context.Context);
                },
                nullptr);

        m_hooked_draw_function = DrawFunction;
        m_hook_id_pre = Ids.first;
        m_hook_id_post = Ids.second;
        m_diag.bHudHookInstalled = true;
        m_diag.HookedFunctionName = DrawFunction->GetFullName();

        Output::send<LogLevel::Default>(STR("[TraceViz] Installed HUD draw hook on {}\n"), m_diag.HookedFunctionName);
    }

    void Bridge::EnsurePlayerControllerHook()
    {
        if (m_hooked_controller_tick_function)
        {
            return;
        }

        // Fails until the level owning this Blueprint is loaded, so this
        // keeps retrying every tick rather than giving up after one miss --
        // the same shape as EnsureHudHook's retry loop.
        UFunction* TickFunction = Reflect::FindFunction(kPathPalControllerTick);
        if (!TickFunction)
        {
            return;
        }

        UObjectGlobals::RegisterHook(
                TickFunction,
                [](UnrealScriptFunctionCallableContext&, void*) {
                    // Nothing to do before the controller's own tick.
                },
                [](UnrealScriptFunctionCallableContext& Context, void*) {
                    Get().NotifyControllerTick(Context.Context);
                },
                nullptr);

        m_hooked_controller_tick_function = TickFunction;

        Output::send<LogLevel::Default>(STR("[TraceViz] Installed player controller hook on {}\n"), TickFunction->GetFullName());
    }

    bool Bridge::ReadViewportSize(UObject* HudObject, float& OutWidth, float& OutHeight)
    {
        // The canvas is only assigned during the HUD render pass, which is
        // exactly when this runs, and its size is the surface we draw into.
        // That makes it the right authority for the projection.
        UObject* Canvas = ReadObjectProperty(HudObject, STR("Canvas"));
        if (!Canvas)
        {
            return false;
        }

        int32_t SizeX = 0;
        int32_t SizeY = 0;
        if (!ReadIntProperty(Canvas, STR("SizeX"), SizeX) || !ReadIntProperty(Canvas, STR("SizeY"), SizeY))
        {
            return false;
        }
        if (SizeX <= 0 || SizeY <= 0)
        {
            return false;
        }

        OutWidth = static_cast<float>(SizeX);
        OutHeight = static_cast<float>(SizeY);
        return true;
    }

    // ---- Rendering ---------------------------------------------------------

    void Bridge::RenderToHud(UObject* HudObject)
    {
        if (!HudObject)
        {
            return;
        }

        ViewInfo View{};
        Settings LocalSettings{};
        {
            std::lock_guard<std::mutex> Lock{m_mutex};
            ++m_diag.HudDrawCallbacks;

            if (!m_settings.bEnabled || !m_view_valid || !m_hud_draw_line.IsValid() || !m_p_start_x)
            {
                return;
            }

            float Width = 0.0f;
            float Height = 0.0f;
            if (ReadViewportSize(HudObject, Width, Height))
            {
                m_view.ViewportWidth = Width;
                m_view.ViewportHeight = Height;
                m_diag.ViewportWidth = Width;
                m_diag.ViewportHeight = Height;
            }
            else if (m_view.ViewportWidth <= 0.0f)
            {
                // No canvas size and no previous value: nothing sane to
                // project against.
                return;
            }

            View = m_view;
            LocalSettings = m_settings;
        }

        // Snapshot outside the lock so mods submitting geometry are not
        // blocked for the duration of the draw.
        static thread_local std::vector<Segment> Segments;
        static thread_local std::vector<TimedLabel> Labels;
        GetDrawList().Snapshot(Segments, Labels);

        const Projector Proj{View};

        uint64_t Drawn = 0;
        uint64_t Clamped = 0;
        uint64_t Failures = 0;

        for (size_t i = 0; i < Segments.size(); ++i)
        {
            if (Drawn >= LocalSettings.MaxSegmentsPerFrame)
            {
                Clamped = Segments.size() - i;
                break;
            }

            const Segment& Seg = Segments[i];

            Vec2 A{};
            Vec2 B{};
            if (!Proj.ProjectSegment(Seg.A, Seg.B, A, B))
            {
                continue;
            }

            Color Tint = Seg.LineColor;
            Tint.A *= LocalSettings.GlobalAlpha;
            if (Tint.A <= 0.0f)
            {
                continue;
            }

            m_hud_draw_line.Reset();
            bool bOk = true;
            bOk = m_hud_draw_line.SetFloat(m_p_start_x, A.X) && bOk;
            bOk = m_hud_draw_line.SetFloat(m_p_start_y, A.Y) && bOk;
            bOk = m_hud_draw_line.SetFloat(m_p_end_x, B.X) && bOk;
            bOk = m_hud_draw_line.SetFloat(m_p_end_y, B.Y) && bOk;
            bOk = m_hud_draw_line.SetLinearColor(m_p_line_color, Tint) && bOk;
            if (m_p_line_thickness)
            {
                m_hud_draw_line.SetFloat(m_p_line_thickness, Seg.Thickness * LocalSettings.GlobalThicknessScale);
            }

            if (!bOk)
            {
                ++Failures;
                continue;
            }

            m_hud_draw_line.Invoke(HudObject);
            ++Drawn;
        }

        {
            std::lock_guard<std::mutex> Lock{m_mutex};
            m_diag.SegmentsDrawnLastFrame = Drawn;
            m_diag.SegmentsClampedLastFrame = Clamped;
            m_diag.DrawLineFailures += Failures;
        }
    }

    // ---- Calibration -------------------------------------------------------

    void Bridge::RequestCalibration()
    {
        std::lock_guard<std::mutex> Lock{m_mutex};
        m_calibration_requested = true;
    }

    void Bridge::RunProjectionCalibration()
    {
        std::lock_guard<std::mutex> Lock{m_mutex};

        m_diag.CalibrationErrorPixels = -1.0;
        m_diag.CalibrationDetail.clear();

        if (!m_project_world_to_screen.IsValid() || !m_player_controller || !m_view_valid)
        {
            m_diag.CalibrationDetail = StringType{STR("Cannot calibrate: ProjectWorldLocationToScreen unbound, or no controller/view yet.")};
            return;
        }
        if (m_view.ViewportWidth <= 0.0f)
        {
            m_diag.CalibrationDetail = StringType{STR("Cannot calibrate: viewport size unknown. Has the HUD drawn at least once?")};
            return;
        }

        const auto* ScreenParam = m_project_world_to_screen.GetParam(STR("ScreenLocation"));
        if (!ScreenParam)
        {
            m_diag.CalibrationDetail = StringType{STR("No ScreenLocation parameter. Signature: ")} + m_project_world_to_screen.DescribeSignature();
            return;
        }

        const Projector Proj{m_view};
        const Basis CameraBasis = MakeBasis(m_view.Rotation);

        // Probe a spread of directions rather than one point straight ahead:
        // an FOV or aspect-constraint mismatch shows up at the edges of the
        // screen, not at the centre where every projection agrees.
        const Vec3 Probes[] = {
                m_view.Location + CameraBasis.Forward * 500.0,
                m_view.Location + CameraBasis.Forward * 500.0 + CameraBasis.Right * 250.0,
                m_view.Location + CameraBasis.Forward * 500.0 - CameraBasis.Right * 250.0,
                m_view.Location + CameraBasis.Forward * 500.0 + CameraBasis.Up * 150.0,
                m_view.Location + CameraBasis.Forward * 500.0 - CameraBasis.Up * 150.0,
        };

        double WorstError = 0.0;
        int Compared = 0;
        StringType Detail{};

        for (const Vec3& Probe : Probes)
        {
            m_project_world_to_screen.Reset();
            if (!m_project_world_to_screen.SetVector(STR("WorldLocation"), Probe))
            {
                m_diag.CalibrationDetail =
                        StringType{STR("No WorldLocation parameter. Signature: ")} + m_project_world_to_screen.DescribeSignature();
                return;
            }
            m_project_world_to_screen.SetBool(STR("bPlayerViewportRelative"), false);
            m_project_world_to_screen.Invoke(m_player_controller);

            bool bEngineOk = false;
            m_project_world_to_screen.GetBool(STR("ReturnValue"), bEngineOk);
            if (!bEngineOk)
            {
                continue;
            }

            double EngineX = 0.0;
            double EngineY = 0.0;
            if (!ReadVector2D(m_project_world_to_screen.GetRaw(ScreenParam), ScreenParam->Size, EngineX, EngineY))
            {
                m_diag.CalibrationDetail = StringType{STR("Unexpected FVector2D size; cannot calibrate.")};
                return;
            }

            Vec2 Ours{};
            double Depth = 0.0;
            if (!Proj.ProjectPoint(Probe, Ours, Depth))
            {
                continue;
            }

            const double DX = static_cast<double>(Ours.X) - EngineX;
            const double DY = static_cast<double>(Ours.Y) - EngineY;
            const double Error = std::sqrt(DX * DX + DY * DY);
            WorstError = std::max(WorstError, Error);
            ++Compared;

            Detail += std::format(STR("  ours=({:.1f},{:.1f}) engine=({:.1f},{:.1f}) delta={:.2f}px\n"),
                                  Ours.X,
                                  Ours.Y,
                                  EngineX,
                                  EngineY,
                                  Error);
        }

        if (Compared == 0)
        {
            m_diag.CalibrationDetail = StringType{STR("No probe points were on screen; aim at open space and retry.")};
            return;
        }

        m_diag.CalibrationErrorPixels = WorstError;
        m_diag.CalibrationDetail = std::format(STR("Compared {} points, worst error {:.2f}px\n"), Compared, WorstError) + Detail;
        Output::send<LogLevel::Default>(STR("[TraceViz] Projection calibration:\n{}"), m_diag.CalibrationDetail);
    }

    // ---- Traces ------------------------------------------------------------

    void Bridge::FillTraceResult(const Reflect::StructReader& Hit, TraceResult& Out)
    {
        if (!Hit.IsValid())
        {
            return;
        }

        Hit.GetBool(STR("bBlockingHit"), Out.bBlockingHit);
        Hit.GetVector(STR("ImpactPoint"), Out.ImpactPoint);
        Hit.GetVector(STR("ImpactNormal"), Out.ImpactNormal);
        Hit.GetVector(STR("Location"), Out.Location);
        Hit.GetFloat(STR("Distance"), Out.Distance);

        // UE4 exposes the hit actor as FHitResult::Actor; UE5 replaced it with
        // HitObjectHandle, which is not a plain pointer. Treat the name as
        // best-effort rather than pretending both are readable the same way.
        UObject* HitObject = nullptr;
        if (Hit.GetObject(STR("Actor"), HitObject) && HitObject)
        {
            Out.HitActorName = HitObject->GetName();
        }

        Out.bValid = true;
    }

    bool Bridge::PrepareTrace(Reflect::FunctionCall& Call, const Vec3& Start, const Vec3& End, uint8_t TraceChannel, bool bTraceComplex, UObject* WorldContext)
    {
        if (!Call.IsValid())
        {
            return false;
        }

        // A zeroed buffer already gives an empty ActorsToIgnore array and
        // sensible defaults, so only the parameters that matter get written.
        // This must happen before any shape-specific parameter is set, which
        // is why preparation and invocation are separate steps.
        Call.Reset();
        Call.SetObject(STR("WorldContextObject"), WorldContext);
        Call.SetVector(STR("Start"), Start);
        Call.SetVector(STR("End"), End);
        Call.SetByte(STR("TraceChannel"), TraceChannel);
        Call.SetBool(STR("bTraceComplex"), bTraceComplex);
        Call.SetByte(STR("DrawDebugType"), kDrawDebugTypeNone);
        Call.SetBool(STR("bIgnoreSelf"), true);
        return true;
    }

    bool Bridge::FinishTrace(Reflect::FunctionCall& Call, UObject* LibraryCdo, TraceResult& Out)
    {
        if (!LibraryCdo)
        {
            return false;
        }

        Call.Invoke(LibraryCdo);

        bool bReturn = false;
        const bool bHasReturn = Call.GetBool(STR("ReturnValue"), bReturn);
        FillTraceResult(Call.GetStruct(STR("OutHit")), Out);
        if (bHasReturn)
        {
            // The return value is the authority on whether anything was hit;
            // the struct read is a bonus.
            Out.bBlockingHit = bReturn;
        }
        return true;
    }

    void Bridge::SubmitImpactVisual(const TraceDrawOptions& Options, const TraceResult& Result)
    {
        if (!Result.bBlockingHit)
        {
            return;
        }

        Shapes::SegmentList Out;
        Shapes::AppendPoint(Out, Result.ImpactPoint, 20.0, Options.HitColor, Options.Thickness);
        if (Options.bDrawImpactNormal && LengthSquared(Result.ImpactNormal) > 1e-9)
        {
            Shapes::AppendArrow(Out,
                                Result.ImpactPoint,
                                Result.ImpactPoint + Normalized(Result.ImpactNormal) * Options.ImpactNormalLength,
                                0.0,
                                Options.HitColor,
                                Options.Thickness);
        }
        GetDrawList().AddSegments(Out, Options.Duration, Options.Category);
    }

    bool Bridge::LineTrace(const Vec3& Start, const Vec3& End, uint8_t TraceChannel, bool bTraceComplex, const TraceDrawOptions& Options, TraceResult& Out)
    {
        UObject* WorldContext = nullptr;
        UObject* LibraryCdo = nullptr;
        {
            std::lock_guard<std::mutex> Lock{m_mutex};
            WorldContext = m_player_controller;
            LibraryCdo = m_kismet_system_library_cdo;
        }

        {
            std::lock_guard<std::mutex> Lock{m_trace_mutex};
            if (!PrepareTrace(m_line_trace, Start, End, TraceChannel, bTraceComplex, WorldContext))
            {
                return false;
            }
            if (!FinishTrace(m_line_trace, LibraryCdo, Out))
            {
                return false;
            }
        }

        Shapes::SegmentList Segments;
        if (Out.bBlockingHit)
        {
            // Showing the consumed part of the ray in the hit colour and the
            // remainder dimmed makes how far the trace actually got legible at
            // a glance, which is the whole point of looking at it.
            Shapes::AppendLine(Segments, Start, Out.ImpactPoint, Options.HitColor, Options.Thickness);
            Shapes::AppendLine(Segments, Out.ImpactPoint, End, Options.MissColor.WithAlpha(Options.MissColor.A * 0.35f), Options.Thickness);
        }
        else
        {
            Shapes::AppendLine(Segments, Start, End, Options.MissColor, Options.Thickness);
        }
        GetDrawList().AddSegments(Segments, Options.Duration, Options.Category);
        SubmitImpactVisual(Options, Out);
        return true;
    }

    bool Bridge::SphereTrace(const Vec3& Start,
                             const Vec3& End,
                             double Radius,
                             uint8_t TraceChannel,
                             bool bTraceComplex,
                             const TraceDrawOptions& Options,
                             TraceResult& Out)
    {
        UObject* WorldContext = nullptr;
        UObject* LibraryCdo = nullptr;
        {
            std::lock_guard<std::mutex> Lock{m_mutex};
            WorldContext = m_player_controller;
            LibraryCdo = m_kismet_system_library_cdo;
        }

        {
            std::lock_guard<std::mutex> Lock{m_trace_mutex};
            if (!PrepareTrace(m_sphere_trace, Start, End, TraceChannel, bTraceComplex, WorldContext))
            {
                return false;
            }
            m_sphere_trace.SetFloat(STR("Radius"), Radius);
            if (!FinishTrace(m_sphere_trace, LibraryCdo, Out))
            {
                return false;
            }
        }

        const Color ShapeColor = Out.bBlockingHit ? Options.HitColor : Options.MissColor;
        const Vec3 StopPoint = Out.bBlockingHit ? Out.Location : End;

        Shapes::SegmentList Segments;
        if (Options.bDrawSweptVolume)
        {
            Shapes::AppendSweptSphere(Segments, Start, StopPoint, Radius, Options.SphereSegments, ShapeColor, Options.Thickness);
        }
        else
        {
            Shapes::AppendSphere(Segments, StopPoint, Radius, Options.SphereSegments, ShapeColor, Options.Thickness);
        }
        GetDrawList().AddSegments(Segments, Options.Duration, Options.Category);
        SubmitImpactVisual(Options, Out);
        return true;
    }

    bool Bridge::CapsuleTrace(const Vec3& Start,
                              const Vec3& End,
                              double Radius,
                              double HalfHeight,
                              uint8_t TraceChannel,
                              bool bTraceComplex,
                              const TraceDrawOptions& Options,
                              TraceResult& Out)
    {
        UObject* WorldContext = nullptr;
        UObject* LibraryCdo = nullptr;
        {
            std::lock_guard<std::mutex> Lock{m_mutex};
            WorldContext = m_player_controller;
            LibraryCdo = m_kismet_system_library_cdo;
        }

        {
            std::lock_guard<std::mutex> Lock{m_trace_mutex};
            if (!PrepareTrace(m_capsule_trace, Start, End, TraceChannel, bTraceComplex, WorldContext))
            {
                return false;
            }
            m_capsule_trace.SetFloat(STR("Radius"), Radius);
            m_capsule_trace.SetFloat(STR("HalfHeight"), HalfHeight);
            if (!FinishTrace(m_capsule_trace, LibraryCdo, Out))
            {
                return false;
            }
        }

        const Color ShapeColor = Out.bBlockingHit ? Options.HitColor : Options.MissColor;
        const Vec3 StopPoint = Out.bBlockingHit ? Out.Location : End;

        // A capsule sweep is always upright in Unreal's trace API: there is no
        // orientation parameter, so the axis is world Z.
        Shapes::SegmentList Segments;
        Shapes::AppendSweptCapsule(Segments, Start, StopPoint, HalfHeight, Radius, Rotator{}, Options.SphereSegments, ShapeColor, Options.Thickness);
        GetDrawList().AddSegments(Segments, Options.Duration, Options.Category);
        SubmitImpactVisual(Options, Out);
        return true;
    }

    bool Bridge::BoxTrace(const Vec3& Start,
                          const Vec3& End,
                          const Vec3& HalfSize,
                          const Rotator& Orientation,
                          uint8_t TraceChannel,
                          bool bTraceComplex,
                          const TraceDrawOptions& Options,
                          TraceResult& Out)
    {
        UObject* WorldContext = nullptr;
        UObject* LibraryCdo = nullptr;
        {
            std::lock_guard<std::mutex> Lock{m_mutex};
            WorldContext = m_player_controller;
            LibraryCdo = m_kismet_system_library_cdo;
        }

        {
            std::lock_guard<std::mutex> Lock{m_trace_mutex};
            if (!PrepareTrace(m_box_trace, Start, End, TraceChannel, bTraceComplex, WorldContext))
            {
                return false;
            }
            m_box_trace.SetVector(STR("HalfSize"), HalfSize);
            m_box_trace.SetRotator(STR("Orientation"), Orientation);
            if (!FinishTrace(m_box_trace, LibraryCdo, Out))
            {
                return false;
            }
        }

        const Color ShapeColor = Out.bBlockingHit ? Options.HitColor : Options.MissColor;
        const Vec3 StopPoint = Out.bBlockingHit ? Out.Location : End;

        Shapes::SegmentList Segments;
        Shapes::AppendSweptBox(Segments, Start, StopPoint, HalfSize, Orientation, ShapeColor, Options.Thickness);
        GetDrawList().AddSegments(Segments, Options.Duration, Options.Category);
        SubmitImpactVisual(Options, Out);
        return true;
    }

    // ---- Accessors ---------------------------------------------------------

    bool Bridge::GetViewInfo(ViewInfo& Out) const
    {
        std::lock_guard<std::mutex> Lock{m_mutex};
        if (!m_view_valid)
        {
            return false;
        }
        Out = m_view;
        return true;
    }

    Diagnostics Bridge::GetDiagnostics() const
    {
        std::lock_guard<std::mutex> Lock{m_mutex};
        return m_diag;
    }

    bool Bridge::IsRendering() const
    {
        std::lock_guard<std::mutex> Lock{m_mutex};
        return m_diag.HudDrawCallbacks > 0;
    }

    Settings Bridge::GetSettingsSnapshot() const
    {
        std::lock_guard<std::mutex> Lock{m_mutex};
        return m_settings;
    }

    void Bridge::ApplySettings(const Settings& NewSettings)
    {
        std::lock_guard<std::mutex> Lock{m_mutex};
        m_settings = NewSettings;
    }

    void Bridge::ToggleEnabled()
    {
        std::lock_guard<std::mutex> Lock{m_mutex};
        m_settings.bEnabled = !m_settings.bEnabled;
    }

    void Bridge::NotifyControllerTick(UObject* Controller)
    {
        std::lock_guard<std::mutex> Lock{m_mutex};
        if (Controller != m_player_controller)
        {
            m_player_controller = Controller;
            m_diag.PlayerControllerClass = SafeObjectClassName(m_player_controller);
        }
    }
} // namespace TraceViz::Engine
