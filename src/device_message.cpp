#include "device_message.h"

#include <cmath>
#include <utility>

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

bool read_device_message(const json &root, std::string &device_id, std::string &error)
{
    device_id.clear();
    error.clear();
    if(!root.is_object() || root.size() != 3 || !root.contains("version") ||
       !root["version"].is_string() || !root.contains("device_id") ||
       !root.contains("data") || !root["data"].is_object())
    {
        error = "require only version string, device_id and data object";
        return false;
    }
    if(root["version"] != DEVICE_MESSAGE_VERSION)
    {
        error = "unsupported version";
        return false;
    }

    const json &id = root["device_id"];
    std::string parsed_id;
    if(id.is_string() && valid_name(id.get_ref<const std::string &>()))
    {
        parsed_id = id.get<std::string>();
    }
    else if(id.is_number())
    {
        // 与 ESP32 的 cJSON 对齐，仅允许 double 可精确表示的正整数。
        double number = id.get<double>();
        if(std::isfinite(number) && number > 0 && number <= 9007199254740991.0 &&
           std::floor(number) == number)
        {
            parsed_id = std::to_string(static_cast<long long>(number));
        }
    }
    if(parsed_id.empty())
    {
        error = "device_id must be a positive safe integer or a valid string of up to 64 bytes";
        return false;
    }

    const json &data = root["data"];
    if(data.empty() || data.size() > 64)
    {
        error = "data must contain 1 to 64 properties";
        return false;
    }
    for(const auto &[field, value] : data.items())
    {
        if(!valid_name(field))
        {
            error = "property names must contain 1 to 64 bytes without control characters";
            return false;
        }
    }

    device_id = std::move(parsed_id);
    return true;
}

bool make_device_message(int device_id, const json &data, std::string &payload, std::string &error)
{
    payload.clear();
    error.clear();
    try
    {
        json root = {{"version", DEVICE_MESSAGE_VERSION}, {"device_id", device_id}, {"data", data}};
        std::string parsed_id;
        if(!read_device_message(root, parsed_id, error))
        {
            return false;
        }

        payload = root.dump();
        return true;
    }
    catch(const std::exception &exception)
    {
        error = exception.what();
        return false;
    }
}
