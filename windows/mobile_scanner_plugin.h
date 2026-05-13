#ifndef FLUTTER_PLUGIN_MOBILE_SCANNER_PLUGIN_H_
#define FLUTTER_PLUGIN_MOBILE_SCANNER_PLUGIN_H_

#include <flutter/encodable_value.h>
#include <flutter/event_channel.h>
#include <flutter/event_sink.h>
#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/texture_registrar.h>

#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace mobile_scanner {

class MobileScannerPlugin : public flutter::Plugin {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows* registrar);

  explicit MobileScannerPlugin(flutter::PluginRegistrarWindows* registrar);
  ~MobileScannerPlugin() override;

  MobileScannerPlugin(const MobileScannerPlugin&) = delete;
  MobileScannerPlugin& operator=(const MobileScannerPlugin&) = delete;

 private:
  struct CameraDevice {
    std::string id;
    std::string name;
    std::wstring symbolic_link;
    bool is_default = false;
  };

  struct ScanWindow {
    double left = 0.0;
    double top = 0.0;
    double right = 1.0;
    double bottom = 1.0;
  };

  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue>& method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  void Start(const flutter::EncodableMap* arguments,
             std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>>
                 result);
  void Stop(std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>>
                result);
  void Pause(std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>>
                 result);
  void UpdateScanWindow(
      const flutter::EncodableMap* arguments,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void GetAvailableCameras(
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void GetSupportedLenses(
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  void ConfigureEventChannels();
  void SendCamerasEvent();
  void SendBarcodeEvent(const std::string& value,
                        const std::vector<uint8_t>& image,
                        int width,
                        int height);

  std::vector<CameraDevice> EnumerateCameras();
  bool OpenCamera(const CameraDevice& camera, int* width, int* height);
  void CaptureLoop();
  void CloseCamera();
  void StopCapture();

  void StoreFrame(const uint8_t* data, size_t length);
  void DecodeFrame(const std::vector<uint8_t>& rgba, int width, int height);
  std::optional<std::string> DecodeQr(const std::vector<uint8_t>& rgba,
                                      int width,
                                      int height);
  const FlutterDesktopPixelBuffer* CopyPixelBuffer(size_t width,
                                                   size_t height);

  flutter::EncodableMap CameraToMap(const CameraDevice& camera) const;
  flutter::EncodableList CamerasToList(
      const std::vector<CameraDevice>& cameras) const;

  flutter::PluginRegistrarWindows* registrar_;
  flutter::TextureRegistrar* texture_registrar_;

  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>>
      method_channel_;
  std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>>
      event_channel_;
  std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>>
      orientation_event_channel_;

  std::mutex event_sink_mutex_;
  std::unique_ptr<flutter::EventSink<flutter::EncodableValue>> event_sink_;

  std::mutex texture_mutex_;
  std::unique_ptr<flutter::TextureVariant> texture_;
  FlutterDesktopPixelBuffer pixel_buffer_ = {};
  std::vector<uint8_t> latest_frame_rgba_;
  int frame_width_ = 0;
  int frame_height_ = 0;
  int64_t texture_id_ = -1;

  std::mutex camera_mutex_;
  Microsoft::WRL::ComPtr<IMFMediaSource> media_source_;
  Microsoft::WRL::ComPtr<IMFSourceReader> source_reader_;

  std::thread capture_thread_;
  std::atomic_bool running_{false};
  std::atomic_bool paused_{false};

  std::mutex scan_window_mutex_;
  std::optional<ScanWindow> scan_window_;

  int detection_speed_ = 1;
  int detection_timeout_ms_ = 250;
  bool return_image_ = false;
  std::string last_detected_value_;
  std::chrono::steady_clock::time_point last_detection_time_{};
  std::chrono::steady_clock::time_point last_decode_attempt_{};
};

}  // namespace mobile_scanner

#endif  // FLUTTER_PLUGIN_MOBILE_SCANNER_PLUGIN_H_
