#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "device_report.h"

// 只处理已注册且本次实际上报的字段，缺失字段不补零。
bool handle_data_fields(const nlohmann::json &data,
                        std::vector<device_value> &values,
                        std::string &error);
