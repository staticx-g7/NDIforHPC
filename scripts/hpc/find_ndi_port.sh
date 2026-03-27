#!/bin/bash
# Find NDI streaming port using netstat/ss

echo "========================================"
echo "NDI Port Detector - Jupiter HPC"
echo "========================================"
echo ""

# Find the ndi_test_streamer process
STREAMER_PID=$(pgrep -f ndi_test_streamer | head -1)

if [ -z "$STREAMER_PID" ]; then
    echo "✗ NDI streamer not found running!"
    echo "  Start it first: ./ndi_test_streamer"
    exit 1
fi

echo "✓ Found streamer process (PID: $STREAMER_PID)"
echo ""

# Try ss first (modern)
if command -v ss &> /dev/null; then
    echo "Checking open ports with ss..."
    echo ""
    
    # Get UDP ports opened by the streamer process
    UDP_PORTS=$(ss -ulnp | grep "$STREAMER_PID" | awk '{print $4}' | grep -v "STATE" | sort -u)
    
    if [ -n "$UDP_PORTS" ]; then
        echo "✓ UDP ports found:"
        echo "$UDP_PORTS" | while read port; do
            echo "  Port: $port"
        done
        echo ""
        
        # Get the first port (likely the main streaming port)
        MAIN_PORT=$(echo "$UDP_PORTS" | head -1)
        MAIN_PORT_NUM=$(echo "$MAIN_PORT" | grep -oE '[0-9]+$')
        
        echo "========================================"
        echo "📡 FOR SSH TUNNELING:"
        echo "========================================"
        echo "Forward this port through SSH tunnel:"
        echo "  ssh -L 6000:localhost:$MAIN_PORT_NUM george2@jupiter"
        echo ""
        echo "On your laptop, NDI receiver will connect to:"
        echo "  localhost:6000"
        echo "========================================"
    else
        echo "⚠️  No UDP ports found for streamer"
    fi
else
    echo "✗ ss command not found"
fi

echo ""
echo "Note: NDI typically uses ports in range 5960-6000"
echo "If no port found, try waiting a few more seconds and run again"
