#include "crow.h"
#include "db/Database.h"

Database db("jobs.db");

int main()
{
    crow::SimpleApp app;

    // INIT TABLE
    db.exec(
        "CREATE TABLE IF NOT EXISTS jobs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT,"
        "type TEXT,"
        "status TEXT"
        ");"
    );

    // GET JOBS
    CROW_ROUTE(app, "/jobs")([&](){
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db.get(),
            "SELECT id, name, type, status FROM jobs",
            -1, &stmt, nullptr);

        crow::json::wvalue res;
        std::vector<crow::json::wvalue> list;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            crow::json::wvalue j;
            j["id"] = sqlite3_column_int(stmt, 0);
            j["name"] = (const char*)sqlite3_column_text(stmt, 1);
            j["type"] = (const char*)sqlite3_column_text(stmt, 2);
            j["status"] = (const char*)sqlite3_column_text(stmt, 3);
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

        std::string name = body["name"].s();
        std::string type = body["type"].s();

        db.exec(
            "INSERT INTO jobs (name, type, status) VALUES ('" +
            name + "','" + type + "','active');"
        );

        crow::json::wvalue res;
        res["message"] = "Job created";
        return res;
    });

    CROW_ROUTE(app, "/jobs/<int>")
([&](int id){

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db.get(),
        "SELECT id, name, type, status FROM jobs WHERE id = ?",
        -1, &stmt, nullptr);

    sqlite3_bind_int(stmt, 1, id);

    crow::json::wvalue res;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        res["id"] = sqlite3_column_int(stmt, 0);
        res["name"] = (const char*)sqlite3_column_text(stmt, 1);
        res["type"] = (const char*)sqlite3_column_text(stmt, 2);
        res["status"] = (const char*)sqlite3_column_text(stmt, 3);
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
        "UPDATE jobs SET status='paused' WHERE id=" + std::to_string(id);

    db.exec(sql);

    crow::json::wvalue res;
    res["message"] = "Job paused";

    return res;
});





CROW_ROUTE(app, "/jobs/<int>/resume").methods(crow::HTTPMethod::POST)
([&](int id){

    std::string sql =
        "UPDATE jobs SET status='active' WHERE id=" + std::to_string(id);

    db.exec(sql);

    crow::json::wvalue res;
    res["message"] = "Job resumed";

    return res;
});



    app.port(18080).multithreaded().run();
}