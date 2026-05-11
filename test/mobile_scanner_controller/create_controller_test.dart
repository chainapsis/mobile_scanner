import 'package:flutter_test/flutter_test.dart';
import 'package:mobile_scanner/src/enums/camera_lens_type.dart';
import 'package:mobile_scanner/src/mobile_scanner_controller.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('controller initializes with default lens type (any)', () {
    final controller = MobileScannerController(autoStart: false);

    expect(controller.lensType, CameraLensType.any);
  });

  test('controller can be created with a camera id', () {
    final controller = MobileScannerController(
      autoStart: false,
      cameraId: 'camera-1',
    );

    expect(controller.cameraId, 'camera-1');
  });

  test('controller can be created with all lens types', () {
    for (final lensType in CameraLensType.values) {
      final controller = MobileScannerController(
        autoStart: false,
        lensType: lensType,
      );

      expect(controller.lensType, lensType);
    }
  });
}
