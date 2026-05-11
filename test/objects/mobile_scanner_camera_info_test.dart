import 'package:flutter_test/flutter_test.dart';
import 'package:mobile_scanner/src/enums/camera_facing.dart';
import 'package:mobile_scanner/src/enums/camera_lens_type.dart';
import 'package:mobile_scanner/src/objects/mobile_scanner_camera_info.dart';

void main() {
  group('$MobileScannerCameraInfo tests', () {
    test('creates instance from platform map', () {
      final camera = MobileScannerCameraInfo.fromMap({
        'id': 'camera-1',
        'name': 'FaceTime HD Camera',
        'facing': CameraFacing.front.rawValue,
        'lensType': CameraLensType.normal.rawValue,
        'isDefault': true,
        'isExternal': false,
      });

      expect(camera.id, 'camera-1');
      expect(camera.name, 'FaceTime HD Camera');
      expect(camera.facing, CameraFacing.front);
      expect(camera.lensType, CameraLensType.normal);
      expect(camera.isDefault, true);
      expect(camera.isExternal, false);
    });

    test('falls back to generated name and unknown enum values', () {
      final camera = MobileScannerCameraInfo.fromMap({
        'id': 'camera-2',
      });

      expect(camera.name, 'Camera camera-2');
      expect(camera.facing, CameraFacing.unknown);
      expect(camera.lensType, CameraLensType.any);
      expect(camera.isDefault, false);
      expect(camera.isExternal, false);
    });
  });
}
