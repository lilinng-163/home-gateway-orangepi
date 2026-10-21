#pragma once
#include <string>

struct Config
{
    std::string remote_ip    = "127.0.0.1";
    int         remote_port  = 1883;
    std::string client_id    = "home-gateway-orangepi";
    int         room_id      = 1;
    std::string topic_sensor = "home/+/+/data";
    std::string topic_cmd    = "home/room1/device1/cmd";
    std::string mqtt_username;
    std::string mqtt_userpassword;
    int load(const std::string &path);
};
