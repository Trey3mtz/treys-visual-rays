#pragma once

// Tessellation of debug shapes into line segments.
//
// Every shape the visualizer understands is reduced to a list of 3D segments
// here, and the renderer only ever has to know how to draw a segment. Shape
// parameters mirror Unreal's DrawDebug* helpers so that anything you already
// know from `DrawDebugCapsule` etc. transfers directly, even though those
// helpers are compiled out of a shipping build.

#include <vector>

#include "TVMath.hpp"

namespace TraceViz::Shapes
{
    using SegmentList = std::vector<Segment>;

    // Circle centred at Center, spanned by the two (assumed orthonormal) axes.
    void AppendCircle(SegmentList& Out,
                      const Vec3& Center,
                      const Vec3& AxisU,
                      const Vec3& AxisV,
                      double Radius,
                      int NumSegments,
                      const Color& LineColor,
                      float Thickness);

    // Half circle sweeping from AxisU towards AxisV. Used for capsule caps.
    void AppendHalfCircle(SegmentList& Out,
                          const Vec3& Center,
                          const Vec3& AxisU,
                          const Vec3& AxisV,
                          double Radius,
                          int NumSegments,
                          const Color& LineColor,
                          float Thickness);

    void AppendLine(SegmentList& Out, const Vec3& Start, const Vec3& End, const Color& LineColor, float Thickness);

    // Line with a four-pronged arrowhead at End.
    void AppendArrow(SegmentList& Out, const Vec3& Start, const Vec3& End, double HeadSize, const Color& LineColor, float Thickness);

    // Three-axis cross marker.
    void AppendPoint(SegmentList& Out, const Vec3& Center, double Size, const Color& LineColor, float Thickness);

    // Three great circles, plus optional latitude rings for a denser wireframe.
    void AppendSphere(SegmentList& Out,
                      const Vec3& Center,
                      double Radius,
                      int NumSegments,
                      const Color& LineColor,
                      float Thickness,
                      int NumLatitudeRings = 0);

    // Axis-aligned-in-local-space box. Extent is the half-size on each axis.
    void AppendBox(SegmentList& Out, const Vec3& Center, const Vec3& Extent, const Rotator& Rotation, const Color& LineColor, float Thickness);

    // Capsule matching Unreal's convention: the axis runs along the rotated Z
    // axis, and HalfHeight is measured to the outside of the caps (so the
    // cylindrical section is HalfHeight - Radius long).
    void AppendCapsule(SegmentList& Out,
                       const Vec3& Center,
                       double HalfHeight,
                       double Radius,
                       const Rotator& Rotation,
                       int NumSegments,
                       const Color& LineColor,
                       float Thickness);

    // Capsule defined by the centres of its two end caps. This is usually what
    // you want when visualising a sweep, since it maps straight onto the
    // start/end of the trace.
    void AppendCapsuleBetween(SegmentList& Out,
                              const Vec3& CapCenterA,
                              const Vec3& CapCenterB,
                              double Radius,
                              int NumSegments,
                              const Color& LineColor,
                              float Thickness);

    // Cone with its apex at Origin opening along Direction.
    void AppendCone(SegmentList& Out,
                    const Vec3& Origin,
                    const Vec3& Direction,
                    double Height,
                    double AngleDegrees,
                    int NumSegments,
                    const Color& LineColor,
                    float Thickness);

    // Red/green/blue forward/right/up triad.
    void AppendCoordinateSystem(SegmentList& Out, const Vec3& Origin, const Rotator& Rotation, double Scale, float Thickness);

    // ---- Sweep helpers -------------------------------------------------------
    //
    // These draw the *swept volume* of a shape trace: the shape at the start,
    // the shape at the end, and connecting lines showing the path. This is the
    // thing that is genuinely hard to picture in your head, and the reason
    // shape traces get mispredicted so often.

    void AppendSweptSphere(SegmentList& Out,
                           const Vec3& Start,
                           const Vec3& End,
                           double Radius,
                           int NumSegments,
                           const Color& LineColor,
                           float Thickness);

    void AppendSweptCapsule(SegmentList& Out,
                            const Vec3& Start,
                            const Vec3& End,
                            double HalfHeight,
                            double Radius,
                            const Rotator& Rotation,
                            int NumSegments,
                            const Color& LineColor,
                            float Thickness);

    void AppendSweptBox(SegmentList& Out,
                        const Vec3& Start,
                        const Vec3& End,
                        const Vec3& Extent,
                        const Rotator& Rotation,
                        const Color& LineColor,
                        float Thickness);
} // namespace TraceViz::Shapes
