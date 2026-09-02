package dev.steenbakker.mobile_scanner.objects

import zxingcpp.BarcodeReader

enum class BarcodeFormats(val rawValue: Int, val zxingFormat: BarcodeReader.Format?) {
    UNKNOWN(-1, null),
    ALL_FORMATS(0, null),
    CODE_128(1, BarcodeReader.Format.CODE_128),
    CODE_39(2, BarcodeReader.Format.CODE_39),
    CODE_93(4, BarcodeReader.Format.CODE_93),
    CODABAR(8, BarcodeReader.Format.CODABAR),
    DATA_MATRIX(16, BarcodeReader.Format.DATA_MATRIX),
    EAN_13(32, BarcodeReader.Format.EAN_13),
    EAN_8(64, BarcodeReader.Format.EAN_8),
    ITF(128, BarcodeReader.Format.ITF),
    QR_CODE(256, BarcodeReader.Format.QR_CODE),
    UPC_A(512, BarcodeReader.Format.UPC_A),
    UPC_E(1024, BarcodeReader.Format.UPC_E),
    PDF417(2048, BarcodeReader.Format.PDF_417),
    AZTEC(4096, BarcodeReader.Format.AZTEC);

    companion object {
        fun fromRawValue(rawValue: Int): BarcodeFormats {
            return when(rawValue) {
                -1 -> UNKNOWN
                0 -> ALL_FORMATS
                1 -> CODE_128
                2 -> CODE_39
                4 -> CODE_93
                8 -> CODABAR
                16 -> DATA_MATRIX
                32 -> EAN_13
                64 -> EAN_8
                126, 127, 128 -> ITF
                256 -> QR_CODE
                512 -> UPC_A
                1024 -> UPC_E
                2048 -> PDF417
                4096 -> AZTEC
                else -> UNKNOWN
            }
        }

        fun rawValueFor(format: BarcodeReader.Format): Int = when (format) {
            BarcodeReader.Format.CODE_128 -> CODE_128.rawValue
            BarcodeReader.Format.CODE_39,
            BarcodeReader.Format.CODE_39_STD,
            BarcodeReader.Format.CODE_39_EXT -> CODE_39.rawValue
            BarcodeReader.Format.CODE_93 -> CODE_93.rawValue
            BarcodeReader.Format.CODABAR -> CODABAR.rawValue
            BarcodeReader.Format.DATA_MATRIX -> DATA_MATRIX.rawValue
            BarcodeReader.Format.EAN_13 -> EAN_13.rawValue
            BarcodeReader.Format.EAN_8 -> EAN_8.rawValue
            BarcodeReader.Format.ITF,
            BarcodeReader.Format.ITF_14 -> ITF.rawValue
            BarcodeReader.Format.QR_CODE,
            BarcodeReader.Format.QR_CODE_MODEL_1,
            BarcodeReader.Format.QR_CODE_MODEL_2,
            BarcodeReader.Format.MICRO_QR_CODE,
            BarcodeReader.Format.RMQR_CODE -> QR_CODE.rawValue
            BarcodeReader.Format.UPC_A -> UPC_A.rawValue
            BarcodeReader.Format.UPC_E -> UPC_E.rawValue
            BarcodeReader.Format.PDF_417,
            BarcodeReader.Format.COMPACT_PDF_417,
            BarcodeReader.Format.MICRO_PDF_417 -> PDF417.rawValue
            BarcodeReader.Format.AZTEC,
            BarcodeReader.Format.AZTEC_CODE,
            BarcodeReader.Format.AZTEC_RUNE -> AZTEC.rawValue
            else -> UNKNOWN.rawValue
        }
    }
}
