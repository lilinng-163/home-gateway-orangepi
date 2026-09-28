#include <iostream>
#include <print>
#include <string>

#include <sqlite3.h>

#include "sqlite_resp.h"

int sqlite_db::open(std::string &absolutely_path)
{
    path_ = absolutely_path;
    if (sqlite3_open(path_.c_str(), &db_) != SQLITE_OK) 
    {
        std::println(stderr, "open fail: {}", sqlite3_errmsg(db_));
        return -1;
    }
    const char *sql =
        "CREATE TABLE IF NOT EXISTS data ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " unix_time INTEGER, room_id INTEGER,"
        " temp REAL, humi REAL, light INTEGER,"
        " pressure INTEGER, noise INTEGER, battery INTEGER,"
        " voltage INTEGER, current INTEGER, power INTEGER, energy INTEGER);";
    char *err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) 
    {
        std::println(stderr, "create fail: {}", err);
        sqlite3_free(err);
        return -1;
    }
    return 0;
}
int sqlite_db::insert(data_table &data)
{
    const char *sql =
        "INSERT INTO data (unix_time,room_id,temp,humi,light,"
        "pressure,noise,battery,voltage,current,power,energy)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?);";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) 
    {
        std::println(stderr, "prepare: {}", sqlite3_errmsg(db_));
        return -1;
    }
    sqlite3_bind_int64 (stmt,  1, data.unix_time);
    sqlite3_bind_int   (stmt,  2, data.room_id);
    sqlite3_bind_double(stmt,  3, data.temp);
    sqlite3_bind_double(stmt,  4, data.humi);
    sqlite3_bind_int   (stmt,  5, data.light);
    sqlite3_bind_int   (stmt,  6, data.pressure);
    sqlite3_bind_int   (stmt,  7, data.noise);
    sqlite3_bind_int   (stmt,  8, data.battery);
    sqlite3_bind_int   (stmt,  9, data.voltage);
    sqlite3_bind_int   (stmt, 10, data.current);
    sqlite3_bind_int   (stmt, 11, data.power);
    sqlite3_bind_int   (stmt, 12, data.energy);

    int ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok ? 0 : -1;
}
int sqlite_db::close(void)
{
    if(db_)
    {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    return 0;
}
