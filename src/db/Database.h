#pragma once
#include <sqlite3.h>
#include <string>
#include <iostream>

class Database {
private:
    sqlite3* db;

public:
    Database(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db)) {
            std::cerr << "DB open error\n";
        }
    }

    sqlite3* get() { return db; }

    void exec(const std::string& q) {
        char* err;
        if (sqlite3_exec(db, q.c_str(), 0, 0, &err) != SQLITE_OK) {
            std::cerr << "SQL error: " << err << std::endl;
            sqlite3_free(err);
        }
    }

    ~Database() {
        sqlite3_close(db);
    }
};