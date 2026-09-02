package dev.steenbakker.mobile_scanner.utils

import android.graphics.Bitmap
import android.graphics.Matrix

fun rotateBitmap(bitmap: Bitmap, rotationDegrees: Int): Bitmap {
    val matrix = Matrix()
    matrix.postRotate(rotationDegrees.toFloat())
    return Bitmap.createBitmap(
        bitmap, 0, 0, bitmap.getWidth(), bitmap.getHeight(), matrix,
        true
    )
}
