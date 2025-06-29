# ESP32-CAM Image Capture Script
# This script captures an image from the ESP32-CAM and saves it locally

$esp32_url = "http://esp32-7c9ebdf16a10.local"

# Capture image with flash
$capture_request = @{
    jsonrpc = "2024-11-05"
    id = 1
    method = "tools/call"
    params = @{
        name = "capture"
        arguments = @{
            flash = "on"
        }
    }
} | ConvertTo-Json -Depth 10

try {
    Write-Host "Capturing image from ESP32-CAM..."
    $response = Invoke-RestMethod -Uri $esp32_url -Method Post -Body $capture_request -ContentType "application/json" -TimeoutSec 30
    
    if ($response.result -and $response.result.content -and $response.result.content.Length -gt 1) {
        $image_content = $response.result.content[1]
        
        if ($image_content.type -eq "image") {
            # Decode base64 image data
            $image_data = [Convert]::FromBase64String($image_content.data)
            
            # Generate filename with timestamp
            $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
            $filename = "esp32cam_capture_$timestamp.jpg"
            $filepath = Join-Path (Get-Location) $filename
            
            # Save image to file
            [System.IO.File]::WriteAllBytes($filepath, $image_data)
            
            Write-Host "Image saved successfully!" -ForegroundColor Green
            Write-Host "File: $filepath" -ForegroundColor Yellow
            Write-Host "Size: $($image_data.Length) bytes" -ForegroundColor Cyan
            
            # Open the image (optional)
            $open = Read-Host "Would you like to open the image now? (y/n)"
            if ($open -eq "y" -or $open -eq "Y") {
                Start-Process $filepath
            }
        } else {
            Write-Host "Error: Response does not contain image data" -ForegroundColor Red
        }
    } else {
        Write-Host "Error: Invalid response from ESP32-CAM" -ForegroundColor Red
        Write-Host "Response: $($response | ConvertTo-Json -Depth 5)" -ForegroundColor Red
    }
} catch {
    Write-Host "Error capturing image: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Make sure the ESP32-CAM is connected and accessible at $esp32_url" -ForegroundColor Yellow
}
