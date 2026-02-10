#pragma once
#include <map>
#include <memory>
#include <optional>
#include <typeindex>


struct ITimeSeries {
    virtual ~ITimeSeries() = default;
};

template <typename T>
struct TimeSeries : public ITimeSeries {
    std::map<double, T> map;
};

// What to do if overlap found when adding a new event
enum OverlapAction {
    // Skip the requested insertion if an overlap was found.
    SKIP,
    // Remove any items in the overlap window, then insert the new item.
    REPLACE,
    // Replace but only if we're at an earlier time
    REPLACE_IF_EARLIER,
};

// Configuration for detecting and handling overlaps in AddEvent
struct OverlapOptions {
    double overlapRadiusMs = 100;
    bool onlyLookForEqual = false;
    OverlapAction overlapAction = SKIP;
};

// A collection of map<double, T> for tracking various "game event" objects alongside their timestamp.
class GameDataTracker {
private:
    mutable std::map<std::type_index, std::unique_ptr<ITimeSeries>> timeSeriesMap;

    template <typename T>
    std::map<double, T>& GetMap() const {
        auto typeIdx = std::type_index(typeid(T));

        auto [it, inserted] = timeSeriesMap.try_emplace(typeIdx, nullptr);
        if (inserted) {
            it->second = std::make_unique<TimeSeries<T>>();
        }

        return static_cast<TimeSeries<T>*>(it->second.get())->map;
    }

public:
    template <typename T>
    void AddEvent(double timeMs, const T& data, OverlapOptions options = {}) {
        auto& map = GetMap<T>();
        auto range = GetRangeAroundInclusive<T>(timeMs, options.overlapRadiusMs);

        for (auto it = range.begin(); it != range.end(); /* increment handled below */) {
            if (!options.onlyLookForEqual || it->second == data) {
                if (options.overlapAction == SKIP) {
                    return;
                }
                else if (options.overlapAction == REPLACE) {
                    it = map.erase(it);
                    continue;
                }
                else if (options.overlapAction == REPLACE_IF_EARLIER) {
                    // HACK: we're assuming there is at most one overlapping event which is not necessarily true
                    // but currently true for our usage of this config...
                    if (timeMs < it->first) {
                        it = map.erase(it);
                        continue;
                    }
                    else {
                        return;
                    }
                }
                // nothing for ADD
            }

            ++it;
        }

        // Can only have one entry per key, so jitter slightly to add if necessary
        while (map.contains(timeMs)) {
            timeMs += 0.000001;
        }

        map.emplace(timeMs, data);
    }

    template <typename T>
    std::ranges::subrange<typename std::map<double, T>::iterator> GetAll() const {
        auto& map = GetMap<T>();
        return std::ranges::subrange(map.begin(), map.end());
    }

    template <typename T>
    std::ranges::subrange<typename std::map<double, T>::iterator> GetRangeInclusive(double minTimeMs, double maxTimeMs) const {
        auto& map = GetMap<T>();
        return std::ranges::subrange(map.lower_bound(minTimeMs), map.upper_bound(maxTimeMs));
    }

    template <typename T>
    std::ranges::subrange<typename std::map<double, T>::iterator> GetRangeAroundInclusive(double timeMs, double radiusMs) const {
        return GetRangeInclusive<T>(timeMs - radiusMs, timeMs + radiusMs);
    }

    template <typename T>
    std::optional<double> GetMostRecentTimeMs(double timeMs) const {
        auto& map = GetMap<T>();

        auto it = map.upper_bound(timeMs);
        if (it == map.begin()) {
            return std::nullopt;
        }
        else {
            return std::prev(it)->first;
        }
    }

    template <typename T>
    std::optional<std::pair<double, T>> GetMostRecent(double timeMs) const {
        auto& map = GetMap<T>();

        auto it = map.upper_bound(timeMs);
        if (it == map.begin()) {
            return std::nullopt;
        }
        else {
            auto prev = std::prev(it);
            return std::make_pair(prev->first, prev->second);
        }
    }

    template <typename T>
    std::optional<std::pair<double, T>> GetClosest(double timeMs) const {
        const auto& map = GetMap<T>();
        if (map.empty()) {
            return std::nullopt;
        }

        auto it_next = map.lower_bound(timeMs);

        // If next is the beginning, then it is closest
        if (it_next == map.begin()) {
            return std::make_pair(it_next->first, it_next->second);
        }

        // If next is the end, then the last element is the closest
        if (it_next == map.end()) {
            auto it_last = std::prev(it_next);
            return std::make_pair(it_last->first, it_last->second);
        }

        // Otherwise, we're between two elements (or equals somewhere)
        auto it_prev = std::prev(it_next);
        double dist_prev = timeMs - it_prev->first;
        double dist_next = it_next->first - timeMs;
        return dist_prev <= dist_next
            ? std::make_pair(it_prev->first, it_prev->second)
            : std::make_pair(it_next->first, it_next->second);
    }

    void Clear() {
        timeSeriesMap.clear();
    }
};