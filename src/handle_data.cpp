#include "handle_data.h"
#include "data_handlers.h"
#include "device_message.h"

#include <ctime>
#include <print>
#include <stdexcept>
#include <utility>

#include <mosquitto.h>
#include <nlohmann/json.hpp>

#include "config.h"
#include "mqtt_process.h"
#include "sqlite_resp.h"

using json = nlohmann::json;

bool parse_device_report(const mqtt_packet &packet, device_report &report, std::string &error)
{
    report = {};
    error.clear();
    if(packet.payload.empty() || packet.payload.size() > 16 * 1024 ||
       packet.payload.find('\0') != std::string::npos)
    {
        error = "empty, oversized or NUL-containing payload";
        return false;
    }

    try
    {
        // 限制嵌套层数，避免异常报文占用过多解析资源。
        auto limit_depth = [](int depth, json::parse_event_t, json &)
        {
            if(depth > 16)
            {
                throw std::runtime_error("JSON nesting exceeds 16 levels");
            }
            return true;
        };
        json root = json::parse(packet.payload, limit_depth);
        std::string device_id;
        if(!read_device_message(root, device_id, error))
        {
            return false;
        }

        const json &data = root["data"];
        device_report parsed;
        parsed.version = root["version"].get<std::string>();
        parsed.device_id = std::move(device_id);
        parsed.topic = packet.topic;
        parsed.payload_json = packet.payload;
        parsed.received_at = static_cast<long long>(std::time(nullptr));
        parsed.values.reserve(data.size());

        // 只调用本次上报字段对应的处理函数，不为缺失字段创建默认值。
        if(!handle_data_fields(data, parsed.values, error))
        {
            return false;
        }

        report = std::move(parsed);
        return true;
    }
    catch(const std::exception &exception)
    {
        error = exception.what();
        return false;
    }
}

void worker(mqtt_process *client, const Config *cfg, sqlite_db *sq)
{
    mqtt_packet packet;
    while(client->get_queue().pop(packet))
    {
        // 配置既可以订阅单个主题，也可以使用 home/+/+/data 这类通配主题。
        bool matches = false;
        if(mosquitto_topic_matches_sub(cfg->topic_sensor.c_str(), packet.topic.c_str(), &matches) !=
           MOSQ_ERR_SUCCESS || !matches)
        {
            continue;
        }

        device_report report;
        std::string error;
        if(!parse_device_report(packet, report, error))
        {
            std::println(stderr, "JSON rejected: topic={} reason={}", packet.topic, error);
            continue;
        }
        if(sq->insert_report(report) != 0)
        {
            std::println(stderr, "report insert failed: device={}", report.device_id);
            continue;
        }
        std::println("report stored: device={} properties={} topic={}",
                     report.device_id, report.values.size(), report.topic);
    }
    std::println("work exit");
}
