// Verification harness for the dependency-free half of the visualizer.
#include "TVShapes.hpp"
#include "TVDrawList.hpp"

#include <cstdio>
#include <cmath>

using namespace TraceViz;

static int g_failures = 0;

static void Check(bool Cond, const char* What)
{
    if (!Cond)
    {
        std::printf("  FAIL: %s\n", What);
        ++g_failures;
    }
}

static bool Near(double A, double B, double Eps = 1e-9)
{
    return std::abs(A - B) <= Eps;
}

// Distance from point P to the segment AB.
static double DistToSegment(const Vec3& P, const Vec3& A, const Vec3& B)
{
    const Vec3 AB = B - A;
    const double L2 = LengthSquared(AB);
    if (L2 <= 1e-12) return Length(P - A);
    double T = Dot(P - A, AB) / L2;
    T = T < 0 ? 0 : (T > 1 ? 1 : T);
    return Length(P - (A + AB * T));
}

static void TestBasis()
{
    std::printf("Basis:\n");
    // Yaw 90 should point forward down +Y, right down -X.
    const Basis Y90 = MakeBasis(Rotator{0, 90, 0});
    Check(Near(Y90.Forward.X, 0, 1e-12) && Near(Y90.Forward.Y, 1, 1e-12) && Near(Y90.Forward.Z, 0, 1e-12), "yaw90 forward == +Y");
    Check(Near(Y90.Right.X, -1, 1e-12) && Near(Y90.Right.Y, 0, 1e-12), "yaw90 right == -X");
    Check(Near(Y90.Up.Z, 1, 1e-12), "yaw90 up == +Z");

    // Pitch 90 should point straight up.
    const Basis P90 = MakeBasis(Rotator{90, 0, 0});
    Check(Near(P90.Forward.Z, 1, 1e-12), "pitch90 forward == +Z");

    // Orthonormality across a spread of rotations.
    const Rotator Samples[] = {{0,0,0},{30,45,60},{-80,170,-35},{12.5,-99,44},{89,1,-179}};
    for (const Rotator& R : Samples)
    {
        const Basis B = MakeBasis(R);
        Check(Near(Length(B.Forward), 1, 1e-12), "forward unit");
        Check(Near(Length(B.Right), 1, 1e-12), "right unit");
        Check(Near(Length(B.Up), 1, 1e-12), "up unit");
        Check(Near(Dot(B.Forward, B.Right), 0, 1e-12), "forward.right orthogonal");
        Check(Near(Dot(B.Forward, B.Up), 0, 1e-12), "forward.up orthogonal");
        Check(Near(Dot(B.Right, B.Up), 0, 1e-12), "right.up orthogonal");
        // Right-handed: forward x right == up (Unreal's convention).
        const Vec3 C = Cross(B.Forward, B.Right);
        Check(Near(C.X, B.Up.X, 1e-12) && Near(C.Y, B.Up.Y, 1e-12) && Near(C.Z, B.Up.Z, 1e-12), "forward x right == up");
    }

    // BasisFromDirection must stay well-conditioned even for near-degenerate seeds.
    const Vec3 Dirs[] = {{0,0,1},{0,0,-1},{1,0,0},{0.0001,0,0.9999},{-3,7,-2}};
    for (const Vec3& D : Dirs)
    {
        const Basis B = BasisFromDirection(D);
        Check(Near(Length(B.Forward), 1, 1e-9), "dir basis forward unit");
        Check(Near(Length(B.Right), 1, 1e-9), "dir basis right unit");
        Check(Near(Length(B.Up), 1, 1e-9), "dir basis up unit");
        Check(Near(Dot(B.Forward, B.Right), 0, 1e-9), "dir basis orthogonal");
    }
}

static void TestSphere()
{
    std::printf("Sphere:\n");
    Shapes::SegmentList Out;
    const Vec3 C{100, -50, 25};
    const double R = 42.0;
    Shapes::AppendSphere(Out, C, R, 24, Colors::Red, 1.0f, 3);
    Check(!Out.empty(), "sphere produced segments");

    double MaxErr = 0;
    for (const Segment& S : Out)
    {
        MaxErr = std::max(MaxErr, std::abs(Length(S.A - C) - R));
        MaxErr = std::max(MaxErr, std::abs(Length(S.B - C) - R));
    }
    std::printf("  segments=%zu  max radial error=%.3e\n", Out.size(), MaxErr);
    Check(MaxErr < 1e-9, "all sphere vertices lie on the sphere");
}

static void TestCapsuleBetween()
{
    std::printf("CapsuleBetween:\n");
    Shapes::SegmentList Out;
    const Vec3 A{0, 0, 0};
    const Vec3 B{0, 0, 200};
    const double R = 34.0;
    Shapes::AppendCapsuleBetween(Out, A, B, R, 24, Colors::Cyan, 1.0f);
    Check(!Out.empty(), "capsule produced segments");

    // Every vertex must sit on the capsule surface: distance to the core
    // segment equals the radius.
    double MaxErr = 0;
    for (const Segment& S : Out)
    {
        MaxErr = std::max(MaxErr, std::abs(DistToSegment(S.A, A, B) - R));
        MaxErr = std::max(MaxErr, std::abs(DistToSegment(S.B, A, B) - R));
    }
    std::printf("  segments=%zu  max surface error=%.3e\n", Out.size(), MaxErr);
    Check(MaxErr < 1e-9, "all capsule vertices lie on the capsule surface");

    // A degenerate capsule (both caps coincident) must fall back to a sphere.
    Shapes::SegmentList Degen;
    Shapes::AppendCapsuleBetween(Degen, A, A, R, 16, Colors::Cyan, 1.0f);
    Check(!Degen.empty(), "degenerate capsule still draws");
    for (const Segment& S : Degen)
    {
        Check(Near(Length(S.A - A), R, 1e-9), "degenerate capsule is a sphere");
    }
}

static void TestUnrealCapsule()
{
    std::printf("Capsule (Unreal convention):\n");
    Shapes::SegmentList Out;
    const Vec3 C{0, 0, 0};
    const double HalfHeight = 96.0;
    const double R = 34.0;
    Shapes::AppendCapsule(Out, C, HalfHeight, R, Rotator{0, 0, 0}, 24, Colors::Green, 1.0f);

    // With an identity rotation the axis is +Z, and the extreme vertices must
    // reach exactly HalfHeight, which is what Unreal's HalfHeight means.
    double MaxZ = -1e30, MinZ = 1e30;
    for (const Segment& S : Out)
    {
        MaxZ = std::max({MaxZ, S.A.Z, S.B.Z});
        MinZ = std::min({MinZ, S.A.Z, S.B.Z});
    }
    std::printf("  z range = [%.4f, %.4f], expected +/-%.4f\n", MinZ, MaxZ, HalfHeight);
    Check(Near(MaxZ, HalfHeight, 1e-9), "capsule top reaches +HalfHeight");
    Check(Near(MinZ, -HalfHeight, 1e-9), "capsule bottom reaches -HalfHeight");

    // Radius <= HalfHeight is the sane case; radius > HalfHeight must clamp
    // rather than invert the capsule.
    Shapes::SegmentList Fat;
    Shapes::AppendCapsule(Fat, C, 10.0, 50.0, Rotator{0, 0, 0}, 16, Colors::Green, 1.0f);
    Check(!Fat.empty(), "over-wide capsule still draws");
}

static void TestBox()
{
    std::printf("Box:\n");
    Shapes::SegmentList Out;
    const Vec3 C{10, 20, 30};
    const Vec3 E{5, 10, 15};
    Shapes::AppendBox(Out, C, E, Rotator{0, 0, 0}, Colors::Yellow, 1.0f);
    Check(Out.size() == 12, "box has 12 edges");

    double MinX=1e30,MaxX=-1e30,MinY=1e30,MaxY=-1e30,MinZ=1e30,MaxZ=-1e30;
    for (const Segment& S : Out)
    {
        for (const Vec3& P : {S.A, S.B})
        {
            MinX=std::min(MinX,P.X); MaxX=std::max(MaxX,P.X);
            MinY=std::min(MinY,P.Y); MaxY=std::max(MaxY,P.Y);
            MinZ=std::min(MinZ,P.Z); MaxZ=std::max(MaxZ,P.Z);
        }
    }
    Check(Near(MinX, C.X-E.X, 1e-12) && Near(MaxX, C.X+E.X, 1e-12), "box x extent");
    Check(Near(MinY, C.Y-E.Y, 1e-12) && Near(MaxY, C.Y+E.Y, 1e-12), "box y extent");
    Check(Near(MinZ, C.Z-E.Z, 1e-12) && Near(MaxZ, C.Z+E.Z, 1e-12), "box z extent");

    // Total edge length of an axis-aligned box is 4*(w+d+h).
    double Total = 0;
    for (const Segment& S : Out) Total += Length(S.B - S.A);
    Check(Near(Total, 4.0*(2*E.X + 2*E.Y + 2*E.Z), 1e-9), "box edge length sums correctly");

    // A rotated box must preserve total edge length.
    Shapes::SegmentList Rot;
    Shapes::AppendBox(Rot, C, E, Rotator{25, -70, 15}, Colors::Yellow, 1.0f);
    double RotTotal = 0;
    for (const Segment& S : Rot) RotTotal += Length(S.B - S.A);
    Check(Near(RotTotal, Total, 1e-9), "rotated box preserves edge length");
}

static void TestColor()
{
    std::printf("Color:\n");
    const Color C = Color::FromRGBA(0xFF8000FFu);
    Check(Near(C.R, 1.0, 1e-6), "red channel");
    Check(Near(C.G, 128.0/255.0, 1e-6), "green channel");
    Check(Near(C.B, 0.0, 1e-6), "blue channel");
    Check(Near(C.A, 1.0, 1e-6), "alpha channel");
    Check(C.ToRGBA() == 0xFF8000FFu, "round trips through packed RGBA");
    Check(Color::FromRGBA(0x00000000u).ToRGBA() == 0x00000000u, "black transparent round trips");
}

static void TestDrawList()
{
    std::printf("DrawList:\n");
    DrawList DL;
    std::vector<Segment> Segs;
    std::vector<TimedLabel> Labels;

    // Immediate mode: must survive at least one snapshot, then go away.
    DL.AddSegment(Segment{{0,0,0},{1,0,0},Colors::White,1.0f}, 0.0f);
    DL.Snapshot(Segs, Labels);
    Check(Segs.size() == 1, "immediate segment visible on the frame it was added");

    DL.Tick(0.016f);
    DL.Snapshot(Segs, Labels);
    Check(Segs.size() == 1, "immediate segment still visible after one tick");

    DL.Tick(0.016f);
    DL.Snapshot(Segs, Labels);
    Check(Segs.empty(), "immediate segment gone after two ticks");

    // Timed: a 1s shape must outlive many frames.
    DL.AddSegment(Segment{{0,0,0},{1,0,0},Colors::White,1.0f}, 1.0f);
    for (int i = 0; i < 30; ++i) DL.Tick(0.016f);
    DL.Snapshot(Segs, Labels);
    Check(Segs.size() == 1, "1s segment alive after ~0.48s");
    for (int i = 0; i < 60; ++i) DL.Tick(0.016f);
    DL.Snapshot(Segs, Labels);
    Check(Segs.empty(), "1s segment expired after ~1.44s");

    // Persistent: never expires on its own.
    DL.AddSegment(Segment{{0,0,0},{1,0,0},Colors::White,1.0f}, kPersistent);
    for (int i = 0; i < 500; ++i) DL.Tick(0.016f);
    DL.Snapshot(Segs, Labels);
    Check(Segs.size() == 1, "persistent segment survives");
    DL.ClearPersistent();
    DL.Snapshot(Segs, Labels);
    Check(Segs.empty(), "ClearPersistent removes it");

    // Categories filter the snapshot.
    DL.AddSegment(Segment{{0,0,0},{1,0,0},Colors::White,1.0f}, kPersistent, 7);
    DL.SetCategoryEnabled(7, false);
    DL.Snapshot(Segs, Labels);
    Check(Segs.empty(), "disabled category is filtered out");
    Check(!DL.IsCategoryEnabled(7), "category reports disabled");
    DL.SetCategoryEnabled(7, true);
    DL.Snapshot(Segs, Labels);
    Check(Segs.size() == 1, "re-enabled category reappears");
    DL.Clear();

    // Overflow must drop rather than grow without bound, and must count
    // the full shortfall.
    std::vector<Segment> Big(DrawList::kMaxSegments + 5000);
    DL.AddSegments(Big, kPersistent);
    const DrawListStats Stats = DL.GetStats();
    std::printf("  after overflow: live=%zu dropped=%zu\n", Stats.SegmentCount, Stats.SegmentsDroppedThisFrame);
    Check(Stats.SegmentCount == DrawList::kMaxSegments, "clamped at the cap");
    Check(Stats.SegmentsDroppedThisFrame == 5000, "counted every dropped segment");

    // Labels follow the same lifetime rules.
    DrawList DL2;
    DL2.AddLabel(Vec3{1,2,3}, "hit", Colors::Red, 0.0f);
    DL2.Snapshot(Segs, Labels);
    Check(Labels.size() == 1 && Labels[0].Text == "hit", "label visible immediately");
    DL2.Tick(0.016f); DL2.Tick(0.016f);
    DL2.Snapshot(Segs, Labels);
    Check(Labels.empty(), "immediate label expires");
}

static void TestSweeps()
{
    std::printf("Sweeps:\n");
    Shapes::SegmentList Out;
    Shapes::AppendSweptSphere(Out, Vec3{0,0,0}, Vec3{500,0,0}, 30.0, 16, Colors::Orange, 1.0f);
    Check(!Out.empty(), "swept sphere draws");

    Out.clear();
    Shapes::AppendSweptBox(Out, Vec3{0,0,0}, Vec3{0,300,0}, Vec3{10,10,10}, Rotator{0,0,0}, Colors::Orange, 1.0f);
    // 12 edges at each end plus 8 connecting rails.
    Check(Out.size() == 32, "swept box has 12+12+8 segments");

    Out.clear();
    Shapes::AppendSweptCapsule(Out, Vec3{0,0,0}, Vec3{0,0,100}, 96.0, 34.0, Rotator{0,0,0}, 16, Colors::Orange, 1.0f);
    Check(!Out.empty(), "swept capsule draws");

    // Zero-length sweeps must not emit rails or blow up.
    Out.clear();
    Shapes::AppendSweptSphere(Out, Vec3{5,5,5}, Vec3{5,5,5}, 20.0, 16, Colors::Orange, 1.0f);
    Check(!Out.empty(), "zero-length swept sphere still draws the shape");
}

static void TestArrowAndCone()
{
    std::printf("Arrow / Cone:\n");
    Shapes::SegmentList Out;
    Shapes::AppendArrow(Out, Vec3{0,0,0}, Vec3{100,0,0}, 0.0, Colors::Magenta, 1.0f);
    Check(Out.size() == 5, "arrow is shaft plus four head prongs");

    // A zero-length arrow must not produce a degenerate head.
    Out.clear();
    Shapes::AppendArrow(Out, Vec3{7,7,7}, Vec3{7,7,7}, 0.0, Colors::Magenta, 1.0f);
    Check(Out.size() == 1, "degenerate arrow is just the (empty) shaft");

    Out.clear();
    Shapes::AppendCone(Out, Vec3{0,0,0}, Vec3{0,0,1}, 200.0, 30.0, 24, Colors::Blue, 1.0f);
    Check(!Out.empty(), "cone draws");
    // The base circle radius must be Height * tan(angle).
    const double Expected = 200.0 * std::tan(DegToRad(30.0));
    double MaxR = 0;
    for (const Segment& S : Out)
    {
        MaxR = std::max(MaxR, std::sqrt(S.A.X*S.A.X + S.A.Y*S.A.Y));
        MaxR = std::max(MaxR, std::sqrt(S.B.X*S.B.X + S.B.Y*S.B.Y));
    }
    std::printf("  cone base radius=%.4f expected=%.4f\n", MaxR, Expected);
    Check(Near(MaxR, Expected, 1e-9), "cone base radius matches height*tan(angle)");
}

int main()
{
    TestBasis();
    TestSphere();
    TestCapsuleBetween();
    TestUnrealCapsule();
    TestBox();
    TestColor();
    TestSweeps();
    TestArrowAndCone();
    TestDrawList();

    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
