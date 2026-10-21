#pragma once

#include <string>

#include <nlohmann/json.hpp>

inline constexpr const char *DEVICE_MESSAGE_VERSION = "0.0.1";

// 上报和控制命令共用外层校验；设备 ID 统一转成文本供业务和数据库使用。
bool read_device_message(const nlohmann::json &root, std::string &device_id, std::string &error);

// 调用者只提供设备 ID 和 data，不再逐处拼接 version/device_id/data。
bool make_device_message(int device_id,
                         const nlohmann::json &data,
                         std::string &payload,
                         std::string &error);
