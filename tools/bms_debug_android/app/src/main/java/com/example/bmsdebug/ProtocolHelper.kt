package com.example.bmsdebug

import android.bluetooth.BluetoothGattCharacteristic
import java.util.UUID

data class ParsedNotify(val raw: ByteArray, val datasetHint: String)

class ProtocolHelper {
    val serviceUUID: UUID = UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
    val writeUUID: UUID = UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e")
    val notifyUUID: UUID = UUID.fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e")

    private val datasetByLength = mapOf(
        0x4C to "0xD000",
        0x82 to "0x2100",
        0x32 to "0xD026",
        0x18 to "0xD115",
        0x2A to "0xD100"
    )

    fun buildRead(addr: Int, words: Int): ByteArray {
        val payload = byteArrayOf(
            0x01,
            0x03,
            ((addr shr 8) and 0xFF).toByte(),
            (addr and 0xFF).toByte(),
            ((words shr 8) and 0xFF).toByte(),
            (words and 0xFF).toByte()
        )
        val crc = crc16(payload)
        return payload + byteArrayOf((crc and 0xFF).toByte(), ((crc shr 8) and 0xFF).toByte())
    }

    fun parseNotify(raw: ByteArray): ParsedNotify? {
        if (raw.size < 5) return null
        val calc = crc16(raw.copyOf(raw.size - 2))
        val recv = (raw[raw.size - 2].toInt() and 0xFF) or ((raw[raw.size - 1].toInt() and 0xFF) shl 8)
        if (calc != recv) return null
        val length = raw[2].toInt() and 0xFF
        val hint = datasetByLength[length] ?: "未知"
        return ParsedNotify(raw, hint)
    }

    private fun crc16(data: ByteArray): Int {
        var crc = 0xFFFF
        data.forEach { byte ->
            crc = crc xor (byte.toInt() and 0xFF)
            repeat(8) {
                crc = if ((crc and 0x0001) != 0) {
                    (crc shr 1) xor 0xA001
                } else {
                    crc shr 1
                }
            }
        }
        return crc and 0xFFFF
    }
}
