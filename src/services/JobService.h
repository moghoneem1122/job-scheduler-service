#pragma once
#include "../repository/JobRepository.h"
#include "../utils/Validator.h"
#include "../utils/Logger.h"
#include "../utils/TimeManager.h"
#include <string>

// ============================================================================
// Pause/Resume/Delete result codes for richer API responses
// ============================================================================

struct ServiceResult {
    bool ok = false;
    std::string message;

    static ServiceResult success(const std::string& msg) { return {true, msg}; }
    static ServiceResult fail(const std::string& msg) { return {false, msg}; }
};

class JobService {
private:
    JobRepository repo;

public:
    JobService(JobRepository r) : repo(r) {}

    int createJob(const std::string& name,
                   const std::string& command,
                   const std::string& type,
                   const std::string& nextRunTime,
                   const std::string& cronExpression,
                   int intervalSeconds,
                   int retryPolicy,
                   int retryDelaySeconds = 10)
    {
        Logger::log(LogLevel::INFO, "createJob: " + name);

        if (name.empty() || name.size() > 100)
            return 0;

        if (!isValidJobType(type))
            return 0;

        if (retryPolicy < 0 || retryPolicy > 10)
            return 0;

        if (retryDelaySeconds < 0 || retryDelaySeconds > 3600)
        {
            Logger::log(LogLevel::WARN, "retryDelaySeconds must be 0-3600");
            return 0;
        }

        // ============================
        // Compute nextRunTime per type
        // ============================
        std::string computedNextRun;

        if (type == "once")
        {
            // One-time: use the user-provided time, validate it's in the future
            computedNextRun = TimeManager::computeOnceNext(nextRunTime);
            if (computedNextRun.empty())
            {
                Logger::log(LogLevel::WARN, "Once-job scheduled in the past — rejected");
                return 0;
            }
        }
        else if (type == "interval")
        {
            // Interval: must have intervalSeconds > 0
            if (intervalSeconds <= 0)
            {
                Logger::log(LogLevel::WARN, "Interval job missing intervalSeconds");
                return 0;
            }

            if (nextRunTime.empty())
            {
                // No start time given — schedule from now + interval
                computedNextRun = TimeManager::computeIntervalNext(
                    TimeManager::now(), intervalSeconds);
            }
            else
            {
                // User provided a start time — validate & use it
                computedNextRun = TimeManager::computeOnceNext(nextRunTime);
                if (computedNextRun.empty())
                {
                    // Start time is in the past — compute next future occurrence
                    computedNextRun = TimeManager::computeIntervalNext(
                        nextRunTime, intervalSeconds);
                }
            }
        }
        else if (type == "cron")
        {
            // Cron: must have a valid cron expression
            if (cronExpression.empty())
            {
                Logger::log(LogLevel::WARN, "Cron job missing cronExpression");
                return 0;
            }

            // Validate cron syntax using the dedicated parser
            CronValidationResult cronValidation = isValidCronExpression(cronExpression);
            if (!cronValidation.valid)
            {
                Logger::log(LogLevel::WARN,
                    "Invalid cron expression: " + cronValidation.errorMessage);
                return 0;
            }

            // Expression is valid — compute next execution time
            computedNextRun = TimeManager::computeCronNext(cronExpression);
            if (computedNextRun.empty())
            {
                Logger::log(LogLevel::WARN,
                    "Cron expression has no future match within search window: " + cronExpression);
                return 0;
            }

            Logger::log(LogLevel::INFO,
                "Cron schedule: " + CronParser::describe(cronExpression));
        }

        Job job;
        job.name = name;
        job.command = command;
        job.type = type;
        job.status = "active";
        job.nextRunTime = computedNextRun;
        job.cronExpression = cronExpression;
        job.intervalSeconds = intervalSeconds;
        job.retryPolicy = retryPolicy;
        job.retryCount = 0;
        job.retryDelaySeconds = retryDelaySeconds;

        int id = repo.create(job);

        Logger::log(LogLevel::INFO, "Job \"" + name + "\" created — nextRunTime: " + computedNextRun);
        return id;
    }

    std::vector<Job> getJobs()
    {
        return repo.getAll();
    }

    Job getJob(int id)
    {
        return repo.getById(id);
    }

    // ==========================================
    // DELETE — state-aware deletion
    //
    // Running jobs are allowed to be deleted;
    // the scheduler will detect the missing job
    // on its next status update and handle it.
    // Cascading: removes execution logs too.
    // ==========================================
    ServiceResult deleteJob(int id)
    {
        Job job = repo.getById(id);
        if (job.id == 0)
            return ServiceResult::fail("Job not found");

        if (job.status == "running")
        {
            Logger::log(LogLevel::WARN,
                "Deleting running job [" + std::to_string(id) + "] — "
                "current execution will complete but results will be discarded");
        }

        bool deleted = repo.remove(id);
        if (!deleted)
            return ServiceResult::fail("Failed to delete job");

        return ServiceResult::success("Job deleted (status was: " + job.status + ")");
    }

    // ==========================================
    // PAUSE — state-aware
    //
    // Valid transitions: active → paused
    // Invalid: running, completed, failed, already paused
    // ==========================================
    ServiceResult pauseJob(int id)
    {
        Job job = repo.getById(id);
        if (job.id == 0)
            return ServiceResult::fail("Job not found");

        int result = repo.pauseJob(id);

        if (result == 0)
            return ServiceResult::fail("Job not found");
        if (result == -1)
        {
            // Provide specific error messages per state
            if (job.status == "paused")
                return ServiceResult::fail("Job is already paused");
            if (job.status == "running")
                return ServiceResult::fail("Cannot pause a running job — wait for it to complete");
            if (job.status == "completed")
                return ServiceResult::fail("Cannot pause a completed job");
            if (job.status == "failed")
                return ServiceResult::fail("Cannot pause a failed job");
            return ServiceResult::fail("Cannot pause job in '" + job.status + "' state");
        }

        return ServiceResult::success("Job paused");
    }

    // ==========================================
    // RESUME — state-aware with time recalculation
    //
    // Valid transitions: paused → active
    // On resume, recalculates nextRunTime if the
    // scheduled time has passed while paused.
    // ==========================================
    ServiceResult resumeJob(int id)
    {
        Job job = repo.getById(id);
        if (job.id == 0)
            return ServiceResult::fail("Job not found");

        int result = repo.resumeJob(id);

        if (result == 0)
            return ServiceResult::fail("Job not found");
        if (result == -1)
        {
            if (job.status == "active")
                return ServiceResult::fail("Job is already active");
            if (job.status == "running")
                return ServiceResult::fail("Job is currently running");
            if (job.status == "completed")
                return ServiceResult::fail("Cannot resume a completed job — create a new one");
            if (job.status == "failed")
                return ServiceResult::fail("Cannot resume a failed job — reset retries first");
            return ServiceResult::fail("Cannot resume job in '" + job.status + "' state");
        }

        // ==========================================
        // Smart resume: if nextRunTime is in the past,
        // recalculate it so the job doesn't fire immediately
        // for every missed interval during the pause window.
        // ==========================================
        std::string recalculated = recalculateNextRunTime(job);
        if (!recalculated.empty() && recalculated != job.nextRunTime)
        {
            repo.updateNextRunTime(id, recalculated);
            Logger::log(LogLevel::INFO,
                "Job [" + std::to_string(id) + "] next run recalculated on resume → " + recalculated);
        }

        // Reset retry count on resume (fresh start)
        repo.resetRetryCount(id);

        return ServiceResult::success("Job resumed (next run: "
            + (recalculated.empty() ? job.nextRunTime : recalculated) + ")");
    }

    // ==========================================
    // RESET FAILED — re-activate a failed job
    //
    // Resets retry count and recalculates nextRunTime
    // ==========================================
    ServiceResult resetFailedJob(int id)
    {
        Job job = repo.getById(id);
        if (job.id == 0)
            return ServiceResult::fail("Job not found");

        if (job.status != "failed")
            return ServiceResult::fail("Job is not in 'failed' state (current: " + job.status + ")");

        // Reset retry count
        repo.resetRetryCount(id);

        // Recalculate next run time
        std::string nextTime = recalculateNextRunTime(job);
        if (nextTime.empty())
            return ServiceResult::fail("Cannot compute next run time for this job");

        repo.updateNextRunTime(id, nextTime);
        repo.updateStatus(id, "active");

        Logger::log(LogLevel::INFO,
            "Job [" + std::to_string(id) + "] reset from FAILED → active, next run: " + nextTime);

        return ServiceResult::success("Job reset and reactivated (next run: " + nextTime + ")");
    }

    std::vector<crow::json::wvalue> getExecutionLog(int jobId, int limit = 20)
    {
        return repo.getExecutionLog(jobId, limit);
    }

    // ==========================================
    // Crash recovery — delegate to repository
    // ==========================================
    int recoverRunningJobs()
    {
        return repo.recoverRunningJobs();
    }

private:
    // ==========================================
    // Recalculate nextRunTime for a job whose
    // scheduled time may have passed.
    // ==========================================
    std::string recalculateNextRunTime(const Job& job)
    {
        // Check if nextRunTime is still in the future
        std::string futureCheck = TimeManager::computeOnceNext(job.nextRunTime);
        if (!futureCheck.empty())
        {
            // Still in the future — no recalculation needed
            return job.nextRunTime;
        }

        // nextRunTime is in the past — recalculate based on type
        if (job.type == "once")
        {
            // One-time jobs can't be recalculated — schedule immediately
            return TimeManager::now();
        }
        else if (job.type == "interval")
        {
            // Leap forward to next future interval
            return TimeManager::computeIntervalNext(job.nextRunTime, job.intervalSeconds);
        }
        else if (job.type == "cron")
        {
            // Find next cron match from now
            return TimeManager::computeCronNext(job.cronExpression);
        }

        return "";
    }
};