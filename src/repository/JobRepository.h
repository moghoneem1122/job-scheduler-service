#pragma once
#include "../models/Job.h"
#include "../db/Database.h"
#include "../utils/Logger.h"
#include "../utils/Validator.h"
#include <vector>
#include <mutex>

class JobRepository {
private:
    Database& db;

    // Helper to parse a row from a prepared statement into a Job
    Job parseRow(sqlite3_stmt* stmt)
    {
        Job job;
        job.id = sqlite3_column_int(stmt, 0);
        job.name = (const char*)sqlite3_column_text(stmt, 1);
        job.type = (const char*)sqlite3_column_text(stmt, 2);
        job.status = (const char*)sqlite3_column_text(stmt, 3);
        job.nextRunTime = (const char*)sqlite3_column_text(stmt, 4);

        const char* cronText = (const char*)sqlite3_column_text(stmt, 5);
        job.cronExpression = cronText ? cronText : "";

        job.intervalSeconds = sqlite3_column_int(stmt, 6);
        job.retryPolicy = sqlite3_column_int(stmt, 7);
        job.retryCount = sqlite3_column_int(stmt, 8);
        return job;
    }

public:
    JobRepository(Database& database) : db(database) {}

    std::vector<Job> getAll()
    {
        std::lock_guard<std::mutex> lock(db.mutex());
        Logger::log(LogLevel::INFO, "Fetching all jobs from DB");

        sqlite3_stmt* stmt;

        sqlite3_prepare_v2(
            db.get(),
            "SELECT id, name, type, status, next_run_time, cron_expression, "
            "interval_seconds, retry_policy, retry_count FROM jobs",
            -1,
            &stmt,
            nullptr
        );

        std::vector<Job> jobs;

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            jobs.push_back(parseRow(stmt));
        }

        sqlite3_finalize(stmt);

        Logger::log(LogLevel::INFO, "Fetched " + std::to_string(jobs.size()) + " jobs");

        return jobs;
    }

    Job getById(int id)
    {
        std::lock_guard<std::mutex> lock(db.mutex());
        Logger::log(LogLevel::INFO, "Fetching job by ID: " + std::to_string(id));

        sqlite3_stmt* stmt;

        sqlite3_prepare_v2(
            db.get(),
            "SELECT id, name, type, status, next_run_time, cron_expression, "
            "interval_seconds, retry_policy, retry_count FROM jobs WHERE id=?",
            -1,
            &stmt,
            nullptr
        );

        sqlite3_bind_int(stmt, 1, id);

        Job job;

        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            job = parseRow(stmt);
            Logger::log(LogLevel::INFO, "Job found: " + job.name);
        }
        else
        {
            Logger::log(LogLevel::WARN, "Job not found with ID: " + std::to_string(id));
        }

        sqlite3_finalize(stmt);
        return job;
    }

    void create(const Job& job)
    {
        std::lock_guard<std::mutex> lock(db.mutex());
        Logger::log(LogLevel::INFO, "Creating job: " + job.name);

        sqlite3_stmt* stmt;

        sqlite3_prepare_v2(
            db.get(),
            "INSERT INTO jobs (name, type, status, next_run_time, cron_expression, "
            "interval_seconds, retry_policy, retry_count) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            -1,
            &stmt,
            nullptr
        );

        sqlite3_bind_text(stmt, 1, job.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, job.type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, job.status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, job.nextRunTime.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, job.cronExpression.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 6, job.intervalSeconds);
        sqlite3_bind_int(stmt, 7, job.retryPolicy);
        sqlite3_bind_int(stmt, 8, job.retryCount);

        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        Logger::log(LogLevel::INFO, "Job created successfully");
    }

    bool remove(int id)
    {
        std::lock_guard<std::mutex> lock(db.mutex());
        Logger::log(LogLevel::INFO, "Deleting job ID: " + std::to_string(id));

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db.get(), "DELETE FROM jobs WHERE id=?", -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return sqlite3_changes(db.get()) > 0;
    }

    bool updateStatus(int id, const std::string& status)
    {
        std::lock_guard<std::mutex> lock(db.mutex());

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db.get(), "UPDATE jobs SET status=? WHERE id=?", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return sqlite3_changes(db.get()) > 0;
    }

    // ==========================================
    // Scheduler-specific methods
    // ==========================================

    // Fetch all active jobs whose next_run_time is at or before the current time
    std::vector<Job> getDueJobs()
    {
        std::lock_guard<std::mutex> lock(db.mutex());

        sqlite3_stmt* stmt;

        sqlite3_prepare_v2(
            db.get(),
            "SELECT id, name, type, status, next_run_time, cron_expression, "
            "interval_seconds, retry_policy, retry_count "
            "FROM jobs WHERE status='active' AND next_run_time <= datetime('now', 'localtime')",
            -1,
            &stmt,
            nullptr
        );

        std::vector<Job> jobs;

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            jobs.push_back(parseRow(stmt));
        }

        sqlite3_finalize(stmt);
        return jobs;
    }

    bool updateNextRunTime(int id, const std::string& nextRunTime)
    {
        std::lock_guard<std::mutex> lock(db.mutex());

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db.get(), "UPDATE jobs SET next_run_time=? WHERE id=?", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, nextRunTime.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return sqlite3_changes(db.get()) > 0;
    }

    bool incrementRetryCount(int id)
    {
        std::lock_guard<std::mutex> lock(db.mutex());

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db.get(), "UPDATE jobs SET retry_count = retry_count + 1 WHERE id=?", -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return sqlite3_changes(db.get()) > 0;
    }

    bool resetRetryCount(int id)
    {
        std::lock_guard<std::mutex> lock(db.mutex());

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db.get(), "UPDATE jobs SET retry_count = 0 WHERE id=?", -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return sqlite3_changes(db.get()) > 0;
    }
};