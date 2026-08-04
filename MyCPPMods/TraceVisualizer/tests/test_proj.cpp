#include "TVProject.hpp"

#include <cstdio>
#include <cmath>

using namespace TraceViz;

static int g_failures = 0;
static void Check(bool Cond, const char* What)
{
    if (!Cond) { std::printf("  FAIL: %s\n", What); ++g_failures; }
}
static bool Near(double A, double B, double Eps = 1e-6) { return std::abs(A - B) <= Eps; }

static ViewInfo MakeView()
{
    ViewInfo V;
    V.Location = Vec3{0, 0, 0};
    V.Rotation = Rotator{0, 0, 0};   // looking down +X
    V.FovDegrees = 90.0;
    V.ViewportWidth = 1920.0f;
    V.ViewportHeight = 1080.0f;
    V.Constraint = AspectConstraint::MaintainXFov;
    V.NearClip = 1.0;
    return V;
}

int main()
{
    const ViewInfo V = MakeView();
    const Projector P{V};

    std::printf("Center / axes:\n");
    {
        Vec2 S; double D;
        Check(P.ProjectPoint(Vec3{1000, 0, 0}, S, D), "point ahead projects");
        std::printf("  straight ahead -> (%.2f, %.2f) depth %.1f\n", S.X, S.Y, D);
        Check(Near(S.X, 960.0) && Near(S.Y, 540.0), "point ahead lands at screen centre");
        Check(Near(D, 1000.0), "depth is forward distance");
    }

    std::printf("FOV edges:\n");
    {
        // With 90 deg horizontal FOV, a point at 45 deg right sits on the right edge.
        Vec2 S; double D;
        Check(P.ProjectPoint(Vec3{1000, 1000, 0}, S, D), "45deg right projects");
        std::printf("  45deg right -> x=%.3f (expect 1920)\n", S.X);
        Check(Near(S.X, 1920.0, 1e-6), "45deg right is the right screen edge");
        Check(Near(S.Y, 540.0), "45deg right stays vertically centred");

        Check(P.ProjectPoint(Vec3{1000, -1000, 0}, S, D), "45deg left projects");
        Check(Near(S.X, 0.0, 1e-6), "45deg left is the left screen edge");

        // Vertical FOV derives from aspect: tan(halfY) = tan(45) * 1080/1920.
        const double TanHalfY = 1.0 * (1080.0 / 1920.0);
        Check(P.ProjectPoint(Vec3{1000, 0, 1000 * TanHalfY}, S, D), "top edge projects");
        std::printf("  vertical edge -> y=%.3f (expect 0)\n", S.Y);
        Check(Near(S.Y, 0.0, 1e-6), "derived vertical FOV puts the point on the top edge");
        Check(Near(S.X, 960.0), "top edge stays horizontally centred");
    }

    std::printf("Up is up:\n");
    {
        Vec2 S; double D;
        P.ProjectPoint(Vec3{1000, 0, 100}, S, D);
        Check(S.Y < 540.0, "world +Z maps to smaller screen Y (upwards)");
        P.ProjectPoint(Vec3{1000, 100, 0}, S, D);
        Check(S.X > 960.0, "world +Y maps to larger screen X (rightwards)");
    }

    std::printf("Near-plane rejection and clipping:\n");
    {
        Vec2 S; double D;
        Check(!P.ProjectPoint(Vec3{-500, 0, 0}, S, D), "point behind camera is rejected");

        Vec2 A, B;
        Check(!P.ProjectSegment(Vec3{-500, 0, 0}, Vec3{-100, 0, 0}, A, B), "fully-behind segment rejected");

        // A segment straddling the camera must clip, not vanish.
        Check(P.ProjectSegment(Vec3{-100, 0, 0}, Vec3{1000, 0, 0}, A, B), "straddling segment survives");
        std::printf("  straddling -> A=(%.1f,%.1f) B=(%.1f,%.1f)\n", A.X, A.Y, B.X, B.Y);
        Check(Near(B.X, 960.0) && Near(B.Y, 540.0), "far end still at centre");
        // The clipped end sits on the axis too, so it also lands at the centre.
        Check(Near(A.X, 960.0) && Near(A.Y, 540.0), "clipped end projects on the near plane");

        // Clip from the other direction as well.
        Check(P.ProjectSegment(Vec3{1000, 0, 0}, Vec3{-100, 0, 0}, A, B), "reversed straddling segment survives");
        Check(Near(A.X, 960.0), "reversed: far end at centre");
    }

    std::printf("Clip position is geometrically right:\n");
    {
        // Segment from behind the camera to a point up and ahead. The clipped
        // endpoint must land exactly where the segment crosses z == NearClip.
        Vec2 A, B;
        const Vec3 S0{-1.0, 0.0, 0.0};
        const Vec3 S1{3.0, 0.0, 4.0};
        Check(P.ProjectSegment(S0, S1, A, B), "diagonal straddling segment survives");
        // Crossing at x==1 means t = (1 - (-1)) / (3 - (-1)) = 0.5, so z = 2.
        // Screen y for (1,0,2): ndcY = (2/1)/TanHalfY, TanHalfY = 0.5625.
        const double TanHalfY = 1.0 * (1080.0 / 1920.0);
        const double NdcY = (2.0 / 1.0) / TanHalfY;
        const double ExpectedY = (0.5 - NdcY * 0.5) * 1080.0;
        std::printf("  clipped A.y=%.3f expected=%.3f\n", A.Y, ExpectedY);
        Check(Near(A.Y, ExpectedY, 1e-6), "clipped endpoint is exactly on the near plane");
    }

    std::printf("Rotation:\n");
    {
        ViewInfo R = MakeView();
        R.Rotation = Rotator{0, 90, 0};   // look down +Y
        const Projector PR{R};
        Vec2 S; double D;
        Check(PR.ProjectPoint(Vec3{0, 1000, 0}, S, D), "point along view dir projects");
        Check(Near(S.X, 960.0) && Near(S.Y, 540.0), "yawed camera centres its own forward axis");
        Check(!PR.ProjectPoint(Vec3{0, -1000, 0}, S, D), "behind the yawed camera is rejected");

        ViewInfo Pitched = MakeView();
        Pitched.Rotation = Rotator{-90, 0, 0};  // look straight down
        const Projector PP{Pitched};
        Check(PP.ProjectPoint(Vec3{0, 0, -1000}, S, D), "downward camera sees below it");
        Check(Near(S.X, 960.0) && Near(S.Y, 540.0), "downward camera centres straight down");
    }

    std::printf("Aspect constraint:\n");
    {
        ViewInfo Y = MakeView();
        Y.Constraint = AspectConstraint::MaintainYFov;
        const Projector PY{Y};
        Vec2 S; double D;
        // Under MaintainYFov the 90deg FOV is vertical, so 45deg up is the top edge.
        Check(PY.ProjectPoint(Vec3{1000, 0, 1000}, S, D), "45deg up projects");
        std::printf("  MaintainYFov 45deg up -> y=%.3f (expect 0)\n", S.Y);
        Check(Near(S.Y, 0.0, 1e-6), "MaintainYFov puts 45deg up on the top edge");
    }

    std::printf("Degenerate inputs:\n");
    {
        ViewInfo Bad = MakeView();
        Bad.FovDegrees = 0.0;           // uninitialised camera manager
        const Projector PB{Bad};
        Vec2 S; double D;
        Check(PB.ProjectPoint(Vec3{1000, 0, 0}, S, D), "zero FOV falls back instead of dividing by zero");
        Check(std::isfinite(S.X) && std::isfinite(S.Y), "fallback produces finite coordinates");

        ViewInfo Zero = MakeView();
        Zero.ViewportWidth = 0.0f;
        Zero.ViewportHeight = 0.0f;
        const Projector PZ{Zero};
        Check(std::isfinite(PZ.GetView().FovDegrees), "zero viewport does not produce NaN");
    }

    std::printf("Offscreen culling:\n");
    {
        Vec2 A, B;
        // Both endpoints far to the left, in front of the camera.
        Check(!P.ProjectSegment(Vec3{10, -10000, 0}, Vec3{20, -20000, 0}, A, B), "segment far off one side is culled");
        // Spanning the screen must not be culled.
        Check(P.ProjectSegment(Vec3{1000, -5000, 0}, Vec3{1000, 5000, 0}, A, B), "segment spanning the screen is kept");
    }

    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
