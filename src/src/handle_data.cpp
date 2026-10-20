#include "handle_data.h"
#include "data_handlers.h"

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

static bool valid_name(const std::string &name)
{
    if(name.empty() || name.size() > 64)
    {
        return false;
    }
    for(unsigned char c : name)
    {
        if(c < 32 || c == 127)
        {
            return false;
        }
    }
    return true;
}

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
        if(!root.is_object() || !root.contains("version") || !root["version"].is_string() ||
           !root.contains("device_id") || !root.contains("data") || !root["data"].is_object())
        {
            error = "require version string, device_id and data object";
            return false;
        }
        if(root["version"] != "0.0.1")
        {
            error = "unsupported version; expected 0.0.1";
            return false;
        }

        // 兼容示例的正整数 ID，也支持 sensor_01 一类字符串 ID，数据库统一存文本。
        const json &id = root["device_id"];
        std::string device_id;
        if(id.is_string())
        {
            device_id = id.get<std::string>();
        }
        else if(id.is_number_unsigned() && id.get<unsigned long long>() > 0)
        {
            device_id = std::to_string(id.get<unsigned long long>());
        }
        else if(id.is_number_integer() && id.get<long long>() > 0)
        {
            device_id = std::to_string(id.get<long long>());
        }
        if(!valid_name(device_id))
        {
            error = "device_id must be a positive integer or a nonempty string of up to 64 bytes";
            return false;
        }

        const json &data = root["data"];
        if(data.empty() || data.size() > 64)
        {
            error = "data must contain 1 to 64 properties";
            return false;
        }
        device_report parsed;
        parsed.version = root["version"].get<std::string>();
        parsed.device_id = std::move(device_id);
        parsed.topic = packet.topic;
        parsed.payload_json = packet.payload;
        parsed.received_at = static_cast<long long>(std::time(nullptr));
        parsed.values.reserve(data.size());

        for(const auto &[field, value] : data.items())
        {
            if(!valid_name(field))
            {
                error = "property names must contain 1 to 64 bytes without control characters";
                return false;
            }
        }
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
