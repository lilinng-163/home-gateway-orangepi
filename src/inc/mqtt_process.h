#pragma once
#include <iostream>
#include <print>
#include <string>

#include <mosquitto.h>

#include "safe_queue.hpp"

using on_con_t = void(*)(struct mosquitto *, void *, int);
using on_msg_t = void(*)(struct mosquitto *, void *, const struct mosquitto_message *);

class mqtt_process
{
public:
    int init(const std::string &name);
    int connect(const std::string &ip, const int port);
    int subscribe(const std::string &topic);
    int publish(const std::string &topic, const std::string &payload);
    void recv(void);
    int exit(void);
    int change_cb(on_con_t new_on_connect, on_msg_t new_on_message);
    struct mosquitto *get_instance(void)
    {
        return this->mosq;
    }
    SafeQueue &get_queue(void)
    {
        return g_queue;
    }
private:
    static void on_connect(struct mosquitto *mosq, void *obj, int rc);
    static void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg);
    SafeQueue g_queue;
    struct mosquitto *mosq;
    static int init_cnt;
    bool is_connect = false;
};