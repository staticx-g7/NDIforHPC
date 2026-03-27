# NDI Streaming from Jupiter HPC to Laptop

**Status:** ✅ Working - NDI video streaming from Jupiter HPC (ARM64) to laptop (x86_64) via SSH tunnel.

## ⚠️ Prerequisites - NDI SDK Required

**Before you begin, you MUST download and install the NDI SDK:**

1. **Download NDI SDK for Linux:**
   - Visit: https://ndi.dev/ndi-ndi-sdk-for-linux
   - Download: "NDI SDK for Linux" (latest version)
   - Extract to: `~/Downloads/NDI SDK for Linux/` (or any location you prefer)

2. **Important:** The NDI SDK is **NOT included** in this repository due to:
   - License restrictions (commercial SDK)
   - Large file size (~100MB+)
   - Architecture-specific binaries (ARM64 for Jupiter, x86_64 for laptop)

3. **Update paths in scripts** to match your installation location:
   - Default laptop path: `~/Downloads/NDI SDK for Linux/`
   - Default Jupiter path: `/e/scratch/cjsc/george2/NDI/NDI SDK for Linux/`
   - Update these paths in all scripts if you install elsewhere

---

## Quick Start

### 1. On Jupiter HPC

```bash
# SSH to Jupiter (use your username and preferred login node)
ssh <your_username>@login01.jupiter.fz-juelich.de

# Navigate to scripts (adjust path to your location)
cd ~/NDI/Scripts/hpc

# Set NDI SDK path (adjust to your installation)
export NDI_PATH="~/NDI/NDI SDK for Linux"

# Compile (if needed)
./build_ndi_test.sh

# Run streamer
export LD_LIBRARY_PATH="$NDI_PATH/lib/aarch64-rpi4-linux-gnueabi:$LD_LIBRARY_PATH"
./ndi_test_streamer
```

### 2. Find Streaming Ports on Jupiter

```bash
ss -tulnp | grep ndi_test_stream
# Look for ports 5959 (discovery), 5960 (service), and 5961 (video)
```

### 3. On Laptop - Create SSH Tunnel

```bash
# SSH tunnel to Jupiter (forward all 3 NDI ports)
ssh -L 5959:localhost:5959 \
    -L 5960:localhost:5960 \
    -L 5961:localhost:5961 \
    -i ~/.ssh/your_ssh_key \
    <your_username>@login01.jupiter.fz-juelich.de

# Note: You can use login01-login10, but login01 is recommended for consistency
```

### 4. On Laptop - View Stream

```bash
cd /home/staticxg7/Desktop/Github/UDPTunnel/scripts/hpc
export LD_LIBRARY_PATH="/home/staticxg7/Downloads/NDI SDK for Linux/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"
./ndi_direct_viewer
```

**A window should appear showing the video stream from Jupiter!**

---

## Full Documentation

See Obsidian notes for detailed guides:

- **[[ndi-jupiter-streaming]]** - Complete NDI streaming setup (main guide)
- **[[udp-tunnel-guide]]** - Original UDP tunnel testing (reference)
- **[[unreal-streaming]]** - Unreal Engine integration plans
- **[[tmux-cheatsheet]]** - tmux commands for managing sessions

---

## File Structure

```
UDPTunnel/
├── scripts/
│   └── hpc/
│       ├── ndi_test_streamer.cpp      # NDI video streamer (Jupiter)
│       ├── ndi_test_streamer          # Compiled binary (ARM64/x86_64)
│       ├── ndi_direct_viewer.cpp      # Direct connect viewer (laptop)
│       ├── ndi_direct_viewer          # Compiled binary
│       ├── ndi_frame_saver.cpp        # Frame saver (optional)
│       ├── ndi_frame_saver            # Compiled binary
│       ├── build_ndi_test.sh          # Build script
│       └── find_ndi_port.sh           # Port finder helper
└── obsidian files/
    ├── ndi-jupiter-streaming.md       # Main documentation ⭐
    ├── udp-tunnel-guide.md            # Original UDP testing (reference)
    ├── unreal-streaming.md            # Unreal integration plans
    └── tmux-cheatsheet.md             # tmux commands
```

---

## Quick Reference

### Ports Used

| Port | Purpose | Forwarded? |
|------|---------|------------|
| 5959 | Discovery | ✅ Yes |
| 5960 | NDI Service | ✅ Yes |
| 5961 | Video Stream | ✅ Yes |

**Note:** All three ports must be forwarded for NDI to work properly.

### Essential Files

- **Jupiter:** `ndi_test_streamer` (streamer binary)
- **Laptop:** `ndi_direct_viewer` (viewer binary)
- **Optional:** `ndi_frame_saver` (saves frames as PNG)

---

## Troubleshooting

**"Connection refused"**
- Check streamer is running on Jupiter
- Verify SSH tunnel is active

**Can't find ports**
```bash
ss -tulnp | grep ndi_test_stream
```

**Low FPS**
- Reduce resolution in `ndi_test_streamer.cpp`

---

**Last Updated:** 2026-03-27  
**Status:** ✅ Working  
**Tested:** Jupiter HPC → Laptop (Ubuntu)
