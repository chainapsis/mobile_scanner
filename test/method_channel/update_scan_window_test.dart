import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mobile_scanner/src/enums/camera_facing.dart';
import 'package:mobile_scanner/src/enums/camera_lens_type.dart';
import 'package:mobile_scanner/src/enums/detection_speed.dart';
import 'package:mobile_scanner/src/method_channel/mobile_scanner_method_channel.dart';
import 'package:mobile_scanner/src/objects/start_options.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('mirrors a scan window horizontally in normalized coordinates', () {
    const window = Rect.fromLTRB(0.1, 0.2, 0.4, 0.8);

    final mirrored = MethodChannelMobileScanner.mirrorScanWindowHorizontally(
      window,
    );

    expect(mirrored, const Rect.fromLTRB(0.6, 0.2, 0.9, 0.8));
  });

  test('a centered scan window is unchanged by horizontal mirroring', () {
    const window = Rect.fromLTRB(0.25, 0.1, 0.75, 0.9);

    final mirrored = MethodChannelMobileScanner.mirrorScanWindowHorizontally(
      window,
    );

    expect(mirrored, window);
  });

  test('mirroring twice restores the original scan window', () {
    const window = Rect.fromLTRB(0.05, 0.15, 0.45, 0.85);

    final mirrored = MethodChannelMobileScanner.mirrorScanWindowHorizontally(
      window,
    );
    final restored = MethodChannelMobileScanner.mirrorScanWindowHorizontally(
      mirrored,
    );

    expect(restored.left, closeTo(window.left, 1e-12));
    expect(restored.top, closeTo(window.top, 1e-12));
    expect(restored.right, closeTo(window.right, 1e-12));
    expect(restored.bottom, closeTo(window.bottom, 1e-12));
  });

  test('sends a mirrored scan window to the Windows native plugin', () async {
    debugDefaultTargetPlatformOverride = TargetPlatform.windows;
    final platform = MethodChannelMobileScanner();
    MethodCall? updateCall;
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(platform.methodChannel, (call) async {
          switch (call.method) {
            case MethodChannelMobileScanner.kAuthorizationStateMethodName:
              return 1;
            case MethodChannelMobileScanner.kStartCameraMethodName:
              return <String, Object?>{
                'textureId': 1,
                'cameraDirection': CameraFacing.external.rawValue,
                'currentTorchState': -1,
                'numberOfCameras': 1,
                'size': <String, double>{'width': 1280, 'height': 720},
              };
            case MethodChannelMobileScanner.kUpdateScanWindowMethodName:
              updateCall = call;
          }
          return null;
        });

    await platform.start(_startOptions);
    await platform.updateScanWindow(const Rect.fromLTRB(0.1, 0.2, 0.4, 0.8));

    expect(updateCall?.arguments, {
      'rect': <double>[0.6, 0.2, 0.9, 0.8],
    });

    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(platform.methodChannel, null);
    debugDefaultTargetPlatformOverride = null;
  });
}

const _startOptions = StartOptions(
  cameraDirection: CameraFacing.external,
  cameraLensType: CameraLensType.any,
  cameraResolution: null,
  detectionSpeed: DetectionSpeed.normal,
  detectionTimeoutMs: 250,
  formats: [],
  returnImage: false,
  torchEnabled: false,
  invertImage: false,
  autoZoom: false,
  initialZoom: null,
);
