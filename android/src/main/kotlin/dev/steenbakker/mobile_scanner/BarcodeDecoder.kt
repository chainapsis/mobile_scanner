package dev.steenbakker.mobile_scanner

import android.graphics.Bitmap
import android.graphics.Point
import android.graphics.Rect
import androidx.camera.core.ImageProxy
import dev.steenbakker.mobile_scanner.objects.BarcodeFormats
import zxingcpp.BarcodeReader

data class DecodedBarcode(
    val format: Int,
    val rawBytes: ByteArray?,
    val rawValue: String?,
    val cornerPoints: List<Point>,
)

interface BarcodeDecoder {
    fun read(image: ImageProxy): List<DecodedBarcode>
    fun read(bitmap: Bitmap, rotationDegrees: Int = 0): List<DecodedBarcode>
    fun close() = Unit
}

internal class ZxingBarcodeDecoder(options: BarcodeReader.Options) : BarcodeDecoder {
    private val reader = BarcodeReader(options)

    override fun read(image: ImageProxy): List<DecodedBarcode> =
        reader.read(image).map { it.toDecodedBarcode() }

    override fun read(bitmap: Bitmap, rotationDegrees: Int): List<DecodedBarcode> =
        reader.read(
            bitmap,
            cropRect = Rect(0, 0, bitmap.width, bitmap.height),
            rotation = rotationDegrees,
        ).map { it.toDecodedBarcode() }

    private fun BarcodeReader.Result.toDecodedBarcode() = DecodedBarcode(
        format = BarcodeFormats.rawValueFor(format),
        rawBytes = bytes,
        rawValue = text,
        cornerPoints = listOf(
            position.topLeft,
            position.topRight,
            position.bottomRight,
            position.bottomLeft,
        ),
    )
}
