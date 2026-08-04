#include "TVDrawList.hpp"

#include <algorithm>

namespace TraceViz
{
    namespace
    {
        // A submission has to survive long enough to be rendered at least once,
        // and we cannot assume whether a mod submits before or after our tick
        // runs within a given frame. Holding every entry for two ticks removes
        // that ordering dependency entirely: worst case a one-frame shape
        // lingers for one extra frame, which is invisible in practice because
        // immediate-mode callers re-submit the same geometry anyway.
        constexpr uint32_t kMinimumAgeTicks = 2;

        template <typename T>
        bool ShouldEvict(const T& Entry)
        {
            if (Entry.bPersistent)
            {
                return false;
            }
            if (Entry.AgeTicks < kMinimumAgeTicks)
            {
                return false;
            }
            return Entry.RemainingSeconds <= 0.0f;
        }
    } // namespace

    void DrawList::AddSegment(const Segment& InSegment, float Duration, CategoryId Category)
    {
        std::lock_guard<std::mutex> Lock{m_mutex};
        if (m_segments.size() >= kMaxSegments)
        {
            ++m_dropped_this_frame;
            return;
        }

        TimedSegment Entry{};
        Entry.Geometry = InSegment;
        Entry.bPersistent = (Duration == kPersistent);
        Entry.RemainingSeconds = Entry.bPersistent ? 0.0f : std::max(Duration, 0.0f);
        Entry.Category = Category;
        m_segments.push_back(Entry);

        m_peak_segments = std::max(m_peak_segments, m_segments.size());
    }

    void DrawList::AddSegments(const std::vector<Segment>& InSegments, float Duration, CategoryId Category)
    {
        std::lock_guard<std::mutex> Lock{m_mutex};

        const bool bPersistent = (Duration == kPersistent);
        const float Remaining = bPersistent ? 0.0f : std::max(Duration, 0.0f);

        for (size_t i = 0; i < InSegments.size(); ++i)
        {
            if (m_segments.size() >= kMaxSegments)
            {
                // Count everything we did not get to, so the stats reflect
                // the real shortfall rather than a single dropped segment.
                m_dropped_this_frame += InSegments.size() - i;
                break;
            }

            TimedSegment Entry{};
            Entry.Geometry = InSegments[i];
            Entry.bPersistent = bPersistent;
            Entry.RemainingSeconds = Remaining;
            Entry.Category = Category;
            m_segments.push_back(Entry);
        }

        m_peak_segments = std::max(m_peak_segments, m_segments.size());
    }

    void DrawList::AddLabel(const Vec3& WorldPosition, std::string Text, const Color& TextColor, float Duration, CategoryId Category)
    {
        std::lock_guard<std::mutex> Lock{m_mutex};
        if (m_labels.size() >= kMaxLabels)
        {
            return;
        }

        TimedLabel Entry{};
        Entry.WorldPosition = WorldPosition;
        Entry.Text = std::move(Text);
        Entry.TextColor = TextColor;
        Entry.bPersistent = (Duration == kPersistent);
        Entry.RemainingSeconds = Entry.bPersistent ? 0.0f : std::max(Duration, 0.0f);
        Entry.Category = Category;
        m_labels.push_back(std::move(Entry));
    }

    void DrawList::Tick(float DeltaSeconds)
    {
        std::lock_guard<std::mutex> Lock{m_mutex};

        m_dropped_this_frame = 0;

        const float Delta = std::max(DeltaSeconds, 0.0f);

        for (TimedSegment& Entry : m_segments)
        {
            if (Entry.AgeTicks < kMinimumAgeTicks)
            {
                ++Entry.AgeTicks;
            }
            if (!Entry.bPersistent && Entry.AgeTicks >= kMinimumAgeTicks)
            {
                Entry.RemainingSeconds -= Delta;
            }
        }
        m_segments.erase(std::remove_if(m_segments.begin(),
                                        m_segments.end(),
                                        [](const TimedSegment& E) {
                                            return ShouldEvict(E);
                                        }),
                         m_segments.end());

        for (TimedLabel& Entry : m_labels)
        {
            if (Entry.AgeTicks < kMinimumAgeTicks)
            {
                ++Entry.AgeTicks;
            }
            if (!Entry.bPersistent && Entry.AgeTicks >= kMinimumAgeTicks)
            {
                Entry.RemainingSeconds -= Delta;
            }
        }
        m_labels.erase(std::remove_if(m_labels.begin(),
                                      m_labels.end(),
                                      [](const TimedLabel& E) {
                                          return ShouldEvict(E);
                                      }),
                       m_labels.end());
    }

    void DrawList::Snapshot(std::vector<Segment>& OutSegments, std::vector<TimedLabel>& OutLabels) const
    {
        std::lock_guard<std::mutex> Lock{m_mutex};

        OutSegments.clear();
        OutSegments.reserve(m_segments.size());
        for (const TimedSegment& Entry : m_segments)
        {
            if (!IsCategoryEnabledUnlocked(Entry.Category))
            {
                continue;
            }
            OutSegments.push_back(Entry.Geometry);
        }

        OutLabels.clear();
        OutLabels.reserve(m_labels.size());
        for (const TimedLabel& Entry : m_labels)
        {
            if (!IsCategoryEnabledUnlocked(Entry.Category))
            {
                continue;
            }
            OutLabels.push_back(Entry);
        }
    }

    void DrawList::Clear()
    {
        std::lock_guard<std::mutex> Lock{m_mutex};
        m_segments.clear();
        m_labels.clear();
    }

    void DrawList::ClearPersistent()
    {
        std::lock_guard<std::mutex> Lock{m_mutex};
        m_segments.erase(std::remove_if(m_segments.begin(),
                                        m_segments.end(),
                                        [](const TimedSegment& E) {
                                            return E.bPersistent;
                                        }),
                         m_segments.end());
        m_labels.erase(std::remove_if(m_labels.begin(),
                                      m_labels.end(),
                                      [](const TimedLabel& E) {
                                          return E.bPersistent;
                                      }),
                       m_labels.end());
    }

    void DrawList::SetCategoryEnabled(CategoryId Category, bool bEnabled)
    {
        std::lock_guard<std::mutex> Lock{m_mutex};
        const auto It = std::find(m_disabled_categories.begin(), m_disabled_categories.end(), Category);
        if (bEnabled)
        {
            if (It != m_disabled_categories.end())
            {
                m_disabled_categories.erase(It);
            }
        }
        else if (It == m_disabled_categories.end())
        {
            m_disabled_categories.push_back(Category);
        }
    }

    bool DrawList::IsCategoryEnabled(CategoryId Category) const
    {
        std::lock_guard<std::mutex> Lock{m_mutex};
        return IsCategoryEnabledUnlocked(Category);
    }

    bool DrawList::IsCategoryEnabledUnlocked(CategoryId Category) const
    {
        return std::find(m_disabled_categories.begin(), m_disabled_categories.end(), Category) == m_disabled_categories.end();
    }

    DrawListStats DrawList::GetStats() const
    {
        std::lock_guard<std::mutex> Lock{m_mutex};
        DrawListStats Stats{};
        Stats.SegmentCount = m_segments.size();
        Stats.LabelCount = m_labels.size();
        Stats.SegmentsDroppedThisFrame = m_dropped_this_frame;
        Stats.PeakSegmentCount = m_peak_segments;
        return Stats;
    }

    DrawList& GetDrawList()
    {
        static DrawList Instance{};
        return Instance;
    }
} // namespace TraceViz
