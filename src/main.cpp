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
#include "handle_data.h"
#include "config.h"

mqtt_process client;
sqlite_db sq;
Config cfg;

static void on_sight(int sig)
{
    if(sig == SIGINT)
    {
        mosquitto_disconnect(client.get_instance());
    }
}

static int handle_option(int &argc, char **&argv)
{
    return 0;
}

int main(int argc, char **argv)
{
    // 配置参数
    handle_option(argc, argv);
    // 读配置
    cfg.load("../conf.json");
    std::println("broker {}:{}  client_id={}  room={}",
                 cfg.remote_ip, cfg.remote_port, cfg.client_id, cfg.room_id);

    // 开数据库 建表
    std::string path = "../home.db";
    if(sq.open(path) != 0)
    {
        sq.close();
        return 1;
    }
    // mqtt 处理 + 线程
    if(client.init(cfg.client_id) != 0)
    {
        sq.close();
        return 1;
    }
    topics_vec topics;
    topics.push_back(cfg.topic_sensor);
    client.subscribe(topics);

    // 发起连接
    if(client.connect(cfg.remote_ip, cfg.remote_port,
                      cfg.mqtt_username, cfg.mqtt_userpassword) != 0)
    {
        client.exit();
        sq.close();
        return 1;
    }

    // 唯一的消息消费者：按主题分发、解析数据并写入数据库。
    std::thread work_thread(worker, &client, &cfg, &sq);

    // 中断退出handler
    signal(SIGINT, on_sight);

    // 阻塞接收
    client.recv();

    //exit
    client.get_queue().stop(); // 唤醒正在等待消息的 worker，允许 join 正常返回。
    work_thread.join();
    sq.close();
    client.exit();

    return 0;
}
