#include <iostream>
#include <print>
#include <string>
#include <cstring>

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
    for(const auto &t : self->topics_)
    {
        mosquitto_subscribe(mosq, NULL, t.c_str(), 1);
        std::println("topic:{}", t);
    }
    std::println("connected & subscribed");
}

void mqtt_process::on_message(struct mosquitto *mosq, void *obj,
                              const struct mosquitto_message *msg)
{
    (void)mosq;
    auto *self = static_cast<mqtt_process *>(obj);
    if(msg == nullptr || msg->topic == nullptr || msg->payloadlen < 0 ||
       (msg->payloadlen > 0 && msg->payload == nullptr))
    {
        return;
    }

    // 网络回调只复制并入队，解析和数据库操作留给唯一的消费者线程。
    mqtt_packet packet;
    packet.topic = msg->topic;
    if(msg->payloadlen > 0)
    {
        packet.payload.assign(static_cast<const char *>(msg->payload), msg->payloadlen);
    }
    // 先记录原始正文，再转移到消费者队列；std::string 按实际长度打印。
    std::println("RX topic={} payload={}", packet.topic, packet.payload);
    self->get_queue().push(std::move(packet));
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

int mqtt_process::connect(const std::string &ip, const int port, std::string mqtt_username, std::string mqtt_userpassword)
{
    mosquitto_username_pw_set(mosq, mqtt_username.c_str(), mqtt_userpassword.c_str());
    int rc = mosquitto_connect(mosq, ip.c_str(), port, 60);
    if(rc != MOSQ_ERR_SUCCESS)
    {
        std::println(stderr, "connect fail: {}", mosquitto_strerror(rc));
        return -1;
    }
    is_connect = true;
    return 0;
}

int mqtt_process::subscribe(const topics_vec &topics)
{
    if(mosq)
    {
        topics_ = topics;
        for(auto topic : topics)
        {
            auto ret = mosquitto_subscribe(mosq, NULL, topic.c_str(), 1);
            if(ret != MOSQ_ERR_SUCCESS)
            {
                return -1;
            }
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
            std::println(stderr, "TX failed topic={} payload={} reason={}",
                         topic, payload, mosquitto_strerror(ret));
            return -1;
        }

        // 发布调用成功后记录，避免把失败的发送打印为成功。
        std::println("TX topic={} payload={}", topic, payload);
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
    if(!mosq && (is_connect == false))
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
    if(mosq != nullptr)
    {
        mosquitto_destroy(mosq);
        mosq = nullptr;
    }
    is_connect = false;
    mosq = nullptr;
    if(init_cnt == 1)
    {
        mosquitto_lib_cleanup();
        init_cnt = 0;
    }
    return 0;
}
