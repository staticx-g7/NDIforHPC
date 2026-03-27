// NDI Test Streamer for Jupiter HPC
// Based on NDI SDK v6 examples
// Creates a moving color bars test pattern
// WITH PORT DETECTION for tunneling

#include <cstddef>  // For NULL
#include <Processing.NDI.Lib.h>
#include <Processing.NDI.Send.h>
#include <Processing.NDI.SendListener.h>  // For port detection

#include <csignal>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdint>
#include <iostream>
#include <string>

static std::atomic<bool> exit_loop(false);
static void sigint_handler(int)
{
    exit_loop = true;
}

int main(int argc, char* argv[])
{
    // Set up signal handler
    std::signal(SIGINT, sigint_handler);
    
    std::cout << "========================================" << std::endl;
    std::cout << "NDI Test Streamer - Jupiter HPC" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    // Initialize NDI
    if (!NDIlib_initialize()) {
        std::cerr << "✗ Cannot initialize NDI. CPU may not be supported." << std::endl;
        std::cerr << "  Check with NDIlib_is_supported_CPU()" << std::endl;
        return 1;
    }
    
    std::cout << "✓ NDI initialized successfully" << std::endl;
    
    // Create NDI sender
    NDIlib_send_create_t send_create_params;
    send_create_params.p_ndi_name = "Jupiter Test Stream";
    send_create_params.p_groups = nullptr;
    send_create_params.clock_video = true;
    send_create_params.clock_audio = false;
    
    NDIlib_send_instance_t p_send = NDIlib_send_create(&send_create_params);
    if (!p_send) {
        std::cerr << "✗ Failed to create NDI sender" << std::endl;
        NDIlib_destroy();
        return 1;
    }
    
    std::cout << "✓ NDI sender created: " << send_create_params.p_ndi_name << std::endl;
    
    // Detect the streaming port using send listener
    std::cout << "Detecting streaming port..." << std::endl;
    
    NDIlib_send_listener_create_t listener_params;
    listener_params.p_url_address = nullptr;
    
    NDIlib_send_listener_instance_t listener = NDIlib_send_listener_create(&listener_params);
    
    int detected_port = -1;
    std::string detected_ip = "unknown";
    
    if (listener) {
        // Wait up to 3 seconds for sender to be advertised
        int retries = 0;
        const int max_retries = 6;
        
        while (retries < max_retries && detected_port < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            retries++;
            
            uint32_t num_senders = 0;
            const NDIlib_sender_t* senders = NDIlib_send_listener_get_senders(listener, &num_senders);
            
            for (uint32_t i = 0; i < num_senders; i++) {
                if (std::string(senders[i].p_name) == send_create_params.p_ndi_name) {
                    detected_port = senders[i].port;
                    detected_ip = senders[i].p_address ? std::string(senders[i].p_address) : "unknown";
                    break;
                }
            }
        }
        
        NDIlib_send_listener_destroy(listener);
    }
    
    if (detected_port > 0) {
        std::cout << "✓ Streaming port detected: " << detected_port << std::endl;
        std::cout << "✓ Streaming IP: " << detected_ip << std::endl;
        std::cout << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "📡 FOR SSH TUNNELING:" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Forward this port through SSH tunnel:" << std::endl;
        std::cout << "  ssh -L 6000:" << detected_ip << ":" << detected_port << " user@jupiter" << std::endl;
        std::cout << std::endl;
        std::cout << "Then configure NDI receiver to connect to:" << std::endl;
        std::cout << "  localhost:6000" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << std::endl;
    } else {
        std::cout << "⚠️  Could not detect streaming port automatically" << std::endl;
        std::cout << "  NDI will use dynamic port allocation" << std::endl;
        std::cout << "  You'll need to monitor ports separately" << std::endl;
        std::cout << std::endl;
    }
    
    // Stream configuration
    const int width = 1920;
    const int height = 1080;
    const int fps_n = 30;
    const int fps_d = 1;
    
    std::cout << "Stream configuration:" << std::endl;
    std::cout << "  Resolution: " << width << "x" << height << std::endl;
    std::cout << "  Frame rate: " << fps_n << "/" << fps_d << std::endl;
    std::cout << "  Format: UYVY (YUV 4:2:2)" << std::endl;
    std::cout << std::endl;
    std::cout << "Press Ctrl+C to stop streaming" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    // Create frame buffer (UYVY format: 2 bytes per pixel)
    std::vector<uint8_t> frame_buffer(width * height * 2);
    
    int frame_count = 0;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    while (!exit_loop) {
        // Create moving color bars test pattern
        int time_offset = (frame_count * 2) % width;
        
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int idx = (y * width + x) * 2;
                
                // Create moving color bars (8 bars)
                int bar_width = width / 8;
                int bar_index = ((x + time_offset) / bar_width) % 8;
                
                // Convert RGB to YUV (UYVY format)
                // Simple approximation for test pattern
                uint8_t y_val, u_val, v_val;
                
                switch (bar_index) {
                    case 0:  // Red
                        y_val = 76; u_val = 84; v_val = 255; break;
                    case 1:  // Orange
                        y_val = 150; u_val = 44; v_val = 21; break;
                    case 2:  // Yellow
                        y_val = 226; u_val = 0; v_val = 148; break;
                    case 3:  // Green
                        y_val = 149; u_val = 43; v_val = 21; break;
                    case 4:  // Cyan
                        y_val = 179; u_val = 170; v_val = 0; break;
                    case 5:  // Blue
                        y_val = 29; u_val = 255; v_val = 107; break;
                    case 6:  // Purple
                        y_val = 105; u_val = 212; v_val = 235; break;
                    case 7:  // Magenta
                        y_val = 105; u_val = 255; v_val = 212; break;
                    default:
                        y_val = 128; u_val = 128; v_val = 128;
                }
                
                // Add some animation based on Y position
                y_val = (y_val + (y / 20) + (frame_count % 64)) % 256;
                
                // UYVY format: U0 Y0 V0 Y1
                if (x % 2 == 0) {
                    // Even pixel: U Y
                    frame_buffer[idx] = u_val;
                    frame_buffer[idx + 1] = y_val;
                } else {
                    // Odd pixel: V Y
                    frame_buffer[idx] = v_val;
                    frame_buffer[idx + 1] = y_val;
                }
            }
        }
        
        // Create NDI video frame
        NDIlib_video_frame_v2_t video_frame;
        video_frame.xres = width;
        video_frame.yres = height;
        video_frame.FourCC = NDIlib_FourCC_type_UYVY;  // UYVY format
        video_frame.frame_rate_N = fps_n;
        video_frame.frame_rate_D = fps_d;
        video_frame.picture_aspect_ratio = static_cast<float>(width) / height;
        video_frame.frame_format_type = NDIlib_frame_format_type_progressive;
        video_frame.timecode = NDIlib_send_timecode_synthesize;
        video_frame.p_data = frame_buffer.data();
        video_frame.line_stride_in_bytes = width * 2;
        video_frame.p_metadata = nullptr;
        video_frame.timestamp = 0;
        
        // Send the frame
        NDIlib_send_send_video_v2(p_send, &video_frame);
        
        frame_count++;
        
        // Print stats every second (30 frames at 30fps)
        if (frame_count % 30 == 0) {
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
            float actual_fps = (frame_count * 1000.0f) / elapsed;
            
            std::cout << "Frame " << frame_count << " | FPS: " << actual_fps << std::endl;
        }
        
        // Maintain frame rate
        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / fps_n));
    }
    
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Stream stopped" << std::endl;
    std::cout << "Total frames sent: " << frame_count << std::endl;
    
    // Calculate final statistics
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    float total_fps = (frame_count * 1000.0f) / total_elapsed;
    
    std::cout << "Total time: " << (total_elapsed / 1000.0f) << " seconds" << std::endl;
    std::cout << "Average FPS: " << total_fps << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Cleanup
    NDIlib_send_destroy(p_send);
    NDIlib_destroy();
    
    return 0;
}