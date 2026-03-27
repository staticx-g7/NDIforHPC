// NDI Direct Connect Viewer - Bypasses discovery, connects directly to port
// Use this when NDI stream is forwarded through SSH tunnel

#include <cstddef>
#include <Processing.NDI.Lib.h>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>
#include <csignal>
#include <atomic>

static std::atomic<bool> exit_loop(false);
static void sigint_handler(int)
{
    exit_loop = true;
}

int main(int argc, char* argv[])
{
    std::signal(SIGINT, sigint_handler);
    
    std::cout << "========================================" << std::endl;
    std::cout << "NDI Direct Connect Viewer" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Connecting directly to localhost:5961 (SSH tunnel)" << std::endl;
    std::cout << "Press 'q' or Ctrl+C to quit" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    // Initialize NDI
    if (!NDIlib_initialize()) {
        std::cerr << "✗ Cannot initialize NDI" << std::endl;
        return 1;
    }
    
    std::cout << "✓ NDI initialized" << std::endl;
    
    // Create receiver
    NDIlib_recv_instance_t p_recv = NDIlib_recv_create_v3();
    if (!p_recv) {
        std::cerr << "✗ Failed to create receiver" << std::endl;
        NDIlib_destroy();
        return 1;
    }
    
    // Connect directly to the forwarded port (bypass discovery)
    NDIlib_source_t source;
    source.p_ndi_name = "Jupiter Test Stream";
    source.p_url_address = "localhost:5961";  // Direct connect to forwarded port
    
    NDIlib_recv_connect(p_recv, &source);
    std::cout << "✓ Connected to: " << source.p_url_address << std::endl;
    std::cout << std::endl;
    
    // Create OpenCV window
    cv::namedWindow("NDI Stream - Jupiter Test Stream", cv::WINDOW_AUTOSIZE);
    
    int frame_count = 0;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    while (!exit_loop) {
        NDIlib_video_frame_v2_t video_frame;
        NDIlib_audio_frame_v2_t audio_frame;
        
        switch (NDIlib_recv_capture_v2(p_recv, &video_frame, &audio_frame, nullptr, 1000)) {
            case NDIlib_frame_type_video: {
                frame_count++;
                
                try {
                    // Convert NDI video frame to OpenCV Mat
                    cv::Mat frame(video_frame.yres, video_frame.xres, CV_8UC2, video_frame.p_data, video_frame.line_stride_in_bytes);
                    cv::Mat display_frame;
                    cv::cvtColor(frame, display_frame, cv::COLOR_YUV2BGR_UYVY);
                    
                    // Add FPS overlay
                    auto now = std::chrono::high_resolution_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
                    float fps = elapsed > 0 ? frame_count / (elapsed / 1000.0f) : 0;
                    
                    std::string overlay = "Frame: " + std::to_string(frame_count) + 
                                          " | FPS: " + std::to_string((int)fps) +
                                          " | " + std::to_string(video_frame.xres) + "x" + std::to_string(video_frame.yres);
                    
                    cv::putText(display_frame, overlay, cv::Point(10, 30), 
                               cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 255), 2);
                    
                    // Display
                    cv::imshow("NDI Stream - Jupiter Test Stream", display_frame);
                    
                    // Check for 'q' key
                    if (cv::waitKey(1) == 'q') {
                        exit_loop = true;
                    }
                } catch (const cv::Exception& e) {
                    std::cerr << "OpenCV error: " << e.what() << std::endl;
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
    std::cout << "Viewer stopped" << std::endl;
    std::cout << "Total frames: " << frame_count << std::endl;
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    float total_fps = total_elapsed > 0 ? frame_count / (total_elapsed / 1000.0f) : 0;
    
    std::cout << "Average FPS: " << total_fps << std::endl;
    std::cout << "========================================" << std::endl;
    
    cv::destroyAllWindows();
    NDIlib_recv_destroy(p_recv);
    NDIlib_destroy();
    
    return 0;
}
