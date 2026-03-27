# NDI Streaming from Jupiter HPC to Laptop

## ⚠️ Prerequisites - NDI SDK Required

**Before following this guide, you MUST download and install the NDI SDK:**

### Download NDI SDK for Linux

1. **Download:**
   - Visit: https://ndi.dev/ndi-ndi-sdk-for-linux
   - Download: "NDI SDK for Linux" (latest version, v6.x)
   - Extract to: `~/Downloads/NDI SDK for Linux/` (recommended)

2. **Why NOT included in repo:**
   - License restrictions (commercial SDK from Vizrt)
   - Large size (~100MB+)
   - Architecture-specific (ARM64 for Jupiter, x86_64 for laptop)

3. **Update paths in scripts:**
   - **Laptop default:** `~/Downloads/NDI SDK for Linux/`
   - **Jupiter default:** `~/NDI/NDI SDK for Linux/`
   - If you install elsewhere, update `NDI_PATH` in all scripts

---

## Overview

Successfully achieved **NDI video streaming from Jupiter HPC (ARM64) to laptop (x86_64)** through SSH tunnel using **direct port forwarding** (no discovery server needed).

## Architecture

```
┌─────────────────────────────────────────┐
│        JUPITER HPC (login01)            │
│           ARM64 / Neoverse-V2           │
├─────────────────────────────────────────┤
│                                         │
│  ndi_test_streamer                      │
│  ├─ Port 5959 (TCP) - Discovery         │
│  ├─ Port 5960 (TCP) - NDI Service       │
│  └─ Port 5961 (TCP) - VIDEO STREAM      │
│                                         │
└─────────────────────────────────────────┘
               │
               │ SSH Tunnel
               │ -L 5959:localhost:5959
               │ -L 5960:localhost:5960
               │ -L 5961:localhost:5961
               │
┌─────────────────────────────────────────┐
│            LAPTOP (x86_64)              │
├─────────────────────────────────────────┤
│                                         │
│  ndi_direct_viewer                      │
│  └─ Connects to localhost:5961          │
│     → Displays video in window          │
│                                         │
└─────────────────────────────────────────┘
```

## Results

✅ **Working:**
- NDI streamer on Jupiter: ~20 FPS (1920x1080)
- Direct SSH port forwarding (no discovery server)
- GUI viewer on laptop showing real-time video
- Moving color bars test pattern visible

**Performance:**
- Resolution: 1920x1080 (1080p)
- Frame Rate: ~19-20 FPS
- Format: UYVY (YUV 4:2:2)
- Latency: ~100-200ms through SSH

---

## Essential Files

### Jupiter HPC
```
~/NDI/Scripts/hpc/
├── ndi_test_streamer.cpp    # Streamer source
├── ndi_test_streamer        # Compiled binary (ARM64)
└── build_ndi_test.sh        # Build script
```

### Laptop
```
/home/staticxg7/Desktop/Github/UDPTunnel/scripts/hpc/
├── ndi_test_streamer.cpp    # Source (for local testing)
├── ndi_test_streamer        # Binary (x86_64)
├── ndi_direct_viewer.cpp    # Viewer source
├── ndi_direct_viewer        # Viewer binary
├── ndi_frame_saver.cpp      # Frame saver source (optional)
├── ndi_frame_saver          # Frame saver binary (optional)
├── build_ndi_test.sh        # Build script
└── find_ndi_port.sh         # Helper script
```

---

## Quick Start Guide

### Step 1: Build on Jupiter

```bash
# SSH to Jupiter
ssh <your_username>@login01.jupiter.fz-juelich.de

# Navigate to directory
cd ~/NDI/Scripts/hpc

# Set NDI SDK path
export NDI_PATH="~/NDI/NDI SDK for Linux"

# Compile
./build_ndi_test.sh
```

### Step 2: Run Streamer on Jupiter

```bash
# Set library path
export LD_LIBRARY_PATH="$NDI_PATH/lib/aarch64-rpi4-linux-gnueabi:$LD_LIBRARY_PATH"

# Run streamer
./ndi_test_streamer
```

**Expected output:**
```
✓ NDI initialized successfully
✓ NDI sender created: Jupiter Test Stream
Frame 30 | FPS: 19.81
Frame 60 | FPS: 19.60
...
```

**Keep this terminal running!**

### Step 3: Find Streaming Ports on Jupiter

```bash
# Find ports used by streamer
ss -tulnp | grep ndi_test_stream
```

**Look for output like:**
```
tcp  LISTEN  0  128  0.0.0.0:5960  0.0.0.0:*  users:(("ndi_test_stream",pid=XXXX,fd=5))
tcp  LISTEN  0  128  0.0.0.0:5961  0.0.0.0:*  users:(("ndi_test_stream",pid=XXXX,fd=10))
```

**Note the ports:**
- **5959** - Discovery port (forward this - needed for NDI)
- **5960** - NDI Service port (forward this)
- **5961** - Video streaming port (forward this - MAIN VIDEO)

### Step 4: Create SSH Tunnel from Laptop

```bash
# On laptop (forward all 3 NDI ports)
ssh -L 5959:localhost:5959 \
    -L 5960:localhost:5960 \
    -L 5961:localhost:5961 \
    -i ~/.ssh/your_ssh_key \
    <your_username>@login01.jupiter.fz-juelich.de

# Note: You can use login01-login10, but login01 is recommended for consistency
```

**Keep this terminal open!**

**Verify tunnel:**
```bash
ss -tulnp | grep -E "5960|5961"
```

Should show both ports listening on localhost.

### Step 5: View Stream on Laptop

```bash
# Navigate to scripts
cd /home/staticxg7/Desktop/Github/UDPTunnel/scripts/hpc

# Set library path
export LD_LIBRARY_PATH="/home/staticxg7/Downloads/NDI SDK for Linux/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"

# Run viewer
./ndi_direct_viewer
```

**Expected output:**
```
✓ NDI initialized
✓ Connected to: localhost:5961
```

**A window should appear with the moving color bars video!**

---

## Alternative: Frame Saver

If GUI viewer has issues, use frame saver:

```bash
# Note: Requires discovery or direct connection setup
./ndi_frame_saver
```

Saves frames as PNG files (frame_30.png, frame_60.png, etc.)

---

## Port Mapping

| Jupiter | Laptop | Purpose | Forwarded? |
|---------|--------|---------|------------|
| 5959 | 5959 | NDI Discovery | ✅ Yes |
| 5960 | 5960 | NDI Service | ✅ Yes |
| 5961 | 5961 | **Video Stream** | ✅ Yes |

**Note:** All three ports must be forwarded for the NDI streamer to work properly with the direct viewer.

---

## Build Instructions (Laptop)

### Install Dependencies
```bash
sudo apt install libopencv-dev
```

### Compile Viewer
```bash
cd /home/staticxg7/Desktop/Github/UDPTunnel/scripts/hpc

g++ -std=c++17 ndi_direct_viewer.cpp \
    -I"/home/staticxg7/Downloads/NDI SDK for Linux/include" \
    -L"/home/staticxg7/Downloads/NDI SDK for Linux/lib/x86_64-linux-gnu" \
    -lndi \
    $(pkg-config --cflags --libs opencv4) \
    -o ndi_direct_viewer \
    -Wl,-rpath,"/home/staticxg7/Downloads/NDI SDK for Linux/lib/x86_64-linux-gnu" \
    -lpthread
```

### Compile Frame Saver (Optional)
```bash
g++ -std=c++17 ndi_frame_saver.cpp \
    -I"/home/staticxg7/Downloads/NDI SDK for Linux/include" \
    -L"/home/staticxg7/Downloads/NDI SDK for Linux/lib/x86_64-linux-gnu" \
    -lndi \
    $(pkg-config --cflags --libs opencv4) \
    -o ndi_frame_saver \
    -Wl,-rpath,"/home/staticxg7/Downloads/NDI SDK for Linux/lib/x86_64-linux-gnu" \
    -lpthread
```

---

## Troubleshooting

### "Connection refused" on viewer
- **Cause:** SSH tunnel not active or streamer not running
- **Fix:** 
  1. Check streamer: `ps aux | grep ndi_test_streamer` (on Jupiter)
  2. Check tunnel: `ss -tulnp | grep 5961` (on laptop)
  3. Re-establish SSH tunnel

### Can't find ports on Jupiter
```bash
# Alternative commands to find ports
netstat -tulnp | grep ndi_test_stream
# or
ss -tulnp | grep <STREAMER_PID>
```

### Low FPS (< 15)
- **Cause:** Network or CPU limitations
- **Fix:** Reduce resolution in `ndi_test_streamer.cpp`:
  ```cpp
  const int width = 1280;  // Change from 1920
  const int height = 720;   // Change from 1080
  ```

### Viewer crashes
- **Cause:** OpenCV/display issues
- **Fix:** Reinstall OpenCV:
  ```bash
  sudo apt install libopencv-dev
  ```

### "No such file or directory"
- **Cause:** Missing NDI library in LD_LIBRARY_PATH
- **Fix:** Set correct path:
  ```bash
  # Jupiter (ARM64)
  export LD_LIBRARY_PATH="~/NDI/NDI SDK for Linux/lib/aarch64-rpi4-linux-gnueabi:$LD_LIBRARY_PATH"
  
  # Laptop (x86_64)
  export LD_LIBRARY_PATH="/home/staticxg7/Downloads/NDI SDK for Linux/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"
  ```

---

## Key Points

1. **No discovery server needed** - We use direct port forwarding
2. **Find ports with `ss -tulnp`** - Shows actual streaming ports (5959, 5960, 5961)
3. **Forward ALL 3 ports** - 5959 (discovery), 5960 (service), and 5961 (video)
4. **Direct connect** - Viewer connects to `localhost:5961`
5. **Architecture matters** - Compile separately for ARM64 (Jupiter) and x86_64 (laptop)

---

## File Cleanup

### Remove Unnecessary Files
```bash
cd /home/staticxg7/Desktop/Github/UDPTunnel/scripts/hpc

# Remove old/unused files
rm -f ndi_gui_viewer ndi_gui_viewer.cpp
rm -f hpc_udp_server.py tcp_to_udp_forwarder.py
rm -f run_ndi_test.sh
rm -f frame_*.png

# Keep only:
# - ndi_test_streamer.cpp + ndi_test_streamer (streamer)
# - ndi_direct_viewer.cpp + ndi_direct_viewer (viewer)
# - ndi_frame_saver.cpp + ndi_frame_saver (optional)
# - build_ndi_test.sh (build script)
# - find_ndi_port.sh (helper)
```

---

## References

- NDI SDK: `/home/staticxg7/Downloads/NDI SDK for Linux/`
- NDI License: https://ndi.link/ndisdk_license

---

**Last Updated:** 2026-03-27  
**Status:** ✅ Working  
**Tested:** Jupiter HPC (login01) → Laptop (Ubuntu x86_64)  
**Method:** Direct SSH port forwarding (no discovery server)
