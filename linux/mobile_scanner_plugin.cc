#include "include/mobile_scanner/mobile_scanner_plugin.h"

#include <dlfcn.h>

#include <Barcode.h>
#include <BarcodeFormat.h>
#include <ImageView.h>
#include <ReadBarcode.h>
#include <ReaderOptions.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#define MOBILE_SCANNER_PLUGIN(obj)                                      \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), mobile_scanner_plugin_get_type(), \
                              MobileScannerPlugin))

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

constexpr int kGstStateNull = 1;
constexpr int kGstStatePlaying = 4;
constexpr int kGstStateChangeFailure = 0;
constexpr uint64_t kGstMillisecond = 1000000;
constexpr bool kMirrorPreviewHorizontally = true;

struct _GstElement;
struct _GstCaps;
struct _GstSample;
struct _GstBuffer;
struct _GstStructure;

using GstElement = _GstElement;
using GstCaps = _GstCaps;
using GstSample = _GstSample;
using GstBuffer = _GstBuffer;
using GstStructure = _GstStructure;

struct GstApi {
  void* gstreamer = nullptr;
  void* gst_app = nullptr;
  std::string load_error;

  void (*init)(int*, char***) = nullptr;
  void (*object_unref)(gpointer) = nullptr;
  GstElement* (*pipeline_new)(const gchar*) = nullptr;
  GstElement* (*element_factory_make)(const gchar*, const gchar*) = nullptr;
  gboolean (*bin_add)(GstElement*, GstElement*) = nullptr;
  gboolean (*element_link)(GstElement*, GstElement*) = nullptr;
  GstCaps* (*caps_from_string)(const gchar*) = nullptr;
  void (*caps_unref)(GstCaps*) = nullptr;
  int (*element_set_state)(GstElement*, int) = nullptr;
  GstSample* (*app_sink_try_pull_sample)(void*, uint64_t) = nullptr;
  void (*sample_unref)(GstSample*) = nullptr;
  GstCaps* (*sample_get_caps)(GstSample*) = nullptr;
  GstBuffer* (*sample_get_buffer)(GstSample*) = nullptr;
  GstStructure* (*caps_get_structure)(GstCaps*, guint) = nullptr;
  gboolean (*structure_get_int)(const GstStructure*, const gchar*, gint*) =
      nullptr;
  gsize (*buffer_get_size)(GstBuffer*) = nullptr;
  gsize (*buffer_extract_dup)(GstBuffer*, gsize, gsize, gpointer*, gsize*) =
      nullptr;
};

template <typename T>
bool LoadSymbol(void* library, const char* name, T* out, std::string* error) {
  *out = reinterpret_cast<T>(dlsym(library, name));
  if (*out == nullptr) {
    *error = std::string("Missing GStreamer symbol: ") + name;
    return false;
  }
  return true;
}

GstApi* LoadGStreamer() {
  static GstApi api;
  static std::once_flag once;
  std::call_once(once, []() {
    api.gstreamer = dlopen("libgstreamer-1.0.so.0", RTLD_LAZY | RTLD_LOCAL);
    if (api.gstreamer == nullptr) {
      api.load_error = dlerror();
      return;
    }
    api.gst_app = dlopen("libgstapp-1.0.so.0", RTLD_LAZY | RTLD_LOCAL);
    if (api.gst_app == nullptr) {
      api.load_error = dlerror();
      return;
    }

    std::string error;
    const bool ok =
        LoadSymbol(api.gstreamer, "gst_init", &api.init, &error) &&
        LoadSymbol(api.gstreamer, "gst_object_unref", &api.object_unref,
                   &error) &&
        LoadSymbol(api.gstreamer, "gst_pipeline_new", &api.pipeline_new,
                   &error) &&
        LoadSymbol(api.gstreamer, "gst_element_factory_make",
                   &api.element_factory_make, &error) &&
        LoadSymbol(api.gstreamer, "gst_bin_add", &api.bin_add, &error) &&
        LoadSymbol(api.gstreamer, "gst_element_link", &api.element_link,
                   &error) &&
        LoadSymbol(api.gstreamer, "gst_caps_from_string",
                   &api.caps_from_string, &error) &&
        LoadSymbol(api.gstreamer, "gst_caps_unref", &api.caps_unref, &error) &&
        LoadSymbol(api.gstreamer, "gst_element_set_state",
                   &api.element_set_state, &error) &&
        LoadSymbol(api.gst_app, "gst_app_sink_try_pull_sample",
                   &api.app_sink_try_pull_sample, &error) &&
        LoadSymbol(api.gstreamer, "gst_sample_unref", &api.sample_unref,
                   &error) &&
        LoadSymbol(api.gstreamer, "gst_sample_get_caps", &api.sample_get_caps,
                   &error) &&
        LoadSymbol(api.gstreamer, "gst_sample_get_buffer",
                   &api.sample_get_buffer, &error) &&
        LoadSymbol(api.gstreamer, "gst_caps_get_structure",
                   &api.caps_get_structure, &error) &&
        LoadSymbol(api.gstreamer, "gst_structure_get_int",
                   &api.structure_get_int, &error) &&
        LoadSymbol(api.gstreamer, "gst_buffer_get_size",
                   &api.buffer_get_size, &error) &&
        LoadSymbol(api.gstreamer, "gst_buffer_extract_dup",
                   &api.buffer_extract_dup, &error);
    if (!ok) {
      api.load_error = error;
      return;
    }

    api.init(nullptr, nullptr);
  });
  return api.init == nullptr ? nullptr : &api;
}

std::string GStreamerLoadError() {
  GstApi* api = LoadGStreamer();
  if (api == nullptr) {
    return "GStreamer runtime is not available.";
  }
  return api->load_error.empty() ? "GStreamer runtime is not available."
                                 : api->load_error;
}

struct CameraDevice {
  std::string id;
  std::string name;
  bool is_default = false;
};

struct ScanWindow {
  double left = 0.0;
  double top = 0.0;
  double right = 1.0;
  double bottom = 1.0;
};

struct TextureFrameStore {
  std::mutex mutex;
  std::vector<uint8_t> frame;
  std::vector<uint8_t> copy;
  uint32_t width = 1;
  uint32_t height = 1;
};

typedef struct _MobileScannerFrameTexture {
  FlPixelBufferTexture parent_instance;
  TextureFrameStore* store;
} MobileScannerFrameTexture;

typedef struct {
  FlPixelBufferTextureClass parent_class;
} MobileScannerFrameTextureClass;

GType mobile_scanner_frame_texture_get_type();

#define MOBILE_SCANNER_FRAME_TEXTURE(obj)                                  \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), mobile_scanner_frame_texture_get_type(), \
                              MobileScannerFrameTexture))

G_DEFINE_TYPE(MobileScannerFrameTexture,
              mobile_scanner_frame_texture,
              fl_pixel_buffer_texture_get_type())

static void mobile_scanner_frame_texture_dispose(GObject* object) {
  MobileScannerFrameTexture* self = MOBILE_SCANNER_FRAME_TEXTURE(object);
  delete self->store;
  self->store = nullptr;
  G_OBJECT_CLASS(mobile_scanner_frame_texture_parent_class)->dispose(object);
}

static gboolean mobile_scanner_frame_texture_copy_pixels(
    FlPixelBufferTexture* texture,
    const uint8_t** out_buffer,
    uint32_t* width,
    uint32_t* height,
    GError** error) {
  MobileScannerFrameTexture* self = MOBILE_SCANNER_FRAME_TEXTURE(texture);
  if (self->store == nullptr) {
    return FALSE;
  }

  std::lock_guard<std::mutex> lock(self->store->mutex);
  if (self->store->frame.empty()) {
    static const uint8_t kBlackPixel[] = {0, 0, 0, 255};
    *out_buffer = kBlackPixel;
    *width = 1;
    *height = 1;
    return TRUE;
  }

  self->store->copy = self->store->frame;
  *out_buffer = self->store->copy.data();
  *width = self->store->width;
  *height = self->store->height;
  return TRUE;
}

static void mobile_scanner_frame_texture_class_init(
    MobileScannerFrameTextureClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = mobile_scanner_frame_texture_dispose;
  FL_PIXEL_BUFFER_TEXTURE_CLASS(klass)->copy_pixels =
      mobile_scanner_frame_texture_copy_pixels;
}

static void mobile_scanner_frame_texture_init(
    MobileScannerFrameTexture* self) {
  self->store = new TextureFrameStore();
}

MobileScannerFrameTexture* mobile_scanner_frame_texture_new() {
  return MOBILE_SCANNER_FRAME_TEXTURE(
      g_object_new(mobile_scanner_frame_texture_get_type(), nullptr));
}

void mobile_scanner_frame_texture_set_frame(MobileScannerFrameTexture* texture,
                                            const std::vector<uint8_t>& rgba,
                                            uint32_t width,
                                            uint32_t height) {
  if (texture == nullptr || texture->store == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(texture->store->mutex);
  texture->store->frame = rgba;
  texture->store->width = width;
  texture->store->height = height;
}

struct MobileScannerPluginState {
  FlMethodChannel* method_channel = nullptr;
  FlEventChannel* event_channel = nullptr;
  FlEventChannel* orientation_event_channel = nullptr;
  FlTextureRegistrar* texture_registrar = nullptr;
  GMainContext* main_context = nullptr;

  std::mutex event_mutex;
  bool event_listening = false;

  std::mutex pipeline_mutex;
  GstElement* pipeline = nullptr;
  GstElement* appsink = nullptr;
  std::thread capture_thread;
  std::atomic_bool running{false};
  std::atomic_bool paused{false};

  MobileScannerFrameTexture* texture = nullptr;

  std::mutex start_mutex;
  std::condition_variable start_cv;
  bool first_frame_received = false;
  std::string startup_error;
  int frame_width = 0;
  int frame_height = 0;

  std::mutex scan_window_mutex;
  std::optional<ScanWindow> scan_window;

  int detection_speed = kDetectionNormal;
  int detection_timeout_ms = 250;
  bool return_image = false;
  std::string last_detected_value;
  std::chrono::steady_clock::time_point last_detection_time{};
  std::chrono::steady_clock::time_point last_decode_attempt{};
  uint32_t decode_attempt_count = 0;
};

}  // namespace

struct _MobileScannerPlugin {
  GObject parent_instance;
  MobileScannerPluginState* state;
};

G_DEFINE_TYPE(MobileScannerPlugin, mobile_scanner_plugin, g_object_get_type())

namespace {

std::vector<CameraDevice> EnumerateCameras(GstApi* api) {
  if (api == nullptr) {
    return {};
  }
  CameraDevice camera;
  camera.id = "0";
  camera.name = "Default camera";
  camera.is_default = true;
  return {camera};
}

GstElement* CreateCameraSource(GstApi* api,
                               const std::string& requested_id,
                               CameraDevice* selected_camera) {
  selected_camera->id = requested_id.empty() ? "0" : requested_id;
  selected_camera->name = "Default camera";
  selected_camera->is_default = true;
  return api->element_factory_make("autovideosrc", nullptr);
}

FlValue* CameraToValue(const CameraDevice& camera) {
  FlValue* map = fl_value_new_map();
  fl_value_set_string_take(map, "id", fl_value_new_string(camera.id.c_str()));
  fl_value_set_string_take(
      map, "name", fl_value_new_string(camera.name.c_str()));
  fl_value_set_string_take(
      map, "facing", fl_value_new_int(camera.id.empty() ? kCameraFacingUnknown
                                                         : kCameraFacingExternal));
  fl_value_set_string_take(map, "lensType", fl_value_new_int(kLensAny));
  fl_value_set_string_take(
      map, "isDefault", fl_value_new_bool(camera.is_default));
  fl_value_set_string_take(map, "isExternal", fl_value_new_bool(true));
  return map;
}

FlValue* CamerasToValue(const std::vector<CameraDevice>& cameras) {
  FlValue* list = fl_value_new_list();
  for (const auto& camera : cameras) {
    fl_value_append_take(list, CameraToValue(camera));
  }
  return list;
}

FlValue* SizeToValue(int width, int height) {
  FlValue* size = fl_value_new_map();
  fl_value_set_string_take(
      size, "width", fl_value_new_float(static_cast<double>(width)));
  fl_value_set_string_take(
      size, "height", fl_value_new_float(static_cast<double>(height)));
  return size;
}

FlValue* FindValue(FlValue* map, const char* key) {
  if (map == nullptr || fl_value_get_type(map) != FL_VALUE_TYPE_MAP) {
    return nullptr;
  }
  return fl_value_lookup_string(map, key);
}

std::string GetString(FlValue* map,
                      const char* key,
                      const std::string& fallback = std::string()) {
  FlValue* value = FindValue(map, key);
  if (value == nullptr || fl_value_get_type(value) != FL_VALUE_TYPE_STRING) {
    return fallback;
  }
  return fl_value_get_string(value);
}

int GetInt(FlValue* map, const char* key, int fallback) {
  FlValue* value = FindValue(map, key);
  if (value == nullptr) {
    return fallback;
  }
  if (fl_value_get_type(value) == FL_VALUE_TYPE_INT) {
    return static_cast<int>(fl_value_get_int(value));
  }
  if (fl_value_get_type(value) == FL_VALUE_TYPE_FLOAT) {
    return static_cast<int>(fl_value_get_float(value));
  }
  return fallback;
}

bool GetBool(FlValue* map, const char* key, bool fallback) {
  FlValue* value = FindValue(map, key);
  if (value == nullptr || fl_value_get_type(value) != FL_VALUE_TYPE_BOOL) {
    return fallback;
  }
  return fl_value_get_bool(value);
}

double GetDoubleFromValue(FlValue* value, double fallback) {
  if (value == nullptr) {
    return fallback;
  }
  if (fl_value_get_type(value) == FL_VALUE_TYPE_FLOAT) {
    return fl_value_get_float(value);
  }
  if (fl_value_get_type(value) == FL_VALUE_TYPE_INT) {
    return static_cast<double>(fl_value_get_int(value));
  }
  return fallback;
}

double ClampUnit(double value) {
  return std::clamp(value, 0.0, 1.0);
}

std::vector<uint8_t> MirrorRgbaHorizontally(const std::vector<uint8_t>& rgba,
                                            int width,
                                            int height) {
  if (rgba.empty() || width <= 1 || height <= 0) {
    return rgba;
  }

  const size_t row_bytes = static_cast<size_t>(width) * 4;
  const size_t required = row_bytes * static_cast<size_t>(height);
  if (rgba.size() < required) {
    return rgba;
  }

  std::vector<uint8_t> mirrored(required);
  for (int y = 0; y < height; ++y) {
    const uint8_t* source_row = rgba.data() + static_cast<size_t>(y) * row_bytes;
    uint8_t* destination_row =
        mirrored.data() + static_cast<size_t>(y) * row_bytes;

    for (int x = 0; x < width; ++x) {
      const uint8_t* source_pixel =
          source_row + static_cast<size_t>(width - 1 - x) * 4;
      uint8_t* destination_pixel = destination_row + static_cast<size_t>(x) * 4;
      std::memcpy(destination_pixel, source_pixel, 4);
    }
  }
  return mirrored;
}

std::optional<std::string> ReadQrFromImage(
    const ZXing::ImageView& image,
    const ZXing::ReaderOptions& options) {
  const auto barcodes = ZXing::ReadBarcodes(image, options);
  for (const auto& barcode : barcodes) {
    if (barcode.isValid()) {
      return barcode.text();
    }
  }
  return std::nullopt;
}

void SetStartupError(MobileScannerPluginState* state, const std::string& error) {
  {
    std::lock_guard<std::mutex> lock(state->start_mutex);
    if (state->startup_error.empty()) {
      state->startup_error = error;
    }
  }
  state->start_cv.notify_all();
}

struct MarkTextureData {
  FlTextureRegistrar* registrar;
  FlTexture* texture;
};

gboolean MarkTextureFrameOnMain(gpointer user_data) {
  std::unique_ptr<MarkTextureData> data(
      static_cast<MarkTextureData*>(user_data));
  fl_texture_registrar_mark_texture_frame_available(data->registrar,
                                                    data->texture);
  g_object_unref(data->texture);
  g_object_unref(data->registrar);
  return G_SOURCE_REMOVE;
}

void ScheduleTextureFrame(MobileScannerPluginState* state) {
  if (state->texture == nullptr || state->texture_registrar == nullptr ||
      state->main_context == nullptr) {
    return;
  }
  auto* data = new MarkTextureData{
      FL_TEXTURE_REGISTRAR(g_object_ref(state->texture_registrar)),
      FL_TEXTURE(g_object_ref(state->texture)),
  };
  g_main_context_invoke(state->main_context, MarkTextureFrameOnMain, data);
}

struct BarcodeEventData {
  FlEventChannel* channel;
  std::string value;
  std::vector<uint8_t> image;
  int width;
  int height;
};

gboolean SendBarcodeEventOnMain(gpointer user_data) {
  std::unique_ptr<BarcodeEventData> data(
      static_cast<BarcodeEventData*>(user_data));

  FlValue* barcode = fl_value_new_map();
  fl_value_set_string_take(
      barcode, "rawValue", fl_value_new_string(data->value.c_str()));
  fl_value_set_string_take(
      barcode, "displayValue", fl_value_new_string(data->value.c_str()));
  fl_value_set_string_take(
      barcode, "format", fl_value_new_int(kBarcodeFormatQrCode));
  fl_value_set_string_take(
      barcode, "type", fl_value_new_int(kBarcodeTypeText));

  FlValue* list = fl_value_new_list();
  fl_value_append_take(list, barcode);

  g_autoptr(FlValue) event = fl_value_new_map();
  fl_value_set_string_take(
      event, "name", fl_value_new_string(kBarcodeEventName));
  fl_value_set_string_take(event, "data", list);

  if (!data->image.empty()) {
    FlValue* image = fl_value_new_map();
    fl_value_set_string_take(
        image, "bytes",
        fl_value_new_uint8_list(data->image.data(), data->image.size()));
    fl_value_set_string_take(
        image, "width", fl_value_new_float(static_cast<double>(data->width)));
    fl_value_set_string_take(
        image, "height", fl_value_new_float(static_cast<double>(data->height)));
    fl_value_set_string_take(event, "image", image);
  }

  g_autoptr(GError) error = nullptr;
  fl_event_channel_send(data->channel, event, nullptr, &error);
  g_object_unref(data->channel);
  return G_SOURCE_REMOVE;
}

void SendBarcodeEvent(MobileScannerPluginState* state,
                      const std::string& value,
                      const std::vector<uint8_t>& image,
                      int width,
                      int height) {
  {
    std::lock_guard<std::mutex> lock(state->event_mutex);
    if (!state->event_listening || state->event_channel == nullptr) {
      return;
    }
  }

  auto* data = new BarcodeEventData{
      FL_EVENT_CHANNEL(g_object_ref(state->event_channel)), value, image, width,
      height};
  g_main_context_invoke(state->main_context, SendBarcodeEventOnMain, data);
}

struct CamerasEventData {
  FlEventChannel* channel;
  std::vector<CameraDevice> cameras;
};

gboolean SendCamerasEventOnMain(gpointer user_data) {
  std::unique_ptr<CamerasEventData> data(
      static_cast<CamerasEventData*>(user_data));
  g_autoptr(FlValue) event = fl_value_new_map();
  fl_value_set_string_take(
      event, "name", fl_value_new_string(kCamerasEventName));
  fl_value_set_string_take(event, "data", CamerasToValue(data->cameras));
  g_autoptr(GError) error = nullptr;
  fl_event_channel_send(data->channel, event, nullptr, &error);
  g_object_unref(data->channel);
  return G_SOURCE_REMOVE;
}

void SendCamerasEvent(MobileScannerPluginState* state) {
  {
    std::lock_guard<std::mutex> lock(state->event_mutex);
    if (!state->event_listening || state->event_channel == nullptr) {
      return;
    }
  }

  auto* data = new CamerasEventData{
      FL_EVENT_CHANNEL(g_object_ref(state->event_channel)),
      EnumerateCameras(LoadGStreamer())};
  g_main_context_invoke(state->main_context, SendCamerasEventOnMain, data);
}

std::optional<std::string> DecodeQr(MobileScannerPluginState* state,
                                    const std::vector<uint8_t>& rgba,
                                    int width,
                                    int height,
                                    bool include_full_frame_fallback) {
  if (rgba.empty() || width <= 0 || height <= 0) {
    return std::nullopt;
  }

  ScanWindow window;
  bool has_window = false;
  {
    std::lock_guard<std::mutex> lock(state->scan_window_mutex);
    if (state->scan_window) {
      window = *state->scan_window;
      has_window = true;
    }
  }

  int left = 0;
  int top = 0;
  int crop_width = width;
  int crop_height = height;
  if (has_window) {
    const double source_left =
        kMirrorPreviewHorizontally ? 1.0 - window.right : window.left;
    const double source_right =
        kMirrorPreviewHorizontally ? 1.0 - window.left : window.right;

    left = std::clamp(static_cast<int>(std::lround(source_left * width)), 0,
                      std::max(width - 1, 0));
    top = std::clamp(static_cast<int>(std::lround(window.top * height)), 0,
                     std::max(height - 1, 0));
    const int right =
        std::clamp(static_cast<int>(std::lround(source_right * width)),
                   left + 1, width);
    const int bottom =
        std::clamp(static_cast<int>(std::lround(window.bottom * height)),
                   top + 1, height);
    crop_width = right - left;
    crop_height = bottom - top;
  }

  ZXing::ReaderOptions options;
  options.setFormats(ZXing::BarcodeFormat::QRCode)
      .setTryHarder(true)
      .setTryRotate(true)
      .setTryInvert(true)
      .setMaxNumberOfSymbols(1);

  const ZXing::ImageView image(
      rgba.data(), width, height, ZXing::ImageFormat::RGBA);
  if (!has_window) {
    return ReadQrFromImage(image, options);
  }

  const ZXing::ImageView crop = image.cropped(left, top, crop_width,
                                              crop_height);
  if (const auto value = ReadQrFromImage(crop, options)) {
    return value;
  }

  if (!include_full_frame_fallback ||
      (left == 0 && top == 0 && crop_width == width &&
       crop_height == height)) {
    return std::nullopt;
  }

  return ReadQrFromImage(image, options);
}

void DecodeFrame(MobileScannerPluginState* state,
                 const std::vector<uint8_t>& rgba,
                 int width,
                 int height) {
  const auto now = std::chrono::steady_clock::now();
  if (state->last_decode_attempt != std::chrono::steady_clock::time_point{} &&
      now - state->last_decode_attempt < std::chrono::milliseconds(60)) {
    return;
  }
  state->last_decode_attempt = now;
  ++state->decode_attempt_count;

  const bool include_full_frame_fallback =
      (state->decode_attempt_count % 5 == 0) ||
      state->detection_speed == kDetectionUnrestricted;
  const auto value = DecodeQr(state, rgba, width, height,
                              include_full_frame_fallback);
  if (!value || value->empty()) {
    return;
  }

  bool should_emit = true;
  if (state->detection_speed == kDetectionNoDuplicates &&
      *value == state->last_detected_value) {
    should_emit = false;
  } else if (state->detection_speed == kDetectionNormal &&
             state->last_detection_time !=
                 std::chrono::steady_clock::time_point{} &&
             now - state->last_detection_time <
                 std::chrono::milliseconds(state->detection_timeout_ms)) {
    should_emit = false;
  } else if (state->detection_speed != kDetectionNoDuplicates &&
             state->detection_speed != kDetectionNormal &&
             state->detection_speed != kDetectionUnrestricted) {
    should_emit = false;
  }

  if (!should_emit) {
    return;
  }

  state->last_detected_value = *value;
  state->last_detection_time = now;
  SendBarcodeEvent(state, *value,
                   state->return_image ? rgba : std::vector<uint8_t>(), width,
                   height);
}

bool ProcessSample(MobileScannerPluginState* state,
                   GstApi* api,
                   GstSample* sample) {
  GstCaps* caps = api->sample_get_caps(sample);
  GstBuffer* buffer = api->sample_get_buffer(sample);
  if (caps == nullptr || buffer == nullptr) {
    return false;
  }

  GstStructure* structure = api->caps_get_structure(caps, 0);
  gint width = 0;
  gint height = 0;
  if (structure == nullptr ||
      !api->structure_get_int(structure, "width", &width) ||
      !api->structure_get_int(structure, "height", &height) || width <= 0 ||
      height <= 0) {
    return false;
  }

  const gsize required =
      static_cast<gsize>(width) * static_cast<gsize>(height) * 4;
  const gsize available = api->buffer_get_size(buffer);
  if (available < required) {
    return false;
  }

  gpointer copied_data = nullptr;
  gsize copied_size = 0;
  api->buffer_extract_dup(buffer, 0, required, &copied_data, &copied_size);
  if (copied_data == nullptr || copied_size < required) {
    g_free(copied_data);
    return false;
  }

  std::vector<uint8_t> rgba(static_cast<uint8_t*>(copied_data),
                            static_cast<uint8_t*>(copied_data) + required);
  g_free(copied_data);

  const std::vector<uint8_t> preview =
      kMirrorPreviewHorizontally ? MirrorRgbaHorizontally(rgba, width, height)
                                 : rgba;
  mobile_scanner_frame_texture_set_frame(
      state->texture, preview, static_cast<uint32_t>(width),
      static_cast<uint32_t>(height));

  {
    std::lock_guard<std::mutex> lock(state->start_mutex);
    state->frame_width = width;
    state->frame_height = height;
    if (!state->first_frame_received) {
      state->first_frame_received = true;
      state->start_cv.notify_all();
    }
  }

  ScheduleTextureFrame(state);
  DecodeFrame(state, rgba, width, height);
  return true;
}

void CaptureLoop(MobileScannerPluginState* state) {
  GstApi* api = LoadGStreamer();
  GstElement* pipeline = nullptr;
  GstElement* appsink = nullptr;
  {
    std::lock_guard<std::mutex> lock(state->pipeline_mutex);
    pipeline = state->pipeline;
    appsink = state->appsink;
  }

  if (api == nullptr || pipeline == nullptr || appsink == nullptr) {
    SetStartupError(state, "The camera pipeline was not initialized.");
    return;
  }

  if (api->element_set_state(pipeline, kGstStatePlaying) ==
      kGstStateChangeFailure) {
    SetStartupError(state, "The camera pipeline could not be started.");
    state->running = false;
  }

  while (state->running) {
    if (state->paused) {
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      continue;
    }

    GstSample* sample =
        api->app_sink_try_pull_sample(appsink, 100 * kGstMillisecond);
    if (sample == nullptr) {
      continue;
    }
    ProcessSample(state, api, sample);
    api->sample_unref(sample);
  }

  api->element_set_state(pipeline, kGstStateNull);
}

void StopCapture(MobileScannerPlugin* self) {
  MobileScannerPluginState* state = self->state;
  if (state == nullptr) {
    return;
  }

  state->running = false;
  state->paused = false;

  GstElement* pipeline = nullptr;
  {
    std::lock_guard<std::mutex> lock(state->pipeline_mutex);
    pipeline = state->pipeline;
  }
  if (pipeline != nullptr) {
    if (GstApi* api = LoadGStreamer()) {
      api->element_set_state(pipeline, kGstStateNull);
    }
  }

  if (state->capture_thread.joinable()) {
    state->capture_thread.join();
  }

  {
    std::lock_guard<std::mutex> lock(state->pipeline_mutex);
    if (state->pipeline != nullptr) {
      if (GstApi* api = LoadGStreamer()) {
        api->object_unref(state->pipeline);
      } else {
        g_object_unref(state->pipeline);
      }
      state->pipeline = nullptr;
      state->appsink = nullptr;
    }
  }

  if (state->texture != nullptr) {
    if (state->texture_registrar != nullptr) {
      fl_texture_registrar_unregister_texture(state->texture_registrar,
                                              FL_TEXTURE(state->texture));
    }
    g_object_unref(state->texture);
    state->texture = nullptr;
  }

  {
    std::lock_guard<std::mutex> lock(state->scan_window_mutex);
    state->scan_window.reset();
  }

  {
    std::lock_guard<std::mutex> lock(state->start_mutex);
    state->first_frame_received = false;
    state->startup_error.clear();
    state->frame_width = 0;
    state->frame_height = 0;
  }
}

bool AddAndLink(GstApi* api,
                GstElement* pipeline,
                const std::vector<GstElement*>& elements) {
  for (GstElement* element : elements) {
    if (element == nullptr || !api->bin_add(pipeline, element)) {
      return false;
    }
  }
  for (size_t i = 1; i < elements.size(); ++i) {
    if (!api->element_link(elements[i - 1], elements[i])) {
      return false;
    }
  }
  return true;
}

FlMethodResponse* Start(MobileScannerPlugin* self, FlValue* arguments) {
  MobileScannerPluginState* state = self->state;
  if (state->running) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        kAlreadyStartedError, "The scanner was already started.", nullptr));
  }

  GstApi* api = LoadGStreamer();
  if (api == nullptr) {
    const std::string message = GStreamerLoadError();
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        kGenericError, message.c_str(), nullptr));
  }

  const auto cameras = EnumerateCameras(api);
  if (cameras.empty()) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        kNoCameraError, "No camera found.", nullptr));
  }

  const std::string requested_camera_id =
      GetString(arguments, "cameraId", std::string());
  CameraDevice selected_camera;
  GstElement* source = CreateCameraSource(api, requested_camera_id,
                                          &selected_camera);
  if (source == nullptr) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        kNoCameraError, "The selected camera could not be opened.", nullptr));
  }

  GstElement* pipeline = api->pipeline_new("mobile_scanner_pipeline");
  GstElement* convert = api->element_factory_make("videoconvert", nullptr);
  GstElement* scale = api->element_factory_make("videoscale", nullptr);
  GstElement* filter = api->element_factory_make("capsfilter", nullptr);
  GstElement* sink = api->element_factory_make("appsink", "sink");
  GstCaps* caps = api->caps_from_string("video/x-raw,format=RGBA");
  if (pipeline == nullptr || convert == nullptr || scale == nullptr ||
      filter == nullptr || sink == nullptr || caps == nullptr) {
    if (caps != nullptr) {
      api->caps_unref(caps);
    }
    for (GstElement* element : {source, pipeline, convert, scale, filter, sink}) {
      if (element != nullptr) {
        api->object_unref(element);
      }
    }
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        kGenericError,
        "The GStreamer camera pipeline elements could not be created.",
        nullptr));
  }

  g_object_set(filter, "caps", caps, nullptr);
  api->caps_unref(caps);
  g_object_set(sink, "emit-signals", FALSE, "sync", FALSE, "max-buffers", 1,
               "drop", TRUE, nullptr);

  if (!AddAndLink(api, pipeline, {source, convert, scale, filter, sink})) {
    api->object_unref(pipeline);
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        kGenericError, "The GStreamer camera pipeline could not be linked.",
        nullptr));
  }

  state->detection_speed = GetInt(arguments, "speed", kDetectionNormal);
  state->detection_timeout_ms = GetInt(arguments, "timeout", 250);
  state->return_image = GetBool(arguments, "returnImage", false);
  state->last_detected_value.clear();
  state->last_detection_time = std::chrono::steady_clock::time_point{};
  state->last_decode_attempt = std::chrono::steady_clock::time_point{};
  state->decode_attempt_count = 0;

  state->texture = mobile_scanner_frame_texture_new();
  if (!fl_texture_registrar_register_texture(state->texture_registrar,
                                             FL_TEXTURE(state->texture))) {
    g_object_unref(state->texture);
    state->texture = nullptr;
    api->object_unref(pipeline);
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        kGenericError, "The preview texture could not be registered.",
        nullptr));
  }

  {
    std::lock_guard<std::mutex> lock(state->start_mutex);
    state->first_frame_received = false;
    state->startup_error.clear();
    state->frame_width = 0;
    state->frame_height = 0;
  }

  {
    std::lock_guard<std::mutex> lock(state->pipeline_mutex);
    state->pipeline = pipeline;
    state->appsink = sink;
  }

  state->paused = false;
  state->running = true;
  state->capture_thread = std::thread(CaptureLoop, state);

  int width = 0;
  int height = 0;
  std::string startup_error;
  {
    std::unique_lock<std::mutex> lock(state->start_mutex);
    state->start_cv.wait_for(lock, std::chrono::seconds(3), [&state]() {
      return state->first_frame_received || !state->startup_error.empty() ||
             !state->running.load();
    });
    width = state->frame_width;
    height = state->frame_height;
    startup_error = state->startup_error;
  }

  if (!startup_error.empty() || width <= 0 || height <= 0) {
    StopCapture(self);
    const std::string message =
        startup_error.empty() ? "Timed out waiting for a camera frame."
                              : startup_error;
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kGenericError, message.c_str(), nullptr));
  }

  SendCamerasEvent(state);

  g_autoptr(FlValue) response = fl_value_new_map();
  fl_value_set_string_take(
      response, "textureId",
      fl_value_new_int(fl_texture_get_id(FL_TEXTURE(state->texture))));
  fl_value_set_string_take(
      response, "cameraDirection", fl_value_new_int(kCameraFacingExternal));
  fl_value_set_string_take(response, "camera", CameraToValue(selected_camera));
  fl_value_set_string_take(
      response, "numberOfCameras",
      fl_value_new_int(static_cast<int64_t>(cameras.size())));
  fl_value_set_string_take(
      response, "currentTorchState", fl_value_new_int(kTorchUnavailable));
  fl_value_set_string_take(response, "size", SizeToValue(width, height));
  return FL_METHOD_RESPONSE(fl_method_success_response_new(response));
}

FlMethodResponse* Stop(MobileScannerPlugin* self) {
  StopCapture(self);
  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

FlMethodResponse* Pause(MobileScannerPlugin* self) {
  self->state->paused = true;
  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

FlMethodResponse* UpdateScanWindow(MobileScannerPlugin* self,
                                   FlValue* arguments) {
  FlValue* rect = FindValue(arguments, "rect");
  std::lock_guard<std::mutex> lock(self->state->scan_window_mutex);
  if (rect == nullptr || fl_value_get_type(rect) != FL_VALUE_TYPE_LIST ||
      fl_value_get_length(rect) < 4) {
    self->state->scan_window.reset();
  } else {
    ScanWindow window;
    window.left = ClampUnit(GetDoubleFromValue(fl_value_get_list_value(rect, 0),
                                               0.0));
    window.top = ClampUnit(GetDoubleFromValue(fl_value_get_list_value(rect, 1),
                                              0.0));
    window.right = ClampUnit(GetDoubleFromValue(fl_value_get_list_value(rect, 2),
                                                1.0));
    window.bottom = ClampUnit(GetDoubleFromValue(
        fl_value_get_list_value(rect, 3), 1.0));
    if (window.right <= window.left || window.bottom <= window.top) {
      self->state->scan_window.reset();
    } else {
      self->state->scan_window = window;
    }
  }

  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

FlMethodResponse* GetAvailableCameras() {
  GstApi* api = LoadGStreamer();
  g_autoptr(FlValue) cameras = CamerasToValue(EnumerateCameras(api));
  return FL_METHOD_RESPONSE(fl_method_success_response_new(cameras));
}

FlMethodResponse* GetSupportedLenses() {
  g_autoptr(FlValue) lenses = fl_value_new_list();
  if (!EnumerateCameras(LoadGStreamer()).empty()) {
    fl_value_append_take(lenses, fl_value_new_int(kLensAny));
  }
  return FL_METHOD_RESPONSE(fl_method_success_response_new(lenses));
}

FlMethodResponse* Unsupported(const char* method) {
  return FL_METHOD_RESPONSE(fl_method_error_response_new(
      kUnsupportedOperationError, method, nullptr));
}

void HandleMethodCall(MobileScannerPlugin* self, FlMethodCall* method_call) {
  g_autoptr(FlMethodResponse) response = nullptr;
  const gchar* method = fl_method_call_get_name(method_call);
  FlValue* arguments = fl_method_call_get_args(method_call);

  if (strcmp(method, "state") == 0) {
    response = FL_METHOD_RESPONSE(
        fl_method_success_response_new(fl_value_new_int(kAuthorizationAuthorized)));
  } else if (strcmp(method, "request") == 0) {
    response = FL_METHOD_RESPONSE(
        fl_method_success_response_new(fl_value_new_bool(true)));
  } else if (strcmp(method, "start") == 0) {
    response = Start(self, arguments);
  } else if (strcmp(method, "stop") == 0) {
    response = Stop(self);
  } else if (strcmp(method, "pause") == 0) {
    response = Pause(self);
  } else if (strcmp(method, "updateScanWindow") == 0) {
    response = UpdateScanWindow(self, arguments);
  } else if (strcmp(method, "getAvailableCameras") == 0) {
    response = GetAvailableCameras();
  } else if (strcmp(method, "getSupportedLenses") == 0) {
    response = GetSupportedLenses();
  } else if (strcmp(method, "resetScale") == 0 ||
             strcmp(method, "setScale") == 0 ||
             strcmp(method, "toggleTorch") == 0) {
    response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
  } else if (strcmp(method, "analyzeImage") == 0 ||
             strcmp(method, "setFocus") == 0) {
    response = Unsupported(method);
  } else {
    response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
  }

  fl_method_call_respond(method_call, response, nullptr);
}

FlMethodErrorResponse* EventListen(FlEventChannel* channel,
                                   FlValue* args,
                                   gpointer user_data) {
  MobileScannerPlugin* self = MOBILE_SCANNER_PLUGIN(user_data);
  {
    std::lock_guard<std::mutex> lock(self->state->event_mutex);
    self->state->event_listening = true;
  }
  SendCamerasEvent(self->state);
  return nullptr;
}

FlMethodErrorResponse* EventCancel(FlEventChannel* channel,
                                   FlValue* args,
                                   gpointer user_data) {
  MobileScannerPlugin* self = MOBILE_SCANNER_PLUGIN(user_data);
  std::lock_guard<std::mutex> lock(self->state->event_mutex);
  self->state->event_listening = false;
  return nullptr;
}

FlMethodErrorResponse* NoopListen(FlEventChannel* channel,
                                  FlValue* args,
                                  gpointer user_data) {
  return nullptr;
}

FlMethodErrorResponse* NoopCancel(FlEventChannel* channel,
                                  FlValue* args,
                                  gpointer user_data) {
  return nullptr;
}

void MethodCallCb(FlMethodChannel* channel,
                  FlMethodCall* method_call,
                  gpointer user_data) {
  HandleMethodCall(MOBILE_SCANNER_PLUGIN(user_data), method_call);
}

}  // namespace

static void mobile_scanner_plugin_dispose(GObject* object) {
  MobileScannerPlugin* self = MOBILE_SCANNER_PLUGIN(object);
  if (self->state != nullptr) {
    StopCapture(self);
    g_clear_object(&self->state->method_channel);
    g_clear_object(&self->state->event_channel);
    g_clear_object(&self->state->orientation_event_channel);
    g_clear_object(&self->state->texture_registrar);
    if (self->state->main_context != nullptr) {
      g_main_context_unref(self->state->main_context);
      self->state->main_context = nullptr;
    }
    delete self->state;
    self->state = nullptr;
  }
  G_OBJECT_CLASS(mobile_scanner_plugin_parent_class)->dispose(object);
}

static void mobile_scanner_plugin_class_init(MobileScannerPluginClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = mobile_scanner_plugin_dispose;
}

static void mobile_scanner_plugin_init(MobileScannerPlugin* self) {
  self->state = new MobileScannerPluginState();
  self->state->main_context = g_main_context_ref(g_main_context_default());
}

void mobile_scanner_plugin_register_with_registrar(
    FlPluginRegistrar* registrar) {
  MobileScannerPlugin* plugin = MOBILE_SCANNER_PLUGIN(
      g_object_new(mobile_scanner_plugin_get_type(), nullptr));

  FlBinaryMessenger* messenger = fl_plugin_registrar_get_messenger(registrar);
  plugin->state->texture_registrar = FL_TEXTURE_REGISTRAR(g_object_ref(
      fl_plugin_registrar_get_texture_registrar(registrar)));

  g_autoptr(FlStandardMethodCodec) method_codec =
      fl_standard_method_codec_new();
  plugin->state->method_channel =
      fl_method_channel_new(messenger, kMethodChannelName,
                            FL_METHOD_CODEC(method_codec));
  fl_method_channel_set_method_call_handler(
      plugin->state->method_channel, MethodCallCb, g_object_ref(plugin),
      g_object_unref);

  g_autoptr(FlStandardMethodCodec) event_codec = fl_standard_method_codec_new();
  plugin->state->event_channel =
      fl_event_channel_new(messenger, kEventChannelName,
                           FL_METHOD_CODEC(event_codec));
  fl_event_channel_set_stream_handlers(
      plugin->state->event_channel, EventListen, EventCancel,
      g_object_ref(plugin), g_object_unref);

  g_autoptr(FlStandardMethodCodec) orientation_codec =
      fl_standard_method_codec_new();
  plugin->state->orientation_event_channel =
      fl_event_channel_new(messenger, kOrientationEventChannelName,
                           FL_METHOD_CODEC(orientation_codec));
  fl_event_channel_set_stream_handlers(
      plugin->state->orientation_event_channel, NoopListen, NoopCancel,
      g_object_ref(plugin), g_object_unref);

  g_object_unref(plugin);
}
