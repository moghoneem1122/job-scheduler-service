#pragma once
#include "../repository/JobRepository.h"
#include "../utils/Validator.h"
#include "../utils/Logger.h"

class JobService {
private:
    JobRepository repo;

public:
    JobService(JobRepository r) : repo(r) {}

    bool createJob(const std::string& name,
                   const std::string& type,
                   const std::string& nextRunTime,
                   int retryPolicy)
    {
        Logger::log(LogLevel::INFO, "createJob: " + name);

        if (name.empty() || name.size() > 100)
            return false;

        if (!isValidJobType(type))
            return false;

        if (retryPolicy < 0 || retryPolicy > 10)
            return false;

        Job job;
        job.name = name;
        job.type = type;
        job.status = "active";
        job.nextRunTime = nextRunTime;
        job.retryPolicy = retryPolicy;
        job.retryCount = 0;

        repo.create(job);
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