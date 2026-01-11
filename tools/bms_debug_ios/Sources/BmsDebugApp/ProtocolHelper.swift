import CoreBluetooth
import Foundation

struct ParsedNotify {
    let raw: Data
    let datasetHint: String
}

final class ProtocolHelper {
    let serviceUUID = CBUUID(string: "6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
    let writeUUID = CBUUID(string: "6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
    let notifyUUID = CBUUID(string: "6E400003-B5A3-F393-E0A9-E50E24DCCA9E")

    private let datasetByLength: [Int: String] = [
        0x4C: "0xD000 单体电压",
        0x82: "0x2100 保护参数",
        0x32: "0xD026 SOC",
        0x18: "0xD115 状态",
        0x2A: "0xD100 保护记录",
    ]

    func buildReadRequest(addr: UInt16, words: UInt16) -> Data {
        var data = Data([0x01, 0x03, UInt8(addr >> 8), UInt8(addr & 0xFF), UInt8(words >> 8), UInt8(words & 0xFF)])
        let crc = crc16(data: data)
        data.append(UInt8(crc & 0xFF))
        data.append(UInt8(crc >> 8))
        return data
    }

    func parseNotify(data: Data) -> ParsedNotify? {
        guard data.count > 5 else { return nil }
        let payload = data.dropFirst(3).dropLast(2)
        let crc = crc16(data: data.dropLast(2))
        let crcRecv = UInt16(data[data.count - 2]) | (UInt16(data[data.count - 1]) << 8)
        guard crc == crcRecv else { return nil }
        let hint = datasetByLength[payload.count] ?? "未知数据集"
        return ParsedNotify(raw: data, datasetHint: hint)
    }

    private func crc16(data: Data) -> UInt16 {
        var crc: UInt16 = 0xFFFF
        for byte in data {
            crc ^= UInt16(byte)
            for _ in 0..<8 {
                if crc & 1 == 1 {
                    crc = (crc >> 1) ^ 0xA001
                } else {
                    crc >>= 1
                }
            }
        }
        return crc
    }
}

extension Data {
    func hexEncodedString() -> String {
        map { String(format: "%02X", $0) }.joined()
    }
}
