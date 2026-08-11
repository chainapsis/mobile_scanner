import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mobile_scanner/mobile_scanner.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('mirrors the camera preview on Windows', (tester) async {
    debugDefaultTargetPlatformOverride = TargetPlatform.windows;
    MobileScannerPlatform.instance = _FakeMobileScannerPlatform();

    await tester.pumpWidget(
      const MaterialApp(home: Scaffold(body: MobileScanner())),
    );
    await tester.pumpAndSettle();

    final transforms = tester.widgetList<Transform>(
      find.ancestor(
        of: find.byKey(const ValueKey('camera-view')),
        matching: find.byType(Transform),
      ),
    );
    expect(
      transforms.any((widget) => widget.transform.entry(0, 0) == -1),
      true,
    );
    debugDefaultTargetPlatformOverride = null;
  });

  testWidgets('does not mirror the camera preview on macOS', (tester) async {
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    MobileScannerPlatform.instance = _FakeMobileScannerPlatform();

    await tester.pumpWidget(
      const MaterialApp(home: Scaffold(body: MobileScanner())),
    );
    await tester.pumpAndSettle();

    final transforms = tester.widgetList<Transform>(
      find.ancestor(
        of: find.byKey(const ValueKey('camera-view')),
        matching: find.byType(Transform),
      ),
    );
    expect(
      transforms.any((widget) => widget.transform.entry(0, 0) == -1),
      false,
    );
    debugDefaultTargetPlatformOverride = null;
  });
}

class _FakeMobileScannerPlatform extends MobileScannerPlatform {
  @override
  Stream<BarcodeCapture?> get barcodesStream => const Stream.empty();

  @override
  Stream<TorchState> get torchStateStream =>
      Stream.value(TorchState.unavailable);

  @override
  Stream<double> get zoomScaleStateStream => Stream.value(1);

  @override
  Future<MobileScannerViewAttributes> start(StartOptions startOptions) async {
    return const MobileScannerViewAttributes(
      cameraDirection: CameraFacing.external,
      currentTorchMode: TorchState.unavailable,
      size: Size(200, 200),
      numberOfCameras: 1,
    );
  }

  @override
  Widget buildCameraView() {
    return const SizedBox.square(
      key: ValueKey('camera-view'),
      dimension: 100,
    );
  }

  @override
  Future<void> stop() async {}

  @override
  Future<void> dispose() async {}
}
