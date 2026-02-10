#pragma once
#include <chrono>
#include <future>
#include <vector>

template <typename T>
class TimedTaskSet {
private:
    struct TaskEntry {
        double timeMs;
        std::future<T> task;
    };

    std::vector<TaskEntry> tasks;

public:
    void Add(double timeMs, std::future<T> task) {
        tasks.push_back(TaskEntry{ timeMs, std::move(task) });
    }

    std::vector<std::pair<double, T>> GetCompletedTasks() {
        std::vector<std::pair<double, T>> results;

        for (auto it = tasks.begin(); it != tasks.end(); /* increment inside */) {
            if (!it->task.valid()) {
                it = tasks.erase(it);
            }
            else if (it->task.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                results.push_back({ it->timeMs, it->task.get() });
                it = tasks.erase(it);
            }
            else {
                ++it;
            }
        }

        return results;
    }

    std::optional<double> GetClosestTimeMs(double timeMs) const {
        if (tasks.empty()) {
            return std::nullopt;
        }

        double closestTimeMs = tasks[0].timeMs;
        double minDiffMs = std::abs(closestTimeMs - timeMs);
        for (size_t i = 1; i < tasks.size(); ++i) {
            double diff = std::abs(tasks[i].timeMs - timeMs);
            if (diff < minDiffMs) {
                minDiffMs = diff;
                closestTimeMs = tasks[i].timeMs;
            }
        }

        return closestTimeMs;
    }

    void WaitAllAndClear() {
        for (auto& entry : tasks) {
            if (entry.task.valid()) {
                entry.task.wait();
            }
        }
        tasks.clear();
    }

    ~TimedTaskSet() {
        WaitAllAndClear();
    }
};