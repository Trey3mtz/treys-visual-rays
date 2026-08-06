// TraceVisualizer - runtime ray and shape trace visualisation for a shipping
// Unreal Engine 5.1 build.
//
// See README.md for the design rationale. The short version: a shipping build
// defines ENABLE_DRAW_DEBUG as 0, which compiles the body out of every
// DrawDebug* helper and out of UKismetSystemLibrary's DrawDebugType handling
// with them. The UFunctions still exist and are still callable, they just do
// nothing. AHUD::DrawLine is not debug code, so it survives, and we drive it
// from inside the HUD's own draw event where the canvas is valid.

#include <Mod/CppUserModBase.hpp>
#include <UE4SSProgram.hpp>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/UEngine.hpp>
#include <Unreal/UObject.hpp>

#include <imgui.h>

#include "TVDrawList.hpp"
#include "TVEngine.hpp"
#include "TVShapes.hpp"

namespace TraceViz
{
    using namespace RC;

    namespace
    {
        // Category used by the panel's own test geometry, so it can be toggled
        // and cleared independently of whatever a mod is drawing.
        constexpr CategoryId kTestPatternCategory = 0xDEB0'0001u;

        void DrawStatusLine(const char* Label, bool bOk, const char* OkText = "yes", const char* BadText = "no")
        {
            ImGui::Text("%s:", Label);
            ImGui::SameLine();
            const ImVec4 Colour = bOk ? ImVec4{0.35f, 0.85f, 0.35f, 1.0f} : ImVec4{0.95f, 0.4f, 0.35f, 1.0f};
            ImGui::TextColored(Colour, "%s", bOk ? OkText : BadText);
        }

        // Places a representative set of shapes in front of the camera. This is
        // the first thing to try: if these do not appear, the problem is in
        // rendering, not in whatever you were trying to visualise.
        void SpawnTestPattern()
        {
            ViewInfo View{};
            if (!Engine::Get().GetViewInfo(View))
            {
                Output::send<LogLevel::Warning>(STR("[TraceViz] No camera view yet; cannot place the test pattern.\n"));
                return;
            }

            const Basis B = MakeBasis(View.Rotation);
            const Vec3 Origin = View.Location + B.Forward * 400.0;

            Shapes::SegmentList Out;

            // A triad at the origin point tells you immediately whether the
            // axis convention and the projection agree with the world.
            Shapes::AppendCoordinateSystem(Out, Origin, View.Rotation, 120.0, 2.0f);

            Shapes::AppendSphere(Out, Origin + B.Right * -250.0, 60.0, 24, Colors::Cyan, 1.0f, 2);
            Shapes::AppendBox(Out, Origin + B.Right * 250.0, Vec3{60, 60, 60}, Rotator{0, 30, 0}, Colors::Yellow, 1.0f);
            Shapes::AppendCapsule(Out, Origin + B.Up * -180.0, 96.0, 34.0, Rotator{}, 24, Colors::Green, 1.0f);
            Shapes::AppendArrow(Out, Origin, Origin + B.Forward * 300.0, 0.0, Colors::Magenta, 2.0f);
            Shapes::AppendCone(Out, Origin + B.Up * 200.0, B.Forward, 200.0, 20.0, 24, Colors::Orange, 1.0f);

            GetDrawList().AddSegments(Out, kPersistent, kTestPatternCategory);
            Output::send<LogLevel::Default>(STR("[TraceViz] Placed test pattern ({} segments).\n"), Out.size());
        }

        // Fires a trace straight out of the camera and visualises it. This
        // exercises the whole path end to end: reflection call, hit readback
        // and drawing.
        void FireTestTrace(int Kind)
        {
            ViewInfo View{};
            if (!Engine::Get().GetViewInfo(View))
            {
                return;
            }

            const Basis B = MakeBasis(View.Rotation);
            const Vec3 Start = View.Location + B.Forward * 50.0;
            const Vec3 End = View.Location + B.Forward * 3000.0;

            Engine::TraceDrawOptions Options{};
            Options.Duration = 5.0f;
            Options.Thickness = 2.0f;
            Options.Category = kTestPatternCategory;

            Engine::TraceResult Result{};
            bool bRan = false;
            switch (Kind)
            {
            case 0:
                bRan = Engine::Get().LineTrace(Start, End, 0, false, Options, Result);
                break;
            case 1:
                bRan = Engine::Get().SphereTrace(Start, End, 40.0, 0, false, Options, Result);
                break;
            case 2:
                bRan = Engine::Get().CapsuleTrace(Start, End, 34.0, 88.0, 0, false, Options, Result);
                break;
            default:
                bRan = Engine::Get().BoxTrace(Start, End, Vec3{40, 40, 40}, Rotator{}, 0, false, Options, Result);
                break;
            }

            if (!bRan)
            {
                Output::send<LogLevel::Warning>(STR("[TraceViz] Test trace could not run; the trace UFunction is unbound.\n"));
                return;
            }
            Output::send<LogLevel::Default>(STR("[TraceViz] Test trace: hit={} distance={:.1f}\n"), Result.bBlockingHit, Result.Distance);
        }
    } // namespace

    class TraceVisualizerMod : public CppUserModBase
    {
      public:
        TraceVisualizerMod() : CppUserModBase()
        {
            ModName = STR("TraceVisualizer");
            ModVersion = STR("0.1.0");
            ModDescription = STR("Draws rays, sweeps and shape traces in-game, in a build where DrawDebug is compiled out.");
            ModAuthors = STR("trey3mtz");

            register_tab(STR("TraceViz"), [](CppUserModBase* Mod) {
                UE4SS_ENABLE_IMGUI()
                static_cast<TraceVisualizerMod*>(Mod)->RenderPanel();
            });

            // F9 toggles drawing without opening the UE4SS GUI, F10 clears.
            // CppUserModBase does not expose register_keydown_event itself
            // (only register_tab); it is a pass-through on UE4SSProgram meant
            // for exactly this kind of external call.
            UE4SSProgram::get_program().register_keydown_event(Input::Key::F9, [](){
                Engine::Get().ToggleEnabled();
                const bool bNowEnabled = Engine::Get().GetSettingsSnapshot().bEnabled;
                Output::send<LogLevel::Default>(STR("[TraceViz] Drawing {}.\n"), bNowEnabled ? STR("enabled") : STR("disabled"));
            });
            UE4SSProgram::get_program().register_keydown_event(Input::Key::F10, [](){
                GetDrawList().Clear();
                Output::send<LogLevel::Default>(STR("[TraceViz] Cleared draw list.\n"));
            });
        }

        ~TraceVisualizerMod() override = default;

        auto on_unreal_init() -> void override
        {
            Engine::Get().OnUnrealInit();

            // Everything that touches the engine has to happen on the game
            // thread. UE4SS's on_update() runs on its own thread, so the engine
            // tick is the only correct place for per-frame work.
            //
            // Unreal::Hook::EngineTickCallback is
            // std::function<void(UEngine*, float)>, not a raw function
            // pointer, and the parameter type here has to be UEngine* (not
            // the more general UObject*) with <Unreal/UEngine.hpp> actually
            // included: std::function's constructor needs to verify the
            // lambda is invocable with a UEngine*, which for a UObject*
            // parameter means checking the derived-to-base conversion, which
            // in turn needs UEngine to be a complete type in this
            // translation unit, not just forward-declared.
            Unreal::Hook::RegisterEngineTickPostCallback([](Unreal::UEngine*, float DeltaSeconds) {
                Engine::Get().OnGameTick(DeltaSeconds);
            });

            Output::send<LogLevel::Default>(STR("[TraceViz] Initialised. Open the UE4SS GUI and select the TraceViz tab.\n"));
        }

        void RenderPanel();
    };

    void TraceVisualizerMod::RenderPanel()
    {
        auto& Bridge = Engine::Get();
        const Engine::Diagnostics Diag = Bridge.GetDiagnostics();

        // A local copy, not a reference: this panel can run on UE4SS's own
        // render thread (RenderMode::ExternalThread), which is a different
        // thread from the game thread that reads Settings on every tick.
        // ImGui widgets below bind directly to fields on this local copy,
        // which is safe because nothing else can see it; the snapshot is
        // written back to the Bridge once, under lock, at the end.
        Engine::Settings Settings = Bridge.GetSettingsSnapshot();

        // ---- Status. This is the section that answers "why do I see nothing".
        if (ImGui::CollapsingHeader("Status", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();
            DrawStatusLine("Unreal initialised", Diag.bUnrealInitialised);
            DrawStatusLine("PlayerController", Diag.bPlayerControllerFound);
            if (Diag.bPlayerControllerFound)
            {
                ImGui::SameLine();
                ImGui::Text("(%S)", Diag.PlayerControllerClass.c_str());
            }
            DrawStatusLine("HUD", Diag.bHudFound);
            if (Diag.bHudFound)
            {
                ImGui::SameLine();
                ImGui::Text("(%S)", Diag.HudClass.c_str());
            }
            DrawStatusLine("Camera manager", Diag.bCameraManagerFound);
            DrawStatusLine("Draw hook installed", Diag.bHudHookInstalled);

            // The single most informative number here: if the HUD never draws,
            // nothing else can possibly appear.
            const bool bDrawing = Diag.HudDrawCallbacks > 0;
            DrawStatusLine("HUD draw callbacks firing", bDrawing);
            ImGui::Text("  game ticks: %llu, hud draws: %llu", static_cast<unsigned long long>(Diag.GameTicks),
                        static_cast<unsigned long long>(Diag.HudDrawCallbacks));

            if (!bDrawing && Diag.bHudHookInstalled)
            {
                ImGui::TextWrapped("The hook is installed but has not fired. The HUD is probably hidden "
                                   "(ShowHUD console command toggles it) or this game routes its HUD "
                                   "through UMG only.");
            }

            ImGui::Text("Segments drawn last frame: %llu", static_cast<unsigned long long>(Diag.SegmentsDrawnLastFrame));
            if (Diag.SegmentsClampedLastFrame > 0)
            {
                ImGui::TextColored(ImVec4{1.0f, 0.7f, 0.2f, 1.0f},
                                   "Clamped %llu segments (raise the per-frame budget below)",
                                   static_cast<unsigned long long>(Diag.SegmentsClampedLastFrame));
            }
            if (Diag.DrawLineFailures > 0)
            {
                ImGui::TextColored(ImVec4{0.95f, 0.4f, 0.35f, 1.0f},
                                   "DrawLine parameter writes failed %llu times",
                                   static_cast<unsigned long long>(Diag.DrawLineFailures));
            }
            if (!Diag.LastError.empty())
            {
                ImGui::TextColored(ImVec4{0.95f, 0.4f, 0.35f, 1.0f}, "%S", Diag.LastError.c_str());
            }
            ImGui::Unindent();
        }

        // ---- Camera and projection.
        if (ImGui::CollapsingHeader("Camera and projection", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();
            ImGui::Text("Location: %.1f, %.1f, %.1f", Diag.CameraLocation.X, Diag.CameraLocation.Y, Diag.CameraLocation.Z);
            ImGui::Text("Rotation: pitch %.1f, yaw %.1f, roll %.1f", Diag.CameraRotation.Pitch, Diag.CameraRotation.Yaw, Diag.CameraRotation.Roll);
            ImGui::Text("FOV: %.2f    Viewport: %.0f x %.0f", Diag.CameraFov, Diag.ViewportWidth, Diag.ViewportHeight);

            int ConstraintIndex = (Settings.Constraint == AspectConstraint::MaintainXFov) ? 0 : 1;
            ImGui::Text("Aspect constraint:");
            ImGui::SameLine();
            if (ImGui::RadioButton("MaintainXFOV", &ConstraintIndex, 0))
            {
                Settings.Constraint = AspectConstraint::MaintainXFov;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("MaintainYFOV", &ConstraintIndex, 1))
            {
                Settings.Constraint = AspectConstraint::MaintainYFov;
            }

            float FovScale = static_cast<float>(Settings.FovScale);
            if (ImGui::SliderFloat("FOV scale", &FovScale, 0.5f, 2.0f, "%.3f"))
            {
                Settings.FovScale = FovScale;
            }

            ImGui::Separator();
            ImGui::TextWrapped("Calibration compares this mod's own projection against the engine's "
                               "ProjectWorldLocationToScreen at five points. Under a pixel means the fast "
                               "path is correct; a large error at the edges but not the centre means the "
                               "aspect constraint or FOV scale above is wrong.");
            if (ImGui::Button("Run projection calibration"))
            {
                // Deferred to the game thread: this panel runs on UE4SS's
                // render thread, and ProcessEvent is not safe from there.
                Bridge.RequestCalibration();
            }
            if (Diag.CalibrationErrorPixels >= 0.0)
            {
                const bool bGood = Diag.CalibrationErrorPixels < 1.0;
                ImGui::TextColored(bGood ? ImVec4{0.35f, 0.85f, 0.35f, 1.0f} : ImVec4{1.0f, 0.7f, 0.2f, 1.0f},
                                   "Worst error: %.2f px",
                                   Diag.CalibrationErrorPixels);
            }
            if (!Diag.CalibrationDetail.empty())
            {
                ImGui::TextWrapped("%S", Diag.CalibrationDetail.c_str());
            }
            ImGui::Unindent();
        }

        // ---- Settings.
        if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();
            ImGui::Checkbox("Enabled (F9)", &Settings.bEnabled);

            int MaxSegments = static_cast<int>(Settings.MaxSegmentsPerFrame);
            if (ImGui::SliderInt("Max segments/frame", &MaxSegments, 100, 40000))
            {
                Settings.MaxSegmentsPerFrame = static_cast<uint32_t>(MaxSegments);
            }
            ImGui::TextWrapped("Each segment is one reflected AHUD::DrawLine call, so this is the main "
                               "cost control.");

            ImGui::SliderFloat("Thickness scale", &Settings.GlobalThicknessScale, 0.1f, 8.0f);
            ImGui::SliderFloat("Global alpha", &Settings.GlobalAlpha, 0.0f, 1.0f);

            float NearClip = static_cast<float>(Settings.NearClip);
            if (ImGui::SliderFloat("Near clip (uu)", &NearClip, 0.1f, 50.0f))
            {
                Settings.NearClip = NearClip;
            }
            ImGui::Unindent();
        }

        // Write back once, after every widget above has had a chance to touch
        // the local copy. Unconditional so a frame with no interaction is a
        // harmless no-op rather than a special case to get right.
        Bridge.ApplySettings(Settings);

        // ---- Test geometry.
        if (ImGui::CollapsingHeader("Test", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();
            ImGui::TextWrapped("Start here. If the test pattern does not appear 4 m in front of you, the "
                               "problem is rendering rather than anything you are trying to visualise.");
            if (ImGui::Button("Place test pattern"))
            {
                SpawnTestPattern();
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear all"))
            {
                GetDrawList().Clear();
            }

            ImGui::Text("Fire a trace from the camera:");
            if (ImGui::Button("Line"))
            {
                FireTestTrace(0);
            }
            ImGui::SameLine();
            if (ImGui::Button("Sphere"))
            {
                FireTestTrace(1);
            }
            ImGui::SameLine();
            if (ImGui::Button("Capsule"))
            {
                FireTestTrace(2);
            }
            ImGui::SameLine();
            if (ImGui::Button("Box"))
            {
                FireTestTrace(3);
            }
            ImGui::TextWrapped("Traces run on the game thread via the engine tick, so a click takes effect "
                               "on the next frame.");
            ImGui::Unindent();
        }

        // ---- Draw list.
        if (ImGui::CollapsingHeader("Draw list"))
        {
            ImGui::Indent();
            const DrawListStats Stats = GetDrawList().GetStats();
            ImGui::Text("Live segments: %zu (peak %zu)", Stats.SegmentCount, Stats.PeakSegmentCount);
            ImGui::Text("Labels: %zu", Stats.LabelCount);
            if (Stats.SegmentsDroppedThisFrame > 0)
            {
                ImGui::TextColored(ImVec4{1.0f, 0.7f, 0.2f, 1.0f}, "Dropped %zu (draw list is full)", Stats.SegmentsDroppedThisFrame);
            }
            if (ImGui::Button("Clear persistent only"))
            {
                GetDrawList().ClearPersistent();
            }
            ImGui::Unindent();
        }

        // ---- Reflection detail. Only interesting when something is wrong,
        // but then it is the first place to look.
        if (ImGui::CollapsingHeader("Bound engine functions"))
        {
            ImGui::Indent();
            if (!Diag.MissingFunctions.empty())
            {
                ImGui::TextColored(ImVec4{0.95f, 0.4f, 0.35f, 1.0f}, "Missing:");
                ImGui::TextWrapped("%S", Diag.MissingFunctions.c_str());
            }
            ImGui::Text("Resolved signatures:");
            ImGui::TextWrapped("%S", Diag.BoundSignatures.c_str());
            if (!Diag.HookedFunctionName.empty())
            {
                ImGui::Text("Hooked: %S", Diag.HookedFunctionName.c_str());
            }
            ImGui::Unindent();
        }
    }
} // namespace TraceViz

#define TRACEVIZ_MOD_EXPORT __declspec(dllexport)
extern "C"
{
    TRACEVIZ_MOD_EXPORT RC::CppUserModBase* start_mod()
    {
        return new TraceViz::TraceVisualizerMod();
    }

    TRACEVIZ_MOD_EXPORT void uninstall_mod(RC::CppUserModBase* Mod)
    {
        delete Mod;
    }
}
