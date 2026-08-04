#pragma once

// Pure math for the trace visualizer.
//
// Deliberately has no UE4SS / Unreal dependency. Everything the engine hands us
// gets converted into these types at the reflection boundary, which keeps the
// geometry code independent of whether the game's FVector is float or double.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace TraceViz
{
    inline constexpr double kPi = 3.14159265358979323846;

    inline constexpr double DegToRad(double Degrees)
    {
        return Degrees * (kPi / 180.0);
    }

    struct Vec3
    {
        double X{};
        double Y{};
        double Z{};

        constexpr Vec3() = default;
        constexpr Vec3(double InX, double InY, double InZ) : X(InX), Y(InY), Z(InZ)
        {
        }

        constexpr Vec3 operator+(const Vec3& O) const
        {
            return {X + O.X, Y + O.Y, Z + O.Z};
        }
        constexpr Vec3 operator-(const Vec3& O) const
        {
            return {X - O.X, Y - O.Y, Z - O.Z};
        }
        constexpr Vec3 operator*(double S) const
        {
            return {X * S, Y * S, Z * S};
        }
        constexpr Vec3 operator-() const
        {
            return {-X, -Y, -Z};
        }

        constexpr Vec3& operator+=(const Vec3& O)
        {
            X += O.X;
            Y += O.Y;
            Z += O.Z;
            return *this;
        }
    };

    inline constexpr double Dot(const Vec3& A, const Vec3& B)
    {
        return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
    }

    inline constexpr Vec3 Cross(const Vec3& A, const Vec3& B)
    {
        return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
    }

    inline double LengthSquared(const Vec3& V)
    {
        return Dot(V, V);
    }

    inline double Length(const Vec3& V)
    {
        return std::sqrt(Dot(V, V));
    }

    // Returns a unit vector, or Fallback when V is degenerate.
    inline Vec3 Normalized(const Vec3& V, const Vec3& Fallback = Vec3{1.0, 0.0, 0.0})
    {
        const double LenSq = Dot(V, V);
        if (LenSq <= 1e-12)
        {
            return Fallback;
        }
        return V * (1.0 / std::sqrt(LenSq));
    }

    inline constexpr Vec3 Lerp(const Vec3& A, const Vec3& B, double T)
    {
        return A + (B - A) * T;
    }

    // Unreal rotation, in degrees, using Unreal's axis convention:
    // X is forward, Y is right, Z is up. Pitch rotates about Y, Yaw about Z, Roll about X.
    struct Rotator
    {
        double Pitch{};
        double Yaw{};
        double Roll{};
    };

    // An orthonormal basis. Mirrors FRotationMatrix::Make so that anything we
    // derive here lines up with what the engine computed.
    struct Basis
    {
        Vec3 Forward{1.0, 0.0, 0.0};
        Vec3 Right{0.0, 1.0, 0.0};
        Vec3 Up{0.0, 0.0, 1.0};
    };

    inline Basis MakeBasis(const Rotator& R)
    {
        const double CP = std::cos(DegToRad(R.Pitch));
        const double SP = std::sin(DegToRad(R.Pitch));
        const double CY = std::cos(DegToRad(R.Yaw));
        const double SY = std::sin(DegToRad(R.Yaw));
        const double CR = std::cos(DegToRad(R.Roll));
        const double SR = std::sin(DegToRad(R.Roll));

        Basis B{};
        B.Forward = {CP * CY, CP * SY, SP};
        B.Right = {SR * SP * CY - CR * SY, SR * SP * SY + CR * CY, -SR * CP};
        B.Up = {-(CR * SP * CY + SR * SY), CY * SR - CR * SP * SY, CR * CP};
        return B;
    }

    // Builds an arbitrary orthonormal basis whose Forward is Dir. Used for
    // capsules and cones, where only the axis is meaningful and the roll is free.
    inline Basis BasisFromDirection(const Vec3& Dir)
    {
        Basis B{};
        B.Forward = Normalized(Dir);

        // Pick whichever world axis is least parallel to Forward so the cross
        // product stays well-conditioned.
        const Vec3 Seed = (std::abs(B.Forward.Z) < 0.9) ? Vec3{0.0, 0.0, 1.0} : Vec3{1.0, 0.0, 0.0};
        B.Right = Normalized(Cross(Seed, B.Forward));
        B.Up = Cross(B.Forward, B.Right);
        return B;
    }

    struct Vec2
    {
        float X{};
        float Y{};
    };

    // Linear color, matching Unreal's FLinearColor component order and range.
    struct Color
    {
        float R{1.0f};
        float G{1.0f};
        float B{1.0f};
        float A{1.0f};

        constexpr Color() = default;
        constexpr Color(float InR, float InG, float InB, float InA = 1.0f) : R(InR), G(InG), B(InB), A(InA)
        {
        }

        // 0xRRGGBBAA, the order people actually type hex colors in.
        static constexpr Color FromRGBA(uint32_t Packed)
        {
            return Color{static_cast<float>((Packed >> 24) & 0xFF) / 255.0f,
                         static_cast<float>((Packed >> 16) & 0xFF) / 255.0f,
                         static_cast<float>((Packed >> 8) & 0xFF) / 255.0f,
                         static_cast<float>(Packed & 0xFF) / 255.0f};
        }

        constexpr uint32_t ToRGBA() const
        {
            const auto Q = [](float V) -> uint32_t {
                const float C = V < 0.0f ? 0.0f : (V > 1.0f ? 1.0f : V);
                return static_cast<uint32_t>(C * 255.0f + 0.5f);
            };
            return (Q(R) << 24) | (Q(G) << 16) | (Q(B) << 8) | Q(A);
        }

        constexpr Color WithAlpha(float NewAlpha) const
        {
            return Color{R, G, B, NewAlpha};
        }
    };

    namespace Colors
    {
        inline constexpr Color White{1.0f, 1.0f, 1.0f};
        inline constexpr Color Red{1.0f, 0.0f, 0.0f};
        inline constexpr Color Green{0.0f, 1.0f, 0.0f};
        inline constexpr Color Blue{0.0f, 0.35f, 1.0f};
        inline constexpr Color Yellow{1.0f, 0.95f, 0.1f};
        inline constexpr Color Orange{1.0f, 0.5f, 0.0f};
        inline constexpr Color Cyan{0.0f, 1.0f, 1.0f};
        inline constexpr Color Magenta{1.0f, 0.0f, 1.0f};
    } // namespace Colors

    // A 3D line segment ready to be projected and drawn. This is the single
    // primitive every shape decomposes into.
    struct Segment
    {
        Vec3 A{};
        Vec3 B{};
        Color LineColor{};
        float Thickness{1.0f};
    };
} // namespace TraceViz
