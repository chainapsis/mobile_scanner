#include "mobile_scanner_plugin.h"

#include <flutter/encodable_value.h>
#include <flutter/event_stream_handler_functions.h>
#include <flutter/standard_method_codec.h>

#include <mfapi.h>
#include <mferror.h>
#include <windows.h>

#include <Barcode.h>
#include <BarcodeFormat.h>
#include <ImageView.h>
#include <ReadBarcode.h>
#include <ReaderOptions.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

namespace mobile_scanner {
namespace {

constexpr char kMethodChannelName[] =
    "dev.steenbakker.mobile_scanner/scanner/method";
constexpr char kEventChannelName[] =
    "dev.steenbakker.mobile_scanner/scanner/event";
constexpr char kOrientationEventChannelName[] =
    "dev.steenbakker.mobile_scanner/scanner/deviceOrientation";

constexpr char kBarcodeEventName[] = "barcode";
constexpr char kCamerasEventName[] = "cameras";
constexpr char kUnsupportedOperationError[] =
    "MOBILE_SCANNER_UNSUPPORTED_OPERATION";
constexpr char kAlreadyStartedError[] = "MOBILE_SCANNER_ALREADY_STARTED_ERROR";
constexpr char kNoCameraError[] = "MOBILE_SCANNER_NO_CAMERA_ERROR";
constexpr char kPermissionDeniedError[] =
    "MOBILE_SCANNER_CAMERA_PERMISSION_DENIED";
constexpr char kGenericError[] = "MOBILE_SCANNER_GENERIC_ERROR";

constexpr int kAuthorizationAuthorized = 1;
constexpr int kTorchUnavailable = -1;
constexpr int kCameraFacingExternal = 2;
constexpr int kCameraFacingUnknown = -1;
constexpr int kLensAny = -1;
constexpr int kBarcodeFormatQrCode = 256;
constexpr int kBarcodeTypeText = 7;
constexpr int kDetectionNoDuplicates = 0;
constexpr int kDetectionNormal = 1;
constexpr int kDetectionUnrestricted = 2;
constexpr DWORD kAllStreams =
    static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
constexpr DWORD kFirstVideoStream =
    static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

HRESULT g_mf_startup_result = E_FAIL;
std::once_flag g_mf_startup_once;

void EnsureMediaFoundation() {
  std::call_once(g_mf_startup_once, []() {
    g_mf_startup_result = MFStartup(MF_VERSION);
  });
}

std::string Utf8FromWide(const std::wstring& value) {
  if (value.empty()) {
    return std::string();
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) {
    return std::string();
  }
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(),
                      static_cast<int>(value.size()), result.data(), size,
                      nullptr, nullptr);
  return result;
}

std::wstring WideFromUtf8(const std::string& value) {
  if (value.empty()) {
    return std::wstring();
  }
  const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr,
                                       0);
  if (size <= 0) {
    return std::wstring();
  }
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(),
                      static_cast<int>(value.size()), result.data(), size);
  return result;
}

std::string HResultMessage(HRESULT result) {
  char buffer[16] = {};
  std::snprintf(buffer, sizeof(buffer), "0x%08X",
                static_cast<unsigned int>(result));
  return buffer;
}

const flutter::EncodableMap* GetMap(
    const flutter::EncodableValue* arguments) {
  if (arguments == nullptr) {
    return nullptr;
  }
  return std::get_if<flutter::EncodableMap>(arguments);
}

const flutter::EncodableValue* FindValue(const flutter::EncodableMap* map,
                                         const char* key) {
  if (map == nullptr) {
    return nullptr;
  }
  const auto it = map->find(flutter::EncodableValue(key));
  if (it == map->end()) {
    return nullptr;
  }
  return &it->second;
}

int GetInt(const flutter::EncodableMap* map, const char* key, int fallback) {
  const flutter::EncodableValue* value = FindValue(map, key);
  if (value == nullptr) {
    return fallback;
  }
  if (const auto int_value = std::get_if<int32_t>(value)) {
    return *int_value;
  }
  if (const auto int64_value = std::get_if<int64_t>(value)) {
    return static_cast<int>(*int64_value);
  }
  return fallback;
}

bool GetBool(const flutter::EncodableMap* map,
             const char* key,
             bool fallback) {
  const flutter::EncodableValue* value = FindValue(map, key);
  if (value == nullptr) {
    return fallback;
  }
  if (const auto bool_value = std::get_if<bool>(value)) {
    return *bool_value;
  }
  return fallback;
}

std::string GetString(const flutter::EncodableMap* map,
                      const char* key,
                      const std::string& fallback = std::string()) {
  const flutter::EncodableValue* value = FindValue(map, key);
  if (value == nullptr) {
    return fallback;
  }
  if (const auto string_value = std::get_if<std::string>(value)) {
    return *string_value;
  }
  return fallback;
}

double GetDoubleFromValue(const flutter::EncodableValue& value,
                          double fallback) {
  if (const auto double_value = std::get_if<double>(&value)) {
    return *double_value;
  }
  if (const auto int_value = std::get_if<int32_t>(&value)) {
    return static_cast<double>(*int_value);
  }
  if (const auto int64_value = std::get_if<int64_t>(&value)) {
    return static_cast<double>(*int64_value);
  }
  return fallback;
}

double ClampUnit(double value) {
  return std::clamp(value, 0.0, 1.0);
}

uint8_t ClampByte(int value) {
  return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

bool GuidEquals(const GUID& lhs, const GUID& rhs) {
  return IsEqualGUID(lhs, rhs);
}

std::optional<FrameFormat> FrameFormatFromSubtype(const GUID& subtype) {
  if (GuidEquals(subtype, MFVideoFormat_RGB32) ||
      GuidEquals(subtype, MFVideoFormat_ARGB32)) {
    return FrameFormat::kBgra32;
  }
  if (GuidEquals(subtype, MFVideoFormat_RGB24)) {
    return FrameFormat::kRgb24;
  }
  if (GuidEquals(subtype, MFVideoFormat_YUY2)) {
    return FrameFormat::kYuy2;
  }
  if (GuidEquals(subtype, MFVideoFormat_NV12)) {
    return FrameFormat::kNv12;
  }
  return std::nullopt;
}

LONG DefaultStride(FrameFormat format, UINT32 width) {
  switch (format) {
    case FrameFormat::kBgra32:
      return static_cast<LONG>(width * 4);
    case FrameFormat::kRgb24:
      return static_cast<LONG>((width * 3 + 3) & ~3);
    case FrameFormat::kYuy2:
      return static_cast<LONG>(width * 2);
    case FrameFormat::kNv12:
      return static_cast<LONG>(width);
  }
  return static_cast<LONG>(width * 4);
}

int MinimumRowBytes(FrameFormat format, int width) {
  switch (format) {
    case FrameFormat::kBgra32:
      return width * 4;
    case FrameFormat::kRgb24:
      return width * 3;
    case FrameFormat::kYuy2:
      return width * 2;
    case FrameFormat::kNv12:
      return width;
  }
  return width * 4;
}

LONG MediaTypeStride(IMFMediaType* media_type,
                     FrameFormat format,
                     UINT32 width) {
  UINT32 stride = 0;
  if (SUCCEEDED(media_type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) &&
      stride != 0) {
    return static_cast<LONG>(stride);
  }
  return DefaultStride(format, width);
}

int FormatRank(FrameFormat format) {
  switch (format) {
    case FrameFormat::kBgra32:
      return 0;
    case FrameFormat::kRgb24:
      return 1;
    case FrameFormat::kYuy2:
      return 2;
    case FrameFormat::kNv12:
      return 3;
  }
  return 10;
}

void YuvToRgb(int y, int u, int v, uint8_t* out) {
  const int c = y - 16;
  const int d = u - 128;
  const int e = v - 128;
  out[0] = ClampByte((298 * c + 409 * e + 128) >> 8);
  out[1] = ClampByte((298 * c - 100 * d - 208 * e + 128) >> 8);
  out[2] = ClampByte((298 * c + 516 * d + 128) >> 8);
  out[3] = 0xFF;
}

bool ReadSupportedVideoType(IMFMediaType* media_type,
                            FrameFormat* format,
                            UINT32* width,
                            UINT32* height,
                            LONG* stride) {
  GUID major_type = {};
  GUID subtype = {};
  if (FAILED(media_type->GetGUID(MF_MT_MAJOR_TYPE, &major_type)) ||
      !GuidEquals(major_type, MFMediaType_Video) ||
      FAILED(media_type->GetGUID(MF_MT_SUBTYPE, &subtype))) {
    return false;
  }

  const auto frame_format = FrameFormatFromSubtype(subtype);
  if (!frame_format) {
    return false;
  }

  UINT32 frame_width = 0;
  UINT32 frame_height = 0;
  if (FAILED(MFGetAttributeSize(media_type, MF_MT_FRAME_SIZE, &frame_width,
                                &frame_height)) ||
      frame_width == 0 || frame_height == 0) {
    return false;
  }

  *format = *frame_format;
  *width = frame_width;
  *height = frame_height;
  *stride = MediaTypeStride(media_type, *frame_format, frame_width);
  return true;
}

struct NativeMediaTypeCandidate {
  Microsoft::WRL::ComPtr<IMFMediaType> media_type;
  FrameFormat format = FrameFormat::kBgra32;
  UINT32 width = 0;
  UINT32 height = 0;
  LONG stride = 0;
  int64_t score = std::numeric_limits<int64_t>::max();
};

int64_t NativeTypeScore(FrameFormat format, UINT32 width, UINT32 height) {
  constexpr int64_t kTargetArea = 1920LL * 1080LL;
  constexpr int64_t kMinimumUsefulArea = 1280LL * 720LL;
  const int64_t area = static_cast<int64_t>(width) * height;
  const int64_t area_delta = area > kTargetArea
                                 ? (area - kTargetArea) * 2
                                 : (kTargetArea - area) * 4;
  const int64_t low_resolution_penalty =
      area < kMinimumUsefulArea ? kTargetArea : 0;
  return static_cast<int64_t>(FormatRank(format)) * 1000 +
         low_resolution_penalty + (area_delta / 1000);
}

bool FindPreferredNativeFrameSize(IMFSourceReader* source_reader,
                                  UINT32* selected_width,
                                  UINT32* selected_height) {
  bool found = false;
  int64_t best_score = std::numeric_limits<int64_t>::max();

  for (DWORD index = 0;; ++index) {
    Microsoft::WRL::ComPtr<IMFMediaType> native_type;
    const HRESULT result = source_reader->GetNativeMediaType(
        kFirstVideoStream, index, &native_type);
    if (result == MF_E_NO_MORE_TYPES) {
      break;
    }
    if (FAILED(result)) {
      continue;
    }

    FrameFormat format = FrameFormat::kBgra32;
    UINT32 width = 0;
    UINT32 height = 0;
    LONG stride = 0;
    if (!ReadSupportedVideoType(native_type.Get(), &format, &width, &height,
                                &stride)) {
      continue;
    }

    const int64_t score = NativeTypeScore(format, width, height);
    if (!found || score < best_score) {
      found = true;
      best_score = score;
      *selected_width = width;
      *selected_height = height;
    }
  }

  return found;
}

HRESULT SetRgb32Output(
    IMFSourceReader* source_reader,
    Microsoft::WRL::ComPtr<IMFMediaType>* current_type) {
  Microsoft::WRL::ComPtr<IMFMediaType> output_type;
  HRESULT result = MFCreateMediaType(&output_type);
  if (FAILED(result)) {
    return result;
  }
  result = output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (FAILED(result)) {
    return result;
  }
  result = output_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
  if (FAILED(result)) {
    return result;
  }
  UINT32 preferred_width = 0;
  UINT32 preferred_height = 0;
  const bool has_preferred_size =
      FindPreferredNativeFrameSize(source_reader, &preferred_width,
                                   &preferred_height) &&
      SUCCEEDED(MFSetAttributeSize(output_type.Get(), MF_MT_FRAME_SIZE,
                                   preferred_width, preferred_height));

  result = source_reader->SetCurrentMediaType(kFirstVideoStream, nullptr,
                                              output_type.Get());
  if (FAILED(result) && has_preferred_size) {
    output_type->DeleteItem(MF_MT_FRAME_SIZE);
    result = source_reader->SetCurrentMediaType(kFirstVideoStream, nullptr,
                                                output_type.Get());
  }
  if (FAILED(result)) {
    return result;
  }
  return source_reader->GetCurrentMediaType(kFirstVideoStream,
                                            current_type->ReleaseAndGetAddressOf());
}

bool SelectNativeMediaType(IMFSourceReader* source_reader,
                           Microsoft::WRL::ComPtr<IMFMediaType>* current_type,
                           FrameFormat* selected_format,
                           UINT32* selected_width,
                           UINT32* selected_height,
                           LONG* selected_stride,
                           HRESULT* failure_result) {
  std::vector<NativeMediaTypeCandidate> candidates;
  HRESULT last_result = MF_E_TOPO_CODEC_NOT_FOUND;

  for (DWORD index = 0;; ++index) {
    Microsoft::WRL::ComPtr<IMFMediaType> native_type;
    const HRESULT result = source_reader->GetNativeMediaType(
        kFirstVideoStream, index, &native_type);
    if (result == MF_E_NO_MORE_TYPES) {
      break;
    }
    if (FAILED(result)) {
      last_result = result;
      continue;
    }

    NativeMediaTypeCandidate candidate;
    if (!ReadSupportedVideoType(native_type.Get(), &candidate.format,
                                &candidate.width, &candidate.height,
                                &candidate.stride)) {
      continue;
    }
    candidate.media_type = native_type;
    candidate.score =
        NativeTypeScore(candidate.format, candidate.width, candidate.height);
    candidates.push_back(std::move(candidate));
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const auto& lhs, const auto& rhs) {
              return lhs.score < rhs.score;
            });

  for (const auto& candidate : candidates) {
    const HRESULT set_result = source_reader->SetCurrentMediaType(
        kFirstVideoStream, nullptr, candidate.media_type.Get());
    if (FAILED(set_result)) {
      last_result = set_result;
      continue;
    }

    Microsoft::WRL::ComPtr<IMFMediaType> selected_type;
    const HRESULT current_result = source_reader->GetCurrentMediaType(
        kFirstVideoStream, &selected_type);
    if (FAILED(current_result)) {
      last_result = current_result;
      continue;
    }

    FrameFormat format = FrameFormat::kBgra32;
    UINT32 width = 0;
    UINT32 height = 0;
    LONG stride = 0;
    if (!ReadSupportedVideoType(selected_type.Get(), &format, &width, &height,
                                &stride)) {
      format = candidate.format;
      width = candidate.width;
      height = candidate.height;
      stride = candidate.stride;
    }

    *current_type = selected_type;
    *selected_format = format;
    *selected_width = width;
    *selected_height = height;
    *selected_stride = stride;
    return true;
  }

  if (failure_result != nullptr) {
    *failure_result = last_result;
  }
  return false;
}

void ReleaseActivateArray(IMFActivate** devices, UINT32 count) {
  if (devices == nullptr) {
    return;
  }
  for (UINT32 i = 0; i < count; ++i) {
    if (devices[i] != nullptr) {
      devices[i]->Release();
    }
  }
  CoTaskMemFree(devices);
}

void FreeAllocatedString(WCHAR* value) {
  if (value != nullptr) {
    CoTaskMemFree(value);
  }
}

}  // namespace

// static
void MobileScannerPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows* registrar) {
  auto plugin = std::make_unique<MobileScannerPlugin>(registrar);

  auto channel = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
      registrar->messenger(), kMethodChannelName,
      &flutter::StandardMethodCodec::GetInstance());

  channel->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](
          const auto& call,
          auto result) { plugin_pointer->HandleMethodCall(call, std::move(result)); });

  plugin->method_channel_ = std::move(channel);
  registrar->AddPlugin(std::move(plugin));
}

MobileScannerPlugin::MobileScannerPlugin(
    flutter::PluginRegistrarWindows* registrar)
    : registrar_(registrar), texture_registrar_(registrar->texture_registrar()) {
  const HRESULT co_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(co_result) && co_result != RPC_E_CHANGED_MODE) {
    // Media Foundation calls below will surface the startup failure.
  }
  EnsureMediaFoundation();
  ConfigureEventChannels();
}

MobileScannerPlugin::~MobileScannerPlugin() {
  StopCapture();
}

void MobileScannerPlugin::ConfigureEventChannels() {
  event_channel_ = std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
      registrar_->messenger(), kEventChannelName,
      &flutter::StandardMethodCodec::GetInstance());

  event_channel_->SetStreamHandler(
      std::make_unique<flutter::StreamHandlerFunctions<flutter::EncodableValue>>(
          [this](const flutter::EncodableValue*,
                 std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>&&
                     events) {
            {
              std::lock_guard<std::mutex> lock(event_sink_mutex_);
              event_sink_ = std::move(events);
            }
            SendCamerasEvent();
            return nullptr;
          },
          [this](const flutter::EncodableValue*) {
            std::lock_guard<std::mutex> lock(event_sink_mutex_);
            event_sink_.reset();
            return nullptr;
          }));

  orientation_event_channel_ =
      std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
          registrar_->messenger(), kOrientationEventChannelName,
          &flutter::StandardMethodCodec::GetInstance());

  orientation_event_channel_->SetStreamHandler(
      std::make_unique<flutter::StreamHandlerFunctions<flutter::EncodableValue>>(
          [](const flutter::EncodableValue*,
             std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>&&) {
            return nullptr;
          },
          [](const flutter::EncodableValue*) { return nullptr; }));
}

void MobileScannerPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  const std::string& method = method_call.method_name();
  const flutter::EncodableMap* arguments = GetMap(method_call.arguments());

  if (method == "state") {
    result->Success(flutter::EncodableValue(kAuthorizationAuthorized));
  } else if (method == "request") {
    result->Success(flutter::EncodableValue(true));
  } else if (method == "start") {
    Start(arguments, std::move(result));
  } else if (method == "stop") {
    Stop(std::move(result));
  } else if (method == "pause") {
    Pause(std::move(result));
  } else if (method == "updateScanWindow") {
    UpdateScanWindow(arguments, std::move(result));
  } else if (method == "getAvailableCameras") {
    GetAvailableCameras(std::move(result));
  } else if (method == "getSupportedLenses") {
    GetSupportedLenses(std::move(result));
  } else if (method == "resetScale" || method == "setScale" ||
             method == "setFocus" || method == "toggleTorch") {
    result->Success();
  } else if (method == "analyzeImage") {
    result->Error(kUnsupportedOperationError,
                  "analyzeImage() is not implemented on Windows.");
  } else {
    result->NotImplemented();
  }
}

void MobileScannerPlugin::Start(
    const flutter::EncodableMap* arguments,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  if (running_) {
    result->Error(kAlreadyStartedError, "The scanner was already started.");
    return;
  }
  if (FAILED(g_mf_startup_result)) {
    result->Error(kGenericError, "Media Foundation could not be initialized.");
    return;
  }

  const auto cameras = EnumerateCameras();
  if (cameras.empty()) {
    result->Error(kNoCameraError, "No camera found.");
    return;
  }

  const std::string requested_camera_id =
      GetString(arguments, "cameraId", std::string());
  CameraDevice selected_camera = cameras.front();
  if (!requested_camera_id.empty()) {
    for (const auto& camera : cameras) {
      if (camera.id == requested_camera_id) {
        selected_camera = camera;
        break;
      }
    }
  }

  detection_speed_ = GetInt(arguments, "speed", kDetectionNormal);
  detection_timeout_ms_ = GetInt(arguments, "timeout", 250);
  return_image_ = GetBool(arguments, "returnImage", false);
  last_detected_value_.clear();
  last_detection_time_ = std::chrono::steady_clock::time_point{};
  last_decode_attempt_ = std::chrono::steady_clock::time_point{};

  int width = 0;
  int height = 0;
  int stride = 0;
  FrameFormat format = FrameFormat::kBgra32;
  HRESULT failure_result = S_OK;
  std::string failure_step;
  if (!OpenCamera(selected_camera, &width, &height, &stride, &format,
                  &failure_result, &failure_step)) {
    if (failure_result == E_ACCESSDENIED) {
      result->Error(kPermissionDeniedError,
                    "Camera access is blocked by Windows privacy settings. "
                    "Turn on Camera access and Let desktop apps access your "
                    "camera, then try again.");
    } else {
      std::string message = "The camera could not be opened";
      if (!failure_step.empty()) {
        message += " while " + failure_step;
      }
      message += ". HRESULT " + HResultMessage(failure_result);
      result->Error(kGenericError, message);
    }
    return;
  }

  {
    std::lock_guard<std::mutex> lock(texture_mutex_);
    frame_width_ = width;
    frame_height_ = height;
    frame_stride_ = stride;
    frame_format_ = format;
    latest_frame_rgba_.clear();
    texture_ = std::make_unique<flutter::TextureVariant>(
        flutter::PixelBufferTexture([this](size_t requested_width,
                                           size_t requested_height) {
          return CopyPixelBuffer(requested_width, requested_height);
        }));
    texture_id_ = texture_registrar_->RegisterTexture(texture_.get());
  }

  if (texture_id_ < 0) {
    CloseCamera();
    result->Error(kGenericError, "The preview texture could not be registered.");
    return;
  }

  paused_ = false;
  running_ = true;
  capture_thread_ = std::thread(&MobileScannerPlugin::CaptureLoop, this);

  SendCamerasEvent();

  flutter::EncodableMap response;
  response[flutter::EncodableValue("textureId")] =
      flutter::EncodableValue(texture_id_);
  response[flutter::EncodableValue("cameraDirection")] =
      flutter::EncodableValue(kCameraFacingExternal);
  response[flutter::EncodableValue("camera")] =
      flutter::EncodableValue(CameraToMap(selected_camera));
  response[flutter::EncodableValue("numberOfCameras")] =
      flutter::EncodableValue(static_cast<int32_t>(cameras.size()));
  response[flutter::EncodableValue("currentTorchState")] =
      flutter::EncodableValue(kTorchUnavailable);
  response[flutter::EncodableValue("size")] = flutter::EncodableValue(
      flutter::EncodableMap{{flutter::EncodableValue("width"),
                             flutter::EncodableValue(static_cast<double>(width))},
                            {flutter::EncodableValue("height"),
                             flutter::EncodableValue(static_cast<double>(height))}});
  result->Success(flutter::EncodableValue(response));
}

void MobileScannerPlugin::Stop(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  StopCapture();
  result->Success();
}

void MobileScannerPlugin::Pause(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  paused_ = true;
  result->Success();
}

void MobileScannerPlugin::UpdateScanWindow(
    const flutter::EncodableMap* arguments,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  const flutter::EncodableValue* rect_value = FindValue(arguments, "rect");
  const auto rect = rect_value == nullptr
                        ? nullptr
                        : std::get_if<flutter::EncodableList>(rect_value);

  {
    std::lock_guard<std::mutex> lock(scan_window_mutex_);
    if (rect == nullptr || rect->size() < 4) {
      scan_window_.reset();
    } else {
      ScanWindow window;
      window.left = ClampUnit(GetDoubleFromValue((*rect)[0], 0.0));
      window.top = ClampUnit(GetDoubleFromValue((*rect)[1], 0.0));
      window.right = ClampUnit(GetDoubleFromValue((*rect)[2], 1.0));
      window.bottom = ClampUnit(GetDoubleFromValue((*rect)[3], 1.0));
      if (window.right <= window.left || window.bottom <= window.top) {
        scan_window_.reset();
      } else {
        scan_window_ = window;
      }
    }
  }

  result->Success();
}

void MobileScannerPlugin::GetAvailableCameras(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  result->Success(flutter::EncodableValue(CamerasToList(EnumerateCameras())));
}

void MobileScannerPlugin::GetSupportedLenses(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  const bool has_camera = !EnumerateCameras().empty();
  flutter::EncodableList lenses;
  if (has_camera) {
    lenses.emplace_back(kLensAny);
  }
  result->Success(flutter::EncodableValue(lenses));
}

std::vector<MobileScannerPlugin::CameraDevice>
MobileScannerPlugin::EnumerateCameras() {
  EnsureMediaFoundation();
  std::vector<CameraDevice> cameras;

  Microsoft::WRL::ComPtr<IMFAttributes> attributes;
  if (FAILED(MFCreateAttributes(&attributes, 1))) {
    return cameras;
  }
  if (FAILED(attributes->SetGUID(
          MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
          MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))) {
    return cameras;
  }

  IMFActivate** devices = nullptr;
  UINT32 count = 0;
  const HRESULT enum_result =
      MFEnumDeviceSources(attributes.Get(), &devices, &count);
  if (FAILED(enum_result)) {
    return cameras;
  }

  for (UINT32 i = 0; i < count; ++i) {
    WCHAR* name = nullptr;
    WCHAR* symbolic_link = nullptr;
    UINT32 name_length = 0;
    UINT32 link_length = 0;

    devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name,
                                   &name_length);
    devices[i]->GetAllocatedString(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &symbolic_link,
        &link_length);

    CameraDevice camera;
    camera.name =
        name == nullptr ? "Camera " + std::to_string(i + 1)
                        : Utf8FromWide(std::wstring(name, name_length));
    camera.symbolic_link =
        symbolic_link == nullptr ? std::wstring()
                                 : std::wstring(symbolic_link, link_length);
    camera.id = camera.symbolic_link.empty()
                    ? std::to_string(i)
                    : Utf8FromWide(camera.symbolic_link);
    camera.is_default = i == 0;
    cameras.push_back(camera);

    FreeAllocatedString(name);
    FreeAllocatedString(symbolic_link);
  }

  ReleaseActivateArray(devices, count);
  return cameras;
}

bool MobileScannerPlugin::OpenCamera(const CameraDevice& camera,
                                     int* width,
                                     int* height,
                                     int* stride,
                                     FrameFormat* format,
                                     HRESULT* failure_result,
                                     std::string* failure_step) {
  auto fail = [failure_result, failure_step](HRESULT result,
                                             const char* step) {
    if (failure_result != nullptr) {
      *failure_result = result;
    }
    if (failure_step != nullptr) {
      *failure_step = step;
    }
    return false;
  };

  CloseCamera();

  Microsoft::WRL::ComPtr<IMFAttributes> attributes;
  HRESULT result = MFCreateAttributes(&attributes, 1);
  if (FAILED(result)) {
    return fail(result, "creating camera attributes");
  }
  result = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                               MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  if (FAILED(result)) {
    return fail(result, "configuring camera enumeration");
  }

  IMFActivate** devices = nullptr;
  UINT32 count = 0;
  result = MFEnumDeviceSources(attributes.Get(), &devices, &count);
  if (FAILED(result)) {
    return fail(result, "enumerating camera devices");
  }

  Microsoft::WRL::ComPtr<IMFMediaSource> media_source;
  HRESULT activate_result = E_FAIL;
  for (UINT32 i = 0; i < count; ++i) {
    WCHAR* symbolic_link = nullptr;
    UINT32 link_length = 0;
    devices[i]->GetAllocatedString(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &symbolic_link,
        &link_length);
    const std::wstring link =
        symbolic_link == nullptr ? std::wstring()
                                 : std::wstring(symbolic_link, link_length);
    FreeAllocatedString(symbolic_link);

    if (link == camera.symbolic_link || camera.symbolic_link.empty()) {
      activate_result = devices[i]->ActivateObject(IID_PPV_ARGS(&media_source));
      break;
    }
  }
  ReleaseActivateArray(devices, count);

  if (!media_source) {
    return fail(activate_result, "activating the camera");
  }

  Microsoft::WRL::ComPtr<IMFAttributes> reader_attributes;
  result = MFCreateAttributes(&reader_attributes, 2);
  if (FAILED(result)) {
    return fail(result, "creating camera reader attributes");
  }
  result = reader_attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,
                                        TRUE);
  if (FAILED(result)) {
    return fail(result, "enabling camera hardware transforms");
  }
  result = reader_attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,
                                        TRUE);
  if (FAILED(result)) {
    return fail(result, "enabling camera video processing");
  }

  Microsoft::WRL::ComPtr<IMFSourceReader> source_reader;
  result = MFCreateSourceReaderFromMediaSource(
      media_source.Get(), reader_attributes.Get(), &source_reader);
  if (FAILED(result)) {
    return fail(result, "creating the camera reader");
  }

  result = source_reader->SetStreamSelection(kAllStreams, FALSE);
  if (FAILED(result)) {
    return fail(result, "configuring camera streams");
  }
  result = source_reader->SetStreamSelection(kFirstVideoStream, TRUE);
  if (FAILED(result)) {
    return fail(result, "selecting the camera video stream");
  }

  Microsoft::WRL::ComPtr<IMFMediaType> current_type;
  FrameFormat selected_format = FrameFormat::kBgra32;
  UINT32 frame_width = 0;
  UINT32 frame_height = 0;
  LONG selected_stride = 0;

  result = SetRgb32Output(source_reader.Get(), &current_type);
  if (SUCCEEDED(result)) {
    if (!ReadSupportedVideoType(current_type.Get(), &selected_format,
                                &frame_width, &frame_height,
                                &selected_stride)) {
      return fail(E_FAIL, "reading the camera RGB32 output format");
    }
  } else {
    HRESULT native_result = result;
    if (!SelectNativeMediaType(source_reader.Get(), &current_type,
                               &selected_format, &frame_width, &frame_height,
                               &selected_stride, &native_result)) {
      return fail(native_result, "selecting a supported camera video format");
    }
  }

  if (frame_width == 0 || frame_height == 0 || selected_stride == 0) {
    return fail(E_FAIL, "reading the camera frame format");
  }

  {
    std::lock_guard<std::mutex> lock(camera_mutex_);
    media_source_ = media_source;
    source_reader_ = source_reader;
  }

  *width = static_cast<int>(frame_width);
  *height = static_cast<int>(frame_height);
  *stride = static_cast<int>(selected_stride);
  *format = selected_format;
  return true;
}

void MobileScannerPlugin::CaptureLoop() {
  const HRESULT co_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool should_uninitialize =
      SUCCEEDED(co_result) && co_result != S_FALSE;

  while (running_) {
    if (paused_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      continue;
    }

    Microsoft::WRL::ComPtr<IMFSourceReader> reader;
    {
      std::lock_guard<std::mutex> lock(camera_mutex_);
      reader = source_reader_;
    }
    if (!reader) {
      break;
    }

    DWORD stream_index = 0;
    DWORD stream_flags = 0;
    LONGLONG timestamp = 0;
    Microsoft::WRL::ComPtr<IMFSample> sample;
    const HRESULT read_result = reader->ReadSample(
        kFirstVideoStream, 0, &stream_index, &stream_flags, &timestamp,
        &sample);

    if (FAILED(read_result)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      continue;
    }
    if ((stream_flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
      break;
    }
    if (!sample) {
      continue;
    }

    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) {
      continue;
    }

    BYTE* data = nullptr;
    DWORD max_length = 0;
    DWORD current_length = 0;
    if (SUCCEEDED(buffer->Lock(&data, &max_length, &current_length))) {
      StoreFrame(data, static_cast<size_t>(current_length));
      buffer->Unlock();
    }
  }

  if (should_uninitialize) {
    CoUninitialize();
  }
}

void MobileScannerPlugin::CloseCamera() {
  std::lock_guard<std::mutex> lock(camera_mutex_);
  if (media_source_) {
    media_source_->Shutdown();
  }
  source_reader_.Reset();
  media_source_.Reset();
}

void MobileScannerPlugin::StopCapture() {
  const bool was_running = running_.exchange(false);
  paused_ = false;

  Microsoft::WRL::ComPtr<IMFSourceReader> reader;
  {
    std::lock_guard<std::mutex> lock(camera_mutex_);
    reader = source_reader_;
  }
  if (reader) {
    reader->Flush(kFirstVideoStream);
  }

  if (capture_thread_.joinable()) {
    capture_thread_.join();
  }

  CloseCamera();

  {
    std::lock_guard<std::mutex> lock(texture_mutex_);
    latest_frame_rgba_.clear();
    frame_width_ = 0;
    frame_height_ = 0;
    frame_stride_ = 0;
    frame_format_ = FrameFormat::kBgra32;
    if (texture_id_ >= 0) {
      texture_registrar_->UnregisterTexture(texture_id_);
      texture_id_ = -1;
    }
    texture_.reset();
  }

  {
    std::lock_guard<std::mutex> lock(scan_window_mutex_);
    scan_window_.reset();
  }

  if (was_running) {
    SendCamerasEvent();
  }
}

void MobileScannerPlugin::StoreFrame(const uint8_t* data, size_t length) {
  int width = 0;
  int height = 0;
  int stride = 0;
  FrameFormat format = FrameFormat::kBgra32;
  {
    std::lock_guard<std::mutex> lock(texture_mutex_);
    width = frame_width_;
    height = frame_height_;
    stride = frame_stride_;
    format = frame_format_;
  }

  if (data == nullptr || width <= 0 || height <= 0 || stride == 0) {
    return;
  }

  const size_t pixel_count =
      static_cast<size_t>(width) * static_cast<size_t>(height);
  const size_t expected_length = pixel_count * 4;
  std::vector<uint8_t> rgba(expected_length);
  const int absolute_stride = std::abs(stride);

  if (format == FrameFormat::kNv12) {
    const size_t y_plane_size =
        static_cast<size_t>(absolute_stride) * static_cast<size_t>(height);
    const size_t uv_plane_size =
        static_cast<size_t>(absolute_stride) *
        static_cast<size_t>((height + 1) / 2);
    if (length < y_plane_size + uv_plane_size ||
        absolute_stride < MinimumRowBytes(format, width)) {
      return;
    }

    const uint8_t* y_plane = data;
    const uint8_t* uv_plane = data + y_plane_size;
    for (int y = 0; y < height; ++y) {
      const uint8_t* y_row = y_plane + static_cast<size_t>(y) * absolute_stride;
      const uint8_t* uv_row =
          uv_plane + static_cast<size_t>(y / 2) * absolute_stride;
      for (int x = 0; x < width; ++x) {
        const int uv_x =
            std::min((x / 2) * 2, std::max(0, absolute_stride - 2));
        const size_t out = (static_cast<size_t>(y) * width + x) * 4;
        YuvToRgb(y_row[x], uv_row[uv_x], uv_row[uv_x + 1], &rgba[out]);
      }
    }
  } else {
    const size_t required_length =
        static_cast<size_t>(absolute_stride) * static_cast<size_t>(height);
    if (length < required_length ||
        absolute_stride < MinimumRowBytes(format, width)) {
      return;
    }

    for (int y = 0; y < height; ++y) {
      const int source_y = stride > 0 ? y : height - 1 - y;
      const uint8_t* row =
          data + static_cast<size_t>(source_y) * absolute_stride;
      for (int x = 0; x < width; ++x) {
        const size_t out = (static_cast<size_t>(y) * width + x) * 4;
        switch (format) {
          case FrameFormat::kBgra32: {
            const uint8_t* pixel = row + static_cast<size_t>(x) * 4;
            rgba[out] = pixel[2];
            rgba[out + 1] = pixel[1];
            rgba[out + 2] = pixel[0];
            rgba[out + 3] = 0xFF;
            break;
          }
          case FrameFormat::kRgb24: {
            const uint8_t* pixel = row + static_cast<size_t>(x) * 3;
            rgba[out] = pixel[2];
            rgba[out + 1] = pixel[1];
            rgba[out + 2] = pixel[0];
            rgba[out + 3] = 0xFF;
            break;
          }
          case FrameFormat::kYuy2: {
            const uint8_t* pair = row + static_cast<size_t>(x / 2) * 4;
            const bool second = (x % 2) == 1;
            YuvToRgb(pair[second ? 2 : 0], pair[1], pair[3], &rgba[out]);
            break;
          }
          case FrameFormat::kNv12:
            break;
        }
      }
    }
  }

  {
    std::lock_guard<std::mutex> lock(texture_mutex_);
    latest_frame_rgba_ = rgba;
  }

  if (texture_id_ >= 0) {
    texture_registrar_->MarkTextureFrameAvailable(texture_id_);
  }

  DecodeFrame(rgba, width, height);
}

void MobileScannerPlugin::DecodeFrame(const std::vector<uint8_t>& rgba,
                                      int width,
                                      int height) {
  const auto now = std::chrono::steady_clock::now();
  if (last_decode_attempt_ != std::chrono::steady_clock::time_point{} &&
      now - last_decode_attempt_ < std::chrono::milliseconds(60)) {
    return;
  }
  last_decode_attempt_ = now;

  const auto value = DecodeQr(rgba, width, height);
  if (!value || value->empty()) {
    return;
  }

  bool should_emit = true;
  if (detection_speed_ == kDetectionNoDuplicates &&
      *value == last_detected_value_) {
    should_emit = false;
  } else if (detection_speed_ == kDetectionNormal &&
             last_detection_time_ != std::chrono::steady_clock::time_point{} &&
             now - last_detection_time_ <
                 std::chrono::milliseconds(detection_timeout_ms_)) {
    should_emit = false;
  } else if (detection_speed_ != kDetectionNoDuplicates &&
             detection_speed_ != kDetectionNormal &&
             detection_speed_ != kDetectionUnrestricted) {
    should_emit = false;
  }

  if (!should_emit) {
    return;
  }

  last_detected_value_ = *value;
  last_detection_time_ = now;
  SendBarcodeEvent(*value, return_image_ ? rgba : std::vector<uint8_t>(), width,
                   height);
}

std::optional<std::string> MobileScannerPlugin::DecodeQr(
    const std::vector<uint8_t>& rgba,
    int width,
    int height) {
  if (rgba.empty() || width <= 0 || height <= 0) {
    return std::nullopt;
  }

  ScanWindow window;
  bool has_window = false;
  {
    std::lock_guard<std::mutex> lock(scan_window_mutex_);
    if (scan_window_) {
      window = *scan_window_;
      has_window = true;
    }
  }

  int left = 0;
  int top = 0;
  int crop_width = width;
  int crop_height = height;
  if (has_window) {
    left = std::clamp(static_cast<int>(std::lround(window.left * width)), 0,
                      std::max(width - 1, 0));
    top = std::clamp(static_cast<int>(std::lround(window.top * height)), 0,
                     std::max(height - 1, 0));
    const int right =
        std::clamp(static_cast<int>(std::lround(window.right * width)), left + 1,
                   width);
    const int bottom = std::clamp(
        static_cast<int>(std::lround(window.bottom * height)), top + 1, height);
    crop_width = right - left;
    crop_height = bottom - top;
  }

  ZXing::ReaderOptions options;
  options.setFormats(ZXing::BarcodeFormat::QRCode)
      .setTryHarder(true)
      .setTryRotate(true)
      .setTryInvert(true)
      .setMaxNumberOfSymbols(1);

  const ZXing::ImageView image(rgba.data(), width, height,
                               ZXing::ImageFormat::RGBA);
  const ZXing::ImageView crop =
      has_window ? image.cropped(left, top, crop_width, crop_height) : image;
  const auto barcodes = ZXing::ReadBarcodes(crop, options);

  for (const auto& barcode : barcodes) {
    if (barcode.isValid()) {
      return barcode.text();
    }
  }
  return std::nullopt;
}

const FlutterDesktopPixelBuffer* MobileScannerPlugin::CopyPixelBuffer(
    size_t width,
    size_t height) {
  std::vector<uint8_t>* frame_copy = nullptr;
  int frame_width = 0;
  int frame_height = 0;
  {
    std::lock_guard<std::mutex> lock(texture_mutex_);
    if (latest_frame_rgba_.empty() || frame_width_ <= 0 || frame_height_ <= 0) {
      return nullptr;
    }
    frame_copy = new std::vector<uint8_t>(latest_frame_rgba_);
    frame_width = frame_width_;
    frame_height = frame_height_;
  }

  pixel_buffer_.buffer = frame_copy->data();
  pixel_buffer_.width = static_cast<size_t>(frame_width);
  pixel_buffer_.height = static_cast<size_t>(frame_height);
  pixel_buffer_.release_context = frame_copy;
  pixel_buffer_.release_callback = [](void* release_context) {
    delete static_cast<std::vector<uint8_t>*>(release_context);
  };
  return &pixel_buffer_;
}

void MobileScannerPlugin::SendCamerasEvent() {
  std::lock_guard<std::mutex> lock(event_sink_mutex_);
  if (!event_sink_) {
    return;
  }
  flutter::EncodableMap event;
  event[flutter::EncodableValue("name")] =
      flutter::EncodableValue(kCamerasEventName);
  event[flutter::EncodableValue("data")] =
      flutter::EncodableValue(CamerasToList(EnumerateCameras()));
  event_sink_->Success(flutter::EncodableValue(event));
}

void MobileScannerPlugin::SendBarcodeEvent(const std::string& value,
                                           const std::vector<uint8_t>& image,
                                           int width,
                                           int height) {
  flutter::EncodableMap barcode;
  barcode[flutter::EncodableValue("rawValue")] = flutter::EncodableValue(value);
  barcode[flutter::EncodableValue("displayValue")] =
      flutter::EncodableValue(value);
  barcode[flutter::EncodableValue("format")] =
      flutter::EncodableValue(kBarcodeFormatQrCode);
  barcode[flutter::EncodableValue("type")] =
      flutter::EncodableValue(kBarcodeTypeText);

  flutter::EncodableMap event;
  event[flutter::EncodableValue("name")] =
      flutter::EncodableValue(kBarcodeEventName);
  event[flutter::EncodableValue("data")] =
      flutter::EncodableValue(flutter::EncodableList{
          flutter::EncodableValue(barcode),
      });

  if (!image.empty()) {
    event[flutter::EncodableValue("image")] = flutter::EncodableValue(
        flutter::EncodableMap{{flutter::EncodableValue("bytes"),
                               flutter::EncodableValue(image)},
                              {flutter::EncodableValue("width"),
                               flutter::EncodableValue(static_cast<double>(width))},
                              {flutter::EncodableValue("height"),
                               flutter::EncodableValue(static_cast<double>(height))}});
  }

  std::lock_guard<std::mutex> lock(event_sink_mutex_);
  if (event_sink_) {
    event_sink_->Success(flutter::EncodableValue(event));
  }
}

flutter::EncodableMap MobileScannerPlugin::CameraToMap(
    const CameraDevice& camera) const {
  return flutter::EncodableMap{
      {flutter::EncodableValue("id"), flutter::EncodableValue(camera.id)},
      {flutter::EncodableValue("name"), flutter::EncodableValue(camera.name)},
      {flutter::EncodableValue("facing"),
       flutter::EncodableValue(camera.id.empty() ? kCameraFacingUnknown
                                                 : kCameraFacingExternal)},
      {flutter::EncodableValue("lensType"), flutter::EncodableValue(kLensAny)},
      {flutter::EncodableValue("isDefault"),
       flutter::EncodableValue(camera.is_default)},
      {flutter::EncodableValue("isExternal"), flutter::EncodableValue(true)},
  };
}

flutter::EncodableList MobileScannerPlugin::CamerasToList(
    const std::vector<CameraDevice>& cameras) const {
  flutter::EncodableList list;
  list.reserve(cameras.size());
  for (const auto& camera : cameras) {
    list.emplace_back(CameraToMap(camera));
  }
  return list;
}

}  // namespace mobile_scanner
