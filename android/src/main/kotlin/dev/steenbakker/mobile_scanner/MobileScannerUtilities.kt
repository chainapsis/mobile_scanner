package dev.steenbakker.mobile_scanner

import android.graphics.Point

internal val DecodedBarcode.data: Map<String, Any?>
    get() = mapOf(
        "calendarEvent" to null,
        "contactInfo" to null,
        "corners" to cornerPoints.map { corner -> corner.data },
        "displayValue" to rawValue,
        "driverLicense" to null,
        "email" to null,
        "format" to format,
        "geoPoint" to null,
        "phone" to null,
        "rawBytes" to rawBytes,
        "rawValue" to rawValue,
        "size" to cornerPoints.boundingSize,
        "sms" to null,
        "type" to 7,
        "url" to null,
        "wifi" to null,
    )

private val Point.data: Map<String, Double>
    get() = mapOf("x" to x.toDouble(), "y" to y.toDouble())

private val List<Point>.boundingSize: Map<String, Double>
    get() = if (isEmpty()) {
        emptyMap()
    } else {
        mapOf(
            "width" to (maxOf { it.x } - minOf { it.x }).toDouble(),
            "height" to (maxOf { it.y } - minOf { it.y }).toDouble(),
        )
    }
