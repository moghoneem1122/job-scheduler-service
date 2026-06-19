#pragma once
#include "../repository/JobRepository.h"
#include "../utils/Validator.h"
#include "../utils/Logger.h"
#include "../utils/TimeManager.h"

class JobService {
private:
    JobRepository repo;

public:
    JobService(JobRepository r) : repo(r) {}

    bool createJob(const std::string& name,
                   const std::string& type,
                   const std::string& nextRunTime,
                   const std::string& cronExpression,
                   int intervalSeconds,
                   int retryPolicy)
    {
        Logger::log(LogLevel::INFO, "createJob: " + name);

        if (name.empty() || name.size() > 100)
            return false;

        if (!isValidJobType(type))
            return false;

        if (retryPolicy < 0 || retryPolicy > 10)
            return false;

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
                return false;
            }
        }
        else if (type == "interval")
        {
            // Interval: must have intervalSeconds > 0
            if (intervalSeconds <= 0)
            {
                Logger::log(LogLevel::WARN, "Interval job missing intervalSeconds");
                return false;
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
                return false;
            }

            // Validate cron syntax using the dedicated parser
            CronValidationResult cronValidation = isValidCronExpression(cronExpression);
            if (!cronValidation.valid)
            {
                Logger::log(LogLevel::WARN,
                    "Invalid cron expression: " + cronValidation.errorMessage);
                return false;
            }

            // Expression is valid — compute next execution time
            computedNextRun = TimeManager::computeCronNext(cronExpression);
            if (computedNextRun.empty())
            {
                Logger::log(LogLevel::WARN,
                    "Cron expression has no future match within search window: " + cronExpression);
                return false;
            }

            Logger::log(LogLevel::INFO,
                "Cron schedule: " + CronParser::describe(cronExpression));
        }

        Job job;
        job.name = name;
        job.type = type;
        job.status = "active";
        job.nextRunTime = computedNextRun;
        job.cronExpression = cronExpression;
        job.intervalSeconds = intervalSeconds;
        job.retryPolicy = retryPolicy;
        job.retryCount = 0;

        repo.create(job);

        Logger::log(LogLevel::INFO, "Job \"" + name + "\" created — nextRunTime: " + computedNextRun);
        return true;
    }

    std::vector<Job> getJobs()
    {
        return repo.getAll();
    }

    Job getJob(int id)
    {
        return repo.getById(id);
    }

    bool deleteJob(int id)
    {
        return repo.remove(id);
    }

    bool pauseJob(int id)
    {
        return repo.updateStatus(id, "paused");
    }

    bool resumeJob(int id)
    {
        return repo.updateStatus(id, "active");
    }
};