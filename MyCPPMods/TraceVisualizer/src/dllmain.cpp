#include "MyMod.hpp"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>

namespace GameMod {

    MyMod::MyMod() : RC::CppUserModBase() {
        ModName = "MyMod";
        ModVersion = "1.0.0";
        ModDescription = "C++ Mod targeting specific UE version";
        ModAuthors = "Developer";
    }

    auto MyMod::on_unreal_init() -> void {
        // Engine singletons (GUObjectArray, UWorld, etc.) are safe to access here.
        // Avoid running heavy GObject scans inside DLLMain or constructor phase.
    }

    auto MyMod::on_update() -> void {
        // Per-frame logic goes here
    }

} // namespace GameMod

// UE4SS Lifecycle Entrypoints
extern "C" __declspec(dllexport) RC::CppUserModBase* start_mod() {
    return new GameMod::MyMod();
}

extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod) {
    delete mod;
}