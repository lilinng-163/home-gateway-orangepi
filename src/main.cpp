#include <iostream>
#include <print>
#include <string>
#include <thread>
#include <cstdint>
#include <ctime>

#include <signal.h>

#include <mosquitto.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include <sqlite3.h>

#include "safe_queue.hpp"

static mosquitto *g_mosq = nullptr;
SafeQueue g_queue;
sqlite3 *g_db = nullptr;

// mosquitto 双回调
static void on_connect(struct mosquitto *mosq, void *obj, int rc)
{
    if(rc != 0)
    {
        std::println(stderr, "connect fail: {}\n", rc);
        return;
    }
    mosquitto_subscribe(mosq, NULL, "home/device/sensor", 1);
}

static void on_message(struct mosquitto *mosq, void *obj,
                       const struct mosquitto_message *msg) {
    (void)mosq; (void)obj;
    g_queue.push(std::string(static_cast<char *>(msg->payload), msg->payloadlen));
}

static int db_insert_data(sqlite3 *hd, long long ts, double temp, double humi, int light, int pir)
{
    char data[128];
    memset(data, 0, sizeof(data));
    // id是自增键，填写是要么写全insert into sensor (ts, temp, humi, light, pir) values (%lld, %f, %f, %d, %d); 或者NULL占位
    snprintf(data, sizeof(data), "insert into sensor values (NULL, %lld, %f, %f, %d, %d);", ts, temp, humi, light, pir);
    char *err = nullptr;
    int ret = sqlite3_exec(hd, data, nullptr, nullptr, &err);
    if(ret != SQLITE_OK)
    {
        std::println(stderr, "insert: {}", err ? err : "(null)");
        sqlite3_free(err);
        return -1;
        
    }
    return 0;
}

static void worker(void)  // 消费者线程
{
    std::string payload;
    while(g_queue.pop(payload))
    {
        try
        {
            auto j = json::parse(payload);
            long long ts    = j.value("ts",    (long long)time(nullptr));  // 没有就用当前时间
            double    temp  = j.value("temp",  0.0);
            double    humi  = j.value("humi",  0.0);
            int       light = j.value("light", 0);
            int       pir   = j.value("pir",   0);

            if(j.contains("temp"))
            {
                temp = j["temp"].get<double>();
                std::println("temp = {}", temp);
            }
            if(j.contains("humi"))
            {
                humi = j["humi"].get<double>();
                std::println("humi = {}", humi);
            }
            if(j.contains("light"))
            {
                light = j["light"].get<double>();
                std::println("light = {}", light);
            }
            if(j.contains("pir"))
            {
                pir =  j["pir"].get<double>();
                std::println("pir = {}", pir);
            }

            int ret = db_insert_data(g_db, ts, temp, humi, light, pir);
            if(ret != 0)
            {
                std::println(stderr, "db_insert_data fail");
            }
        }
        catch (const json::exception &e)
        {
            std::println(stderr, "json error: {}", e.what());
        }
    }
    std::println("work exit");
}

static void on_sight (int sig)
{
    mosquitto_disconnect(g_mosq);
}

const char *sql =
    "CREATE TABLE IF NOT EXISTS sensor ("
    "  id    INTEGER PRIMARY KEY AUTOINCREMENT,"   // 自增主键
    "  ts    INTEGER,"                             // unix 时间戳
    "  temp  REAL,"
    "  humi  REAL,"
    "  light INTEGER,"
    "  pir   INTEGER);";

int main(void)
{
    mosquitto_lib_init();   // 进程初始化 退出时需要 mosquitto_lib_cleanup()
    // client_id同一个broker上必须唯一 true保持每次连接都是干净会话 数据指针
    struct mosquitto *mosq = mosquitto_new("opi-gateway", true, NULL);
    if(!mosq)
    {
        std::println(stderr, "new failed");
        return -1;
    }
    
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);

    // 开数据库 建表
    int fd = sqlite3_open("../home.db", &g_db);
    if(fd != SQLITE_OK)
    {
        std::println(stderr, "open fail");
        return -1;
    }
    sqlite3_exec(g_db,
        "CREATE TABLE IF NOT EXISTS sensor ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " ts INTEGER, temp REAL, humi REAL, light INTEGER, pir INTEGER);",
        nullptr, nullptr, nullptr);


    int rc = mosquitto_connect(mosq, "47.113.149.47", 1883, 60);
    if(rc != MOSQ_ERR_SUCCESS)
    {
        std::println(stderr, "connect fail: {}", mosquitto_strerror(rc));
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return -1;
    }
    
    std::thread work_thread(worker);
    
    g_mosq = mosq;
    signal(SIGINT, on_sight);

    // 阻塞式网络循环   
    mosquitto_loop_forever(mosq, -1, 1);

    //exit
    g_queue.stop();
    work_thread.join();  
    sqlite3_close(g_db);

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    return 0;
}