#include "data_handlers.h"

#include <cmath>
#include <utility>

using json = nlohmann::json;
using handler_function = bool (*)(const json &, device_value &, std::string &);

struct field_handler
{
    const char *field;
    handler_function handle;
};

static bool handle_number(const json &input, device_value &value, std::string &error)
{
    if(!input.is_number())
    {
        error = value.field + " must be a number";
        return false;
    }
    double number = input.get<double>();
    if(!std::isfinite(number))
    {
        error = value.field + " must be finite";
        return false;
    }
    value.numeric = true;
    value.number = number;
    value.value_json = input.dump();
    return true;
}

// 各字段保留独立函数，以后在这里添加自己的校准、换算或业务处理。
static bool handle_temp(const json &input, device_value &value, std::string &error)
{
    return handle_number(input, value, error);
}

static bool handle_humi(const json &input, device_value &value, std::string &error)
{
    if(!handle_number(input, value, error))
    {
        return false;
    }
    if(value.number < 0 || value.number > 100)
    {
        error = value.field + " must be 0..100 percent";
        return false;
    }
    return true;
}

static bool handle_light(const json &input, device_value &value, std::string &error)
{
    return handle_number(input, value, error);
}

static bool handle_pressure(const json &input, device_value &value, std::string &error)
{
    return handle_number(input, value, error);
}

static bool handle_noise(const json &input, device_value &value, std::string &error)
{
    return handle_number(input, value, error);
}

static bool handle_battery(const json &input, device_value &value, std::string &error)
{
    return handle_number(input, value, error);
}

static bool handle_voltage(const json &input, device_value &value, std::string &error)
{
    return handle_number(input, value, error);
}

static bool handle_current(const json &input, device_value &value, std::string &error)
{
    return handle_number(input, value, error);
}

static bool handle_switch(const json &input, device_value &value, std::string &error)
{
    if(!input.is_boolean() &&
       !(input.is_number_integer() && (input == 0 || input == 1)))
    {
        error = value.field + " must be boolean or integer 0/1";
        return false;
    }
    // 开关统一存布尔值，原始数值形式仍保留在消息 JSON 中。
    value.value_json = input == true || input == 1 ? "true" : "false";
    value.numeric = false;
    return true;
}

static bool handle_led(const json &input, device_value &value, std::string &error)
{
    return handle_switch(input, value, error);
}

static bool handle_enabled(const json &input, device_value &value, std::string &error)
{
    return handle_switch(input, value, error);
}

static bool handle_power(const json &input, device_value &value, std::string &error)
{
    // 兼容功率数值与已有灯光状态中的布尔 power，保留各自类型。
    if(input.is_boolean())
    {
        return handle_switch(input, value, error);
    }
    return handle_number(input, value, error);
}

static bool handle_energy(const json &input, device_value &value, std::string &error)
{
    return handle_number(input, value, error);
}

static bool handle_mode(const json &input, device_value &value, std::string &error)
{
    if(!input.is_string() || input.get_ref<const std::string &>().size() > 64)
    {
        error = value.field + " must be a string of up to 64 bytes";
        return false;
    }
    value.value_json = input.dump();
    return true;
}

// 新增一种字段：先写处理函数，再在这里增加一项即可。
static const field_handler handlers[] =
{
    {"temp", handle_temp},
    {"temperature", handle_temp},
    {"humi", handle_humi},
    {"humidity", handle_humi},
    {"light", handle_light},
    {"brightness", handle_light},
    {"pressure", handle_pressure},
    {"noise", handle_noise},
    {"battery", handle_battery},
    {"voltage", handle_voltage},
    {"current", handle_current},
    {"power", handle_power},
    {"energy", handle_energy},
    {"led", handle_led},
    {"enabled", handle_enabled},
    {"mode", handle_mode},
};

bool handle_data_fields(const json &data, std::vector<device_value> &values, std::string &error)
{
    std::vector<device_value> parsed;
    for(const auto &entry : handlers)
    {
        auto item = data.find(entry.field);
        if(item == data.end())
        {
            continue;
        }

        device_value value;
        value.field = entry.field;
        if(!entry.handle(*item, value, error))
        {
            return false;
        }
        parsed.push_back(std::move(value));
    }
    if(parsed.empty())
    {
        error = "data contains no registered properties";
        return false;
    }
    values = std::move(parsed);
    return true;
}
