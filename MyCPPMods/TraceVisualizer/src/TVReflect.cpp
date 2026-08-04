#include "TVReflect.hpp"

#include <algorithm>
#include <cstring>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FField.hpp>
#include <Unreal/Property/FObjectProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UnrealFlags.hpp>

namespace TraceViz::Reflect
{
    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        // Byte counts for the float and double variants of Unreal's core math
        // structs. Everything keys off these rather than off sizeof() in our
        // own translation unit.
        constexpr int32_t kVector3f = 12;
        constexpr int32_t kVector3d = 24;
        constexpr int32_t kLinearColorSize = 16;

        // Classifies a property from its reflected class name and size. Using
        // the name rather than IsA<> keeps this working even for property
        // classes our UE4SS headers do not expose a type for.
        ParamKind ClassifyProperty(FProperty* Property)
        {
            if (!Property)
            {
                return ParamKind::Unknown;
            }

            const StringType ClassName = Property->GetClass().GetName();
            const int32_t Size = Property->GetSize();

            if (ClassName == STR("FloatProperty"))
            {
                return ParamKind::Float;
            }
            if (ClassName == STR("DoubleProperty"))
            {
                return ParamKind::Double;
            }
            if (ClassName == STR("BoolProperty"))
            {
                return ParamKind::Boolean;
            }
            if (ClassName == STR("ByteProperty") || ClassName == STR("EnumProperty"))
            {
                return (Size == 1) ? ParamKind::Byte : ParamKind::Integer;
            }
            if (ClassName == STR("IntProperty") || ClassName == STR("Int8Property") || ClassName == STR("Int16Property") ||
                ClassName == STR("Int64Property") || ClassName == STR("UInt16Property") || ClassName == STR("UInt32Property") ||
                ClassName == STR("UInt64Property"))
            {
                return ParamKind::Integer;
            }
            if (ClassName == STR("ArrayProperty"))
            {
                return ParamKind::Array;
            }
            if (ClassName == STR("ObjectProperty") || ClassName == STR("ClassProperty") || ClassName == STR("WeakObjectProperty") ||
                ClassName == STR("SoftObjectProperty") || ClassName == STR("ObjectPtrProperty") || ClassName == STR("InterfaceProperty"))
            {
                return ParamKind::Object;
            }

            if (auto* AsStruct = CastField<FStructProperty>(Property); AsStruct)
            {
                UScriptStruct* Inner = AsStruct->GetStruct();
                if (!Inner)
                {
                    return ParamKind::Struct;
                }
                const StringType StructName = Inner->GetName();
                if (StructName == STR("Vector"))
                {
                    return ParamKind::Vector;
                }
                if (StructName == STR("Rotator"))
                {
                    return ParamKind::RotatorKind;
                }
                if (StructName == STR("LinearColor"))
                {
                    return ParamKind::LinearColor;
                }
                return ParamKind::Struct;
            }

            return ParamKind::Unknown;
        }

        // Returns a view rather than a raw pointer so this stays correct
        // whatever UE4SS's TCHAR is configured to.
        StringViewType KindToString(ParamKind Kind)
        {
            switch (Kind)
            {
            case ParamKind::Float:
                return STR("float");
            case ParamKind::Double:
                return STR("double");
            case ParamKind::Integer:
                return STR("int");
            case ParamKind::Boolean:
                return STR("bool");
            case ParamKind::Byte:
                return STR("byte/enum");
            case ParamKind::Object:
                return STR("object");
            case ParamKind::Vector:
                return STR("FVector");
            case ParamKind::RotatorKind:
                return STR("FRotator");
            case ParamKind::LinearColor:
                return STR("FLinearColor");
            case ParamKind::Struct:
                return STR("struct");
            case ParamKind::Array:
                return STR("array");
            default:
                return STR("?");
            }
        }
    } // namespace

    bool WriteVectorBySize(void* Dest, int32_t SizeInBytes, const Vec3& Value)
    {
        if (SizeInBytes == kVector3d)
        {
            double Tmp[3] = {Value.X, Value.Y, Value.Z};
            std::memcpy(Dest, Tmp, sizeof(Tmp));
            return true;
        }
        if (SizeInBytes == kVector3f)
        {
            float Tmp[3] = {static_cast<float>(Value.X), static_cast<float>(Value.Y), static_cast<float>(Value.Z)};
            std::memcpy(Dest, Tmp, sizeof(Tmp));
            return true;
        }
        return false;
    }

    bool ReadVectorBySize(const void* Src, int32_t SizeInBytes, Vec3& Out)
    {
        if (SizeInBytes == kVector3d)
        {
            double Tmp[3]{};
            std::memcpy(Tmp, Src, sizeof(Tmp));
            Out = Vec3{Tmp[0], Tmp[1], Tmp[2]};
            return true;
        }
        if (SizeInBytes == kVector3f)
        {
            float Tmp[3]{};
            std::memcpy(Tmp, Src, sizeof(Tmp));
            Out = Vec3{Tmp[0], Tmp[1], Tmp[2]};
            return true;
        }
        return false;
    }

    bool WriteRotatorBySize(void* Dest, int32_t SizeInBytes, const Rotator& Value)
    {
        // FRotator's member order is Pitch, Yaw, Roll.
        if (SizeInBytes == kVector3d)
        {
            double Tmp[3] = {Value.Pitch, Value.Yaw, Value.Roll};
            std::memcpy(Dest, Tmp, sizeof(Tmp));
            return true;
        }
        if (SizeInBytes == kVector3f)
        {
            float Tmp[3] = {static_cast<float>(Value.Pitch), static_cast<float>(Value.Yaw), static_cast<float>(Value.Roll)};
            std::memcpy(Dest, Tmp, sizeof(Tmp));
            return true;
        }
        return false;
    }

    bool WriteScalarBySize(void* Dest, int32_t SizeInBytes, double Value)
    {
        if (SizeInBytes == 8)
        {
            std::memcpy(Dest, &Value, sizeof(double));
            return true;
        }
        if (SizeInBytes == 4)
        {
            const float Tmp = static_cast<float>(Value);
            std::memcpy(Dest, &Tmp, sizeof(float));
            return true;
        }
        return false;
    }

    bool ReadScalarBySize(const void* Src, int32_t SizeInBytes, double& Out)
    {
        if (SizeInBytes == 8)
        {
            double Tmp{};
            std::memcpy(&Tmp, Src, sizeof(double));
            Out = Tmp;
            return true;
        }
        if (SizeInBytes == 4)
        {
            float Tmp{};
            std::memcpy(&Tmp, Src, sizeof(float));
            Out = Tmp;
            return true;
        }
        return false;
    }

    UFunction* FindFunction(StringViewType Path)
    {
        const StringType PathCopy{Path};
        auto* Function = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, PathCopy);
        if (!Function)
        {
            Output::send<LogLevel::Warning>(STR("[TraceViz] UFunction not found: {}\n"), PathCopy);
        }
        return Function;
    }

    UObject* FindClassDefaultObject(StringViewType ClassPath)
    {
        const StringType PathCopy{ClassPath};
        auto* Class = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, PathCopy);
        if (!Class)
        {
            Output::send<LogLevel::Warning>(STR("[TraceViz] UClass not found: {}\n"), PathCopy);
            return nullptr;
        }
        UObject* Cdo = Class->GetClassDefaultObject();
        if (!Cdo)
        {
            Output::send<LogLevel::Warning>(STR("[TraceViz] Class has no default object: {}\n"), PathCopy);
        }
        return Cdo;
    }

    // ---- StructReader ------------------------------------------------------

    StructReader::StructReader(UScriptStruct* Struct, const void* Data) : m_struct(Struct), m_data(Data)
    {
    }

    FProperty* StructReader::FindField(StringViewType FieldName) const
    {
        if (!m_struct)
        {
            return nullptr;
        }
        for (FProperty* Field : m_struct->ForEachPropertyInChain())
        {
            // Compare as views so the string types line up regardless of how
            // UE4SS is configured.
            if (Field && StringViewType{Field->GetName()} == FieldName)
            {
                return Field;
            }
        }
        return nullptr;
    }

    bool StructReader::HasField(StringViewType FieldName) const
    {
        return FindField(FieldName) != nullptr;
    }

    bool StructReader::GetBool(StringViewType FieldName, bool& Out) const
    {
        FProperty* Field = FindField(FieldName);
        if (!Field || !m_data)
        {
            return false;
        }
        // Booleans inside engine structs are frequently packed bitfields, so
        // treat any non-zero byte at the field offset as true. This is correct
        // for a byte-wide bool and for the first bit of a bitfield, which is
        // what FHitResult's bBlockingHit is.
        const auto* Base = static_cast<const uint8_t*>(m_data) + Field->GetOffset_Internal();
        Out = (*Base != 0);
        return true;
    }

    bool StructReader::GetFloat(StringViewType FieldName, double& Out) const
    {
        FProperty* Field = FindField(FieldName);
        if (!Field || !m_data)
        {
            return false;
        }
        const auto* Base = static_cast<const uint8_t*>(m_data) + Field->GetOffset_Internal();
        return ReadScalarBySize(Base, Field->GetSize(), Out);
    }

    bool StructReader::GetVector(StringViewType FieldName, Vec3& Out) const
    {
        FProperty* Field = FindField(FieldName);
        if (!Field || !m_data)
        {
            return false;
        }
        const auto* Base = static_cast<const uint8_t*>(m_data) + Field->GetOffset_Internal();
        return ReadVectorBySize(Base, Field->GetSize(), Out);
    }

    bool StructReader::GetObject(StringViewType FieldName, UObject*& Out) const
    {
        FProperty* Field = FindField(FieldName);
        if (!Field || !m_data)
        {
            return false;
        }
        // Object references inside structs may be raw pointers, TObjectPtr, or
        // weak pointers. Only the pointer-sized strong forms are safe to read
        // as a plain pointer.
        if (Field->GetSize() != static_cast<int32_t>(sizeof(void*)))
        {
            return false;
        }
        const auto* Base = static_cast<const uint8_t*>(m_data) + Field->GetOffset_Internal();
        UObject* Value{};
        std::memcpy(&Value, Base, sizeof(void*));
        Out = Value;
        return true;
    }

    StringType StructReader::DescribeFields() const
    {
        StringType Result{};
        if (!m_struct)
        {
            return STR("<null struct>");
        }
        for (FProperty* Field : m_struct->ForEachPropertyInChain())
        {
            if (!Field)
            {
                continue;
            }
            if (!Result.empty())
            {
                Result += STR(", ");
            }
            Result += Field->GetName();
        }
        return Result;
    }

    // ---- FunctionCall ------------------------------------------------------

    bool FunctionCall::Bind(StringViewType FunctionPath)
    {
        return BindTo(FindFunction(FunctionPath));
    }

    bool FunctionCall::BindTo(UFunction* Function)
    {
        m_function = nullptr;
        m_params.clear();
        m_buffer.clear();

        if (!Function)
        {
            return false;
        }

        int32_t RequiredSize = 0;
        for (FProperty* Property : Function->ForEachProperty())
        {
            if (!Property || !Property->HasAnyPropertyFlags(CPF_Parm))
            {
                continue;
            }

            Param Info{};
            Info.Property = Property;
            Info.Offset = Property->GetOffset_Internal();
            Info.Size = Property->GetSize();
            Info.Kind = ClassifyProperty(Property);

            RequiredSize = std::max(RequiredSize, Info.Offset + Info.Size);
            m_params.emplace(Property->GetName(), Info);
        }

        // ProcessEvent reads exactly the parameter block, but a little slack
        // costs nothing and protects against an off-by-alignment surprise.
        m_buffer.assign(static_cast<size_t>(RequiredSize) + 64, 0);
        m_function = Function;
        return true;
    }

    const FunctionCall::Param* FunctionCall::GetParam(StringViewType ParamName) const
    {
        const auto It = m_params.find(StringType{ParamName});
        return (It == m_params.end()) ? nullptr : &It->second;
    }

    void FunctionCall::Reset()
    {
        std::fill(m_buffer.begin(), m_buffer.end(), static_cast<uint8_t>(0));
    }

    bool FunctionCall::SetFloat(const Param* P, double Value)
    {
        if (!P)
        {
            return false;
        }
        return WriteScalarBySize(DataAt(*P), P->Size, Value);
    }

    bool FunctionCall::SetInt(const Param* P, int64_t Value)
    {
        if (!P)
        {
            return false;
        }
        void* Dest = DataAt(*P);
        switch (P->Size)
        {
        case 1:
        {
            const auto V = static_cast<uint8_t>(Value);
            std::memcpy(Dest, &V, 1);
            return true;
        }
        case 2:
        {
            const auto V = static_cast<int16_t>(Value);
            std::memcpy(Dest, &V, 2);
            return true;
        }
        case 4:
        {
            const auto V = static_cast<int32_t>(Value);
            std::memcpy(Dest, &V, 4);
            return true;
        }
        case 8:
            std::memcpy(Dest, &Value, 8);
            return true;
        default:
            return false;
        }
    }

    bool FunctionCall::SetBool(const Param* P, bool Value)
    {
        if (!P || P->Size < 1)
        {
            return false;
        }
        // UHT emits function parameters as byte-wide bools, never as bitfields,
        // so a plain byte write is correct here.
        const uint8_t V = Value ? 1 : 0;
        std::memcpy(DataAt(*P), &V, 1);
        return true;
    }

    bool FunctionCall::SetByte(const Param* P, uint8_t Value)
    {
        if (!P || P->Size < 1)
        {
            return false;
        }
        std::memcpy(DataAt(*P), &Value, 1);
        return true;
    }

    bool FunctionCall::SetObject(const Param* P, UObject* Value)
    {
        if (!P || P->Size != static_cast<int32_t>(sizeof(void*)))
        {
            return false;
        }
        std::memcpy(DataAt(*P), &Value, sizeof(void*));
        return true;
    }

    bool FunctionCall::SetVector(const Param* P, const Vec3& Value)
    {
        if (!P)
        {
            return false;
        }
        return WriteVectorBySize(DataAt(*P), P->Size, Value);
    }

    bool FunctionCall::SetRotator(const Param* P, const Rotator& Value)
    {
        if (!P)
        {
            return false;
        }
        return WriteRotatorBySize(DataAt(*P), P->Size, Value);
    }

    bool FunctionCall::SetLinearColor(const Param* P, const Color& Value)
    {
        if (!P || P->Size != kLinearColorSize)
        {
            return false;
        }
        const float Tmp[4] = {Value.R, Value.G, Value.B, Value.A};
        std::memcpy(DataAt(*P), Tmp, sizeof(Tmp));
        return true;
    }

    bool FunctionCall::GetBool(StringViewType Name, bool& Out) const
    {
        const Param* P = GetParam(Name);
        if (!P || P->Size < 1)
        {
            return false;
        }
        Out = (*static_cast<const uint8_t*>(DataAt(*P)) != 0);
        return true;
    }

    bool FunctionCall::GetFloat(StringViewType Name, double& Out) const
    {
        const Param* P = GetParam(Name);
        if (!P)
        {
            return false;
        }
        return ReadScalarBySize(DataAt(*P), P->Size, Out);
    }

    bool FunctionCall::GetVector(StringViewType Name, Vec3& Out) const
    {
        const Param* P = GetParam(Name);
        if (!P)
        {
            return false;
        }
        return ReadVectorBySize(DataAt(*P), P->Size, Out);
    }

    bool FunctionCall::GetObject(StringViewType Name, UObject*& Out) const
    {
        const Param* P = GetParam(Name);
        if (!P || P->Size != static_cast<int32_t>(sizeof(void*)))
        {
            return false;
        }
        UObject* Value{};
        std::memcpy(&Value, DataAt(*P), sizeof(void*));
        Out = Value;
        return true;
    }

    StructReader FunctionCall::GetStruct(StringViewType Name) const
    {
        const Param* P = GetParam(Name);
        if (!P)
        {
            return StructReader{};
        }
        auto* AsStruct = CastField<FStructProperty>(P->Property);
        if (!AsStruct)
        {
            return StructReader{};
        }
        return StructReader{AsStruct->GetStruct(), DataAt(*P)};
    }

    void* FunctionCall::GetRaw(const Param* P)
    {
        return P ? DataAt(*P) : nullptr;
    }

    const void* FunctionCall::GetRaw(const Param* P) const
    {
        return P ? DataAt(*P) : nullptr;
    }

    void FunctionCall::Invoke(UObject* Context)
    {
        if (!m_function || !Context || m_buffer.empty())
        {
            return;
        }
        Context->ProcessEvent(m_function, m_buffer.data());
    }

    StringType FunctionCall::DescribeSignature() const
    {
        if (!m_function)
        {
            return STR("<unbound>");
        }

        // Sort by offset so the printed order matches the declaration order,
        // which makes eyeballing it against the engine header trivial.
        std::vector<const Param*> Ordered;
        Ordered.reserve(m_params.size());
        for (const auto& Entry : m_params)
        {
            Ordered.push_back(&Entry.second);
        }
        std::sort(Ordered.begin(), Ordered.end(), [](const Param* A, const Param* B) {
            return A->Offset < B->Offset;
        });

        StringType Result = m_function->GetName();
        Result += STR("(");
        bool bFirst = true;
        for (const Param* P : Ordered)
        {
            if (!bFirst)
            {
                Result += STR(", ");
            }
            bFirst = false;
            Result += P->Property->GetName();
            Result += STR(":");
            Result += KindToString(P->Kind);
        }
        Result += STR(")");
        return Result;
    }
} // namespace TraceViz::Reflect
