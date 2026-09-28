#include <iostream>
#include <print>
#include <string>
#include <thread>
#include <cstdint>
#include <ctime>
#include <sstream>
#include <cstdlib>
#include <atomic>

#include <signal.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <mosquitto.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include <sqlite3.h>

#include "safe_queue.hpp"
#include "mqtt_process.h"
#include "sqlite_resp.h"
#include "config.h"

mqtt_process proc;
sqlite_db sq;
Config cfg;
static std::atomic<bool> g_running{true};

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
            dt.unix_time = ts;
            dt.room_id   = cfg.room_id;

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
    int ep = epoll_create1(0);
    if(ep < 0)
    {
        perror("epoll_create1");
        return;
    }
    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = STDIN_FILENO;
    if(epoll_ctl(ep, EPOLL_CTL_ADD, STDIN_FILENO, &ev) < 0)
    {
        perror("epoll_ctl");
        close(ep);
        return;
    }
    while(g_running)
    {
        struct epoll_event out;
        int n = epoll_wait(ep, &out, 1, 200);
        if(n <= 0)
        {
            continue;
        }
        if(!std::getline(std::cin, line))
        {
            break;
        }
        if(line.empty())
        {
            continue;
        }
        if(line == "q")
        {
            break;
        }

        json cmd = json::object();
        std::istringstream iss(line);
        std::string tok;
        while(iss >> tok)
        {
            auto eq = tok.find("=");
            if(eq == std::string::npos)
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
        if(proc.publish(cfg.topic_cmd, payload) == 0)
        {
            std::println("send -> {}: {}", cfg.topic_cmd, payload);
        }
    }
    close(ep);
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
    // 读配置
    cfg.load("../conf.json");
    std::println("broker {}:{}  client_id={}  room={}",
                 cfg.remote_ip, cfg.remote_port, cfg.client_id, cfg.room_id);

    // 开数据库 建表
    std::string path = "../home.db";
    sq.open(path);
    // mqtt 处理 + 线程
    proc.init(cfg.client_id);
    proc.set_sub_topics({ cfg.topic_sensor });
    proc.connect(cfg.remote_ip, cfg.remote_port);

    std::thread cmd_thread(console_thread);
    std::thread work_thread(worker);

    // 中断退出handler
    signal(SIGINT, on_sight);

    // 阻塞接收
    proc.recv();

    //exit
    g_running = false;
    proc.get_queue().stop();
    cmd_thread.join();
    work_thread.join();
    sq.close();
    proc.exit();

    return 0;
}
