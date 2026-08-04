#pragma once

// World-space to screen-space projection.
//
// We do this ourselves rather than routing every vertex through
// APlayerController::ProjectWorldLocationToScreen, because a single sphere
// wireframe is ~150 vertices and each reflected call is a full ProcessEvent.
// The engine call is still available as a cross-check (see
// TVEngine's projection calibration), so the fast path can be verified against
// ground truth in-game instead of taken on faith.
//
// Conventions follow Unreal: X forward, Y right, Z up; the camera FOV reported
// by APlayerCameraManager::GetFOVAngle is the *horizontal* field of view under
// the default AspectRatio_MaintainXFOV constraint.

#include "TVMath.hpp"

namespace TraceViz
{
    enum class AspectConstraint
    {
        // Horizontal FOV is fixed; vertical FOV follows from the aspect ratio.
        // This is Unreal's default.
        MaintainXFov,
        // Vertical FOV is fixed; horizontal FOV follows. Some games override
        // to this, which is why it is switchable at runtime.
        MaintainYFov,
    };

    struct ViewInfo
    {
        Vec3 Location{};
        Rotator Rotation{};
        // Camera FOV in degrees, as reported by the engine.
        double FovDegrees{90.0};
        float ViewportWidth{1920.0f};
        float ViewportHeight{1080.0f};
        AspectConstraint Constraint{AspectConstraint::MaintainXFov};
        // Distance in front of the camera at which geometry is clipped. Kept
        // small so lines stay visible right up to the camera; this is a
        // rendering convenience, not the engine's actual near plane.
        double NearClip{1.0};
    };

    // Precomputes the camera basis and FOV scales once per frame, then projects
    // many points cheaply.
    class Projector
    {
      public:
        explicit Projector(const ViewInfo& View);

        // Projects a world point. Returns false when the point is at or behind
        // the near plane, in which case the outputs are untouched.
        bool ProjectPoint(const Vec3& WorldPosition, Vec2& OutScreen, double& OutDepth) const;

        // Projects a segment, clipping it against the near plane so segments
        // that straddle the camera still draw their visible portion. Returns
        // false when the segment is entirely behind the camera or entirely
        // outside the viewport.
        bool ProjectSegment(const Vec3& WorldA, const Vec3& WorldB, Vec2& OutA, Vec2& OutB) const;

        const ViewInfo& GetView() const
        {
            return m_view;
        }

      private:
        // Camera-space coordinates: X right, Y up, Z forward.
        Vec3 ToCameraSpace(const Vec3& WorldPosition) const;
        Vec2 CameraSpaceToScreen(const Vec3& CameraSpace) const;

        ViewInfo m_view{};
        Basis m_basis{};
        double m_tan_half_x{1.0};
        double m_tan_half_y{1.0};
    };
} // namespace TraceViz
