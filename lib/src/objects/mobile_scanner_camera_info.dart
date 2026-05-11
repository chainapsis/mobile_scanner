import 'package:mobile_scanner/src/enums/camera_facing.dart';
import 'package:mobile_scanner/src/enums/camera_lens_type.dart';

/// Describes a camera that can be used by the scanner.
class MobileScannerCameraInfo {
  /// Creates a camera descriptor.
  const MobileScannerCameraInfo({
    required this.id,
    required this.name,
    required this.facing,
    required this.lensType,
    required this.isDefault,
    required this.isExternal,
  });

  /// Creates a camera descriptor from a platform channel map.
  factory MobileScannerCameraInfo.fromMap(Map<Object?, Object?> map) {
    final id = map['id'];
    final name = map['name'];

    if (id is! String || id.isEmpty) {
      throw ArgumentError.value(
        id,
        'id',
        'Camera id must be a non-empty String',
      );
    }

    return MobileScannerCameraInfo(
      id: id,
      name: name is String && name.isNotEmpty ? name : 'Camera $id',
      facing: CameraFacing.fromRawValue(map['facing'] as int?),
      lensType: CameraLensType.fromRawValue(map['lensType'] as int?),
      isDefault: map['isDefault'] == true,
      isExternal: map['isExternal'] == true,
    );
  }

  /// Platform-specific stable camera identifier.
  final String id;

  /// Human-readable camera name.
  final String name;

  /// Camera facing direction.
  final CameraFacing facing;

  /// Camera lens type when the platform can infer it.
  final CameraLensType lensType;

  /// Whether this camera is the platform default for scanner startup.
  final bool isDefault;

  /// Whether this camera is an external device.
  final bool isExternal;

  /// Converts this camera descriptor to a map.
  Map<String, Object?> toMap() {
    return <String, Object?>{
      'id': id,
      'name': name,
      'facing': facing.rawValue,
      'lensType': lensType.rawValue,
      'isDefault': isDefault,
      'isExternal': isExternal,
    };
  }
}
