#!/bin/bash

# Script to show Firmware API quota details
# Usage: ./show_quota.sh [API_KEY]

# Get API key from argument or environment variable
API_KEY="${1:-$FIRMWARE_API_KEY}"

if [ -z "$API_KEY" ]; then
    echo "Error: API key not provided. Set FIRMWARE_API_KEY environment variable or pass as argument."
    echo "Usage: $0 [API_KEY]"
    exit 1
fi

# Extract token: if API key starts with "fw_api_", use the part after it
if [[ "$API_KEY" == fw_api_* ]]; then
    TOKEN="${API_KEY#fw_api_}"
else
    TOKEN="$API_KEY"
fi

# Try different auth methods
# Method 1: Bearer with full key
response=$(curl -s -H "Authorization: Bearer $API_KEY" https://app.firmware.ai/api/v1/quota)

# Only try other methods if unauthorized
if echo "$response" | grep -q "Unauthorized"; then
    # Method 2: Bearer with extracted token
    response=$(curl -s -H "Authorization: Bearer $TOKEN" https://app.firmware.ai/api/v1/quota)
    
    if echo "$response" | grep -q "Unauthorized"; then
        # Method 3: X-API-Key header
        response=$(curl -s -H "X-API-Key: $API_KEY" https://app.firmware.ai/api/v1/quota)
        
        if echo "$response" | grep -q "Unauthorized"; then
            # Method 4: Authorization without Bearer
            response=$(curl -s -H "Authorization: $API_KEY" https://app.firmware.ai/api/v1/quota)
            
            # Final check after all methods
            if echo "$response" | grep -q "Unauthorized"; then
                echo "Error: Unauthorized after trying all auth methods. Raw response:"
                echo "$response"
                exit 1
            fi
        fi
    fi
fi

# Parse and display the quota
used=$(echo "$response" | jq -r '.used')
reset=$(echo "$response" | jq -r '.reset')

if [ "$used" = "null" ] || [ -z "$used" ]; then
    echo "Failed to parse response. Raw response:"
    echo "$response"
    exit 1
fi

# Calculate percentage
percentage=$(echo "scale=2; $used * 100" | bc 2>/dev/null || echo "N/A")

echo "Firmware API Quota Details:"
echo "=========================="
echo "Used: $percentage% ($used)"
if [ "$reset" != "null" ] && [ -n "$reset" ]; then
    # Convert ISO timestamp to readable format if possible
    if command -v date >/dev/null 2>&1; then
        reset_readable=$(date -d "$reset" '+%Y-%m-%d %H:%M:%S %Z' 2>/dev/null || echo "$reset")
        echo "Reset: $reset_readable"
    else
        echo "Reset: $reset"
    fi
else
    echo "Reset: No active window (quota not used recently)"
fi