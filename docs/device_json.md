# 设备 JSON 处理

当前实现位于 main 分支；公共报文约定见 [device_message.md](device_message.md)，与 ESP32 一致。

```json
{
    "version": "0.0.1",
    "device_id": 1,
    "data": {
        "temperature": 25.6,
        "humidity": 60
    }
}
```

另一个设备可以直接使用不同的属性，无需修改数据库列：

```json
{
    "version": "0.0.1",
    "device_id": "socket_01",
    "data": {
        "power": 135.2,
        "enabled": true,
        "mode": "auto"
    }
}
```

## 数据路径

MQTT 回调复制 topic/payload → SafeQueue → handle_data.cpp 的唯一 worker → 校验和解析 → SQLite 事务。

main 负责启动，handle_data.cpp 负责 JSON，sqlite_resp.cpp 负责 SQL。不会为每个设备增加线程。

device_id 表示设备身份，不再使用它作为不同 JSON 格式的 switch 分支。需要设备专属业务时，在解析成功后增加相应处理函数。

## 校验规则

- 外层只包含 version、device_id、data；公共字段由 device_message.cpp 统一校验。
- version 必须为字符串 0.0.1，未知版本明确拒绝。
- device_id 支持 1～9007199254740991 的正整数或 1～64 字节的字符串，数据库统一保存为文本。数字 1 与字符串 "1" 表示同一个 ID。
- data 必须是非空对象，最多 64 个属性；属性名最多 64 字节，不包含控制字符。
- data_handlers.cpp 为每个已知字段准备独立处理函数，有上报才调用，没有则跳过。
- temperature/temp、humidity/humi、light/brightness、pressure、noise、battery、voltage、current、power、energy 为数值处理项。
- humidity/humi 按百分比校验 0～100；led/enabled 接受布尔或整数 0/1，统一存布尔。
- mode 接受最多 64 字节的字符串；power 兼容已有布尔开关状态与数值功率。
- 未注册字段不生成属性记录；如果还有其他已注册字段，完整原始消息仍保留这些未知字段。
- 整条消息没有已注册字段时拒绝入库；已知字段类型或范围错误时整条消息拒绝。
- 消息最大 16 KiB，嵌套最多 16 层，拒绝原始零字节和无效 JSON。
- 完整校验后才入库，错误日志只记录主题与原因。
- 订阅主题仍由现有配置 topic_sensor 决定，可使用 home/+/+/data 等通配主题。

## 数据库

保留原 data 表及旧接口，新的报告使用两张表：

- device_messages：版本、设备 ID、主题、服务器接收时间、完整 JSON。
- device_values：消息 ID、属性名、属性 JSON 值、可选的数值列。

value_json 保存原始 JSON 类型；value_number 仅对数值填写。布尔 true 保存为 JSON 文本 true，不当作数值 1。字符串等已注册类型按对应函数保存；未知字段的数组、对象和 null 保留在原始消息中。数值列使用 REAL 便于统计，精确表示仍可从 JSON 值读取。

一条消息与所有属性在同一个事务中写入，任意属性写入失败都会回滚，避免只保存半条消息。

查询某设备的温度：

```sql
SELECT m.received_at, v.value_number
FROM device_messages AS m
JOIN device_values AS v ON v.message_id = m.id
WHERE m.device_id = '1' AND v.field = 'temperature'
ORDER BY m.received_at, m.id;
```

查询所有设备最近的原始消息：

```sql
SELECT device_id, topic, received_at, payload_json
FROM device_messages
ORDER BY id DESC
LIMIT 20;
```

## 编译与验证

```sh
cmake --build build -j2
python3 tools/device_json_check.py
```

验证脚本读取现有 broker 配置，使用独立客户端 ID、临时测试主题和临时数据库，覆盖两种设备数据、错误消息拒绝、事务回滚后继续接收和正常退出。不会写入正式 home.db。

## 新增处理函数

在 src/data_handlers.cpp 中写一个 handle_xxx()，函数接收字段 JSON，校验并填写 device_value。然后在 handlers 表增加：

    {"xxx", handle_xxx},

处理函数只会在 data 实际包含该字段时调用。不存在的字段不会被写成 0；增加字段不需要创建线程或修改数据库列。
