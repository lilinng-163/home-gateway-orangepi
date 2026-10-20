"""验证真实 MQTT 接收、异构 JSON、错误拒绝和 SQLite 原子入库，不使用正式数据库。"""
import json
import os
from pathlib import Path
import pty
import signal
import sqlite3
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parent.parent
config = json.loads((root / "conf.json").read_text())
username = str(config.get("mqtt_username", ""))
password = str(config.get("mqtt_userpassword", config.get("mqtt_password", "")))

def wait_for(condition, process, log, timeout=10):
    until = time.monotonic() + timeout
    while not condition():
        assert process.poll() is None, "test process exited unexpectedly"
        if time.monotonic() >= until:
            output = log.read_text()
            for secret in (password, username):
                if secret:
                    output = output.replace(secret, "[REDACTED]")
            raise AssertionError(output)
        time.sleep(0.05)

with tempfile.TemporaryDirectory(prefix="device-json-check-") as directory:
    temp = Path(directory)
    (temp / "build").mkdir()
    prefix = "home/test/json-" + str(os.getpid())
    config["client_id"] = "json-check-" + str(os.getpid())
    config["topic_sensor"] = prefix + "/+"
    (temp / "conf.json").write_text(json.dumps(config))
    (temp / "conf.json").chmod(0o600)
    master, slave = pty.openpty()
    log = temp / "output.log"
    with log.open("w") as output:
        process = subprocess.Popen(["stdbuf", "-oL", "-eL", str(root / "build/prj")],
                                   cwd=temp / "build", stdin=slave,
                                   stdout=output, stderr=subprocess.STDOUT)
    os.close(slave)

    def query(sql):
        with sqlite3.connect(temp / "home.db") as db:
            return db.execute(sql).fetchall()

    def publish(payload, suffix="sensor"):
        body = payload if isinstance(payload, str) else json.dumps(payload)
        command = ["mosquitto_pub", "-h", config["remote_ip"], "-p", str(config["remote_port"]),
                   "-u", username, "-P", password, "-q", "1", "-t", prefix + "/" + suffix,
                   "-m", body]
        result = subprocess.run(command, capture_output=True, timeout=8)
        assert result.returncode == 0, "test MQTT publish failed"

    try:
        wait_for(lambda: "connected & subscribed" in log.read_text(), process, log)
        time.sleep(1)
        publish({"version": "0.0.1", "device_id": 1,
                 "data": {"temperature": 25.6, "humidity": 60}})
        publish({"version": "0.0.1", "device_id": "socket_01",
                 "data": {"power": 135.2, "enabled": True, "mode": "auto",
                          "channels": [1, 2], "calibration": None}}, "socket")
        wait_for(lambda: query("SELECT COUNT(*) FROM device_messages")[0][0] == 2, process, log)
        assert query("SELECT device_id FROM device_messages ORDER BY id") == [("1",), ("socket_01",)]
        assert query("SELECT value_json,value_number FROM device_values WHERE field='enabled'") == [("true", None)]
        assert query("SELECT value_json,value_number FROM device_values WHERE field='mode'") == [('"auto"', None)]
        assert query("SELECT COUNT(*) FROM device_values WHERE field='channels'")[0][0] == 0
        raw = json.loads(query("SELECT payload_json FROM device_messages WHERE device_id='socket_01'")[0][0])
        assert raw["data"]["channels"] == [1, 2]
        assert query("SELECT COUNT(*) FROM device_values WHERE field='calibration'")[0][0] == 0
        print("PASS: registered fields dispatched; missing and unregistered fields skipped")

        invalid = [
            "{",
            {"version": "1.0", "device_id": 1, "data": {"temperature": 1}},
            {"version": "0.0.1", "device_id": 1, "data": "not an object"},
            {"version": "0.0.1", "device_id": 0, "data": {"temperature": 1}},
        ]
        invalid += [
            {"version": "0.0.1", "device_id": 1, "data": {"temperature": "hot"}},
            {"version": "0.0.1", "device_id": 1, "data": {"humidity": 101}},
            {"version": "0.0.1", "device_id": 1, "data": {"led": 2}},
        ]
        for body in invalid:
            publish(body)
        wait_for(lambda: log.read_text().count("JSON rejected:") == len(invalid), process, log)
        assert query("SELECT COUNT(*) FROM device_messages")[0][0] == 2
        print("PASS: invalid JSON, version, data type and device ID do not enter the database")

        # 只在临时数据库注入属性写入失败，检查原始消息也随事务回滚。
        with sqlite3.connect(temp / "home.db") as db:
            db.execute("CREATE TRIGGER reject_property BEFORE INSERT ON device_values "
                       "WHEN NEW.field='humidity' BEGIN SELECT RAISE(ABORT,'test rollback'); END")
        publish({"version": "0.0.1", "device_id": "rollback",
                 "data": {"temperature": 1, "humidity": 2}})
        wait_for(lambda: "report transaction failed:" in log.read_text(), process, log)
        assert query("SELECT COUNT(*) FROM device_messages")[0][0] == 2
        assert query("SELECT COUNT(*) FROM device_values")[0][0] == 5

        publish({"version": "0.0.1", "device_id": "light_01",
                 "data": {"power": False, "brightness": 80}}, "light")
        wait_for(lambda: query("SELECT COUNT(*) FROM device_messages")[0][0] == 3, process, log)
        assert query("SELECT COUNT(*) FROM device_values")[0][0] == 7
        assert query("SELECT COUNT(*) FROM device_values WHERE value_number IS NOT NULL")[0][0] == 4
        assert query("SELECT COUNT(*) FROM data")[0][0] == 0
        print("PASS: atomic rollback, following-message recovery and legacy table preserved")

        process.send_signal(signal.SIGINT)
        assert process.wait(timeout=5) == 0
        print("PASS: consumer wakes and exits cleanly")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)
