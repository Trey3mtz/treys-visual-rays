#include "TVShapes.hpp"

namespace TraceViz::Shapes
{
    namespace
    {
        // Keeps segment counts sane no matter what a caller passes in.
        int ClampSegments(int NumSegments)
        {
            if (NumSegments < 4)
            {
                return 4;
            }
            if (NumSegments > 128)
            {
                return 128;
            }
            return NumSegments;
        }

        // Emits an arc of Sweep radians starting from AxisU rotating towards AxisV.
        void AppendArc(SegmentList& Out,
                       const Vec3& Center,
                       const Vec3& AxisU,
                       const Vec3& AxisV,
                       double Radius,
                       double SweepRadians,
                       int NumSegments,
                       const Color& LineColor,
                       float Thickness)
        {
            const int Count = ClampSegments(NumSegments);
            const double Step = SweepRadians / static_cast<double>(Count);

            Vec3 Prev = Center + AxisU * Radius;
            for (int i = 1; i <= Count; ++i)
            {
                const double Angle = Step * static_cast<double>(i);
                const Vec3 Next = Center + AxisU * (Radius * std::cos(Angle)) + AxisV * (Radius * std::sin(Angle));
                Out.push_back(Segment{Prev, Next, LineColor, Thickness});
                Prev = Next;
            }
        }

        // The eight corners of an oriented box, ordered so that bit 0 is the
        // forward axis, bit 1 the right axis and bit 2 the up axis.
        void ComputeBoxCorners(const Vec3& Center, const Vec3& Extent, const Basis& B, Vec3 (&OutCorners)[8])
        {
            for (int i = 0; i < 8; ++i)
            {
                const double SX = (i & 1) ? 1.0 : -1.0;
                const double SY = (i & 2) ? 1.0 : -1.0;
                const double SZ = (i & 4) ? 1.0 : -1.0;
                OutCorners[i] = Center + B.Forward * (Extent.X * SX) + B.Right * (Extent.Y * SY) + B.Up * (Extent.Z * SZ);
            }
        }

        void AppendBoxEdges(SegmentList& Out, const Vec3 (&Corners)[8], const Color& LineColor, float Thickness)
        {
            // Each pair of corner indices differing by exactly one bit is an edge.
            static constexpr int Edges[12][2] = {
                    {0, 1}, {2, 3}, {4, 5}, {6, 7}, // along forward
                    {0, 2}, {1, 3}, {4, 6}, {5, 7}, // along right
                    {0, 4}, {1, 5}, {2, 6}, {3, 7}, // along up
            };
            for (const auto& E : Edges)
            {
                Out.push_back(Segment{Corners[E[0]], Corners[E[1]], LineColor, Thickness});
            }
        }
    } // namespace

    void AppendCircle(SegmentList& Out,
                      const Vec3& Center,
                      const Vec3& AxisU,
                      const Vec3& AxisV,
                      double Radius,
                      int NumSegments,
                      const Color& LineColor,
                      float Thickness)
    {
        AppendArc(Out, Center, AxisU, AxisV, Radius, 2.0 * kPi, NumSegments, LineColor, Thickness);
    }

    void AppendHalfCircle(SegmentList& Out,
                          const Vec3& Center,
                          const Vec3& AxisU,
                          const Vec3& AxisV,
                          double Radius,
                          int NumSegments,
                          const Color& LineColor,
                          float Thickness)
    {
        AppendArc(Out, Center, AxisU, AxisV, Radius, kPi, std::max(2, NumSegments / 2), LineColor, Thickness);
    }

    void AppendLine(SegmentList& Out, const Vec3& Start, const Vec3& End, const Color& LineColor, float Thickness)
    {
        Out.push_back(Segment{Start, End, LineColor, Thickness});
    }

    void AppendArrow(SegmentList& Out, const Vec3& Start, const Vec3& End, double HeadSize, const Color& LineColor, float Thickness)
    {
        Out.push_back(Segment{Start, End, LineColor, Thickness});

        const Vec3 Delta = End - Start;
        const double Len = Length(Delta);
        if (Len <= 1e-6)
        {
            return;
        }

        const Basis B = BasisFromDirection(Delta);
        // Default the head to a fraction of the shaft so short arrows do not
        // end up with a head bigger than the line itself.
        const double Head = (HeadSize > 0.0) ? HeadSize : std::min(Len * 0.15, 24.0);
        const Vec3 Back = End - B.Forward * Head;

        Out.push_back(Segment{End, Back + B.Right * Head * 0.5, LineColor, Thickness});
        Out.push_back(Segment{End, Back - B.Right * Head * 0.5, LineColor, Thickness});
        Out.push_back(Segment{End, Back + B.Up * Head * 0.5, LineColor, Thickness});
        Out.push_back(Segment{End, Back - B.Up * Head * 0.5, LineColor, Thickness});
    }

    void AppendPoint(SegmentList& Out, const Vec3& Center, double Size, const Color& LineColor, float Thickness)
    {
        const double H = Size * 0.5;
        Out.push_back(Segment{Center - Vec3{H, 0, 0}, Center + Vec3{H, 0, 0}, LineColor, Thickness});
        Out.push_back(Segment{Center - Vec3{0, H, 0}, Center + Vec3{0, H, 0}, LineColor, Thickness});
        Out.push_back(Segment{Center - Vec3{0, 0, H}, Center + Vec3{0, 0, H}, LineColor, Thickness});
    }

    void AppendSphere(SegmentList& Out,
                      const Vec3& Center,
                      double Radius,
                      int NumSegments,
                      const Color& LineColor,
                      float Thickness,
                      int NumLatitudeRings)
    {
        const Vec3 X{1.0, 0.0, 0.0};
        const Vec3 Y{0.0, 1.0, 0.0};
        const Vec3 Z{0.0, 0.0, 1.0};

        AppendCircle(Out, Center, X, Y, Radius, NumSegments, LineColor, Thickness);
        AppendCircle(Out, Center, X, Z, Radius, NumSegments, LineColor, Thickness);
        AppendCircle(Out, Center, Y, Z, Radius, NumSegments, LineColor, Thickness);

        // Optional horizontal rings, evenly spaced in latitude between the poles.
        for (int i = 1; i <= NumLatitudeRings; ++i)
        {
            const double T = static_cast<double>(i) / static_cast<double>(NumLatitudeRings + 1);
            const double Latitude = (T - 0.5) * kPi; // -pi/2 .. +pi/2
            const double RingRadius = Radius * std::cos(Latitude);
            const double RingHeight = Radius * std::sin(Latitude);
            if (RingRadius <= 1e-6)
            {
                continue;
            }
            AppendCircle(Out, Center + Z * RingHeight, X, Y, RingRadius, NumSegments, LineColor, Thickness);
        }
    }

    void AppendBox(SegmentList& Out, const Vec3& Center, const Vec3& Extent, const Rotator& Rotation, const Color& LineColor, float Thickness)
    {
        const Basis B = MakeBasis(Rotation);
        Vec3 Corners[8];
        ComputeBoxCorners(Center, Extent, B, Corners);
        AppendBoxEdges(Out, Corners, LineColor, Thickness);
    }

    void AppendCapsule(SegmentList& Out,
                       const Vec3& Center,
                       double HalfHeight,
                       double Radius,
                       const Rotator& Rotation,
                       int NumSegments,
                       const Color& LineColor,
                       float Thickness)
    {
        const Basis B = MakeBasis(Rotation);
        // Unreal measures HalfHeight to the outside of the cap, so the straight
        // section is shorter than HalfHeight by one radius. Clamp at zero so a
        // sphere-like capsule (HalfHeight <= Radius) still renders sensibly.
        const double HalfAxis = std::max(HalfHeight - Radius, 0.0);

        const Vec3 Top = Center + B.Up * HalfAxis;
        const Vec3 Bottom = Center - B.Up * HalfAxis;

        AppendCircle(Out, Top, B.Forward, B.Right, Radius, NumSegments, LineColor, Thickness);
        AppendCircle(Out, Bottom, B.Forward, B.Right, Radius, NumSegments, LineColor, Thickness);

        // Domed caps: two half circles per end, in perpendicular planes.
        AppendHalfCircle(Out, Top, B.Right, B.Up, Radius, NumSegments, LineColor, Thickness);
        AppendHalfCircle(Out, Top, B.Forward, B.Up, Radius, NumSegments, LineColor, Thickness);
        AppendHalfCircle(Out, Bottom, B.Right, -B.Up, Radius, NumSegments, LineColor, Thickness);
        AppendHalfCircle(Out, Bottom, B.Forward, -B.Up, Radius, NumSegments, LineColor, Thickness);

        // Four lines joining the caps along the cylinder.
        Out.push_back(Segment{Top + B.Forward * Radius, Bottom + B.Forward * Radius, LineColor, Thickness});
        Out.push_back(Segment{Top - B.Forward * Radius, Bottom - B.Forward * Radius, LineColor, Thickness});
        Out.push_back(Segment{Top + B.Right * Radius, Bottom + B.Right * Radius, LineColor, Thickness});
        Out.push_back(Segment{Top - B.Right * Radius, Bottom - B.Right * Radius, LineColor, Thickness});
    }

    void AppendCapsuleBetween(SegmentList& Out,
                              const Vec3& CapCenterA,
                              const Vec3& CapCenterB,
                              double Radius,
                              int NumSegments,
                              const Color& LineColor,
                              float Thickness)
    {
        const Vec3 Axis = CapCenterB - CapCenterA;
        const double AxisLen = Length(Axis);

        // Degenerate case: both caps coincide, so this is just a sphere.
        if (AxisLen <= 1e-6)
        {
            AppendSphere(Out, CapCenterA, Radius, NumSegments, LineColor, Thickness);
            return;
        }

        // BasisFromDirection puts the axis in Forward; rotate the roles so the
        // axis lands on Up, matching AppendCapsule's convention.
        const Basis Raw = BasisFromDirection(Axis);
        const Basis Capsule{Raw.Right, Raw.Up, Raw.Forward};

        AppendCircle(Out, CapCenterB, Capsule.Forward, Capsule.Right, Radius, NumSegments, LineColor, Thickness);
        AppendCircle(Out, CapCenterA, Capsule.Forward, Capsule.Right, Radius, NumSegments, LineColor, Thickness);

        AppendHalfCircle(Out, CapCenterB, Capsule.Right, Capsule.Up, Radius, NumSegments, LineColor, Thickness);
        AppendHalfCircle(Out, CapCenterB, Capsule.Forward, Capsule.Up, Radius, NumSegments, LineColor, Thickness);
        AppendHalfCircle(Out, CapCenterA, Capsule.Right, -Capsule.Up, Radius, NumSegments, LineColor, Thickness);
        AppendHalfCircle(Out, CapCenterA, Capsule.Forward, -Capsule.Up, Radius, NumSegments, LineColor, Thickness);

        Out.push_back(Segment{CapCenterB + Capsule.Forward * Radius, CapCenterA + Capsule.Forward * Radius, LineColor, Thickness});
        Out.push_back(Segment{CapCenterB - Capsule.Forward * Radius, CapCenterA - Capsule.Forward * Radius, LineColor, Thickness});
        Out.push_back(Segment{CapCenterB + Capsule.Right * Radius, CapCenterA + Capsule.Right * Radius, LineColor, Thickness});
        Out.push_back(Segment{CapCenterB - Capsule.Right * Radius, CapCenterA - Capsule.Right * Radius, LineColor, Thickness});
    }

    void AppendCone(SegmentList& Out,
                    const Vec3& Origin,
                    const Vec3& Direction,
                    double Height,
                    double AngleDegrees,
                    int NumSegments,
                    const Color& LineColor,
                    float Thickness)
    {
        const Basis B = BasisFromDirection(Direction);
        const double Radius = Height * std::tan(DegToRad(AngleDegrees));
        const Vec3 BaseCenter = Origin + B.Forward * Height;

        AppendCircle(Out, BaseCenter, B.Right, B.Up, Radius, NumSegments, LineColor, Thickness);

        // Four ribs from the apex to the base circle.
        const int Ribs = 4;
        for (int i = 0; i < Ribs; ++i)
        {
            const double Angle = (2.0 * kPi * static_cast<double>(i)) / static_cast<double>(Ribs);
            const Vec3 Rim = BaseCenter + B.Right * (Radius * std::cos(Angle)) + B.Up * (Radius * std::sin(Angle));
            Out.push_back(Segment{Origin, Rim, LineColor, Thickness});
        }
    }

    void AppendCoordinateSystem(SegmentList& Out, const Vec3& Origin, const Rotator& Rotation, double Scale, float Thickness)
    {
        const Basis B = MakeBasis(Rotation);
        Out.push_back(Segment{Origin, Origin + B.Forward * Scale, Colors::Red, Thickness});
        Out.push_back(Segment{Origin, Origin + B.Right * Scale, Colors::Green, Thickness});
        Out.push_back(Segment{Origin, Origin + B.Up * Scale, Colors::Blue, Thickness});
    }

    void AppendSweptSphere(SegmentList& Out,
                           const Vec3& Start,
                           const Vec3& End,
                           double Radius,
                           int NumSegments,
                           const Color& LineColor,
                           float Thickness)
    {
        AppendSphere(Out, Start, Radius, NumSegments, LineColor, Thickness);
        AppendSphere(Out, End, Radius, NumSegments, LineColor, Thickness);

        const Vec3 Delta = End - Start;
        if (LengthSquared(Delta) <= 1e-12)
        {
            return;
        }

        // Four rails along the swept tube, dimmed so the spheres stay readable.
        const Basis B = BasisFromDirection(Delta);
        const Color RailColor = LineColor.WithAlpha(LineColor.A * 0.55f);
        const Vec3 Offsets[4] = {B.Right * Radius, -B.Right * Radius, B.Up * Radius, -B.Up * Radius};
        for (const Vec3& O : Offsets)
        {
            Out.push_back(Segment{Start + O, End + O, RailColor, Thickness});
        }
    }

    void AppendSweptCapsule(SegmentList& Out,
                            const Vec3& Start,
                            const Vec3& End,
                            double HalfHeight,
                            double Radius,
                            const Rotator& Rotation,
                            int NumSegments,
                            const Color& LineColor,
                            float Thickness)
    {
        AppendCapsule(Out, Start, HalfHeight, Radius, Rotation, NumSegments, LineColor, Thickness);
        AppendCapsule(Out, End, HalfHeight, Radius, Rotation, NumSegments, LineColor, Thickness);

        const Vec3 Delta = End - Start;
        if (LengthSquared(Delta) <= 1e-12)
        {
            return;
        }

        // Join the two capsule positions at the cap centres so the sweep path
        // reads clearly without cluttering the silhouette.
        const Basis B = MakeBasis(Rotation);
        const double HalfAxis = std::max(HalfHeight - Radius, 0.0);
        const Color RailColor = LineColor.WithAlpha(LineColor.A * 0.55f);
        Out.push_back(Segment{Start + B.Up * HalfAxis, End + B.Up * HalfAxis, RailColor, Thickness});
        Out.push_back(Segment{Start - B.Up * HalfAxis, End - B.Up * HalfAxis, RailColor, Thickness});
    }

    void AppendSweptBox(SegmentList& Out,
                        const Vec3& Start,
                        const Vec3& End,
                        const Vec3& Extent,
                        const Rotator& Rotation,
                        const Color& LineColor,
                        float Thickness)
    {
        const Basis B = MakeBasis(Rotation);

        Vec3 StartCorners[8];
        Vec3 EndCorners[8];
        ComputeBoxCorners(Start, Extent, B, StartCorners);
        ComputeBoxCorners(End, Extent, B, EndCorners);

        AppendBoxEdges(Out, StartCorners, LineColor, Thickness);
        AppendBoxEdges(Out, EndCorners, LineColor, Thickness);

        if (LengthSquared(End - Start) <= 1e-12)
        {
            return;
        }

        const Color RailColor = LineColor.WithAlpha(LineColor.A * 0.55f);
        for (int i = 0; i < 8; ++i)
        {
            Out.push_back(Segment{StartCorners[i], EndCorners[i], RailColor, Thickness});
        }
    }
} // namespace TraceViz::Shapes
