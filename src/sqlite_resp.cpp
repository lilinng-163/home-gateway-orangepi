#include <iostream>
#include <cstring>
#include <print>
#include <string>
#include <vector>

#include <mosquitto.h>
#include <sqlite3.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "sqlite_resp.h"
#include "config.h"

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
    return create_report_tables();
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

int sqlite_db::list_all(std::vector<data_table> &out)
{
    const char *sql =
        "SELECT id,unix_time,room_id,temp,humi,light,"
        "pressure,noise,battery,voltage,current,power,energy FROM data;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) 
    {
        std::println(stderr, "prepare: {}", sqlite3_errmsg(db_));
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)      // 还有一行就继续
    {
        data_table d{};
        d.id        = sqlite3_column_int   (stmt,  0);   // 列下标从 0 开始
        d.unix_time = sqlite3_column_int64 (stmt,  1);
        d.room_id   = sqlite3_column_int   (stmt,  2);
        d.temp      = sqlite3_column_double(stmt,  3);
        d.humi      = sqlite3_column_double(stmt,  4);
        d.light     = sqlite3_column_int   (stmt,  5);
        d.pressure  = sqlite3_column_int   (stmt,  6);
        d.noise     = sqlite3_column_int   (stmt,  7);
        d.battery   = sqlite3_column_int   (stmt,  8);
        d.voltage   = sqlite3_column_int   (stmt,  9);
        d.current   = sqlite3_column_int   (stmt, 10);
        d.power     = sqlite3_column_int   (stmt, 11);
        d.energy    = sqlite3_column_int   (stmt, 12);

        out.push_back(d);                          // 存进结果
    }
    sqlite3_finalize(stmt);
    return 0;
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

int sqlite_db::create_report_tables(void)
{
    sqlite3_busy_timeout(db_, 2000);

    // 通用消息表保留完整报文；属性表同时保存原始 JSON 值和可统计的数值。
    // 原来的 data 表继续保留，不改写已有数据。
    const char *sql =
        "PRAGMA foreign_keys=ON;"
        "CREATE TABLE IF NOT EXISTS device_messages("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "version TEXT NOT NULL,"
        "device_id TEXT NOT NULL,"
        "topic TEXT NOT NULL,"
        "received_at INTEGER NOT NULL,"
        "payload_json TEXT NOT NULL);"
        "CREATE INDEX IF NOT EXISTS device_messages_device_time "
        "ON device_messages(device_id,received_at);"
        "CREATE TABLE IF NOT EXISTS device_values("
        "message_id INTEGER NOT NULL,"
        "field TEXT NOT NULL,"
        "value_json TEXT NOT NULL,"
        "value_number REAL,"
        "PRIMARY KEY(message_id,field),"
        "FOREIGN KEY(message_id) REFERENCES device_messages(id) ON DELETE CASCADE);"
        "CREATE INDEX IF NOT EXISTS device_values_field ON device_values(field,message_id);";
    if(sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        std::println(stderr, "create report tables failed: {}", sqlite3_errmsg(db_));
        return -1;
    }
    return 0;
}

int sqlite_db::insert_report(const device_report &report)
{
    if(db_ == nullptr)
    {
        return -1;
    }
    if(sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        std::println(stderr, "begin report transaction failed: {}", sqlite3_errmsg(db_));
        return -1;
    }

    sqlite3_stmt *message = nullptr;
    sqlite3_stmt *property = nullptr;
    bool success = false;

    // 一条消息与它的全部属性必须同时成功，任何一步失败都回滚。
    do
    {
        const char *message_sql =
            "INSERT INTO device_messages(version,device_id,topic,received_at,payload_json)"
            " VALUES(?,?,?,?,?)";
        if(sqlite3_prepare_v2(db_, message_sql, -1, &message, nullptr) != SQLITE_OK)
        {
            break;
        }
        if(sqlite3_bind_text(message, 1, report.version.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
           sqlite3_bind_text(message, 2, report.device_id.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
           sqlite3_bind_text(message, 3, report.topic.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
           sqlite3_bind_int64(message, 4, report.received_at) != SQLITE_OK ||
           sqlite3_bind_text(message, 5, report.payload_json.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
        {
            break;
        }
        if(sqlite3_step(message) != SQLITE_DONE)
        {
            break;
        }
        sqlite3_int64 message_id = sqlite3_last_insert_rowid(db_);

        const char *property_sql =
            "INSERT INTO device_values(message_id,field,value_json,value_number) VALUES(?,?,?,?)";
        if(sqlite3_prepare_v2(db_, property_sql, -1, &property, nullptr) != SQLITE_OK)
        {
            break;
        }

        bool properties_ok = true;
        for(const auto &value : report.values)
        {
            if(sqlite3_reset(property) != SQLITE_OK || sqlite3_clear_bindings(property) != SQLITE_OK ||
               sqlite3_bind_int64(property, 1, message_id) != SQLITE_OK ||
               sqlite3_bind_text(property, 2, value.field.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
               sqlite3_bind_text(property, 3, value.value_json.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
            {
                properties_ok = false;
                break;
            }
            int result = value.numeric ? sqlite3_bind_double(property, 4, value.number)
                                       : sqlite3_bind_null(property, 4);
            if(result != SQLITE_OK || sqlite3_step(property) != SQLITE_DONE)
            {
                properties_ok = false;
                break;
            }
        }
        if(!properties_ok)
        {
            break;
        }
        success = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK;
    }
    while(false);

    std::string error = success ? "" : sqlite3_errmsg(db_);
    sqlite3_finalize(message);
    sqlite3_finalize(property);
    if(!success)
    {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        std::println(stderr, "report transaction failed: {}", error);
        return -1;
    }
    return 0;
}
