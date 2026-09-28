#pragma once
#include <string>

struct Config
{
    std::string remote_ip    = "127.0.0.1";
    int         remote_port  = 1883;
    std::string client_id    = "opi-gateway";
    int         room_id      = 1;
    std::string topic_sensor = "home/device/sensor";
    std::string topic_cmd    = "home/device/cmd";

    int load(const std::string &path);
};
