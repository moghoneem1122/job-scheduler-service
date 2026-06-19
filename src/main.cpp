#include "crow.h"
#include "services/JobService.h"
#include "services/SchedulerEngine.h"
#include "utils/Response.h"
#include "db/Database.h"
#include "repository/JobRepository.h"


Database db("jobs.db");
JobRepository repo(db);
JobService service(repo);
int main()
{
    crow::SimpleApp app;

    // =========================
    // Initialize the DB tables
    // =========================
    db.exec(
        "CREATE TABLE IF NOT EXISTS jobs ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT,"
        "  command TEXT DEFAULT '',"
        "  type TEXT,"
        "  status TEXT,"
        "  next_run_time TEXT,"
        "  cron_expression TEXT DEFAULT '',"
        "  interval_seconds INTEGER DEFAULT 0,"
        "  retry_policy INTEGER DEFAULT 0,"
        "  retry_count INTEGER DEFAULT 0,"
        "  retry_delay_seconds INTEGER DEFAULT 0,"
        "  last_run_time TEXT DEFAULT '',"
        "  last_result TEXT DEFAULT ''"
        ")"
    );

    db.exec(
        "CREATE TABLE IF NOT EXISTS execution_log ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  job_id INTEGER NOT NULL,"
        "  status TEXT NOT NULL,"
        "  attempt INTEGER DEFAULT 0,"
        "  start_time TEXT NOT NULL,"
        "  end_time TEXT,"
        "  duration_ms REAL DEFAULT 0,"
        "  exit_code INTEGER DEFAULT -1,"
        "  output TEXT DEFAULT '',"
        "  error_output TEXT DEFAULT '',"
        "  FOREIGN KEY (job_id) REFERENCES jobs(id) ON DELETE CASCADE"
        ")"
    );

    // =========================
    // Crash Recovery
    // Reset any jobs stuck in "running" from a previous crash
    // =========================
    int recovered = service.recoverRunningJobs();
    if (recovered > 0)
    {
        Logger::log(LogLevel::WARN,
            "Crash recovery: reset " + std::to_string(recovered) + " running job(s) → active");
    }

    // =========================
    // Start the Scheduler Engine
    // =========================
    SchedulerEngine scheduler(repo, 5, 4);  // poll every 5s, 4 worker threads
    scheduler.start();

    // =========================
    // GET ALL JOBS
    // =========================
    CROW_ROUTE(app, "/jobs")([&](){

        auto jobs = service.getJobs();

        crow::json::wvalue res;
        std::vector<crow::json::wvalue> list;

        for (auto& j : jobs)
        {
            list.push_back(toJson(j));
        }

        res["status"] = "success";
        res["data"] = std::move(list);

        return res;
    });

    // =========================
    // CREATE JOB
    // =========================
    CROW_ROUTE(app, "/jobs").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req){

        auto body = crow::json::load(req.body);

        if (!body)
            return error("Invalid JSON");

        std::string name = body["name"].s();
        std::string command = body.has("command") ? std::string(body["command"].s()) : std::string("");
        std::string type = body["type"].s();
        std::string nextRunTime = body.has("nextRunTime") ? std::string(body["nextRunTime"].s()) : std::string("");
        std::string cronExpression = body.has("cronExpression") ? std::string(body["cronExpression"].s()) : std::string("");
        int intervalSeconds = body.has("intervalSeconds") ? (int)body["intervalSeconds"].i() : 0;
        int retryPolicy = body.has("retryPolicy") ? (int)body["retryPolicy"].i() : 0;
        int retryDelaySeconds = body.has("retryDelaySeconds") ? (int)body["retryDelaySeconds"].i() : 10;

        if (!service.createJob(name, command, type, nextRunTime, cronExpression,
                               intervalSeconds, retryPolicy, retryDelaySeconds))
            return error("Invalid input or validation failed");

        return success("Job created");
    });

    // =========================
    // GET JOB BY ID
    // =========================
    CROW_ROUTE(app, "/jobs/<int>")
    ([&](int id){

        auto job = service.getJob(id);

        if (job.id == 0)
            return error("Job not found");

        crow::json::wvalue res;
        res["status"] = "success";
        res["data"] = toJson(job);

        return res;
    });

    // =========================
    // DELETE JOB (state-aware)
    // =========================
    CROW_ROUTE(app, "/jobs/<int>").methods(crow::HTTPMethod::DELETE)
    ([&](int id){

        auto result = service.deleteJob(id);

        if (!result.ok)
            return error(result.message);

        return success(result.message);
    });

    // =========================
    // PAUSE JOB (state-aware)
    // =========================
    CROW_ROUTE(app, "/jobs/<int>/pause").methods(crow::HTTPMethod::POST)
    ([&](int id){

        auto result = service.pauseJob(id);

        if (!result.ok)
            return error(result.message);

        return success(result.message);
    });

    // =========================
    // RESUME JOB (state-aware, recalculates time)
    // =========================
    CROW_ROUTE(app, "/jobs/<int>/resume").methods(crow::HTTPMethod::POST)
    ([&](int id){

        auto result = service.resumeJob(id);

        if (!result.ok)
            return error(result.message);

        return success(result.message);
    });

    // =========================
    // RESET FAILED JOB
    // =========================
    CROW_ROUTE(app, "/jobs/<int>/reset").methods(crow::HTTPMethod::POST)
    ([&](int id){

        auto result = service.resetFailedJob(id);

        if (!result.ok)
            return error(result.message);

        return success(result.message);
    });

    // =========================
    // GET EXECUTION LOG FOR JOB
    // =========================
    CROW_ROUTE(app, "/jobs/<int>/executions")
    ([&](int id){

        auto job = service.getJob(id);
        if (job.id == 0)
            return error("Job not found");

        auto logs = service.getExecutionLog(id);

        crow::json::wvalue res;
        res["status"] = "success";
        res["jobId"] = id;
        res["jobName"] = job.name;
        res["totalRetries"] = job.retryCount;
        res["maxRetries"] = job.retryPolicy;
        res["currentStatus"] = job.status;
        res["data"] = std::move(logs);

        return res;
    });

    // =========================
    // SCHEDULER STATUS
    // =========================
    CROW_ROUTE(app, "/scheduler/status")([&](){

        crow::json::wvalue res;
        res["status"] = "success";

        crow::json::wvalue data;
        data["running"] = scheduler.isRunning();
        data["poolSize"] = (int)scheduler.getPoolSize();
        data["activeWorkers"] = scheduler.getActiveWorkers();
        data["pendingTasks"] = (int)scheduler.getPendingTasks();
        data["completedTasks"] = scheduler.getCompletedTasks();

        res["data"] = std::move(data);

        return res;
    });

    // =========================
    // START SERVER
    // =========================
    app.port(18080).multithreaded().run();

    // Graceful shutdown
    scheduler.stop();
}