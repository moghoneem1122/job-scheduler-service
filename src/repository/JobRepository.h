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

    // Helper to safely get text from a column (null-safe)
    static std::string safeText(sqlite3_stmt* stmt, int col)
    {
        const char* text = (const char*)sqlite3_column_text(stmt, col);
        return text ? text : "";
    }

    // Helper to parse a row from a prepared statement into a Job
    Job parseRow(sqlite3_stmt* stmt)
    {
        Job job;
        job.id               = sqlite3_column_int(stmt, 0);
        job.name             = safeText(stmt, 1);
        job.command          = safeText(stmt, 2);
        job.type             = safeText(stmt, 3);
        job.status           = safeText(stmt, 4);
        job.nextRunTime      = safeText(stmt, 5);
        job.cronExpression   = safeText(stmt, 6);
        job.intervalSeconds  = sqlite3_column_int(stmt, 7);
        job.retryPolicy      = sqlite3_column_int(stmt, 8);
        job.retryCount       = sqlite3_column_int(stmt, 9);
        job.retryDelaySeconds = sqlite3_column_int(stmt, 10);
        job.lastRunTime      = safeText(stmt, 11);
        job.lastResult       = safeText(stmt, 12);
        return job;
    }

    static constexpr const char* SELECT_COLS =
        "id, name, command, type, status, next_run_time, cron_expression, "
        "interval_seconds, retry_policy, retry_count, retry_delay_seconds, "
        "last_run_time, last_result";

public:
    JobRepository(Database& database) : db(database) {}

    // ==========================================
    // CRUD operations
    // ==========================================

    std::vector<Job> getAll()
    {
        std::lock_guard<std::mutex> lock(db.mutex());
        Logger::log(LogLevel::INFO, "Fetching all jobs from DB");

        sqlite3_stmt* stmt;
        std::string sql = std::string("SELECT ") + SELECT_COLS + " FROM jobs";
        sqlite3_prepare_v2(db.get(), sql.c_str(), -1, &stmt, nullptr);

        std::vector<Job> jobs;
        while (sqlite3_step(stmt) == SQLITE_ROW)
            jobs.push_back(parseRow(stmt));

        sqlite3_finalize(stmt);
        Logger::log(LogLevel::INFO, "Fetched " + std::to_string(jobs.size()) + " jobs");
        return jobs;
    }

    Job getById(int id)
    {
        std::lock_guard<std::mutex> lock(db.mutex());
        Logger::log(LogLevel::INFO, "Fetching job by ID: " + std::to_string(id));

        sqlite3_stmt* stmt;
        std::string sql = std::string("SELECT ") + SELECT_COLS + " FROM jobs WHERE id=?";
        sqlite3_prepare_v2(db.get(), sql.c_str(), -1, &stmt, nullptr);
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
            "INSERT INTO jobs (name, command, type, status, next_run_time, cron_expression, "
            "interval_seconds, retry_policy, retry_count, retry_delay_seconds) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            -1, &stmt, nullptr
        );

        sqlite3_bind_text(stmt, 1, job.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, job.command.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, job.type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, job.status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, job.nextRunTime.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, job.cronExpression.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 7, job.intervalSeconds);
        sqlite3_bind_int(stmt, 8, job.retryPolicy);
        sqlite3_bind_int(stmt, 9, job.retryCount);
        sqlite3_bind_int(stmt, 10, job.retryDelaySeconds);

        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        Logger::log(LogLevel::INFO, "Job created successfully");
    }

    // ==========================================
    // Delete with cascading cleanup
    // Removes the job AND its execution_log entries
    // ==========================================
    bool remove(int id)
    {
        std::lock_guard<std::mutex> lock(db.mutex());
        Logger::log(LogLevel::INFO, "Deleting job ID: " + std::to_string(id));

        // Delete execution logs first
        sqlite3_stmt* logStmt;
        sqlite3_prepare_v2(db.get(), "DELETE FROM execution_log WHERE job_id=?", -1, &logStmt, nullptr);
        sqlite3_bind_int(logStmt, 1, id);
        sqlite3_step(logStmt);
        int logsDeleted = sqlite3_changes(db.get());
        sqlite3_finalize(logStmt);

        // Delete the job
        sqlite3_stmt* jobStmt;
        sqlite3_prepare_v2(db.get(), "DELETE FROM jobs WHERE id=?", -1, &jobStmt, nullptr);
        sqlite3_bind_int(jobStmt, 1, id);
        sqlite3_step(jobStmt);
        bool deleted = sqlite3_changes(db.get()) > 0;
        sqlite3_finalize(jobStmt);

        if (deleted)
        {
            Logger::log(LogLevel::INFO,
                "Job [" + std::to_string(id) + "] deleted (+" + std::to_string(logsDeleted) + " log entries)");
        }

        return deleted;
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
    // State-aware pause: only pauses active jobs
    // Returns: 1 = paused, 0 = not found, -1 = invalid state
    // ==========================================
    int pauseJob(int id)
    {
        std::lock_guard<std::mutex> lock(db.mutex());

        // First check current status
        sqlite3_stmt* checkStmt;
        std::string sql = "SELECT status FROM jobs WHERE id=?";
        sqlite3_prepare_v2(db.get(), sql.c_str(), -1, &checkStmt, nullptr);
        sqlite3_bind_int(checkStmt, 1, id);

        std::string currentStatus;
        if (sqlite3_step(checkStmt) == SQLITE_ROW)
        {
            currentStatus = safeText(checkStmt, 0);
        }
        else
        {
            sqlite3_finalize(checkStmt);
            return 0; // not found
        }
        sqlite3_finalize(checkStmt);

        // Only active jobs can be paused
        if (currentStatus != "active")
        {
            Logger::log(LogLevel::WARN,
                "Cannot pause job [" + std::to_string(id) + "] — current status: " + currentStatus);
            return -1; // invalid state
        }

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db.get(),
            "UPDATE jobs SET status='paused' WHERE id=?",
            -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        Logger::log(LogLevel::INFO,
            "Job [" + std::to_string(id) + "] PAUSED");
        return 1;
    }

    // ==========================================
    // State-aware resume: only resumes paused jobs
    // Returns: 1 = resumed, 0 = not found, -1 = invalid state
    // ==========================================
    int resumeJob(int id)
    {
        std::lock_guard<std::mutex> lock(db.mutex());

        // First check current status
        sqlite3_stmt* checkStmt;
        std::string sql = "SELECT status FROM jobs WHERE id=?";
        sqlite3_prepare_v2(db.get(), sql.c_str(), -1, &checkStmt, nullptr);
        sqlite3_bind_int(checkStmt, 1, id);

        std::string currentStatus;
        if (sqlite3_step(checkStmt) == SQLITE_ROW)
        {
            currentStatus = safeText(checkStmt, 0);
        }
        else
        {
            sqlite3_finalize(checkStmt);
            return 0; // not found
        }
        sqlite3_finalize(checkStmt);

        // Only paused jobs can be resumed
        if (currentStatus != "paused")
        {
            Logger::log(LogLevel::WARN,
                "Cannot resume job [" + std::to_string(id) + "] — current status: " + currentStatus);
            return -1; // invalid state
        }

        // Resume: mark as active
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db.get(),
            "UPDATE jobs SET status='active' WHERE id=?",
            -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        Logger::log(LogLevel::INFO,
            "Job [" + std::to_string(id) + "] RESUMED");
        return 1;
    }

    // ==========================================
    // Execution tracking
    // ==========================================

    bool updateLastRun(int id, const std::string& lastRunTime, const std::string& lastResult)
    {
        std::lock_guard<std::mutex> lock(db.mutex());

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db.get(),
            "UPDATE jobs SET last_run_time=?, last_result=? WHERE id=?",
            -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, lastRunTime.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, lastResult.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return sqlite3_changes(db.get()) > 0;
    }

    void insertExecutionLog(int jobId, const std::string& status, int attempt,
                            const std::string& startTime, const std::string& endTime,
                            double durationMs, int exitCode,
                            const std::string& output, const std::string& errorOutput)
    {
        std::lock_guard<std::mutex> lock(db.mutex());

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db.get(),
            "INSERT INTO execution_log (job_id, status, attempt, start_time, end_time, "
            "duration_ms, exit_code, output, error_output) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
            -1, &stmt, nullptr);

        sqlite3_bind_int(stmt, 1, jobId);
        sqlite3_bind_text(stmt, 2, status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, attempt);
        sqlite3_bind_text(stmt, 4, startTime.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, endTime.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 6, durationMs);
        sqlite3_bind_int(stmt, 7, exitCode);
        sqlite3_bind_text(stmt, 8, output.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, errorOutput.c_str(), -1, SQLITE_TRANSIENT);

        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Get execution history for a specific job (most recent first)
    std::vector<crow::json::wvalue> getExecutionLog(int jobId, int limit = 20)
    {
        std::lock_guard<std::mutex> lock(db.mutex());

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db.get(),
            "SELECT id, job_id, status, attempt, start_time, end_time, "
            "duration_ms, exit_code, output, error_output "
            "FROM execution_log WHERE job_id=? ORDER BY id DESC LIMIT ?",
            -1, &stmt, nullptr);

        sqlite3_bind_int(stmt, 1, jobId);
        sqlite3_bind_int(stmt, 2, limit);

        std::vector<crow::json::wvalue> logs;

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            crow::json::wvalue entry;
            entry["id"]          = sqlite3_column_int(stmt, 0);
            entry["jobId"]       = sqlite3_column_int(stmt, 1);
            entry["status"]      = safeText(stmt, 2);
            entry["attempt"]     = sqlite3_column_int(stmt, 3);
            entry["startTime"]   = safeText(stmt, 4);
            entry["endTime"]     = safeText(stmt, 5);
            entry["durationMs"]  = sqlite3_column_double(stmt, 6);
            entry["exitCode"]    = sqlite3_column_int(stmt, 7);
            entry["output"]      = safeText(stmt, 8);
            entry["errorOutput"] = safeText(stmt, 9);

            logs.push_back(std::move(entry));
        }

        sqlite3_finalize(stmt);
        return logs;
    }

    // ==========================================
    // Retry system
    // ==========================================

    bool scheduleRetry(int id, const std::string& retryTime)
    {
        std::lock_guard<std::mutex> lock(db.mutex());

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db.get(),
            "UPDATE jobs SET retry_count = retry_count + 1, "
            "next_run_time = ?, status = 'active' WHERE id=?",
            -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, retryTime.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return sqlite3_changes(db.get()) > 0;
    }

    // ==========================================
    // Scheduler-specific methods
    // ==========================================

    std::vector<Job> getDueJobs()
    {
        std::lock_guard<std::mutex> lock(db.mutex());

        sqlite3_stmt* stmt;
        std::string sql = std::string("SELECT ") + SELECT_COLS +
            " FROM jobs WHERE status='active' AND next_run_time <= datetime('now', 'localtime')";
        sqlite3_prepare_v2(db.get(), sql.c_str(), -1, &stmt, nullptr);

        std::vector<Job> jobs;
        while (sqlite3_step(stmt) == SQLITE_ROW)
            jobs.push_back(parseRow(stmt));

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

    // ==========================================
    // Crash recovery: reset "running" → "active"
    //
    // On startup, any jobs stuck in "running" state
    // were interrupted by a crash. Reset them so the
    // scheduler can pick them up again.
    // ==========================================
    int recoverRunningJobs()
    {
        std::lock_guard<std::mutex> lock(db.mutex());

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db.get(),
            "UPDATE jobs SET status='active' WHERE status='running'",
            -1, &stmt, nullptr);
        sqlite3_step(stmt);
        int recovered = sqlite3_changes(db.get());
        sqlite3_finalize(stmt);

        return recovered;
    }
};