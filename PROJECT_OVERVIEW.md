# ESP32-CAM MCP Server

A Model Context Protocol (MCP) server implementation for ESP32-CAM that enables remote camera control and system monitoring through standardized HTTP/JSON-RPC API.

## Project Overview

This project transforms an ESP32-CAM module into a remotely controllable camera system using the Model Context Protocol. The server provides HTTP endpoints for camera operations, LED control, and system monitoring, making it suitable for integration with AI assistants and automation systems.

## Key Features

- **Remote Camera Control**: Capture images with optional flash
- **LED Management**: Control built-in LED state
- **System Monitoring**: WiFi status and hardware diagnostics
- **MCP Protocol Compliance**: Standard JSON-RPC 2.0 interface
- **Network Reliability**: Auto-reconnection and watchdog protection

## Hardware Requirements

- ESP32-CAM module (AI-Thinker compatible)
- 5V power supply (minimum 2A recommended)
- WiFi network (2.4GHz)
- FTDI USB-to-Serial adapter (for programming)

## Installation

1. Create a `.env` file in the project root with WiFi credentials:
```properties
WIFI_SSID="YourNetwork"
WIFI_PASSWORD="YourPassword"
```

2. Build and upload:
```bash
pio run --target upload
pio device monitor
```

3. Note the IP address from serial output for API access.

## Available Tools

### LED Control
- **Function**: Control built-in LED
- **Parameters**: `state` ("on" or "off")

### Flash Control  
- **Function**: Trigger camera flash
- **Parameters**: `duration` (5-100ms, default 50ms)

### Image Capture
- **Function**: Capture JPEG image
- **Parameters**: `flash` ("on" or "off", optional)
- **Output**: Base64-encoded JPEG image data
- **Important**: Images are automatically resized to stay below 4KB base64 encoding limit due to AI client data constraints

### WiFi Status
- **Function**: Network connection information
- **Output**: IP address, signal strength, network details

### System Status
- **Function**: Hardware diagnostics
- **Output**: Memory usage, uptime, CPU frequency, camera status

## API Usage

The server accepts HTTP POST requests with JSON-RPC 2.0 format on port 80.

### Basic Request Structure
```json
{
  "jsonrpc": "2024-11-05",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "tool_name",
    "arguments": {}
  }
}
```

### Example: Capture Image
```bash
curl -X POST http://192.168.1.100/ \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2024-11-05",
    "id": 1,
    "method": "tools/call",
    "params": {
      "name": "capture",
      "arguments": {"flash": "on"}
    }
  }'
```

### Example: Control LED
```bash
curl -X POST http://192.168.1.100/ \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2024-11-05",
    "id": 1,
    "method": "tools/call",
    "params": {
      "name": "led",
      "arguments": {"state": "on"}
    }
  }'
```

## Integration Examples

### Python Client
```python
import requests
import base64

def capture_image(esp32_ip):
    payload = {
        "jsonrpc": "2024-11-05",
        "id": 1,
        "method": "tools/call",
        "params": {
            "name": "capture",
            "arguments": {"flash": "on"}
        }
    }
    
    response = requests.post(f"http://{esp32_ip}/", json=payload)
    data = response.json()
    
    if "result" in data:
        image_data = data["result"]["content"][1]["data"]
        with open("image.jpg", "wb") as f:
            f.write(base64.b64decode(image_data))
        return True
    return False
```

### PowerShell
```powershell
$payload = @{
    jsonrpc = "2024-11-05"
    id = 1
    method = "tools/call"
    params = @{
        name = "capture"
        arguments = @{flash = "on"}
    }
} | ConvertTo-Json -Depth 10

Invoke-RestMethod -Uri "http://192.168.1.100/" -Method Post -Body $payload -ContentType "application/json"
```

## Use Cases

- **Security Monitoring**: Remote surveillance with motion-triggered capture
- **Home Automation**: Integration with smart home systems
- **AI Applications**: Vision-based analysis and decision making
- **IoT Projects**: Remote sensor and camera control
- **Educational**: Learning MCP protocol and embedded systems

## Technical Specifications

- **Protocol**: Model Context Protocol 2024-11-05
- **Network**: HTTP server on port 80
- **Image Format**: JPEG with base64 encoding
- **Image Size Limit**: Under 4KB encoded (due to AI client limitations)
- **Memory Usage**: ~200KB free heap during operation
- **Response Time**: <500ms for control commands, 2-3s for image capture

## Configuration Options

### Camera Quality Settings
- **Resolution**: QVGA (320x240) recommended for size constraints
- **JPEG Quality**: 12-20 (lower number = higher quality)
- **Frame Buffers**: 1-2 buffers based on memory availability

### Network Settings
- **Reconnection**: Automatic with 30-second intervals
- **Watchdog**: 10-second timeout protection
- **mDNS**: Automatic hostname assignment

## Troubleshooting

### Common Issues
- **Camera Init Failed**: Check power supply (5V required)
- **WiFi Connection**: Verify credentials and 2.4GHz network
- **Memory Errors**: Reduce image quality or frame buffer count
- **Large Images**: Adjust JPEG quality to stay under 4KB limit

### Debug Information
Monitor serial output at 115200 baud for:
- WiFi connection status
- Camera initialization results
- Memory usage statistics
- Error messages and codes

## Architecture

The system consists of:
- **MCP Protocol Handler**: JSON-RPC request processing
- **Tool Handlers**: Individual function implementations
- **Camera Manager**: Image capture and encoding
- **Network Layer**: WiFi management and HTTP server
- **Hardware Abstraction**: GPIO and peripheral control

## Security Notes

- **No Authentication**: Open HTTP server (add custom auth if needed)
- **Local Network**: Device accessible to all network clients
- **Unencrypted**: Plain HTTP communication
- **Physical Access**: Secure device placement for intended use

## Performance Optimization

- Use QVGA resolution to minimize data size
- Adjust JPEG quality based on use case requirements
- Monitor memory usage via system status tool
- Ensure stable power supply for reliable operation
