# UDP Tunnel Test - Jupiter HPC

## Overview

This project tests UDP traffic forwarding over SSH tunnel to Jupiter HPC (FZ Jülich), enabling UDP-based applications (like NDI video streams) to work through SSH-only connections.

## Architecture

```
Laptop                          Jupiter (same login node!)
───────────────────────────────────────────────────────────
Client (UDP:5960)              Echo Server (UDP:5960)
        ↓                                ↑
UDP→TCP Gateway                    TCP→UDP Forwarder
        ↓ TCP:51515                    ↑ TCP:51515
        └──→ SSH tunnel ───────────────┘
```

## File Structure

```
UDPTunnel/
├── scripts/
│   ├── hpc/
│   │   ├── hpc_udp_server.py          # UDP echo server (Jupiter)
│   │   └── tcp_to_udp_forwarder.py    # TCP→UDP forwarder (Jupiter)
│   ├── laptop/
│   │   └── laptop_client.py           # Test client (Laptop)
│   └── shared/
│       └── simple_gateway.py          # UDP→TCP gateway (Both)
├── obsidian files/
│   ├── tmux-cheatsheet.md
│   └── udp-tunnel-guide.md
└── README.md
```

## Setup

### Step 1: Upload Scripts to Jupiter

```bash
scp -i ~/.ssh/ed_25519_universal_openssh \
  scripts/hpc/*.py scripts/laptop/*.py \
  george2@login01.jupiter.fz-juelich.de:/e/scratch/cjsc/george2/UDPTunnel/scripts/
```

### Step 2: On Jupiter (specific login node, e.g., login01)

**Option A: Background processes (simplest)**
```bash
cd /e/scratch/cjsc/george2/UDPTunnel/scripts/hpc
python3 hpc_udp_server.py --port 5960 &
python3 tcp_to_udp_forwarder.py --tcp-host 0.0.0.0 --tcp-port 51515 --udp-port 5960
```

**Option B: tmux (recommended for long sessions)**
```bash
tmux new -s udptest
cd /e/scratch/cjsc/george2/UDPTunnel/scripts/hpc
python3 hpc_udp_server.py --port 5960
# Split pane: Ctrl+b → %
# Switch to pane 2: Ctrl+b → q → 2
python3 tcp_to_udp_forwarder.py --tcp-host 0.0.0.0 --tcp-port 51515 --udp-port 5960
```

### Step 3: On Laptop

**Terminal 1 - SSH tunnel (to same node as Jupiter services):**
```bash
ssh -L 51515:localhost:51515 -i ~/.ssh/ed_25519_universal_openssh \
  -o MACs=hmac-sha2-256-etm@openssh.com,hmac-sha2-512-etm@openssh.com,umac-128-etm@openssh.com \
  george2@login01.jupiter.fz-juelich.de
```

**Terminal 2 - UDP→TCP gateway:**
```bash
cd /home/staticxg7/Desktop/Github/UDPTunnel/scripts/shared
python3 simple_gateway.py --mode udp-to-tcp --udp-port 5960 --tcp-port 51515
```

### Step 4: Test

**Terminal 3 - Run test client:**
```bash
cd /home/staticxg7/Desktop/Github/UDPTunnel/scripts/laptop
python3 laptop_client.py --host localhost --port 5960 --count 5
```

Expected output:
```
✓ [1/5] SUCCESS - RTT: 102.73ms
  Response: Echo [2026-03-27 16:20:24]: Test message 1/5 - ...
...
Results: 5/5 successful
Average RTT: 122.58ms
```

## Important Notes

- **All Jupiter services must run on the same login node**
- SSH tunnel must connect to the **same node** where services are running
- Use specific node names: `login01.jupiter.fz-juelich.de`, `login02.jupiter.fz-juelich.de`, etc.
- Default ports: UDP 5960, TCP 51515 (change if needed)

## For NDI Streaming

Once this works, replace the echo server with an NDI stream generator:
- Use `python-ndi` or similar library to generate NDI stream on UDP 5960
- The tunnel will forward the UDP NDI packets to your laptop
- Use NDI viewer on laptop to receive the stream

## Troubleshooting

**"Connection refused" or "Connection reset by peer":**
- Check that all services are on the same login node
- Verify SSH tunnel connects to the correct node
- Ensure no firewall is blocking ports

**Port already in use:**
- Change TCP port (e.g., 41414, 51515, 61616)
- Update all commands accordingly

**Timeout errors:**
- Check all 4 components are running (2 on Jupiter, 2 on laptop)
- Verify SSH tunnel is still active

## tmux Quick Reference

See [[tmux-cheatsheet]] for tmux commands.
