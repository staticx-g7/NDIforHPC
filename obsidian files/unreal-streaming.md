# Unreal Engine Video Streaming on Jupiter HPC

## Overview

Unreal Engine (UE) has **built-in streaming capabilities** that are better than external FFmpeg for your use case.

## Options for UE Streaming

### Option 1: NDI Plugin for Unreal (Likely NOT Available)

**Official plugin from NewTek:**
- **Status:** ❌ Probably won't work on Jupiter
- **Reason:** NDI SDK doesn't support ARM64 (Jupiter's architecture)
- **Check:** Run `check_ndi_support.sh` on Jupiter to verify

**See:** [[ndi-arm64-compatibility]] for detailed compatibility analysis
1. Download NDI Plugin from Marketplace or GitHub
2. Enable in Plugins > Media > NDI Plugin
3. Use NDI Source/Destination actors in your scene
4. Configure output to UDP port
```

### Option 2: Unreal Media Framework + FFmpeg (Recommended)

**UE's Media Framework** integrates FFmpeg internally:

**Pros:**
- ✅ Built into Unreal Engine
- ✅ H.264/H.265 encoding support
- ✅ Works with your UDP tunnel
- ✅ No external processes needed
- ✅ Hardware encoding via NVIDIA NVENC

**Setup in UE:**
```cpp
// C++ Example: Stream viewport via Media Framework
#include "Media/MediaPlayer.h"
#include "Media/MediaTexture.h"

// Create media player
UMediaPlayer* MediaPlayer = CreateDefaultSubobject<UMediaPlayer>("ViewportStream");
MediaPlayer->SetMediaSource(FMediaSource::CreateFileSource("udp://127.0.0.1:5960"));

// Or use socket directly
FSocket* Socket = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)
    ->CreateSocket(NAME_Datagram, "UDP Stream", false);
Socket->Bind(*("0.0.0.0"), 5960);
```

**Blueprint approach:**
1. Add `Media Player` actor
2. Set URL to `udp://127.0.0.1:5960`
3. Use `Media Texture` to display
4. For encoding, use `Media Sound Bus` or custom plugin

### Option 3: Custom UDP Socket Streaming (Most Control)

**Use UE's socket system directly:**

```cpp
// In your Actor or GameInstance
class UVideoStreamer : public UActorComponent
{
    FSocket* UdpSocket;
    
    void BeginPlay() override
    {
        UdpSocket = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)
            ->CreateSocket(NAME_Datagram, TEXT("VideoStream"), false);
        UdpSocket->Bind(*("0.0.0.0"), 5960);
    }
    
    void StreamFrame(FRawColorArray& FrameData)
    {
        // Compress frame (use libx264 or send raw)
        TArray<uint8> CompressedData = CompressFrame(FrameData);
        
        // Send via UDP
        FSocketAddress* Addr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)
            ->CreateInternetSocketAddress("127.0.0.1", 5960);
        UdpSocket->SendTo(CompressedData.GetData(), CompressedData.Num(), Addr);
    }
    
    TArray<uint8> CompressFrame(const FRawColorArray& Frame)
    {
        // Use libx264 or send raw RGBA
        // For simplicity, send raw: Width * Height * 4 bytes
        TArray<uint8> Result;
        Result.Append(Frame.GetData(), Frame.Num());
        return Result;
    }
};
```

### Option 4: WebRTC Plugin (Modern Alternative)

**Unreal WebRTC Plugin:**
- GitHub: https://github.com/EpicGames-Research/UnrealEngineWebRTCPlugin
- **Pros:** Low latency, works over standard ports, built-in H.264
- **Cons:** More complex setup, requires signaling server

## Recommended Setup for Jupiter

### Architecture:
```
Unreal Engine (Jupiter)                    Laptop
─────────────────────────────────────────────────────────
Game Viewport                              Video Viewer
       ↓
Media Framework / Socket
       ↓ H.264/H.265
       ↓ UDP:5960
TCP→UDP Forwarder
       ↓ TCP:51515
       └──→ SSH tunnel ───→ UDP→TCP Gateway
                                             ↓ UDP:5960
                                         ffplay / VLC / UE
```

### Step-by-Step:

1. **In Unreal Engine Project:**
   - Enable `Media` plugin
   - Enable `WebSockets` plugin (optional)
   - Create custom streaming component

2. **For H.264 Encoding in UE:**
   ```cpp
   // Use libx264 via Media Framework
   #include "Media/MediaWriter.h"
   
   // Configure encoder
   IVideoEncoder* Encoder = FVideoEncoderModule::Get().CreateEncoder("libx264");
   Encoder->SetOutputFormat(EVideoEncoderOutputFormat::H264Raw);
   ```

3. **Simple Raw Stream (no encoding):**
   ```cpp
   // Send raw viewport data
   FRenderCommandFence Fence;
   FRawColorArray ViewportData;
   FPlatformProcess::Sleep(0);
   
   // Render to texture
   UTexture2D* RenderTarget = ...;
   FRenderTarget* RT = RenderTarget->GameThread_GetRenderTargetResource();
   FReadSurfaceDataFlags Flags;
   FRenderTarget::ReadSurfaceDataAsArray(RT, ViewportData, Flags);
   
   // Send via UDP
   SendViaUdp(ViewportData);
   ```

## FFmpeg Integration with UE

**If you need FFmpeg specifically:**

1. **FFmpeg Plugin for Unreal:**
   - Marketplace: "FFMpeg Plugin"
   - Allows encoding/decoding in UE

2. **External FFmpeg Process:**
   ```cpp
   // Spawn FFmpeg process from UE
   FProcessHandle FfmpegProcess;
   FString Command = TEXT("ffmpeg -f rawvideo -pixel_format bgra -video_size 1920x1080 -framerate 30 -i pipe:0 -c:v libx264 -f mpegts udp://127.0.0.1:5960");
   FPlatformProcess::SpawnProcess(*Command, &FfmpegProcess, nullptr, *TEXT(""), 0, nullptr, 0);
   ```

## Performance Considerations for Jupiter

### NVIDIA H100 GPU (GH200):
- **NVENC encoding** available via UE Media Framework
- **96 GB HBM3** - plenty for high-res textures
- **72 CPU cores** - can handle encoding on CPU if needed

### Recommended Settings:
```
Resolution: 1920x1080 (1080p)
FPS: 30-60
Encoding: H.264 NVENC (hardware)
Bitrate: 5-10 Mbps
Latency: < 200ms (with UDP tunnel)
```

## Comparison

| Method | Latency | Complexity | Compression | Works on Jupiter |
|--------|---------|------------|-------------|------------------|
| NDI Plugin | Low | Low | H.264 | ❓ ARM64 support? |
| UE Media Framework | Low | Medium | H.264/H.265 | ✅ Yes |
| Custom UDP Socket | Lowest | High | Raw/Custom | ✅ Yes |
| WebRTC | Low | High | H.264 | ✅ Yes |
| External FFmpeg | Medium | Medium | H.264/H.265 | ✅ Yes |

## Recommendation

**For your Jupiter HPC setup:**

1. **Start with:** UE Media Framework + UDP socket (built-in, no extra deps)
2. **If you need better compression:** Use FFmpeg plugin or external FFmpeg process
3. **If NDI plugin works on ARM64:** Use NDI (easiest integration)
4. **For production:** WebRTC plugin (most robust)

## Next Steps

1. Test current UDP tunnel with simple video (FFmpeg test pattern)
2. Create UE project with Media Framework
3. Implement viewport capture → H.264 encode → UDP send
4. Receive on laptop with ffplay/VLC
5. Optimize with NVENC hardware encoding

Would you like me to create a complete UE5 C++ example for UDP video streaming?
