# 🐝 智能蜂箱硬件 MQTT 接入指南

## 📡 连接信息

| 项目 | 值 |
|------|-----|
| **服务器地址** | `119.27.186.189` |
| **端口** | `1883` |
| **用户名** | `admin` |
| **密码** | `123456` |
| **协议** | MQTT v3.1.1 |

## 📤 硬件上报数据 (发送)

### Topic格式
```
$thing/up/property/DG647QTNIJ/{设备ID}
```

### 完整数据上报格式 (Full Data Update)
```json
{
  "method": "report",
  "type": "full",             // 可选，默认为"full"
  "params": {
    "temp": 25.3,              // 温度 (°C)
    "humidity": 65.2,          // 湿度 (%)
    "current_co2": 420,        // CO2浓度 (ppm)
    "weight": 15.8,            // 蜂箱重量 (kg)
    "lac": 11,                 // 地区区域码
    "cid": 21,                 // 基站ID
    "number_0": 850,           // 区域0蜜蜂数量 (只)
    "number_1": 920,           // 区域1蜜蜂数量 (只)
    "number_2": 780,           // 区域2蜜蜂数量 (只)
    "number_3": 660,           // 区域3蜜蜂数量 (只)
    "number_4": 1050,          // 区域4蜜蜂数量 (只)
    "number_5": 950,           // 区域5蜜蜂数量 (只)
    "number_6": 820,           // 区域6蜜蜂数量 (只)
    "number_7": 890,           // 区域7蜜蜂数量 (只)
    "number_total": 6920,      // 总蜜蜂数量 (只)
    "timestamp": "75020"       // 设备时间戳
  }
}
```

### 🆕 单传感器数据上报格式 (Single Sensor Update)

**重要说明**: 新增功能，支持单独上报特定传感器数据，减少带宽占用和功耗。

#### 重量传感器单独上报
```json
{
  "method": "report",
  "type": "single",           // 必须设置为"single"
  "params": {
    "weight": 15.9,           // 只上报重量数据
    "weight_interval":30,      // 新增：上报间隔(s)
    "timestamp": "75025"
  }
}
```

#### 温湿度传感器上报（可一起或分开）

**温湿度一起上报：**
```json
{
  "method": "report",
  "type": "single",
  "params": {
    "temp": 26.1,             // 温度
    "humidity": 64.8,         // 湿度
    "timestamp": "75030"
  }
}
```

**只上报温度：**
```json
{
  "method": "report",
  "type": "single",
  "params": {
    "temp": 26.1,             // 只有温度
    "temp_interval":30,      // 新增：上报间隔(s)
    "timestamp": "75030"
  }
}
```

**只上报湿度：**
```json
{
  "method": "report",
  "type": "single",
  "params": {
    "humidity": 64.8,         // 只有湿度
    "humi_interval":30,      // 新增：上报间隔(s)
    "timestamp": "75035"
  }
}
```

#### CO2传感器单独上报
```json
{
  "method": "report",
  "type": "single",
  "params": {
    "current_co2": 430,       // CO2浓度
    "co2_interval":30,      // 新增：上报间隔(s)
    "timestamp": "75035"
  }
}
```

#### 蜜蜂数量统计上报
```json
{
  "method": "report",
  "type": "single",
  "params": {
    "number_total": 7150,     // 总蜜蜂数量
    "number_interval": 30,  // 新增：上报间隔(s)
    "timestamp": "75040"
  }
}
```

### 数据规则
- **温度**: -40°C ~ 80°C
- **湿度**: 0% ~ 100%
- **CO2**: 0 ~ 5000 ppm
- **重量**: 0 ~ 100 kg
- **区域蜜蜂数量**: 0 ~ 10000 只 (每个区域)
- **总蜜蜂数量**: 0 ~ 100000 只
- **LAC**: 地区区域码
- **CID**: 基站ID

### 🔄 发送频率建议

#### 完整数据上报 (Full Data Update)
- **频率**: 建议5分钟一次
- **用途**: 系统状态完整快照，设备启动时首次上报

#### 单传感器上报 (Single Sensor Update)
- **重量传感器**: 5-10秒一次 (高频监控)
- **温湿度传感器**: 30秒一次 (中频监控)
- **CO2传感器**: 60秒一次 (低频监控)
- **蜜蜂计数**: 事件触发 (数量变化时上报)

### ✨ 单传感器上报优势

1. **带宽节省**: 减少70-90%的数据传输量
2. **功耗优化**: 降低无线传输功耗，延长电池寿命
3. **实时性提升**: 重要传感器可高频更新
4. **灵活配置**: 不同传感器可设置不同上报频率
5. **成本降低**: 减少蜂窝网络流量费用

## 📥 命令接收 (接收)

### 订阅Topic
```
$thing/down/action/DG647QTNIJ/{设备ID}
```

### 查询命令示例
```json
{
  "method": "get",
  "params": {
    "cmd": "status"
  },
  "msgId": "12345"
}
```

### 设置命令示例
```json
{
  "method": "set", 
  "params": {
    "heater": true,      // 加热器开关
    "fan": false,        // 风扇开关
    "light": true        // 照明开关
  },
  "msgId": "12346"
}
```

## 🔧 Python连接示例

```python
import paho.mqtt.client as mqtt
import json
import time

# 连接配置
BROKER = "119.27.186.189"
PORT = 1883
USERNAME = "admin" 
PASSWORD = "123456"
DEVICE_ID = "你的设备ID"

# 创建客户端
client = mqtt.Client()
client.username_pw_set(USERNAME, PASSWORD)

# 连接回调
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("✅ 连接成功")
        # 订阅命令主题
        client.subscribe(f"$thing/down/action/DG647QTNIJ/{DEVICE_ID}")
    else:
        print(f"❌ 连接失败: {rc}")

# 消息回调
def on_message(client, userdata, msg):
    command = json.loads(msg.payload.decode())
    print(f"📨 收到命令: {command}")

client.on_connect = on_connect
client.on_message = on_message

# 连接服务器
client.connect(BROKER, PORT, 60)
client.loop_start()

# 发送完整遥测数据
def send_full_telemetry():
    data = {
        "method": "report",
        "type": "full",
        "params": {
            "temp": 25.3,
            "humidity": 65.2,
            "current_co2": 420,
            "weight": 15.8,
            "lac": 11,
            "cid": 21,
            "number_0": 850,
            "number_1": 920,
            "number_2": 780,
            "number_3": 660,
            "number_4": 1050,
            "number_5": 950,
            "number_6": 820,
            "number_7": 890,
            "number_total": 6920,
            "timestamp": str(int(time.time()))
        }
    }
    
    topic = f"$thing/up/property/DG647QTNIJ/{DEVICE_ID}"
    client.publish(topic, json.dumps(data))
    print("📤 完整数据已发送")

# 🆕 发送单传感器数据
def send_single_sensor(sensor_type, value):
    data = {
        "method": "report",
        "type": "single",
        "params": {
            "timestamp": str(int(time.time()))
        }
    }
    
    # 根据传感器类型添加对应数据
    if sensor_type == "weight":
        data["params"]["weight"] = value
        print(f"⚖️  重量传感器上报: {value}kg")
    elif sensor_type == "temp":
        data["params"]["temp"] = value
        print(f"🌡️  温度传感器上报: {value}°C")
    elif sensor_type == "humidity":
        data["params"]["humidity"] = value
        print(f"💧 湿度传感器上报: {value}%")
    elif sensor_type == "co2":
        data["params"]["current_co2"] = value
        print(f"🌬️  CO2传感器上报: {value}ppm")
    elif sensor_type == "bee_count":
        data["params"]["number_total"] = value
        print(f"🐝 蜜蜂计数上报: {value}只")
    
    topic = f"$thing/up/property/DG647QTNIJ/{DEVICE_ID}"
    client.publish(topic, json.dumps(data))

# 使用示例
send_full_telemetry()                    # 启动时发送完整数据
time.sleep(2)

# 高频上报重量数据 (每10秒)
send_single_sensor("weight", 15.9)       
time.sleep(10)
send_single_sensor("weight", 16.1)

# 中频上报温湿度 (每30秒)
send_single_sensor("temp", 26.2)
send_single_sensor("humidity", 64.5)

# 低频上报CO2 (每60秒)
send_single_sensor("co2", 435)
```

## 📋 Topic汇总

| 方向 | Topic | 用途 |
|------|-------|------|
| 上行 | `$thing/up/property/DG647QTNIJ/{device_id}` | 硬件发送遥测数据 |
| 下行 | `$thing/down/action/DG647QTNIJ/{device_id}` | 硬件接收控制命令 |
| 上行 | `DG647QTNIJ/{device_id}/image` | 硬件发送图像分片数据 |
| 上行 | `DG647QTNIJ/{device_id}/audio` | 硬件发送音频分片数据 |

## 📸 媒体数据上报 (新增功能)

### 图像数据Topic格式
```
DG647QTNIJ/{设备ID}/image
```

### 音频数据Topic格式
```
DG647QTNIJ/{设备ID}/audio
```

### 媒体分片JSON格式
```json
{
  "frame_id": 12345,              // 当前帧的唯一标识
  "chunk_id": 0,                  // 当前分片编号，从0开始逐增
  "is_last": false,               // 当前分片是否是该帧的最后一片
  "data": "iVBORw0KGgoAAAANSUhEUgAA..." // Base64编码后的图像(JPEG)或音频(PCM)数据子段
}
```

### 分片传输规则
- **frame_id**: 一个完整媒体文件的唯一标识，建议使用时间戳
- **chunk_id**: 分片编号，从0开始递增，服务器会按此顺序重组数据
- **is_last**: 最后一片必须设置为true，服务器据此判断传输完成
- **data**: 媒体数据的Base64编码，每片建议1KB以内
- **图像格式**: 支持JPEG格式
- **音频格式**: 支持PCM格式
- **分片大小**: 建议每片512B-2KB，避免MQTT消息过大

### Python媒体上传示例
```python
import base64
import json
import time

def upload_image_chunks(client, device_id, image_data, chunk_size=1024):
    """上传图像分片"""
    frame_id = int(time.time() * 1000)  # 使用毫秒时间戳作为帧ID
    topic = f"DG647QTNIJ/{device_id}/image"
    
    # 计算分片数量
    total_chunks = (len(image_data) + chunk_size - 1) // chunk_size
    
    for i in range(total_chunks):
        start = i * chunk_size
        end = min(start + chunk_size, len(image_data))
        chunk_data = image_data[start:end]
        
        chunk = {
            "frame_id": frame_id,
            "chunk_id": i,
            "is_last": (i == total_chunks - 1),
            "data": base64.b64encode(chunk_data).decode('utf-8')
        }
        
        client.publish(topic, json.dumps(chunk))
        print(f"📤 发送分片 {i+1}/{total_chunks}")

def upload_audio_chunks(client, device_id, audio_data, chunk_size=512):
    """上传音频分片"""
    frame_id = int(time.time() * 1000)  # 使用毫秒时间戳作为帧ID
    topic = f"DG647QTNIJ/{device_id}/audio"
    
    # 计算分片数量
    total_chunks = (len(audio_data) + chunk_size - 1) // chunk_size
    
    for i in range(total_chunks):
        start = i * chunk_size
        end = min(start + chunk_size, len(audio_data))
        chunk_data = audio_data[start:end]
        
        chunk = {
            "frame_id": frame_id,
            "chunk_id": i,
            "is_last": (i == total_chunks - 1),
            "data": base64.b64encode(chunk_data).decode('utf-8')
        }
        
        client.publish(topic, json.dumps(chunk))
        print(f"🎵 发送音频分片 {i+1}/{total_chunks}")

# 使用示例
with open("beehive_photo.jpg", "rb") as f:
    image_data = f.read()
    upload_image_chunks(client, "BH001", image_data)

with open("beehive_sound.pcm", "rb") as f:
    audio_data = f.read()
    upload_audio_chunks(client, "BH001", audio_data)
```

### 媒体数据测试命令
```bash
# 发布图像分片测试数据
mosquitto_pub -h 119.27.186.189 -p 1883 -u admin -P 123456 \
  -t 'DG647QTNIJ/TEST001/image' \
  -m '{"frame_id":12345,"chunk_id":0,"is_last":true,"data":"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8/5+hHgAHggJ/PchI7wAAAABJRU5ErkJggg=="}'

# 发布音频分片测试数据  
mosquitto_pub -h 119.27.186.189 -p 1883 -u admin -P 123456 \
  -t 'DG647QTNIJ/TEST001/audio' \
  -m '{"frame_id":12346,"chunk_id":0,"is_last":true,"data":"UENNIEFVRElPIERBVEE="}'
```

## 🚨 注意事项

1. **设备ID**: 每个硬件设备需要唯一的ID
2. **认证**: 必须使用用户名密码连接
3. **数据格式**: 严格按照JSON格式发送
4. **频率控制**: 避免过于频繁发送数据
5. **错误处理**: 实现重连机制
6. **单传感器上报**: 必须包含 `"type": "single"` 字段

## 🔍 调试工具

**MQTT客户端工具推荐:**
- MQTTX (桌面版)
- MQTT Explorer
- Mosquitto命令行工具

**测试命令:**
```bash
# 发布完整测试数据
mosquitto_pub -h 119.27.186.189 -p 1883 -u admin -P 123456 \
  -t '$thing/up/property/DG647QTNIJ/TEST001' \
  -m '{"method":"report","type":"full","params":{"temp":25.3,"humidity":65.2,"number_total":6920,"timestamp":"75020"}}'

# 🆕 发布单传感器测试数据
mosquitto_pub -h 119.27.186.189 -p 1883 -u admin -P 123456 \
  -t '$thing/up/property/DG647QTNIJ/TEST001' \
  -m '{"method":"report","type":"single","params":{"weight":15.8,"timestamp":"75020"}}'

# 订阅命令主题
mosquitto_sub -h 119.27.186.189 -p 1883 -u admin -P 123456 \
  -t '$thing/down/action/DG647QTNIJ/TEST001'
```
