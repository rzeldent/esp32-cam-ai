# ESP32-CAM MCP Server - Quick Reference

## 🚀 Quick Start Commands

### Build and Upload
```bash
# Build the project
pio run

# Upload to ESP32-CAM
pio run --target upload

# Monitor serial output
pio device monitor
```

### Basic Configuration
```ini
# platformio.ini
build_flags = 
    -DWIFI_SSID=\"YourWiFiNetwork\"
    -DWIFI_PASSWORD=\"YourPassword\"
    -DLED_GPIO=33
    -DFLASH_GPIO=4
```

## 🔧 MCP Tools Summary

| Tool | Parameters | Description |
|------|------------|-------------|
| `led` | `state`: "on"/"off" | Control built-in LED |
| `flash` | `duration`: 5-100ms | Trigger camera flash |
| `capture` | `flash`: "on"/"off" | Take photo with optional flash |
| `wifi_status` | None | Get network information |
| `system_status` | None | Get system diagnostics |

## 📡 API Examples

### PowerShell
```powershell
# Initialize MCP connection
$init = @{
    jsonrpc = "2024-11-05"
    id = 1
    method = "initialize"
    params = @{
        protocolVersion = "2024-11-05"
        capabilities = @{}
        clientInfo = @{
            name = "PowerShell Client"
            version = "1.0.0"
        }
    }
} | ConvertTo-Json -Depth 10

Invoke-RestMethod -Uri "http://192.168.1.100/" -Method Post -Body $init -ContentType "application/json"

# List available tools
$listTools = @{
    jsonrpc = "2024-11-05"
    id = 2
    method = "tools/list"
} | ConvertTo-Json

Invoke-RestMethod -Uri "http://192.168.1.100/" -Method Post -Body $listTools -ContentType "application/json"

# Turn on LED
$ledOn = @{
    jsonrpc = "2024-11-05"
    id = 3
    method = "tools/call"
    params = @{
        name = "led"
        arguments = @{
            state = "on"
        }
    }
} | ConvertTo-Json -Depth 10

Invoke-RestMethod -Uri "http://192.168.1.100/" -Method Post -Body $ledOn -ContentType "application/json"

# Capture image with flash
$capture = @{
    jsonrpc = "2024-11-05"
    id = 4
    method = "tools/call"
    params = @{
        name = "capture"
        arguments = @{
            flash = "on"
        }
    }
} | ConvertTo-Json -Depth 10

$response = Invoke-RestMethod -Uri "http://192.168.1.100/" -Method Post -Body $capture -ContentType "application/json"

# Save image from response
if ($response.result.content[1].type -eq "image") {
    $imageData = [Convert]::FromBase64String($response.result.content[1].data)
    [System.IO.File]::WriteAllBytes("captured_image.jpg", $imageData)
    Write-Host "Image saved as captured_image.jpg"
}
```

### cURL
```bash
# Initialize
curl -X POST http://192.168.1.100/ \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2024-11-05",
    "id": 1,
    "method": "initialize",
    "params": {
      "protocolVersion": "2024-11-05",
      "capabilities": {},
      "clientInfo": {
        "name": "curl Client",
        "version": "1.0.0"
      }
    }
  }'

# List tools
curl -X POST http://192.168.1.100/ \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2024-11-05",
    "id": 2,
    "method": "tools/list"
  }'

# Turn on LED
curl -X POST http://192.168.1.100/ \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2024-11-05",
    "id": 3,
    "method": "tools/call",
    "params": {
      "name": "led",
      "arguments": {
        "state": "on"
      }
    }
  }'

# Flash for 75ms
curl -X POST http://192.168.1.100/ \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2024-11-05",
    "id": 4,
    "method": "tools/call",
    "params": {
      "name": "flash",
      "arguments": {
        "duration": 75
      }
    }
  }'

# Capture image
curl -X POST http://192.168.1.100/ \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2024-11-05",
    "id": 5,
    "method": "tools/call",
    "params": {
      "name": "capture",
      "arguments": {
        "flash": "on"
      }
    }
  }'

# Get WiFi status
curl -X POST http://192.168.1.100/ \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2024-11-05",
    "id": 6,
    "method": "tools/call",
    "params": {
      "name": "wifi_status",
      "arguments": {}
    }
  }'

# Get system status
curl -X POST http://192.168.1.100/ \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2024-11-05",
    "id": 7,
    "method": "tools/call",
    "params": {
      "name": "system_status",
      "arguments": {}
    }
  }'
```

## 🐍 Python Integration Example

```python
import requests
import json
import base64
from datetime import datetime

class ESP32CamClient:
    def __init__(self, ip_address):
        self.base_url = f"http://{ip_address}/"
        self.request_id = 1
    
    def _make_request(self, method, params=None):
        payload = {
            "jsonrpc": "2024-11-05",
            "id": self.request_id,
            "method": method
        }
        if params:
            payload["params"] = params
        
        self.request_id += 1
        response = requests.post(self.base_url, json=payload)
        return response.json()
    
    def initialize(self):
        return self._make_request("initialize", {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {
                "name": "Python Client",
                "version": "1.0.0"
            }
        })
    
    def list_tools(self):
        return self._make_request("tools/list")
    
    def led_control(self, state):
        return self._make_request("tools/call", {
            "name": "led",
            "arguments": {"state": state}
        })
    
    def flash_control(self, duration=50):
        return self._make_request("tools/call", {
            "name": "flash",
            "arguments": {"duration": duration}
        })
    
    def capture_image(self, use_flash=False, save_path=None):
        result = self._make_request("tools/call", {
            "name": "capture",
            "arguments": {"flash": "on" if use_flash else "off"}
        })
        
        if "result" in result and len(result["result"]["content"]) > 1:
            image_content = result["result"]["content"][1]
            if image_content["type"] == "image":
                image_data = base64.b64decode(image_content["data"])
                
                if save_path:
                    with open(save_path, "wb") as f:
                        f.write(image_data)
                    print(f"Image saved to {save_path}")
                
                return image_data
        
        return None
    
    def wifi_status(self):
        return self._make_request("tools/call", {
            "name": "wifi_status",
            "arguments": {}
        })
    
    def system_status(self):
        return self._make_request("tools/call", {
            "name": "system_status",
            "arguments": {}
        })

# Usage example
if __name__ == "__main__":
    # Create client
    cam = ESP32CamClient("192.168.1.100")
    
    # Initialize connection
    print("Initializing...")
    init_result = cam.initialize()
    print(f"Initialization: {init_result.get('result', {}).get('serverInfo', {}).get('name', 'Unknown')}")
    
    # List available tools
    print("\nAvailable tools:")
    tools = cam.list_tools()
    for tool in tools.get('result', {}).get('tools', []):
        print(f"- {tool['name']}: {tool['description']}")
    
    # Turn on LED
    print("\nTurning on LED...")
    cam.led_control("on")
    
    # Flash the camera
    print("Flashing camera...")
    cam.flash_control(75)
    
    # Capture image
    print("Capturing image...")
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = f"esp32cam_capture_{timestamp}.jpg"
    image_data = cam.capture_image(use_flash=True, save_path=filename)
    
    if image_data:
        print(f"Captured image ({len(image_data)} bytes)")
    
    # Get status information
    print("\nWiFi Status:")
    wifi_info = cam.wifi_status()
    print(json.dumps(wifi_info.get('result', {}), indent=2))
    
    print("\nSystem Status:")
    system_info = cam.system_status()
    print(json.dumps(system_info.get('result', {}), indent=2))
    
    # Turn off LED
    print("\nTurning off LED...")
    cam.led_control("off")
```

## 🔧 Common Troubleshooting

### Camera Issues
```bash
# Check serial output for camera errors
pio device monitor

# Look for these messages:
Camera init failed with error 0x[code]
Camera initialized: Yes/No
```

### WiFi Issues
```bash
# Check WiFi connection status
# Serial output will show:
WiFi connected to: [SSID]
Local IP address: [IP]
```

### Memory Issues
```bash
# Monitor heap usage in serial output:
Free heap: [bytes] bytes
Min free heap: [bytes] bytes
```

## 📝 Configuration Examples

### High Quality Images
```cpp
// In camera_config.h
.frame_size = FRAMESIZE_SVGA,  // 800x600
.jpeg_quality = 8,             // Higher quality (1-63)
.fb_count = 2                  // Frame buffers
```

### Low Memory Usage
```cpp
// In camera_config.h  
.frame_size = FRAMESIZE_QVGA,  // 320x240
.jpeg_quality = 20,            // Lower quality
.fb_count = 1                  // Single buffer
```

### Custom GPIO Pins
```ini
# In platformio.ini
build_flags = 
    -DLED_GPIO=12      # Custom LED pin
    -DFLASH_GPIO=14    # Custom flash pin
```

## 🎯 Integration Tips

### MCP Client Configuration
```json
{
  "mcpServers": {
    "esp32-cam": {
      "type": "http",
      "url": "http://192.168.1.100",
      "timeout": 30000
    }
  }
}
```

### Home Assistant Integration
```yaml
# configuration.yaml
rest_command:
  esp32_capture:
    url: "http://192.168.1.100/"
    method: POST
    headers:
      Content-Type: "application/json"
    payload: |
      {
        "jsonrpc": "2024-11-05",
        "id": 1,
        "method": "tools/call",
        "params": {
          "name": "capture",
          "arguments": {"flash": "on"}
        }
      }
```

## 📊 Performance Optimization

### Reduce Image Size
- Lower JPEG quality (higher number = lower quality)
- Use smaller frame size (QVGA vs SVGA)
- Optimize network conditions

### Improve Reliability
- Use stable 5V power supply
- Ensure strong WiFi signal
- Enable watchdog timer
- Implement retry logic in clients

### Memory Management
- Use single frame buffer for low memory
- Avoid rapid consecutive captures
- Monitor heap usage via system_status tool
