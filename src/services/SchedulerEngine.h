#pragma once
#include "../repository/JobRepository.h"
#include "../utils/Logger.h"
#include "../utils/TimeManager.h"
#include "ThreadPool.h"
#include "JobExecutor.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <cmath>
#include <algorithm>

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
                    // =============================
                    // Status → RUNNING
                    // =============================
                    repo.updateStatus(job.id, "running");

                    // Submit to thread pool — non-blocking
                    Job capturedJob = job;
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
    // Uses JobExecutor for actual command execution
    // ==========================================
    void executeJob(Job job)
    {
        // Current attempt number (0-based retryCount → 1-based attempt)
        int attemptNumber = job.retryCount + 1;

        if (job.retryCount > 0)
        {
            Logger::log(LogLevel::INFO,
                "Job [" + std::to_string(job.id) + "] retry attempt "
                + std::to_string(job.retryCount) + "/" + std::to_string(job.retryPolicy));
        }

        // Run the job command via JobExecutor
        ExecutionResult result = JobExecutor::execute(job);

        // Record execution in the log table (with attempt number)
        std::string resultStatus = result.success ? "completed" : "failed";

        repo.insertExecutionLog(
            job.id,
            resultStatus,
            attemptNumber,
            result.startTime,
            result.endTime,
            result.durationMs,
            result.exitCode,
            result.output,
            result.errorOutput
        );

        // Update the job's last run tracking
        repo.updateLastRun(job.id, result.startTime, resultStatus);

        if (result.success)
        {
            // =============================
            // Status → COMPLETED (or reschedule)
            // =============================
            onJobSuccess(job, result);
        }
        else
        {
            // =============================
            // Status → FAILED (or retry with backoff)
            // =============================
            onJobFailure(job, result);
        }
    }

    // ==========================================
    // Success handler
    // ==========================================
    void onJobSuccess(Job& job, const ExecutionResult& result)
    {
        Logger::log(LogLevel::INFO,
            "Job [" + std::to_string(job.id) + "] COMPLETED — exit: "
            + std::to_string(result.exitCode) + ", duration: "
            + std::to_string((int)result.durationMs) + "ms");

        // Reset retry count on success
        repo.resetRetryCount(job.id);

        if (job.type == "once")
        {
            // One-shot job — mark COMPLETED
            repo.updateStatus(job.id, "completed");
        }
        else
        {
            // Recurring job (interval / cron) — reschedule
            reschedule(job);
        }
    }

    // ==========================================
    // Failure handler with exponential backoff retry
    //
    // Retry delay formula:
    //   delay = baseDelay * 2^(retryCount)
    //   capped at 1 hour max
    //
    // If retryDelaySeconds == 0, default base = 10s
    // ==========================================
    void onJobFailure(Job& job, const ExecutionResult& result)
    {
        Logger::log(LogLevel::WARN,
            "Job [" + std::to_string(job.id) + "] FAILED — exit: "
            + std::to_string(result.exitCode) + ", duration: "
            + std::to_string((int)result.durationMs) + "ms"
            + (result.errorOutput.empty() ? "" : ", error: " + result.errorOutput));

        if (job.retryCount < job.retryPolicy)
        {
            // ==========================================
            // RETRY: compute backoff delay and schedule
            // ==========================================
            int baseDelay = job.retryDelaySeconds > 0 ? job.retryDelaySeconds : 10;

            // Exponential backoff: baseDelay * 2^retryCount, capped at 3600s (1 hour)
            int backoffDelay = static_cast<int>(
                baseDelay * std::pow(2.0, static_cast<double>(job.retryCount))
            );
            backoffDelay = std::min(backoffDelay, 3600);

            // Compute the retry time
            std::string retryTime = TimeManager::computeIntervalNext(
                TimeManager::now(), backoffDelay);

            // Atomic: increment retry_count + set next_run_time + set status=active
            repo.scheduleRetry(job.id, retryTime);

            Logger::log(LogLevel::INFO,
                "Job [" + std::to_string(job.id) + "] scheduled for retry "
                + std::to_string(job.retryCount + 1) + "/"
                + std::to_string(job.retryPolicy)
                + " — backoff: " + std::to_string(backoffDelay) + "s"
                + " — next attempt: " + retryTime);
        }
        else
        {
            // ==========================================
            // ALL RETRIES EXHAUSTED → mark FAILED
            // ==========================================
            repo.updateStatus(job.id, "failed");

            Logger::log(LogLevel::ERROR,
                "Job [" + std::to_string(job.id) + "] FAILED permanently — "
                + std::to_string(job.retryPolicy) + " retries exhausted");
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
            repo.updateStatus(job.id, "completed");
            Logger::log(LogLevel::WARN,
                "Job [" + std::to_string(job.id) + "] could not be rescheduled — status: COMPLETED");
            return;
        }

        // Reset retry count for the next scheduled run
        repo.resetRetryCount(job.id);
        repo.updateNextRunTime(job.id, nextTime);
        repo.updateStatus(job.id, "active");

        Logger::log(LogLevel::INFO,
            "Job [" + std::to_string(job.id) + "] rescheduled → " + nextTime);
    }

public:
    // ==========================================
    // Constructor
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
            schedulerThread.join();

        pool.shutdown();
    }

    bool isRunning() const { return running.load(); }

    // Pool stats
    int getActiveWorkers() const { return pool.getActiveWorkers(); }
    size_t getPendingTasks() const { return pool.pendingTasks(); }
    int getCompletedTasks() const { return pool.getCompletedTasks(); }
    size_t getPoolSize() const { return pool.getPoolSize(); }

    ~SchedulerEngine() { stop(); }
};
