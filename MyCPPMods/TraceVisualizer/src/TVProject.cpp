#include "TVProject.hpp"

namespace TraceViz
{
    namespace
    {
        // Generous margin so that a segment with one endpoint just off screen
        // still draws its visible part. Only segments comfortably outside on
        // the same side get rejected.
        constexpr float kOffscreenMargin = 4096.0f;

        bool BothOutsideSameSide(const Vec2& A, const Vec2& B, float Width, float Height)
        {
            if (A.X < -kOffscreenMargin && B.X < -kOffscreenMargin) return true;
            if (A.Y < -kOffscreenMargin && B.Y < -kOffscreenMargin) return true;
            if (A.X > Width + kOffscreenMargin && B.X > Width + kOffscreenMargin) return true;
            if (A.Y > Height + kOffscreenMargin && B.Y > Height + kOffscreenMargin) return true;
            return false;
        }
    } // namespace

    Projector::Projector(const ViewInfo& View) : m_view(View)
    {
        m_basis = MakeBasis(View.Rotation);

        // Guard against a nonsensical FOV; a camera manager that has not been
        // initialised yet can report 0.
        double Fov = View.FovDegrees;
        if (!(Fov > 1.0) || Fov >= 179.0)
        {
            Fov = 90.0;
        }

        const double Width = (View.ViewportWidth > 1.0f) ? static_cast<double>(View.ViewportWidth) : 1920.0;
        const double Height = (View.ViewportHeight > 1.0f) ? static_cast<double>(View.ViewportHeight) : 1080.0;
        const double TanHalf = std::tan(DegToRad(Fov * 0.5));

        if (View.Constraint == AspectConstraint::MaintainXFov)
        {
            m_tan_half_x = TanHalf;
            m_tan_half_y = TanHalf * (Height / Width);
        }
        else
        {
            m_tan_half_y = TanHalf;
            m_tan_half_x = TanHalf * (Width / Height);
        }
    }

    Vec3 Projector::ToCameraSpace(const Vec3& WorldPosition) const
    {
        const Vec3 Delta = WorldPosition - m_view.Location;
        return Vec3{Dot(Delta, m_basis.Right), Dot(Delta, m_basis.Up), Dot(Delta, m_basis.Forward)};
    }

    Vec2 Projector::CameraSpaceToScreen(const Vec3& CameraSpace) const
    {
        // Normalised device coordinates in [-1, 1].
        const double NdcX = (CameraSpace.X / CameraSpace.Z) / m_tan_half_x;
        const double NdcY = (CameraSpace.Y / CameraSpace.Z) / m_tan_half_y;

        // Screen origin is top-left, so Y is flipped.
        Vec2 Screen{};
        Screen.X = static_cast<float>((NdcX * 0.5 + 0.5) * static_cast<double>(m_view.ViewportWidth));
        Screen.Y = static_cast<float>((0.5 - NdcY * 0.5) * static_cast<double>(m_view.ViewportHeight));
        return Screen;
    }

    bool Projector::ProjectPoint(const Vec3& WorldPosition, Vec2& OutScreen, double& OutDepth) const
    {
        const Vec3 Cam = ToCameraSpace(WorldPosition);
        if (Cam.Z < m_view.NearClip)
        {
            return false;
        }
        OutScreen = CameraSpaceToScreen(Cam);
        OutDepth = Cam.Z;
        return true;
    }

    bool Projector::ProjectSegment(const Vec3& WorldA, const Vec3& WorldB, Vec2& OutA, Vec2& OutB) const
    {
        Vec3 CamA = ToCameraSpace(WorldA);
        Vec3 CamB = ToCameraSpace(WorldB);

        const bool bAVisible = CamA.Z >= m_view.NearClip;
        const bool bBVisible = CamB.Z >= m_view.NearClip;

        if (!bAVisible && !bBVisible)
        {
            return false;
        }

        // One endpoint is behind the near plane: slide it forward along the
        // segment to the plane so the visible portion still draws. Without this
        // a ray cast from the player's own camera would vanish the moment its
        // origin passed behind the near plane.
        if (!bAVisible || !bBVisible)
        {
            const double DeltaZ = CamB.Z - CamA.Z;
            if (std::abs(DeltaZ) <= 1e-12)
            {
                return false;
            }
            const double T = (m_view.NearClip - CamA.Z) / DeltaZ;
            const Vec3 Clipped = Lerp(CamA, CamB, T);
            if (!bAVisible)
            {
                CamA = Clipped;
            }
            else
            {
                CamB = Clipped;
            }
        }

        OutA = CameraSpaceToScreen(CamA);
        OutB = CameraSpaceToScreen(CamB);

        if (BothOutsideSameSide(OutA, OutB, m_view.ViewportWidth, m_view.ViewportHeight))
        {
            return false;
        }
        return true;
    }
} // namespace TraceViz
