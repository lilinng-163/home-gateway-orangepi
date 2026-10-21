# home-gateway-orangepi

运行在 Orange Pi 5B 上的智能家居家庭网关。订阅设备的 MQTT 上报，校验统一 JSON 报文，调用对应字段的处理函数，再通过 SQLite 事务保存原始报文和属性值。

配套无线设备：[home-device-esp32](https://github.com/lilinng-163/home-device-esp32)。MQTT broker 独立部署；Orange Pi 是 MQTT 客户端和家庭数据处理端。

## 当前功能

- 使用 libmosquitto 连接 broker，支持用户名、密码和通配主题订阅。
- 网络回调复制消息，交给一个消费者线程处理，避免解析和数据库写入阻塞 MQTT 回调。
- 统一校验 `version`、`device_id`、`data`，根据实际出现的字段分发处理。
- SQLite 保存完整消息和属性值；不同设备上报不同字段，不需要增加数据库列。
- MQTT 收发日志打印 `RX/TX topic=... payload=...`，错误报文打印拒绝原因。
- 提供统一组包函数，便于下发设备命令。

## 编译环境

当前使用 Orange Pi 5B、Linux、GCC 15、CMake 4 编译。项目要求 CMake 3.22 以上，以及支持 `std::println` 的 C++23 编译器和标准库。

Debian / Ubuntu 上安装依赖：

```sh
sudo apt install cmake libmosquitto-dev libsqlite3-dev nlohmann-json3-dev mosquitto-clients
```

编译器需要单独安装并确认版本；较旧发行版自带的 GCC 可能还不能编译 `std::println`。

## 配置、编译和运行

```sh
git clone https://github.com/lilinng-163/home-gateway-orangepi.git
cd home-gateway-orangepi
cp conf.example.json conf.json
```

编辑 `conf.json`，填入自己的 broker 和认证信息：

```json
{
    "remote_ip": "mqtt.example.com",
    "remote_port": 1883,
    "client_id": "home-gateway-orangepi",
    "room_id": 1,
    "topic_sensor": "home/+/+/data",
    "topic_cmd": "home/room1/device1/cmd",
    "mqtt_username": "YOUR_MQTT_USERNAME",
    "mqtt_userpassword": "YOUR_MQTT_PASSWORD"
}
```

`remote_ip` 只填写 IP 或域名，不带 `http://`、端口或路径。不同客户端使用不同的 `client_id`。如果 broker 不要求认证，将用户名、密码设为空字符串。

```sh
cmake -S . -B build
cmake --build build -j2
cd build
./home-gateway-orangepi
```

当前程序从运行目录的 `../conf.json` 读配置，并将数据库写入 `../home.db`，因此按上面的方式在 `build` 目录运行。按 Ctrl+C 退出，等待消费者线程结束。

`conf.json`、数据库和构建产物仅保留在本地，不提交仓库。

## MQTT 报文

设备上报示例，主题 `home/room1/device1/data`：

```json
{"version":"0.0.1","device_id":1,"data":{"temperature":25.6,"humidity":60}}
```

控制 LED 的示例，主题 `home/room1/device1/cmd`：

```json
{"version":"0.0.1","device_id":1,"data":{"led":1}}
```

外层只包含这三个字段。`version` 固定为 `"0.0.1"`，`device_id` 支持正整数或字符串，`data` 是非空对象。完整规则见 [设备报文说明](docs/device_message.md)。

使用 MQTT 工具时，`-p` 指定端口，`-P` 指定密码。可以把 [examples](examples) 中的 JSON 文件作为报文正文；其中的温湿度是示例值。

## 数据处理和数据库

```text
MQTT 回调
    → 复制 topic / payload
    → SafeQueue
    → 唯一 worker
    → 校验外层 / 分发 data 字段
    → SQLite 事务
```

公共外层在 `device_message.cpp` 校验。`data_handlers.cpp` 为温湿度、电压、电流、功率、LED、模式等字段准备了独立函数，有什么字段就处理什么字段。

未知字段不会生成属性记录；如果消息还含有已注册字段，完整原始报文仍会保留。已知字段类型或取值错误时，整条消息拒绝入库。

- `device_messages`：版本、设备 ID、主题、接收时间、完整 JSON。
- `device_values`：所属消息、属性名、JSON 值、可选数值。
- 原消息与属性同处一个事务，失败则整体回滚。设备 ID 在数据库中统一保存为文本。

查询温度记录：

```sql
SELECT m.received_at, v.value_number
FROM device_messages AS m
JOIN device_values AS v ON v.message_id = m.id
WHERE m.device_id = '1' AND v.field = 'temperature'
ORDER BY m.received_at, m.id;
```

详细规则与扩展方法见 [JSON 字段处理](docs/device_json.md)。

## 扩展

新增传感器字段时，在 `src/data_handlers.cpp` 写 `handle_xxx()`，校验并填写 `device_value`，再加入字段处理表。不要为每个设备新增线程，也不要按设备 ID 写一大串格式分支。

下发命令时只提供 `data`，外层使用公共函数生成：

```cpp
std::string payload;
std::string error;
nlohmann::json data = {{"led", 1}};

if (make_device_message(1, data, payload, error))
{
    client.publish("home/room1/device1/cmd", payload);
}
```

需要包含 `device_message.h`。当前主程序的常驻流程负责上报入库，上面是增加命令发送入口时的用法。

## 目录

```text
include/            配置、MQTT、队列、报文、字段处理、数据库接口
src/                启动流程及各接口实现
examples/           传感器上报、LED 控制的 JSON 示例
docs/               两端报文约定、字段处理与数据库说明
tools/              MQTT 和 SQLite 集成验证
conf.example.json   可公开的配置模板
```

## 验证

```sh
python3 tools/device_json_check.py
```

脚本使用本地 `conf.json` 连接 broker，创建临时测试主题和临时数据库，检查字段处理、错误拒绝、事务回滚与退出流程，不写入正式 `home.db`。运行前确认当前账号有测试主题的发布和订阅权限。
