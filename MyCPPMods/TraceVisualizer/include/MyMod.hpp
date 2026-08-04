#pragma once

#include <Mod/CppUserModBase.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UnrealDef.hpp>

namespace GameMod {

    class MyMod : public RC::CppUserModBase {
    public:
        MyMod();
        ~MyMod() override = default;

        // Called when the Unreal Engine subsystem is fully initialized in memory
        auto on_unreal_init() -> void override;

        // Called once per frame tick
        auto on_update() -> void override;
    };

} // namespace GameMod