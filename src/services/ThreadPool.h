#pragma once
#include "../utils/Logger.h"
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <string>

// ============================================================================
// ThreadPool — Fixed-size pool of worker threads with a shared task queue
//
// Usage:
//   ThreadPool pool(4);          // 4 worker threads
//   pool.submit([](){ ... });    // enqueue a task
//   pool.shutdown();             // finish pending tasks and stop
//
// The pool processes tasks in FIFO order. Each worker thread blocks on the
// condition variable until a task is available or shutdown is signaled.
// ============================================================================

class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> taskQueue;

    std::mutex queueMutex;
    std::condition_variable condition;

    std::atomic<bool> stopped{false};
    std::atomic<bool> shutdownRequested{false};
    std::atomic<int> activeWorkers{0};
    std::atomic<int> totalTasksCompleted{0};

    size_t poolSize;

    // ==========================================
    // Worker thread loop
    // ==========================================
    void workerLoop(int workerId)
    {
        Logger::log(LogLevel::INFO,
            "ThreadPool worker #" + std::to_string(workerId) + " started");

        while (true)
        {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(queueMutex);

                // Wait until there's a task or we're shutting down
                condition.wait(lock, [this]() {
                    return !taskQueue.empty() || shutdownRequested.load();
                });

                // If shutdown requested and no tasks left, exit
                if (shutdownRequested.load() && taskQueue.empty())
                {
                    break;
                }

                // Grab the next task
                task = std::move(taskQueue.front());
                taskQueue.pop();
            }

            // Execute the task outside the lock
            activeWorkers.fetch_add(1);

            try
            {
                task();
            }
            catch (const std::exception& e)
            {
                Logger::log(LogLevel::ERROR,
                    "ThreadPool worker #" + std::to_string(workerId)
                    + " caught exception: " + std::string(e.what()));
            }
            catch (...)
            {
                Logger::log(LogLevel::ERROR,
                    "ThreadPool worker #" + std::to_string(workerId)
                    + " caught unknown exception");
            }

            activeWorkers.fetch_sub(1);
            totalTasksCompleted.fetch_add(1);
        }

        Logger::log(LogLevel::INFO,
            "ThreadPool worker #" + std::to_string(workerId) + " stopped");
    }

public:
    // ==========================================
    // Constructor — spawns numThreads worker threads
    // ==========================================
    explicit ThreadPool(size_t numThreads = 4)
        : poolSize(numThreads)
    {
        Logger::log(LogLevel::INFO,
            "ThreadPool starting with " + std::to_string(numThreads) + " worker(s)");

        workers.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i)
        {
            workers.emplace_back(&ThreadPool::workerLoop, this, static_cast<int>(i));
        }
    }

    // ==========================================
    // Submit a task to the queue
    // Returns false if the pool is shutting down
    // ==========================================
    bool submit(std::function<void()> task)
    {
        if (shutdownRequested.load())
        {
            Logger::log(LogLevel::WARN,
                "ThreadPool: task rejected — pool is shutting down");
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            taskQueue.push(std::move(task));
        }

        condition.notify_one();
        return true;
    }

    // ==========================================
    // Graceful shutdown — finish all pending tasks, then stop
    // ==========================================
    void shutdown()
    {
        if (stopped.load())
            return;

        Logger::log(LogLevel::INFO, "ThreadPool: graceful shutdown requested");

        shutdownRequested.store(true);
        condition.notify_all();

        for (auto& worker : workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }

        stopped.store(true);

        Logger::log(LogLevel::INFO,
            "ThreadPool: shutdown complete. Total tasks executed: "
            + std::to_string(totalTasksCompleted.load()));
    }

    // ==========================================
    // Stats / queries
    // ==========================================
    size_t pendingTasks() const
    {
        // Note: not perfectly thread-safe for reading, but
        // good enough for logging/monitoring purposes
        return taskQueue.size();
    }

    int getActiveWorkers() const
    {
        return activeWorkers.load();
    }

    int getCompletedTasks() const
    {
        return totalTasksCompleted.load();
    }

    size_t getPoolSize() const
    {
        return poolSize;
    }

    bool isStopped() const
    {
        return stopped.load();
    }

    // ==========================================
    // Destructor — ensures shutdown
    // ==========================================
    ~ThreadPool()
    {
        shutdown();
    }
};
