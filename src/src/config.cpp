#include "config.h"

#include <fstream>
#include <print>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

int Config::load(const std::string &path)
{
    std::ifstream in(path);
    if (!in)
    {
        std::println(stderr, "config not found: {}, using defaults", path);
        return -1;
    }

    try
    {
        json j = json::parse(in);
        remote_ip    = j.value("remote_ip",    remote_ip);
        remote_port  = j.value("remote_port",  remote_port);
        client_id    = j.value("client_id",    client_id);
        room_id      = j.value("room_id",      room_id);
        topic_sensor = j.value("topic_sensor", topic_sensor);
        topic_cmd    = j.value("topic_cmd",    topic_cmd);
    }
    catch (const json::exception &e)
    {
        std::println(stderr, "config parse error: {}", e.what());
        return -1;
    }
    return 0;
}
