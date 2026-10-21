#pragma once

#include <string>

#include "device_report.h"
#include "mqtt_packet.h"

class mqtt_process;
class sqlite_db;
struct Config;

// 纯解析不操作数据库；整条消息校验通过后，worker 才提交一次入库。
bool parse_device_report(const mqtt_packet &packet, device_report &report, std::string &error);

void worker(mqtt_process *client, const Config *cfg, sqlite_db *sq);
