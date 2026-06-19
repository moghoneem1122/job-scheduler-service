#pragma once
#include "../repository/JobRepository.h"
#include "../utils/Logger.h"
#include "../utils/TimeManager.h"
#include "ThreadPool.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <functional>

class SchedulerEngine {
private:
    JobRepository& repo;
    ThreadPool pool;
    std::thread schedulerThread;
    std::atomic<bool> running{false};
    int pollIntervalSeconds;

    // ==========================================
    // Main scheduler loop (non-blocking)
    //
    // This loop ONLY detects due jobs and submits
    // them to the thread pool. It never blocks on
    // job execution, so it can continuously monitor
    // the database at the configured poll interval.
    // ==========================================
    void run()
    {
        Logger::log(LogLevel::INFO, "Scheduler engine started (polling every "
            + std::to_string(pollIntervalSeconds) + "s, pool size: "
            + std::to_string(pool.getPoolSize()) + ")");

        while (running.load())
        {
            try
            {
                auto dueJobs = repo.getDueJobs();

                if (!dueJobs.empty())
                {
                    Logger::log(LogLevel::INFO,
                        "Detected " + std::to_string(dueJobs.size()) + " due job(s)"
                        + " [active workers: " + std::to_string(pool.getActiveWorkers())
                        + ", pending: " + std::to_string(pool.pendingTasks()) + "]");
                }

                for (auto& job : dueJobs)
                {
                    // Mark as running immediately so it won't be picked up again
                    repo.updateStatus(job.id, "running");

                    // Submit to thread pool — non-blocking
                    Job capturedJob = job; // copy for the lambda
                    pool.submit([this, capturedJob]() mutable {
                        executeJob(capturedJob);
                    });
                }
            }
            catch (const std::exception& e)
            {
                Logger::log(LogLevel::ERROR,
                    "Scheduler error: " + std::string(e.what()));
            }

            // Sleep in small increments so we can respond to stop() quickly
            for (int i = 0; i < pollIntervalSeconds * 10 && running.load(); ++i)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        Logger::log(LogLevel::INFO, "Scheduler engine stopped");
    }

    // ==========================================
    // Job execution — runs on a pool worker thread
    // ==========================================
    void executeJob(Job job)
    {
        Logger::log(LogLevel::INFO,
            "Executing job [" + std::to_string(job.id) + "] \"" + job.name
            + "\" (type: " + job.type + ") on worker thread");

        // Simulate execution — replace with real job logic
        // (HTTP call, script execution, function invocation, etc.)
        bool success = true;

        if (success)
        {
            onJobSuccess(job);
        }
        else
        {
            onJobFailure(job);
        }
    }

    // ==========================================
    // Success handler
    // ==========================================
    void onJobSuccess(Job& job)
    {
        Logger::log(LogLevel::INFO,
            "Job [" + std::to_string(job.id) + "] completed successfully");

        // Reset retry count on success
        repo.resetRetryCount(job.id);

        if (job.type == "once")
        {
            // One-shot job — mark completed
            repo.updateStatus(job.id, "completed");
        }
        else
        {
            // Recurring job (interval / cron) — reschedule
            reschedule(job);
        }
    }

    // ==========================================
    // Failure handler with retry logic
    // ==========================================
    void onJobFailure(Job& job)
    {
        Logger::log(LogLevel::WARN,
            "Job [" + std::to_string(job.id) + "] execution failed");

        if (job.retryCount < job.retryPolicy)
        {
            repo.incrementRetryCount(job.id);
            repo.updateStatus(job.id, "active");

            Logger::log(LogLevel::INFO,
                "Job [" + std::to_string(job.id) + "] queued for retry ("
                + std::to_string(job.retryCount + 1) + "/"
                + std::to_string(job.retryPolicy) + ")");
        }
        else
        {
            repo.updateStatus(job.id, "failed");

            Logger::log(LogLevel::ERROR,
                "Job [" + std::to_string(job.id) + "] exhausted all retries — marked as failed");
        }
    }

    // ==========================================
    // Reschedule a recurring job using TimeManager
    // ==========================================
    void reschedule(Job& job)
    {
        std::string nextTime;

        if (job.type == "interval")
        {
            nextTime = TimeManager::computeIntervalNext(
                job.nextRunTime, job.intervalSeconds);

            Logger::log(LogLevel::INFO,
                "Job [" + std::to_string(job.id) + "] interval reschedule (+"
                + std::to_string(job.intervalSeconds) + "s)");
        }
        else if (job.type == "cron")
        {
            nextTime = TimeManager::computeCronNext(
                job.cronExpression, job.nextRunTime);

            Logger::log(LogLevel::INFO,
                "Job [" + std::to_string(job.id) + "] cron reschedule (expr: "
                + job.cronExpression + ")");
        }

        if (nextTime.empty())
        {
            // No valid next run time — mark as completed
            repo.updateStatus(job.id, "completed");
            Logger::log(LogLevel::WARN,
                "Job [" + std::to_string(job.id) + "] could not be rescheduled — marked completed");
            return;
        }

        repo.updateNextRunTime(job.id, nextTime);
        repo.updateStatus(job.id, "active");

        Logger::log(LogLevel::INFO,
            "Job [" + std::to_string(job.id) + "] rescheduled → " + nextTime);
    }

public:
    // ==========================================
    // Constructor
    // pollInterval:  how often (seconds) to check for due jobs
    // numWorkers:    size of the worker thread pool
    // ==========================================
    SchedulerEngine(JobRepository& repository,
                    int pollInterval = 5,
                    size_t numWorkers = 4)
        : repo(repository),
          pool(numWorkers),
          pollIntervalSeconds(pollInterval) {}

    void start()
    {
        if (running.load())
        {
            Logger::log(LogLevel::WARN, "Scheduler is already running");
            return;
        }

        running.store(true);
        schedulerThread = std::thread(&SchedulerEngine::run, this);
    }

    void stop()
    {
        running.store(false);

        if (schedulerThread.joinable())
        {
            schedulerThread.join();
        }

        // Gracefully shut down the thread pool — finishes pending jobs
        pool.shutdown();
    }

    bool isRunning() const
    {
        return running.load();
    }

    // ==========================================
    // Pool stats — exposed for the API
    // ==========================================
    int getActiveWorkers() const { return pool.getActiveWorkers(); }
    size_t getPendingTasks() const { return pool.pendingTasks(); }
    int getCompletedTasks() const { return pool.getCompletedTasks(); }
    size_t getPoolSize() const { return pool.getPoolSize(); }

    ~SchedulerEngine()
    {
        stop();
    }
};
