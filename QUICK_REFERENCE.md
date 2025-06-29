# ESP32-CAM MCP Server - Quick Reference

## Quick Start Commands

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

Create a `.env` file in the project root:

```properties
# .env file
WIFI_SSID="YourWiFiNetwork"
WIFI_PASSWORD="YourPassword"
```

**Note**: The `.env` file is required for building. GPIO pins are pre-configured in `platformio.ini`.

### Camera Orientation

**Important**: Position the ESP32-CAM with the **flash LED facing downward** for optimal image capture:

- Provides proper subject lighting
- Prevents flash reflection
- Natural illumination direction
- Expected perspective for most use cases

## MCP Tools Summary

| Tool | Parameters | Description |
|------|------------|-------------|
| `led` | `state`: "on"/"off" | Control built-in LED |
| `flash` | `duration`: 5-100ms | Trigger camera flash |
| `capture` | `flash`: "on"/"off" | Take photo with optional flash |
| `wifi_status` | None | Get network information |
| `system_status` | None | Get system diagnostics |

## System Status Parameters

The `system_status` tool returns comprehensive diagnostic information about the ESP32-CAM:

### Hardware Information

- **CPU Frequency**: Operating frequency in MHz (typically 240 MHz)
- **Flash Size**: Total flash memory size in bytes (typically 4,194,304 bytes = 4MB)
- **Flash Speed**: Flash memory clock speed in Hz (typically 40,000,000 Hz = 40MHz)
- **Internal Temperature**: ESP32 chip temperature in Celsius (normal range: 40-75°C)

### Memory Statistics

- **Free Heap**: Currently available RAM in bytes
- **Min Free Heap**: Minimum free heap recorded since boot (indicates memory pressure)
- **Max Alloc Heap**: Maximum contiguous memory block that can be allocated
- **Sketch Size**: Size of the compiled firmware in bytes
- **Free Sketch Space**: Remaining flash space for firmware updates

### System Status

- **Uptime**: Time since last boot/restart in seconds
- **SDK Version**: ESP-IDF framework version (e.g., v4.4.7-dirty)
- **Reset Reason**: Code indicating why the system last restarted:
  - `1`: Power-on reset (normal startup)
  - `2`: External reset (reset button)
  - `3`: Software reset (ESP.restart())
  - `12`: Brownout reset (power supply issue)
  - `14`: RTC watchdog reset

### Camera Status

- **Camera Initialized**: Shows "Yes" if camera is working, "No" with error code if failed
  - Success: "Yes" (camera ready for use)
  - Failure: "No (code = 0x[hex])" where hex codes indicate specific camera errors

### Temperature Monitoring

- **Normal Operation**: 40-60°C during typical use
- **Heavy Load**: 60-75°C during intensive operations (camera capture, WiFi activity)
- **Warning Zone**: Above 75°C (check ventilation and power supply)
- **Critical**: Above 85°C (automatic thermal protection may activate)

### Memory Health Indicators

- **Free Heap > 100KB**: Healthy memory state
- **Free Heap 50-100KB**: Moderate memory usage
- **Free Heap < 50KB**: High memory pressure (may cause instability)
- **Min Free Heap**: Should remain above 30KB for stable operation

## API Examples

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

## Python Integration Example

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

## Common Troubleshooting

### Camera Orientation Issues

**Problem**: Poor image quality, unexpected lighting, or flash reflection
**Solution**: Ensure ESP32-CAM is positioned with **flash LED facing downward**

- Camera lens should face the subject
- Small flash LED (next to lens) should point toward ground/subject
- This provides natural lighting and prevents reflection issues

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

## Configuration Examples

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

## Integration Tips

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

## Visual Studio Code Integration

### MCP Configuration for VS Code

1. **Install MCP Extension**
   - Install the Model Context Protocol extension from the VS Code marketplace
   - Or use VS Code with built-in MCP support

2. **Configure MCP Server**
   Create or update your MCP configuration file:

**Windows**: `%APPDATA%\Code\User\mcp_servers.json`
**macOS**: `~/Library/Application Support/Code/User/mcp_servers.json`
**Linux**: `~/.config/Code/User/mcp_servers.json`

```json
{
  "mcpServers": {
    "esp32-cam": {
      "command": "node",
      "args": ["-e", "require('http').createServer((req,res)=>{let body='';req.on('data',d=>body+=d);req.on('end',()=>{fetch('http://192.168.1.100/',{method:'POST',headers:{'Content-Type':'application/json'},body}).then(r=>r.text()).then(t=>{res.writeHead(200,{'Content-Type':'application/json'});res.end(t)}).catch(e=>{res.writeHead(500);res.end(JSON.stringify({error:e.message}))})})}).listen(3001)"],
      "env": {
        "ESP32_CAM_IP": "192.168.1.100"
      }
    }
  }
}
```

**Alternative: Direct HTTP MCP Server**
   For simpler setup, configure direct HTTP access:

```json
{
  "mcpServers": {
    "esp32-cam": {
      "type": "http",
      "url": "http://192.168.1.100",
      "timeout": 30000,
      "headers": {
        "Content-Type": "application/json"
      }
    }
  }
}
```

### Using ESP32-CAM in VS Code

1. **Open Command Palette** (`Ctrl+Shift+P` / `Cmd+Shift+P`)

2. **Access MCP Tools**
   - Type "MCP" to see available MCP commands
   - Select "MCP: Call Tool" to access ESP32-CAM functions

3. **Available MCP Tools in VS Code:**
   - **LED Control**: Turn camera LED on/off
   - **Flash Control**: Trigger camera flash with custom duration
   - **Image Capture**: Take photos with optional flash
   - **WiFi Status**: Check network connectivity
   - **System Status**: Monitor ESP32-CAM health

### VS Code Workspace Configuration

Create `.vscode/settings.json` in your project:

```json
{
  "mcp.servers": {
    "esp32-cam": {
      "type": "http",
      "url": "http://192.168.1.100",
      "displayName": "ESP32-CAM Controller",
      "description": "Remote camera control and image capture"
    }
  },
  "mcp.autoConnect": true,
  "mcp.timeout": 30000
}
```

### VS Code Tasks for ESP32-CAM

Create `.vscode/tasks.json` for common operations:

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "ESP32-CAM: Take Photo",
      "type": "shell",
      "command": "curl",
      "args": [
        "-X", "POST",
        "http://192.168.1.100/",
        "-H", "Content-Type: application/json",
        "-d", "{\"jsonrpc\":\"2024-11-05\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"capture\",\"arguments\":{\"flash\":\"on\"}}}"
      ],
      "group": "build",
      "presentation": {
        "echo": true,
        "reveal": "always",
        "focus": false,
        "panel": "shared"
      },
      "problemMatcher": []
    },
    {
      "label": "ESP32-CAM: LED On",
      "type": "shell",
      "command": "curl",
      "args": [
        "-X", "POST",
        "http://192.168.1.100/",
        "-H", "Content-Type: application/json",
        "-d", "{\"jsonrpc\":\"2024-11-05\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"led\",\"arguments\":{\"state\":\"on\"}}}"
      ],
      "group": "build"
    },
    {
      "label": "ESP32-CAM: LED Off",
      "type": "shell",
      "command": "curl",
      "args": [
        "-X", "POST",
        "http://192.168.1.100/",
        "-H", "Content-Type: application/json",
        "-d", "{\"jsonrpc\":\"2024-11-05\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"led\",\"arguments\":{\"state\":\"off\"}}}"
      ],
      "group": "build"
    },
    {
      "label": "ESP32-CAM: Status Check",
      "type": "shell",
      "command": "curl",
      "args": [
        "-X", "POST", 
        "http://192.168.1.100/",
        "-H", "Content-Type: application/json",
        "-d", "{\"jsonrpc\":\"2024-11-05\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"system_status\",\"arguments\":{}}}"
      ],
      "group": "test"
    }
  ]
}
```

### VS Code Snippets

Create `.vscode/esp32cam.code-snippets`:

```json
{
  "ESP32-CAM MCP Call": {
    "prefix": "esp32-mcp",
    "body": [
      "{",
      "  \"jsonrpc\": \"2024-11-05\",",
      "  \"id\": ${1:1},",
      "  \"method\": \"tools/call\",",
      "  \"params\": {",
      "    \"name\": \"${2|led,flash,capture,wifi_status,system_status|}\",",
      "    \"arguments\": {",
      "      $3",
      "    }",
      "  }",
      "}"
    ],
    "description": "ESP32-CAM MCP tool call template"
  },
  "ESP32-CAM LED Control": {
    "prefix": "esp32-led",
    "body": [
      "{",
      "  \"jsonrpc\": \"2024-11-05\",",
      "  \"id\": ${1:1},",
      "  \"method\": \"tools/call\",",
      "  \"params\": {",
      "    \"name\": \"led\",",
      "    \"arguments\": {",
      "      \"state\": \"${2|on,off|}\"",
      "    }",
      "  }",
      "}"
    ],
    "description": "ESP32-CAM LED control"
  },
  "ESP32-CAM Capture": {
    "prefix": "esp32-capture",
    "body": [
      "{",
      "  \"jsonrpc\": \"2024-11-05\",",
      "  \"id\": ${1:1},",
      "  \"method\": \"tools/call\",",
      "  \"params\": {",
      "    \"name\": \"capture\",",
      "    \"arguments\": {",
      "      \"flash\": \"${2|on,off|}\"",
      "    }",
      "  }",
      "}"
    ],
    "description": "ESP32-CAM image capture"
  }
}
```

### Using with VS Code Extensions

#### REST Client Extension

Create `test-esp32cam.http`:

```http
### Initialize MCP Connection
POST http://192.168.1.100/
Content-Type: application/json

{
  "jsonrpc": "2024-11-05",
  "id": 1,
  "method": "initialize",
  "params": {
    "protocolVersion": "2024-11-05",
    "capabilities": {},
    "clientInfo": {
      "name": "VS Code REST Client",
      "version": "1.0.0"
    }
  }
}

### List Available Tools
POST http://192.168.1.100/
Content-Type: application/json

{
  "jsonrpc": "2024-11-05",
  "id": 2,
  "method": "tools/list"
}

### Turn LED On
POST http://192.168.1.100/
Content-Type: application/json

{
  "jsonrpc": "2024-11-05",
  "id": 3,
  "method": "tools/call",
  "params": {
    "name": "led",
    "arguments": {
      "state": "on"
    }
  }
}

### Capture Image with Flash
POST http://192.168.1.100/
Content-Type: application/json

{
  "jsonrpc": "2024-11-05",
  "id": 4,
  "method": "tools/call",
  "params": {
    "name": "capture",
    "arguments": {
      "flash": "on"
    }
  }
}

### Get System Status
POST http://192.168.1.100/
Content-Type: application/json

{
  "jsonrpc": "2024-11-05",
  "id": 5,
  "method": "tools/call",
  "params": {
    "name": "system_status",
    "arguments": {}
  }
}
```

### Debugging in VS Code

1. **Enable Serial Monitor**
   - Install PlatformIO extension
   - Use `Ctrl+Shift+P` → "PlatformIO: Serial Monitor"
   - Monitor ESP32-CAM debug output

2. **Network Debugging**
   - Use Developer Tools (`F12`) in VS Code
   - Monitor MCP requests/responses
   - Check network connectivity to ESP32-CAM

3. **Common Issues**
   - **Connection refused**: Check ESP32-CAM IP address
   - **Timeout errors**: Increase MCP timeout settings
   - **Invalid JSON**: Validate request format
   - **Camera errors**: Check serial output for hardware issues

### VS Code Keybindings

Add to `keybindings.json`:

```json
[
  {
    "key": "ctrl+alt+c",
    "command": "workbench.action.tasks.runTask",
    "args": "ESP32-CAM: Take Photo"
  },
  {
    "key": "ctrl+alt+l",
    "command": "workbench.action.tasks.runTask", 
    "args": "ESP32-CAM: LED On"
  },
  {
    "key": "ctrl+alt+shift+l",
    "command": "workbench.action.tasks.runTask",
    "args": "ESP32-CAM: LED Off"
  }
]
```

**Note**: Images captured are automatically optimized to stay below 4KB base64 encoding for compatibility with AI clients and VS Code MCP integrations.

### Browser Integration Support

The ESP32-CAM MCP server includes **CORS (Cross-Origin Resource Sharing)** headers, enabling direct access from web browsers and browser-based applications:

- **Access-Control-Allow-Origin**: `*` (allows all origins)
- **Access-Control-Allow-Methods**: `POST, OPTIONS`
- **Access-Control-Allow-Headers**: `Content-Type, Authorization`
- **Preflight Requests**: Automatic OPTIONS method handling

This enables direct HTTP requests from JavaScript running in web browsers, making the ESP32-CAM accessible from web applications without proxy servers.

### Testing CORS Support

#### PowerShell CORS Test

```powershell
# Run the CORS test script
.\test_cors.ps1

# Expected output should show:
# - OPTIONS request success (HTTP 200)
# - CORS headers present in response
# - POST request success with MCP response
# - Browser compatibility confirmed
```

#### Manual CORS Test

```powershell
# Test OPTIONS preflight request
$headers = @{
    'Origin' = 'http://localhost:3000'
    'Access-Control-Request-Method' = 'POST'
    'Access-Control-Request-Headers' = 'Content-Type'
}
Invoke-WebRequest -Uri "http://esp32-7c9ebdf16a10.local" -Method Options -Headers $headers
```
