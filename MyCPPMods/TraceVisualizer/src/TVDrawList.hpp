#pragma once

// The shared store of things to draw.
//
// Threading: mods submit shapes from whatever thread they happen to be on
// (usually the game thread, inside a trace call). The renderer consumes them
// from the HUD draw callback, which is also the game thread but at a different
// point in the frame. Everything here is therefore mutex-guarded, and the
// renderer takes a snapshot rather than holding the lock while it draws.
//
// Lifetimes: a duration of <= 0 means "one frame" (immediate mode, the common
// case when you re-submit every tick). A positive duration keeps the shape
// alive for that many seconds. Persistent shapes use kPersistent and stay until
// explicitly flushed.

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "TVMath.hpp"

namespace TraceViz
{
    inline constexpr float kPersistent = -1.0f;

    // Categories let a modder turn groups of visuals on and off without
    // changing the code that submits them.
    using CategoryId = uint32_t;
    inline constexpr CategoryId kDefaultCategory = 0;

    struct TimedSegment
    {
        Segment Geometry{};
        // Seconds remaining. Immediate-mode entries are stored with
        // RemainingSeconds == 0 and dropped at the end of the frame they were
        // submitted in.
        float RemainingSeconds{0.0f};
        bool bPersistent{false};
        CategoryId Category{kDefaultCategory};
        // Number of ticks this entry has survived. See TVDrawList.cpp for why
        // eviction waits on this rather than on duration alone.
        uint32_t AgeTicks{0};
    };

    // A text label anchored to a world position. Used for hit distances,
    // impact normals, and whatever a modder wants to annotate.
    struct TimedLabel
    {
        Vec3 WorldPosition{};
        std::string Text{};
        Color TextColor{};
        float RemainingSeconds{0.0f};
        bool bPersistent{false};
        CategoryId Category{kDefaultCategory};
        uint32_t AgeTicks{0};
    };

    struct DrawListStats
    {
        size_t SegmentCount{};
        size_t LabelCount{};
        size_t SegmentsDroppedThisFrame{};
        size_t PeakSegmentCount{};
    };

    class DrawList
    {
      public:
        // Hard ceiling on live segments. Past this, new submissions are dropped
        // and counted, rather than letting a runaway loop stall the game.
        static constexpr size_t kMaxSegments = 200000;
        static constexpr size_t kMaxLabels = 4096;

        void AddSegment(const Segment& InSegment, float Duration, CategoryId Category = kDefaultCategory);
        void AddSegments(const std::vector<Segment>& InSegments, float Duration, CategoryId Category = kDefaultCategory);
        void AddLabel(const Vec3& WorldPosition, std::string Text, const Color& TextColor, float Duration, CategoryId Category = kDefaultCategory);

        // Advances lifetimes and evicts expired entries. Call once per game
        // tick, before rendering.
        void Tick(float DeltaSeconds);

        // Copies live geometry out for the renderer. Entries whose category is
        // disabled are filtered out here so the renderer stays simple.
        void Snapshot(std::vector<Segment>& OutSegments, std::vector<TimedLabel>& OutLabels) const;

        // Removes everything, or just the persistent entries.
        void Clear();
        void ClearPersistent();

        void SetCategoryEnabled(CategoryId Category, bool bEnabled);
        bool IsCategoryEnabled(CategoryId Category) const;

        DrawListStats GetStats() const;

      private:
        // Immediate-mode entries are submitted during frame N and must survive
        // until the renderer has seen them. Tick() runs before the HUD draw, so
        // we age entries by frame index rather than dropping them the instant
        // their duration hits zero.
        mutable std::mutex m_mutex;
        std::vector<TimedSegment> m_segments;
        std::vector<TimedLabel> m_labels;
        std::vector<CategoryId> m_disabled_categories;
        size_t m_dropped_this_frame{};
        size_t m_peak_segments{};

        bool IsCategoryEnabledUnlocked(CategoryId Category) const;
    };

    // Process-wide instance. The mod owns its lifetime; everything else just
    // talks to it.
    DrawList& GetDrawList();
} // namespace TraceViz
