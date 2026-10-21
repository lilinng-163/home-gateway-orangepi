#pragma once

#include <string>

// 回调结束后库会回收原始消息，队列必须持有自己的主题和正文副本。
struct mqtt_packet
{
    std::string topic;
    std::string payload;
};
