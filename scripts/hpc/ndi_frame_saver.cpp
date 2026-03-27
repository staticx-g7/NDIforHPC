// NDI Receiver that saves frames to file
// No GUI - just saves video frames as images

#include <cstddef>
#include <Processing.NDI.Lib.h>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>
#include <csignal>
#include <atomic>
#include <iomanip>

static std::atomic<bool> exit_loop(false);
static void sigint_handler(int)
{
    exit_loop = true;
}

int main(int argc, char* argv[])
{
    std::signal(SIGINT, sigint_handler);
    
    std::cout << "========================================" << std::endl;
    std::cout << "NDI Frame Saver" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "This will save NDI frames as PNG images." << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    // Initialize NDI
    if (!NDIlib_initialize()) {
        std::cerr << "✗ Cannot initialize NDI" << std::endl;
        return 1;
    }
    
    std::cout << "✓ NDI initialized" << std::endl;
    
    // Create finder
    NDIlib_find_instance_t p_find = NDIlib_find_create_v2();
    if (!p_find) {
        std::cerr << "✗ Failed to create finder" << std::endl;
        return 1;
    }
    
    std::cout << "Looking for NDI sources..." << std::endl;
    
    // Wait for sources
    uint32_t no_sources = 0;
    const NDIlib_source_t* p_sources = nullptr;
    
    int wait_count = 0;
    while (!exit_loop && no_sources == 0 && wait_count < 10) {
        NDIlib_find_wait_for_sources(p_find, 1000);
        p_sources = NDIlib_find_get_current_sources(p_find, &no_sources);
        wait_count++;
    }
    
    if (exit_loop || no_sources == 0) {
        std::cerr << "✗ No NDI sources found" << std::endl;
        NDIlib_find_destroy(p_find);
        NDIlib_destroy();
        return 1;
    }
    
    std::cout << "Found " << no_sources << " source(s):" << std::endl;
    for (uint32_t i = 0; i < no_sources; i++) {
        std::cout << "  " << (i+1) << ". " << p_sources[i].p_ndi_name << std::endl;
    }
    std::cout << std::endl;
    
    // Create receiver
    NDIlib_recv_instance_t p_recv = NDIlib_recv_create_v3();
    if (!p_recv) {
        std::cerr << "✗ Failed to create receiver" << std::endl;
        NDIlib_find_destroy(p_find);
        NDIlib_destroy();
        return 1;
    }
    
    // Connect to first source
    const NDIlib_source_t* selected_source = &p_sources[0];
    NDIlib_recv_connect(p_recv, selected_source);
    std::cout << "✓ Connected to: " << selected_source->p_ndi_name << std::endl;
    
    // Destroy finder
    NDIlib_find_destroy(p_find);
    
    int frame_count = 0;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::cout << "Receiving frames..." << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    while (!exit_loop) {
        NDIlib_video_frame_v2_t video_frame;
        NDIlib_audio_frame_v2_t audio_frame;
        
        switch (NDIlib_recv_capture_v2(p_recv, &video_frame, &audio_frame, nullptr, 1000)) {
            case NDIlib_frame_type_video: {
                frame_count++;
                
                // Convert NDI video frame to OpenCV Mat
                cv::Mat frame(video_frame.yres, video_frame.xres, CV_8UC2, video_frame.p_data, video_frame.line_stride_in_bytes);
                cv::Mat display_frame;
                cv::cvtColor(frame, display_frame, cv::COLOR_YUV2BGR_UYVY);
                
                // Save every 30th frame (about 1 per second at 30fps)
                if (frame_count % 30 == 0) {
                    auto now = std::chrono::high_resolution_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
                    float fps = elapsed > 0 ? frame_count / (elapsed / 1000.0f) : 0;
                    
                    std::string filename = "frame_" + std::to_string(frame_count) + ".png";
                    cv::imwrite(filename, display_frame);
                    
                    std::cout << "Frame " << std::setw(5) << frame_count 
                              << " | FPS: " << std::fixed << std::setprecision(2) << fps
                              << " | Saved: " << filename << std::endl;
                }
                
                NDIlib_recv_free_video_v2(p_recv, &video_frame);
                break;
            }
            
            case NDIlib_frame_type_audio:
                NDIlib_recv_free_audio_v2(p_recv, &audio_frame);
                break;
                
            case NDIlib_frame_type_none:
                break;
        }
    }
    
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Receiver stopped" << std::endl;
    std::cout << "Total frames received: " << frame_count << std::endl;
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    float total_fps = total_elapsed > 0 ? frame_count / (total_elapsed / 1000.0f) : 0;
    
    std::cout << "Average FPS: " << total_fps << std::endl;
    std::cout << "Frames saved: " << (frame_count / 30) << std::endl;
    std::cout << "========================================" << std::endl;
    
    NDIlib_recv_destroy(p_recv);
    NDIlib_destroy();
    
    return 0;
}
