#include <iostream>
#include <print>
#include <string>
#include <thread>
#include <cstdint>
#include <ctime>
#include <sstream>

#include <signal.h>

#include <mosquitto.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include <sqlite3.h>

#include "safe_queue.hpp"
#include "mqtt_process.h"
#include "sqlite_resp.h"

mqtt_process proc;
sqlite_db sq;

static void worker(void)  // 消费者线程
{
    std::string payload;
    while(proc.get_queue().pop(payload))
    {
        try
        {
            data_table dt = {};
            memset(&dt, 0, sizeof(dt));
            auto j = json::parse(payload);
            long long ts    = j.value("ts",    (long long)time(nullptr));  // 没有就用当前时间

            if(j.contains("temp"))
            {
                dt.temp = j["temp"].get<double>();
                std::println("temp = {}", dt.temp);
            }
            if(j.contains("humi"))
            {
                dt.humi = j["humi"].get<double>();
                std::println("humi = {}", dt.humi);
            }
            if(j.contains("light"))
            {
                dt.light = j["light"].get<double>();
                std::println("light = {}", dt.light);
            }
            int ret = sq.insert(dt);
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

static void console_thread(void)
{
    std::string line;
    while(std::getline(std::cin, line))
    {
        if(line.empty())
        {
            continue;
        }
        if(line == "q") // quit
        {
            break;
        }

        json cmd = json::object();
        std::istringstream iss(line);
        std::string tok;
        while(iss >> tok)
        {
            auto eq = tok.find("=");
            if(eq == std::string::npos) // 没有找到返回的一个极大值
            {
                continue;
            }
            std::string k = tok.substr(0, eq);
            std::string v = tok.substr(eq + 1);
            try
            {
                cmd[k] = std::stod(v);
            }
            catch (...)
            {
                cmd[k] = v;
            }
        }
        std::string payload = cmd.dump();
        if(proc.publish("home/device/cmd", payload) == 0)
        {
            std::println("send -> home/device/cmd: {}", payload);
        }
    }
}

static void on_sight(int sig)
{
    if(sig == SIGINT)
    {
        mosquitto_disconnect(proc.get_instance());
    }
}

int main(int argc, char **argv)
{
    // 开数据库 建表
    std::string path = "../home.db";
    sq.open(path);
    // mqtt 处理 + 线程
    proc.init("opi-gateway");
    proc.connect("47.113.149.47", 1883);
    
    std::thread cmd_thread(console_thread);
    cmd_thread.detach();    // 开启这个线程就不管了，因为主线程一直阻塞没时间join
    std::thread work_thread(worker);
    
    // 中断退出handler
    signal(SIGINT, on_sight);

    // 阻塞接收
    proc.recv();

    //exit
    proc.get_queue().stop();
    work_thread.join();  
    sq.close();
    proc.exit();
    return 0;
}