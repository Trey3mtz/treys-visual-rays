#pragma once

// Reflection-driven UFunction calls.
//
// Why this exists: the obvious way to call an engine function from a mod is to
// declare a matching `struct Params { ... }` and hand it to ProcessEvent. That
// only works if your struct layout exactly matches the game's, which depends on
// the UE version, on whether FVector is float or double, on packing, and on
// which engine modules were compiled in. Get it wrong and you corrupt memory in
// a way that surfaces as a crash somewhere else entirely.
//
// Instead, everything here is driven by the reflection data the game itself
// carries: parameter offsets come from FProperty::GetOffset_Internal(), sizes
// from FProperty::GetSize(), and struct fields are looked up by name. Nothing
// is assumed about layout, so this works unchanged across engine versions and
// reports a mismatch loudly instead of corrupting memory.

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <String/StringType.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UScriptStruct.hpp>

#include "TVMath.hpp"

namespace TraceViz::Reflect
{
    using RC::Unreal::FProperty;
    using RC::Unreal::UFunction;
    using RC::Unreal::UObject;
    using RC::Unreal::UScriptStruct;

    // Looks up a UFunction by full object path, e.g. "/Script/Engine.HUD:DrawLine".
    UFunction* FindFunction(RC::StringViewType Path);

    // Looks up a class default object, which is what static Blueprint function
    // libraries (UKismetSystemLibrary and friends) need as a calling context.
    UObject* FindClassDefaultObject(RC::StringViewType ClassPath);

    // The parameter kinds we know how to marshal. Resolved once at bind time so
    // the per-call path never inspects type information again.
    enum class ParamKind : uint8_t
    {
        Unknown,
        Float,        // FFloatProperty, 4 bytes
        Double,       // FDoubleProperty, 8 bytes
        Integer,      // any signed/unsigned integer property
        Boolean,      // FBoolProperty (byte-wide, as UHT emits for parameters)
        Byte,         // FByteProperty / FEnumProperty with a 1-byte underlying type
        Object,       // FObjectProperty and friends: a pointer
        Vector,       // FStructProperty over Vector (float or double variant)
        RotatorKind,  // FStructProperty over Rotator
        LinearColor,  // FStructProperty over LinearColor
        Struct,       // any other struct; read via StructReader
        Array,        // TArray; left zeroed, which is a valid empty array
    };

    // Reads named fields out of an arbitrary UStruct instance. Used for
    // FHitResult, whose layout differs across engine versions and which we
    // therefore never describe in C++.
    class StructReader
    {
      public:
        StructReader() = default;
        StructReader(UScriptStruct* Struct, const void* Data);

        bool IsValid() const
        {
            return m_struct != nullptr && m_data != nullptr;
        }

        // Each getter returns false when the field is absent or has an
        // unexpected size, leaving the output untouched.
        bool GetBool(RC::StringViewType FieldName, bool& Out) const;
        bool GetFloat(RC::StringViewType FieldName, double& Out) const;
        bool GetVector(RC::StringViewType FieldName, Vec3& Out) const;
        bool GetObject(RC::StringViewType FieldName, UObject*& Out) const;
        bool HasField(RC::StringViewType FieldName) const;

        // Lists every field name, for diagnosing an unexpected struct layout
        // from the log rather than by guesswork.
        RC::StringType DescribeFields() const;

      private:
        FProperty* FindField(RC::StringViewType FieldName) const;

        UScriptStruct* m_struct{};
        const void* m_data{};
    };

    // Binds to a UFunction and builds its parameter buffer from reflection
    // data. Reuse one instance across frames: Bind() is the expensive part and
    // only needs doing once.
    class FunctionCall
    {
      public:
        // Everything needed to write one parameter, resolved at bind time.
        struct Param
        {
            FProperty* Property{};
            int32_t Offset{};
            int32_t Size{};
            ParamKind Kind{ParamKind::Unknown};
        };

        FunctionCall() = default;

        bool Bind(RC::StringViewType FunctionPath);
        bool BindTo(UFunction* Function);

        bool IsValid() const
        {
            return m_function != nullptr;
        }

        UFunction* GetFunction() const
        {
            return m_function;
        }

        // Resolves a parameter once so hot loops can skip the name lookup
        // entirely. The returned pointer stays valid for the life of the bind.
        const Param* GetParam(RC::StringViewType ParamName) const;

        bool HasParam(RC::StringViewType ParamName) const
        {
            return GetParam(ParamName) != nullptr;
        }

        // Zeroes the parameter buffer. An all-zero buffer is a valid default
        // for every kind we support, including empty TArrays, so callers only
        // set the parameters they actually care about.
        void Reset();

        // ---- Writers. Each returns false if the parameter is missing or its
        // reflected size does not match, so a signature change in a future game
        // patch surfaces as a logged failure rather than a crash.

        bool SetFloat(const Param* P, double Value);
        bool SetInt(const Param* P, int64_t Value);
        bool SetBool(const Param* P, bool Value);
        bool SetByte(const Param* P, uint8_t Value);
        bool SetObject(const Param* P, UObject* Value);
        bool SetVector(const Param* P, const Vec3& Value);
        bool SetRotator(const Param* P, const Rotator& Value);
        bool SetLinearColor(const Param* P, const Color& Value);

        // Name-based convenience wrappers, for setup and cold paths.
        bool SetFloat(RC::StringViewType Name, double Value)
        {
            return SetFloat(GetParam(Name), Value);
        }
        bool SetInt(RC::StringViewType Name, int64_t Value)
        {
            return SetInt(GetParam(Name), Value);
        }
        bool SetBool(RC::StringViewType Name, bool Value)
        {
            return SetBool(GetParam(Name), Value);
        }
        bool SetByte(RC::StringViewType Name, uint8_t Value)
        {
            return SetByte(GetParam(Name), Value);
        }
        bool SetObject(RC::StringViewType Name, UObject* Value)
        {
            return SetObject(GetParam(Name), Value);
        }
        bool SetVector(RC::StringViewType Name, const Vec3& Value)
        {
            return SetVector(GetParam(Name), Value);
        }
        bool SetRotator(RC::StringViewType Name, const Rotator& Value)
        {
            return SetRotator(GetParam(Name), Value);
        }
        bool SetLinearColor(RC::StringViewType Name, const Color& Value)
        {
            return SetLinearColor(GetParam(Name), Value);
        }

        // ---- Readers, valid after Invoke(). ProcessEvent copies out
        // parameters back into our buffer, so this is where trace results and
        // return values come from.

        bool GetBool(RC::StringViewType Name, bool& Out) const;
        bool GetFloat(RC::StringViewType Name, double& Out) const;
        bool GetVector(RC::StringViewType Name, Vec3& Out) const;
        bool GetObject(RC::StringViewType Name, UObject*& Out) const;
        StructReader GetStruct(RC::StringViewType Name) const;

        // Raw pointer to a parameter's bytes, for the handful of types that
        // need bespoke handling (FVector2D, for instance, which has no
        // dedicated accessor here). Returns nullptr for an unknown parameter.
        void* GetRaw(const Param* P);
        const void* GetRaw(const Param* P) const;

        // Calls the function on Context. Context must be an instance of, or
        // derived from, the function's owning class; for static library
        // functions the class default object works.
        void Invoke(UObject* Context);

        // Human-readable dump of the bound signature, printed at startup so a
        // mismatch after a game patch is visible in the log directly.
        RC::StringType DescribeSignature() const;

      private:
        void* DataAt(const Param& P)
        {
            return m_buffer.data() + P.Offset;
        }
        const void* DataAt(const Param& P) const
        {
            return m_buffer.data() + P.Offset;
        }

        UFunction* m_function{};
        std::unordered_map<RC::StringType, Param> m_params;
        std::vector<uint8_t> m_buffer;
    };

    // ---- Size-driven typed access -----------------------------------------
    //
    // Unreal switched FVector/FRotator from float to double in UE5, and a mod
    // binary cannot know at compile time which the target game uses. These pick
    // based on the reflected size, so one build is correct either way.

    bool WriteVectorBySize(void* Dest, int32_t SizeInBytes, const Vec3& Value);
    bool ReadVectorBySize(const void* Src, int32_t SizeInBytes, Vec3& Out);
    bool WriteRotatorBySize(void* Dest, int32_t SizeInBytes, const Rotator& Value);
    bool WriteScalarBySize(void* Dest, int32_t SizeInBytes, double Value);
    bool ReadScalarBySize(const void* Src, int32_t SizeInBytes, double& Out);
} // namespace TraceViz::Reflect
