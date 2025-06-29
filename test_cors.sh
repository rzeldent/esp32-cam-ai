#!/bin/bash
# Test CORS headers on ESP32-CAM MCP server

ESP32_URL="http://esp32-7c9ebdf16a10.local"

echo "Testing CORS headers on ESP32-CAM MCP server..."
echo "=========================================="

# Test OPTIONS preflight request
echo "1. Testing OPTIONS (preflight) request:"
curl -i -X OPTIONS "$ESP32_URL" \
  -H "Origin: http://localhost:3000" \
  -H "Access-Control-Request-Method: POST" \
  -H "Access-Control-Request-Headers: Content-Type"

echo -e "\n\n2. Testing POST request with CORS headers:"
curl -i -X POST "$ESP32_URL" \
  -H "Content-Type: application/json" \
  -H "Origin: http://localhost:3000" \
  -d '{
    "jsonrpc": "2024-11-05",
    "id": 1,
    "method": "initialize",
    "params": {
      "protocolVersion": "2024-11-05",
      "capabilities": {},
      "clientInfo": {
        "name": "CORS Test Client",
        "version": "1.0.0"
      }
    }
  }'

echo -e "\n\nExpected CORS headers in response:"
echo "- Access-Control-Allow-Origin: *"
echo "- Access-Control-Allow-Methods: POST, OPTIONS"
echo "- Access-Control-Allow-Headers: Content-Type, Authorization"
echo "- Access-Control-Max-Age: 86400"
