#include "crow.h"
#include "db/Database.h"

const std::string STATUS_ACTIVE = "active";
const std::string STATUS_PAUSED = "paused";
const std::string STATUS_RUNNING = "running";
const std::string STATUS_COMPLETED = "completed";
const std::string STATUS_FAILED = "failed";
Database db("./jobs.db");

int main()
{
    crow::SimpleApp app;

    // INIT TABLE
   db.exec(
    "CREATE TABLE IF NOT EXISTS jobs ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "name TEXT,"
    "type TEXT,"
    "status TEXT,"
    "next_run_time TEXT,"
    "retry_policy INTEGER,"
    "retry_count INTEGER"
    ");"
);

    // GET JOBS
    CROW_ROUTE(app, "/jobs")([&](){
        sqlite3_stmt* stmt;
      sqlite3_prepare_v2(
    db.get(),
    "SELECT id, name, type, status, "
    "next_run_time, retry_policy, retry_count "
    "FROM jobs",
    -1,
    &stmt,
    nullptr
);

        crow::json::wvalue res;
        std::vector<crow::json::wvalue> list;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            crow::json::wvalue j;
            j["id"] = sqlite3_column_int(stmt, 0);
            j["name"] = (const char*)sqlite3_column_text(stmt, 1);
            j["type"] = (const char*)sqlite3_column_text(stmt, 2);
            j["status"] = (const char*)sqlite3_column_text(stmt, 3);
            j["nextRunTime"] = (const char*)sqlite3_column_text(stmt, 4);
            j["retryPolicy"] = sqlite3_column_int(stmt, 5);
            j["retryCount"] = sqlite3_column_int(stmt, 6);
            list.push_back(j);
        }

        sqlite3_finalize(stmt);

        res["jobs"] = std::move(list);
        return res;
    });

    // POST JOB
   CROW_ROUTE(app, "/jobs").methods(crow::HTTPMethod::POST)
([&](const crow::request& req){

    auto body = crow::json::load(req.body);

    if (!body ||
        !body.has("name") ||
        !body.has("type") ||
        !body.has("nextRunTime") ||
        !body.has("retryPolicy"))
    {
        crow::json::wvalue err;
        err["error"] = "Missing required fields";
        return err;
    }

    std::string name = body["name"].s();
    std::string type = body["type"].s();

    if(type != "once" &&
   type != "interval" &&
   type != "cron")
{
    crow::json::wvalue err;
    err["error"] = "Invalid job type";
    return err;
}
    std::string nextRunTime = body["nextRunTime"].s();

    int retryPolicy = body["retryPolicy"].i();

    std::string sql =
        "INSERT INTO jobs "
        "(name, type, status, next_run_time, retry_policy, retry_count) "
        "VALUES ('" +
        name + "','" +
       type + "','" + STATUS_ACTIVE + "','" +
        nextRunTime + "'," +
        std::to_string(retryPolicy) + ",0);";

    db.exec(sql);

    crow::json::wvalue res;
    res["message"] = "Job created";

    return res;
});



    CROW_ROUTE(app, "/jobs/<int>")
([&](int id){

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db.get(),
        "SELECT id, name, type, status, next_run_time, retry_policy, retry_count FROM jobs WHERE id = ?",
        -1, &stmt, nullptr);

    sqlite3_bind_int(stmt, 1, id);

    crow::json::wvalue res;

   if (sqlite3_step(stmt) == SQLITE_ROW) {
    res["id"] = sqlite3_column_int(stmt, 0);
    res["name"] = (const char*)sqlite3_column_text(stmt, 1);
    res["type"] = (const char*)sqlite3_column_text(stmt, 2);
    res["status"] = (const char*)sqlite3_column_text(stmt, 3);
    res["nextRunTime"] = (const char*)sqlite3_column_text(stmt, 4);
    res["retryPolicy"] = sqlite3_column_int(stmt, 5);
    res["retryCount"] = sqlite3_column_int(stmt, 6);
} else {
    res["error"] = "Job not found";
}

    sqlite3_finalize(stmt);
    return res;
});

CROW_ROUTE(app, "/jobs/<int>").methods(crow::HTTPMethod::DELETE)
([&](int id){

    std::string sql = "DELETE FROM jobs WHERE id = " + std::to_string(id);
    db.exec(sql);

    crow::json::wvalue res;
    res["message"] = "Job deleted";

    return res;
});



CROW_ROUTE(app, "/jobs/<int>/pause").methods(crow::HTTPMethod::POST)
([&](int id){

std::string sql =
    "UPDATE jobs SET status='paused' WHERE id=" +
    std::to_string(id);

    db.exec(sql);

    crow::json::wvalue res;
    res["message"] = "Job paused";

    return res;
});





CROW_ROUTE(app, "/jobs/<int>/resume").methods(crow::HTTPMethod::POST)
([&](int id){

   std::string sql =
    "UPDATE jobs SET status='active' WHERE id=" +
    std::to_string(id);

    db.exec(sql);

    crow::json::wvalue res;
    res["message"] = "Job resumed";

    return res;
});



    app.port(18080).multithreaded().run();
}