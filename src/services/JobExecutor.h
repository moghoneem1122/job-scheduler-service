#pragma once
#include "../models/Job.h"
#include "../utils/Logger.h"
#include "../utils/TimeManager.h"
#include <string>
#include <array>
#include <memory>
#include <chrono>
#include <cstdio>
#include <sstream>

// ============================================================================
// ExecutionResult — captures the outcome of a job execution
// ============================================================================

struct ExecutionResult {
    bool success = false;
    int exitCode = -1;
    std::string output;
    std::string errorOutput;
    double durationMs = 0.0;
    std::string startTime;
    std::string endTime;
};

// ============================================================================
// JobExecutor — Executes job commands and captures results
//
// Supports execution of shell commands specified in job.command.
// Captures stdout, exit code, and execution timing.
// Status transitions:  active → RUNNING → COMPLETED / FAILED
// ============================================================================

class JobExecutor {
public:

    // ==========================================
    // Execute a job's command
    //
    // Runs job.command as a shell command via popen(),
    // captures stdout, exit code, and timing.
    // Returns a full ExecutionResult.
    // ==========================================
    static ExecutionResult execute(const Job& job)
    {
        ExecutionResult result;
        result.startTime = TimeManager::now();

        Logger::log(LogLevel::INFO,
            "JobExecutor [" + std::to_string(job.id) + "] RUNNING \"" + job.name
            + "\" — command: " + job.command);

        if (job.command.empty())
        {
            // No command specified — simulate successful execution
            Logger::log(LogLevel::INFO,
                "JobExecutor [" + std::to_string(job.id) + "] no command specified, simulating success");

            result.success = true;
            result.exitCode = 0;
            result.output = "No command specified — simulated execution";
            result.endTime = TimeManager::now();
            result.durationMs = 0.0;
            return result;
        }

        auto startClock = std::chrono::steady_clock::now();

        try
        {
            // Redirect stderr to stdout so we capture everything
            std::string fullCommand = job.command + " 2>&1";

            // Open process
            FILE* pipe = popen(fullCommand.c_str(), "r");
            if (!pipe)
            {
                result.success = false;
                result.exitCode = -1;
                result.errorOutput = "Failed to open process pipe";
                result.endTime = TimeManager::now();

                Logger::log(LogLevel::ERROR,
                    "JobExecutor [" + std::to_string(job.id) + "] FAILED — could not open pipe");
                return result;
            }

            // Read output
            std::ostringstream outputStream;
            std::array<char, 4096> buffer;

            while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
            {
                outputStream << buffer.data();
            }

            // Get exit code
            int rawStatus = pclose(pipe);
            result.exitCode = WEXITSTATUS(rawStatus);
            result.output = outputStream.str();

            // Trim trailing newline from output
            while (!result.output.empty() && result.output.back() == '\n')
            {
                result.output.pop_back();
            }

            // Determine success based on exit code
            result.success = (result.exitCode == 0);
        }
        catch (const std::exception& e)
        {
            result.success = false;
            result.exitCode = -1;
            result.errorOutput = "Exception during execution: " + std::string(e.what());

            Logger::log(LogLevel::ERROR,
                "JobExecutor [" + std::to_string(job.id) + "] exception: " + e.what());
        }

        auto endClock = std::chrono::steady_clock::now();
        result.durationMs = std::chrono::duration<double, std::milli>(endClock - startClock).count();
        result.endTime = TimeManager::now();

        // Log result
        if (result.success)
        {
            Logger::log(LogLevel::INFO,
                "JobExecutor [" + std::to_string(job.id) + "] COMPLETED (exit: 0, "
                + std::to_string((int)result.durationMs) + "ms)");
        }
        else
        {
            Logger::log(LogLevel::WARN,
                "JobExecutor [" + std::to_string(job.id) + "] FAILED (exit: "
                + std::to_string(result.exitCode) + ", "
                + std::to_string((int)result.durationMs) + "ms)");
        }

        return result;
    }
};
