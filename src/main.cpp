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

    app.port(18080).multithreaded().run();
}