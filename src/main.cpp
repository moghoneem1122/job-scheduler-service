#include "crow.h"
#include "services/JobService.h"
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
        std::string nextRunTime = body["nextRunTime"].s();
        int retryPolicy = body["retryPolicy"].i();

        if (!service.createJob(name, type, nextRunTime, retryPolicy))
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
    // START SERVER
    // =========================
    app.port(18080).multithreaded().run();
}