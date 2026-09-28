#include <iostream>
#include <print>
#include <string>

#include <mosquitto.h>

#include "mqtt_process.h"

int mqtt_process::init_cnt = 0;

void mqtt_process::on_connect(struct mosquitto *mosq, void *obj, int rc)
{
    auto *self = static_cast<mqtt_process *>(obj);
    if(rc != 0)
    {
        std::println(stderr, "connect fail: {}", rc);
        return;
    }
    for(const auto &t : self->sub_topics_)
    {
        mosquitto_subscribe(mosq, NULL, t.c_str(), 1);
    }
    std::println("connected & subscribed");
}

void mqtt_process::on_message(struct mosquitto *mosq, void *obj,
                       const struct mosquitto_message *msg) {
    (void)mosq;
    auto *self = static_cast<mqtt_process *>(obj);
    self->g_queue.push(std::string(static_cast<char *>(msg->payload), msg->payloadlen));
}


int mqtt_process::init(const std::string &name)
{
    if(init_cnt == 0)
    {
        mosquitto_lib_init();   // 进程初始化 退出时需要 mosquitto_lib_cleanup()
        init_cnt = 1;
    }
    // client_id同一个broker上必须唯一 true保持每次连接都是干净会话 数据指针
    mosq = mosquitto_new(name.c_str(), true, this);
    if(!mosq)
    {
        std::println(stderr, "new failed");
        return -1;
    }

    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);
    return 0;
}

int mqtt_process::connect(const std::string &ip, const int port)
{
    int rc = mosquitto_connect(mosq, ip.c_str(), port, 60);
    if(rc != MOSQ_ERR_SUCCESS)
    {
        std::println(stderr, "connect fail: {}", mosquitto_strerror(rc));
        this->exit();
        return -1;
    }
    is_connect = true;
    return 0;
}

int mqtt_process::subscribe(const std::string &topic)
{
    if(mosq)
    {
        auto ret = mosquitto_subscribe(mosq, NULL, topic.c_str(), 1);
        if(ret != MOSQ_ERR_SUCCESS)
        {
            return -1;
        }
    }
    else
    {
        std::println(stderr, "subscribe fail");
        return -1;
    }
    return 0;
}

int mqtt_process::publish(const std::string &topic, const std::string &payload)
{
    if(mosq)
    {
        auto ret = mosquitto_publish(mosq, NULL, topic.c_str(), static_cast<int>(payload.size()), payload.c_str(), 1, false);
        if(ret != MOSQ_ERR_SUCCESS)
        {
            return -1;
        }
    }
    else
    {
        std::println(stderr, "publish fail");
        return -1;
    }
    return 0;

}

void mqtt_process::recv(void)
{
    if(mosq)
    {
        mosquitto_loop_forever(mosq, -1, 1);
    }
    else
    {
        std::println(stderr, "loop fail");
    }
}

int mqtt_process::change_cb(on_con_t new_on_connect, on_msg_t new_on_message)
{
    if(!mosq && is_connect == false)
    {
        mosquitto_connect_callback_set(mosq, new_on_connect);
        mosquitto_message_callback_set(mosq, new_on_message);
    }
    else
    {
        std::println(stderr, "change_cb fail");
        return -1;
    }
    return 0;
}

int mqtt_process::exit(void)
{
    mosquitto_destroy(mosq);
    if(init_cnt == 1)
    {
        mosquitto_lib_cleanup();
        init_cnt = 0;
    }
    return 0;
}
