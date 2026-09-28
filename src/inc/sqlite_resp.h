#pragma once
#include <iostream>
#include <print>
#include <string>

#include <sqlite3.h>

typedef struct
{
    int id;
    long long unix_time;
    int room_id;    // 房间号 下面的字段根据需要取舍
    double temp;
    double humi;
    int light;
    int pressure;
    int noise;
    int battery;
    int voltage;
    int current;
    int power;
    int energy; 
}data_table;

class sqlite_db
{
public:
    int open(std::string &absolutely_path);
    int insert(data_table &data);
    int close(void);
private:
    std::string path_;
    sqlite3 *db_;
};