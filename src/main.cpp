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
    // Initialize the DB table
    // =========================
    db.exec(
        "CREATE TABLE IF NOT EXISTS jobs ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT,"
        "  type TEXT,"
        "  status TEXT,"
        "  next_run_time TEXT,"
        "  cron_expression TEXT DEFAULT '',"
        "  interval_seconds INTEGER DEFAULT 0,"
        "  retry_policy INTEGER,"
        "  retry_count INTEGER"
        ")"
    );

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
            crow::json::wvalue item;
            item["id"] = j.id;
            item["name"] = j.name;
            item["type"] = j.type;
            item["status"] = j.status;
            item["nextRunTime"] = j.nextRunTime;
            item["cronExpression"] = j.cronExpression;
            item["intervalSeconds"] = j.intervalSeconds;
            item["retryPolicy"] = j.retryPolicy;
            item["retryCount"] = j.retryCount;

            list.push_back(std::move(item));
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
        std::string type = body["type"].s();
        std::string nextRunTime = body.has("nextRunTime") ? body["nextRunTime"].s() : "";
        std::string cronExpression = body.has("cronExpression") ? body["cronExpression"].s() : "";
        int intervalSeconds = body.has("intervalSeconds") ? (int)body["intervalSeconds"].i() : 0;
        int retryPolicy = body["retryPolicy"].i();

        if (!service.createJob(name, type, nextRunTime, cronExpression, intervalSeconds, retryPolicy))
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

        crow::json::wvalue j;
        j["id"] = job.id;
        j["name"] = job.name;
        j["type"] = job.type;
        j["status"] = job.status;
        j["nextRunTime"] = job.nextRunTime;
        j["cronExpression"] = job.cronExpression;
        j["intervalSeconds"] = job.intervalSeconds;
        j["retryPolicy"] = job.retryPolicy;
        j["retryCount"] = job.retryCount;

        res["data"] = std::move(j);

        return res;
    });

    // =========================
    // DELETE JOB
    // =========================
    CROW_ROUTE(app, "/jobs/<int>").methods(crow::HTTPMethod::DELETE)
    ([&](int id){

        if (!service.deleteJob(id))
            return error("Job not found");

        return success("Job deleted");
    });

    // =========================
    // PAUSE JOB
    // =========================
    CROW_ROUTE(app, "/jobs/<int>/pause").methods(crow::HTTPMethod::POST)
    ([&](int id){

        if (!service.pauseJob(id))
            return error("Job not found");

        return success("Job paused");
    });

    // =========================
    // RESUME JOB
    // =========================
    CROW_ROUTE(app, "/jobs/<int>/resume").methods(crow::HTTPMethod::POST)
    ([&](int id){

        if (!service.resumeJob(id))
            return error("Job not found");

        return success("Job resumed");
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