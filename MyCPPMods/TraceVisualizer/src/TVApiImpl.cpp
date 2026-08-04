// Implementation of the public C ABI declared in TraceViz/TraceVizAPI.h.
//
// This is the only file that other mods' code paths reach, so it is
// deliberately thin: convert the plain C types to our internal ones, forward,
// and never let an exception escape across the ABI boundary.

#include <TraceViz/TraceVizAPI.h>

#include "TVDrawList.hpp"
#include "TVEngine.hpp"
#include "TVShapes.hpp"

namespace
{
    using namespace TraceViz;

    Vec3 ToVec3(const TraceVizVec3& V)
    {
        return Vec3{V.X, V.Y, V.Z};
    }

    TraceVizVec3 FromVec3(const Vec3& V)
    {
        TraceVizVec3 Out{};
        Out.X = V.X;
        Out.Y = V.Y;
        Out.Z = V.Z;
        return Out;
    }

    Rotator ToRotator(const TraceVizRotator& R)
    {
        return Rotator{R.Pitch, R.Yaw, R.Roll};
    }

    Color ToColor(const TraceVizColor& C)
    {
        return Color{C.R, C.G, C.B, C.A};
    }

    int ClampSegments(int32_t Requested)
    {
        if (Requested <= 0)
        {
            return 16;
        }
        return (Requested > 128) ? 128 : static_cast<int>(Requested);
    }

    void FillHit(const Engine::TraceResult& Result, TraceVizHit* Out)
    {
        if (!Out)
        {
            return;
        }
        Out->bBlockingHit = Result.bBlockingHit ? 1 : 0;
        Out->ImpactPoint = FromVec3(Result.ImpactPoint);
        Out->ImpactNormal = FromVec3(Result.ImpactNormal);
        Out->Location = FromVec3(Result.Location);
        Out->Distance = Result.Distance;
    }

    Engine::TraceDrawOptions MakeOptions(float Duration, uint32_t Category)
    {
        Engine::TraceDrawOptions Options{};
        Options.Duration = Duration;
        Options.Category = Category;
        return Options;
    }

    // ---- Drawing -----------------------------------------------------------

    void API_DrawLine(TraceVizVec3 Start, TraceVizVec3 End, TraceVizColor InColor, float Thickness, float Duration, uint32_t Category)
    {
        GetDrawList().AddSegment(Segment{ToVec3(Start), ToVec3(End), ToColor(InColor), Thickness}, Duration, Category);
    }

    void API_DrawArrow(TraceVizVec3 Start, TraceVizVec3 End, double HeadSize, TraceVizColor InColor, float Thickness, float Duration, uint32_t Category)
    {
        Shapes::SegmentList Out;
        Shapes::AppendArrow(Out, ToVec3(Start), ToVec3(End), HeadSize, ToColor(InColor), Thickness);
        GetDrawList().AddSegments(Out, Duration, Category);
    }

    void API_DrawPoint(TraceVizVec3 Center, double Size, TraceVizColor InColor, float Thickness, float Duration, uint32_t Category)
    {
        Shapes::SegmentList Out;
        Shapes::AppendPoint(Out, ToVec3(Center), Size, ToColor(InColor), Thickness);
        GetDrawList().AddSegments(Out, Duration, Category);
    }

    void API_DrawSphere(TraceVizVec3 Center, double Radius, int32_t Segments, TraceVizColor InColor, float Thickness, float Duration, uint32_t Category)
    {
        Shapes::SegmentList Out;
        Shapes::AppendSphere(Out, ToVec3(Center), Radius, ClampSegments(Segments), ToColor(InColor), Thickness);
        GetDrawList().AddSegments(Out, Duration, Category);
    }

    void API_DrawBox(TraceVizVec3 Center,
                     TraceVizVec3 Extent,
                     TraceVizRotator Rotation,
                     TraceVizColor InColor,
                     float Thickness,
                     float Duration,
                     uint32_t Category)
    {
        Shapes::SegmentList Out;
        Shapes::AppendBox(Out, ToVec3(Center), ToVec3(Extent), ToRotator(Rotation), ToColor(InColor), Thickness);
        GetDrawList().AddSegments(Out, Duration, Category);
    }

    void API_DrawCapsule(TraceVizVec3 Center,
                         double HalfHeight,
                         double Radius,
                         TraceVizRotator Rotation,
                         int32_t Segments,
                         TraceVizColor InColor,
                         float Thickness,
                         float Duration,
                         uint32_t Category)
    {
        Shapes::SegmentList Out;
        Shapes::AppendCapsule(Out, ToVec3(Center), HalfHeight, Radius, ToRotator(Rotation), ClampSegments(Segments), ToColor(InColor), Thickness);
        GetDrawList().AddSegments(Out, Duration, Category);
    }

    void API_DrawCone(TraceVizVec3 Origin,
                      TraceVizVec3 Direction,
                      double Height,
                      double AngleDegrees,
                      int32_t Segments,
                      TraceVizColor InColor,
                      float Thickness,
                      float Duration,
                      uint32_t Category)
    {
        Shapes::SegmentList Out;
        Shapes::AppendCone(Out, ToVec3(Origin), ToVec3(Direction), Height, AngleDegrees, ClampSegments(Segments), ToColor(InColor), Thickness);
        GetDrawList().AddSegments(Out, Duration, Category);
    }

    void API_DrawCoordinateSystem(TraceVizVec3 Origin, TraceVizRotator Rotation, double Scale, float Thickness, float Duration, uint32_t Category)
    {
        Shapes::SegmentList Out;
        Shapes::AppendCoordinateSystem(Out, ToVec3(Origin), ToRotator(Rotation), Scale, Thickness);
        GetDrawList().AddSegments(Out, Duration, Category);
    }

    // ---- Traces ------------------------------------------------------------

    int32_t API_LineTrace(TraceVizVec3 Start,
                          TraceVizVec3 End,
                          uint8_t TraceChannel,
                          int32_t bTraceComplex,
                          float Duration,
                          uint32_t Category,
                          TraceVizHit* OutHit)
    {
        Engine::TraceResult Result{};
        const bool bRan =
                Engine::Get().LineTrace(ToVec3(Start), ToVec3(End), TraceChannel, bTraceComplex != 0, MakeOptions(Duration, Category), Result);
        FillHit(Result, OutHit);
        return bRan ? 1 : 0;
    }

    int32_t API_SphereTrace(TraceVizVec3 Start,
                            TraceVizVec3 End,
                            double Radius,
                            uint8_t TraceChannel,
                            int32_t bTraceComplex,
                            float Duration,
                            uint32_t Category,
                            TraceVizHit* OutHit)
    {
        Engine::TraceResult Result{};
        const bool bRan = Engine::Get().SphereTrace(ToVec3(Start),
                                                    ToVec3(End),
                                                    Radius,
                                                    TraceChannel,
                                                    bTraceComplex != 0,
                                                    MakeOptions(Duration, Category),
                                                    Result);
        FillHit(Result, OutHit);
        return bRan ? 1 : 0;
    }

    int32_t API_CapsuleTrace(TraceVizVec3 Start,
                             TraceVizVec3 End,
                             double Radius,
                             double HalfHeight,
                             uint8_t TraceChannel,
                             int32_t bTraceComplex,
                             float Duration,
                             uint32_t Category,
                             TraceVizHit* OutHit)
    {
        Engine::TraceResult Result{};
        const bool bRan = Engine::Get().CapsuleTrace(ToVec3(Start),
                                                     ToVec3(End),
                                                     Radius,
                                                     HalfHeight,
                                                     TraceChannel,
                                                     bTraceComplex != 0,
                                                     MakeOptions(Duration, Category),
                                                     Result);
        FillHit(Result, OutHit);
        return bRan ? 1 : 0;
    }

    int32_t API_BoxTrace(TraceVizVec3 Start,
                         TraceVizVec3 End,
                         TraceVizVec3 HalfSize,
                         TraceVizRotator Orientation,
                         uint8_t TraceChannel,
                         int32_t bTraceComplex,
                         float Duration,
                         uint32_t Category,
                         TraceVizHit* OutHit)
    {
        Engine::TraceResult Result{};
        const bool bRan = Engine::Get().BoxTrace(ToVec3(Start),
                                                 ToVec3(End),
                                                 ToVec3(HalfSize),
                                                 ToRotator(Orientation),
                                                 TraceChannel,
                                                 bTraceComplex != 0,
                                                 MakeOptions(Duration, Category),
                                                 Result);
        FillHit(Result, OutHit);
        return bRan ? 1 : 0;
    }

    // ---- Management --------------------------------------------------------

    void API_Clear(void)
    {
        GetDrawList().Clear();
    }

    void API_ClearPersistent(void)
    {
        GetDrawList().ClearPersistent();
    }

    void API_SetCategoryEnabled(uint32_t Category, int32_t bEnabled)
    {
        GetDrawList().SetCategoryEnabled(Category, bEnabled != 0);
    }

    int32_t API_IsRendering(void)
    {
        return Engine::Get().IsRendering() ? 1 : 0;
    }

    const TraceVizAPI g_api = {
            sizeof(TraceVizAPI),
            TRACEVIZ_API_VERSION,
            &API_DrawLine,
            &API_DrawArrow,
            &API_DrawPoint,
            &API_DrawSphere,
            &API_DrawBox,
            &API_DrawCapsule,
            &API_DrawCone,
            &API_DrawCoordinateSystem,
            &API_LineTrace,
            &API_SphereTrace,
            &API_CapsuleTrace,
            &API_BoxTrace,
            &API_Clear,
            &API_ClearPersistent,
            &API_SetCategoryEnabled,
            &API_IsRendering,
    };
} // namespace

extern "C" __declspec(dllexport) const TraceVizAPI* TraceViz_GetAPI(uint32_t RequestedVersion)
{
    // Refusing a mismatched major version is better than handing back a table
    // the caller will misread. Consumers are expected to treat null as
    // "visualisation unavailable".
    if (RequestedVersion != TRACEVIZ_API_VERSION)
    {
        return nullptr;
    }
    return &g_api;
}
