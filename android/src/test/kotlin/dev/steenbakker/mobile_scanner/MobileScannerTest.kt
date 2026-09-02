package dev.steenbakker.mobile_scanner

import android.app.Activity
import android.graphics.Point
import androidx.camera.core.ImageInfo
import androidx.camera.core.ImageProxy
import kotlin.test.Test
import org.mockito.Mockito
import io.flutter.view.TextureRegistry
import zxingcpp.BarcodeReader
import kotlin.test.expect

/*
 * This demonstrates a simple unit test of the Kotlin portion of this plugin's implementation.
 *
 * Once you have built the plugin's example app, you can run these tests from the command
 * line by running `./gradlew testDebugUnitTest` in the `example/android/` directory, or
 * you can run them directly from IDEs that support JUnit such as Android Studio.
 */

internal class MobileScannerTest {
    @Test
    fun isBarcodeInScanWindow_canHandleNaNValues() {
        val barcodeScannerMock = Mockito.mock(BarcodeDecoder::class.java)

        val mobileScanner = MobileScanner(
            Mockito.mock(Activity::class.java),
            Mockito.mock(TextureRegistry::class.java),
            { _: List<Map<String, Any?>>, _: ByteArray?, _: Int?, _: Int? -> },
            { _: String  -> },
            DeviceOrientationListener(Mockito.mock(Activity::class.java)),
            { _: BarcodeReader.Options -> barcodeScannerMock }
        )

        // Intentional suppression for the mock value in the test,
        // since there is no NaN constant.
        @Suppress("DIVISION_BY_ZERO")
        val notANumber = 0.0f / 0.0f

        val barcode = DecodedBarcode(
            format = 256,
            rawBytes = null,
            rawValue = "test",
            cornerPoints = listOf(
                Point(100, 100),
                Point(200, 100),
                Point(200, 300),
                Point(100, 300),
            ),
        )
        val imageMock: ImageProxy = Mockito.mock(ImageProxy::class.java)
        val imageInfoMock: ImageInfo = Mockito.mock(ImageInfo::class.java)
        Mockito.`when`(imageMock.imageInfo).thenReturn(imageInfoMock)
        Mockito.`when`(imageInfoMock.rotationDegrees).thenReturn(0)
        Mockito.`when`(imageMock.height).thenReturn(400)
        Mockito.`when`(imageMock.width).thenReturn(400)

        // Use a scan window that has an invalid value, but otherwise uses the entire image.
        val scanWindow: List<Float> = listOf(0f, notANumber, 100f, 100f)

        expect(false) {
            mobileScanner.isBarcodeInScanWindow(scanWindow, barcode, imageMock)
        }
    }
}
