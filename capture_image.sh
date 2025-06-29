#!/bin/bash
# ESP32-CAM Image Capture Script (Bash/cURL)

ESP32_URL="http://esp32-7c9ebdf16a10.local"

TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
FILENAME="esp32cam_capture_$TIMESTAMP.jpg"

echo "Capturing image from ESP32-CAM..."

# Capture image with flash
RESPONSE=$(curl -s -X POST "$ESP32_URL" \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2024-11-05",
    "id": 1,
    "method": "tools/call",
    "params": {
      "name": "capture",
      "arguments": {
        "flash": "on"
      }
    }
  }')

# Extract base64 image data using jq (requires jq to be installed)
if command -v jq &> /dev/null; then
    IMAGE_DATA=$(echo "$RESPONSE" | jq -r '.result.content[1].data // empty')
    
    if [ ! -z "$IMAGE_DATA" ]; then
        # Decode base64 and save to file
        echo "$IMAGE_DATA" | base64 -d > "$FILENAME"
        echo "Image saved successfully!"
        echo "File: $FILENAME"
        echo "Size: $(wc -c < "$FILENAME") bytes"
    else
        echo "Error: No image data found in response"
        echo "Response: $RESPONSE"
    fi
else
    echo "Error: jq is required but not installed"
    echo "Install jq: https://stedolan.github.io/jq/download/"
    echo "Raw response: $RESPONSE"
fi
