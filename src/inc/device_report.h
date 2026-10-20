#pragma once

#include <string>
#include <vector>

// 原始消息保留完整 JSON；属性值另存 JSON，数值再额外提取以便统计。
struct device_value
{
    std::string field;
    std::string value_json;
    bool numeric = false;
    double number = 0;
};

struct device_report
{
    std::string version;
    std::string device_id;
    std::string topic;
    std::string payload_json;
    long long received_at = 0;
    std::vector<device_value> values;
};
