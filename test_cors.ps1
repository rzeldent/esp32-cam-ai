# ESP32-CAM CORS Test Script
# This script tests CORS (Cross-Origin Resource Sharing) headers on the ESP32-CAM MCP server

$esp32_url = "http://esp32-7c9ebdf16a10.local"

Write-Host "Testing CORS headers on ESP32-CAM MCP server..." -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

# Test 1: OPTIONS preflight request
Write-Host ""
Write-Host "1. Testing OPTIONS (preflight) request:" -ForegroundColor Yellow

try {
    $options_headers = @{
        'Origin' = 'http://localhost:3000'
        'Access-Control-Request-Method' = 'POST'
        'Access-Control-Request-Headers' = 'Content-Type'
    }
    
    Write-Host "Sending OPTIONS request with headers:" -ForegroundColor Gray
    $options_headers.GetEnumerator() | ForEach-Object { Write-Host "  $($_.Key): $($_.Value)" -ForegroundColor Gray }
    
    $options_response = Invoke-WebRequest -Uri $esp32_url -Method Options -Headers $options_headers -TimeoutSec 10
    
    Write-Host ""
    Write-Host "OPTIONS Response:" -ForegroundColor Green
    Write-Host "Status Code: $($options_response.StatusCode)" -ForegroundColor Green
    Write-Host "Status Description: $($options_response.StatusDescription)" -ForegroundColor Green
    
    Write-Host ""
    Write-Host "CORS Headers in Response:" -ForegroundColor Green
    $cors_headers = @(
        'Access-Control-Allow-Origin',
        'Access-Control-Allow-Methods', 
        'Access-Control-Allow-Headers',
        'Access-Control-Max-Age'
    )
    
    $found_cors_headers = $false
    foreach ($header in $cors_headers) {
        if ($options_response.Headers[$header]) {
            Write-Host "  ${header}: $($options_response.Headers[$header])" -ForegroundColor Green
            $found_cors_headers = $true
        }
    }
    
    if (-not $found_cors_headers) {
        Write-Host "  No CORS headers found in response" -ForegroundColor Red
    }
    
} catch {
    Write-Host "OPTIONS request failed: $($_.Exception.Message)" -ForegroundColor Red
}

# Test 2: POST request with Origin header
Write-Host ""
Write-Host "2. Testing POST request with CORS headers:" -ForegroundColor Yellow

$post_request = @{
    jsonrpc = "2024-11-05"
    id = 1
    method = "initialize"
    params = @{
        protocolVersion = "2024-11-05"
        capabilities = @{}
        clientInfo = @{
            name = "CORS Test Client"
            version = "1.0.0"
        }
    }
} | ConvertTo-Json -Depth 10

try {
    $post_headers = @{
        'Content-Type' = 'application/json'
        'Origin' = 'http://localhost:3000'
    }
    
    Write-Host "Sending POST request with Origin header..." -ForegroundColor Gray
    
    $post_response = Invoke-WebRequest -Uri $esp32_url -Method Post -Body $post_request -Headers $post_headers -TimeoutSec 30
    
    Write-Host ""
    Write-Host "POST Response:" -ForegroundColor Green
    Write-Host "Status Code: $($post_response.StatusCode)" -ForegroundColor Green
    Write-Host "Status Description: $($post_response.StatusDescription)" -ForegroundColor Green
    
    Write-Host ""
    Write-Host "CORS Headers in POST Response:" -ForegroundColor Green
    $found_cors_headers = $false
    foreach ($header in $cors_headers) {
        if ($post_response.Headers[$header]) {
            Write-Host "  ${header}: $($post_response.Headers[$header])" -ForegroundColor Green
            $found_cors_headers = $true
        }
    }
    
    if (-not $found_cors_headers) {
        Write-Host "  No CORS headers found in POST response" -ForegroundColor Red
    }
    
    # Parse JSON response
    try {
        $json_response = $post_response.Content | ConvertFrom-Json
        Write-Host ""
        Write-Host "MCP Response Content:" -ForegroundColor Green
        Write-Host "  Protocol Version: $($json_response.result.protocolVersion)" -ForegroundColor Green
        Write-Host "  Server Name: $($json_response.result.serverInfo.name)" -ForegroundColor Green
        Write-Host "  Server Version: $($json_response.result.serverInfo.version)" -ForegroundColor Green
    } catch {
        Write-Host "Could not parse JSON response" -ForegroundColor Yellow
    }
    
} catch {
    Write-Host "POST request failed: $($_.Exception.Message)" -ForegroundColor Red
}

# Test 3: Verify expected CORS configuration
Write-Host ""
Write-Host "3. CORS Configuration Analysis:" -ForegroundColor Yellow

$expected_cors = @{
    'Access-Control-Allow-Origin' = '*'
    'Access-Control-Allow-Methods' = 'POST, OPTIONS'
    'Access-Control-Allow-Headers' = 'Content-Type, Authorization'
    'Access-Control-Max-Age' = '86400'
}

Write-Host ""
Write-Host "Expected CORS Headers:" -ForegroundColor Cyan
$expected_cors.GetEnumerator() | ForEach-Object { 
    Write-Host "  $($_.Key): $($_.Value)" -ForegroundColor Cyan 
}

# Test 4: Browser compatibility test
Write-Host ""
Write-Host "4. Browser Compatibility Assessment:" -ForegroundColor Yellow

if ($found_cors_headers) {
    Write-Host "Success - CORS headers are present - Browser access should work" -ForegroundColor Green
    Write-Host "Success - Web applications can make direct requests to ESP32-CAM" -ForegroundColor Green
    Write-Host "Success - No proxy server required for browser-based clients" -ForegroundColor Green
    
    Write-Host ""
    Write-Host "Recommended browser test:" -ForegroundColor Cyan
    Write-Host "1. Open browser_controller.html in a web browser" -ForegroundColor White
    Write-Host "2. Verify the ESP32-CAM URL is correct" -ForegroundColor White
    Write-Host "3. Click 'Initialize MCP' to test connectivity" -ForegroundColor White
    Write-Host "4. Try other functions like LED control and image capture" -ForegroundColor White
} else {
    Write-Host "Error - CORS headers missing - Browser access may be blocked" -ForegroundColor Red
    Write-Host "Error - Web applications will encounter CORS errors" -ForegroundColor Red
    Write-Host "Error - Proxy server may be required for browser-based clients" -ForegroundColor Red
}

Write-Host ""
Write-Host "CORS Test Complete!" -ForegroundColor Cyan
