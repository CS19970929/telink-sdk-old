import CoreBluetooth
import Foundation
import SwiftUI

struct DiscoveredDevice: Identifiable, Equatable {
    let id = UUID()
    let peripheral: CBPeripheral
    let name: String
    let rssi: Int

    static func == (lhs: DiscoveredDevice, rhs: DiscoveredDevice) -> Bool {
        lhs.peripheral.identifier == rhs.peripheral.identifier
    }
}

final class BleViewModel: NSObject, ObservableObject {
    @Published var devices: [DiscoveredDevice] = []
    @Published var logLines: [String] = []
    @Published var isScanning = false
    @Published var isConnected = false
    @Published var errorMessage: String?
    @Published var autoReconnect = true
    @Published var readAddress: String = "0xD000"
    @Published var readWords: String = "0x0038"
    @Published var filterPrefix: String = "BT"
    @Published var isPolling = false

    private lazy var central = CBCentralManager(delegate: self, queue: nil)
    private var targetPeripheral: CBPeripheral?
    private var notifyChar: CBCharacteristic?
    private var writeChar: CBCharacteristic?
    private var helper = ProtocolHelper()
    private var pollTimer: Timer?

    func startScan() {
        devices.removeAll()
        guard central.state == .poweredOn else {
            errorMessage = "蓝牙未开启"
            return
        }
        isScanning = true
        central.scanForPeripherals(withServices: [helper.serviceUUID], options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
        DispatchQueue.main.asyncAfter(deadline: .now() + 5) { [weak self] in
            self?.stopScan()
        }
    }

    func stopScan() {
        guard isScanning else { return }
        central.stopScan()
        isScanning = false
    }

    func connect(_ device: DiscoveredDevice) {
        stopScan()
        targetPeripheral = device.peripheral
        central.connect(device.peripheral, options: nil)
    }

    func disconnect() {
        if let peripheral = targetPeripheral {
            central.cancelPeripheralConnection(peripheral)
        }
        targetPeripheral = nil
        isConnected = false
    }

    func sendReadCommand() {
        guard let writeChar = writeChar, let peripheral = targetPeripheral else { return }
        let addr = UInt16(readAddress.replacingOccurrences(of: "0x", with: ""), radix: 16) ?? 0xD000
        let words = UInt16(readWords.replacingOccurrences(of: "0x", with: ""), radix: 16) ?? 0x0038
        let data = helper.buildReadRequest(addr: addr, words: words)
        peripheral.writeValue(data, for: writeChar, type: .withoutResponse)
    }

    func togglePolling(enable: Bool) {
        pollTimer?.invalidate()
        pollTimer = nil
        isPolling = enable
        guard enable else { return }
        pollTimer = Timer.scheduledTimer(withTimeInterval: 5, repeats: true) { [weak self] _ in
            self?.sendReadCommand()
        }
    }

    private func appendLog(_ text: String) {
        DispatchQueue.main.async {
            self.logLines.insert(text, at: 0)
            if self.logLines.count > 100 {
                self.logLines.removeLast()
            }
        }
    }
}

extension BleViewModel: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state != .poweredOn {
            errorMessage = "蓝牙不可用"
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = peripheral.name ?? "未知设备"
        if !filterPrefix.isEmpty && !name.hasPrefix(filterPrefix) {
            return
        }
        let device = DiscoveredDevice(peripheral: peripheral, name: name, rssi: RSSI.intValue)
        DispatchQueue.main.async {
            if !self.devices.contains(device) {
                self.devices.append(device)
            }
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        appendLog("已连接：\(peripheral.name ?? peripheral.identifier.uuidString)")
        isConnected = true
        peripheral.delegate = self
        peripheral.discoverServices([helper.serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        errorMessage = "连接失败：\(error?.localizedDescription ?? "未知")"
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        appendLog("连接断开")
        isConnected = false
        notifyChar = nil
        writeChar = nil
        if autoReconnect, let target = targetPeripheral {
            central.connect(target, options: nil)
        }
    }
}

extension BleViewModel: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        peripheral.services?.forEach { service in
            peripheral.discoverCharacteristics([helper.notifyUUID, helper.writeUUID], for: service)
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        service.characteristics?.forEach { characteristic in
            if characteristic.uuid == helper.notifyUUID {
                notifyChar = characteristic
                peripheral.setNotifyValue(true, for: characteristic)
            } else if characteristic.uuid == helper.writeUUID {
                writeChar = characteristic
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard characteristic.uuid == helper.notifyUUID, let data = characteristic.value else { return }
        if let parsed = helper.parseNotify(data: data) {
            appendLog("集合=\(parsed.datasetHint) Raw=\(parsed.raw.hexEncodedString())")
        }
    }
}
