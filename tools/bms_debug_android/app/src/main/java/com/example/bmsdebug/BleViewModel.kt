package com.example.bmsdebug

import android.annotation.SuppressLint
import android.app.Application
import android.bluetooth.*
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.Context
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import java.io.File

data class AndroidBleDevice(val name: String, val address: String, val peripheral: BluetoothDevice)

class BleViewModel(app: Application) : AndroidViewModel(app) {
    private val bluetoothManager = app.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val adapter = bluetoothManager.adapter
    private var gatt: BluetoothGatt? = null
    private var notifyChar: BluetoothGattCharacteristic? = null
    private var writeChar: BluetoothGattCharacteristic? = null
    private val helper = ProtocolHelper()

    private val _devices = MutableStateFlow<List<AndroidBleDevice>>(emptyList())
    val devices: StateFlow<List<AndroidBleDevice>> = _devices

    private val _logLines = MutableStateFlow<List<String>>(emptyList())
    val logLines: StateFlow<List<String>> = _logLines

    private val _isConnected = MutableStateFlow(false)
    val isConnected: StateFlow<Boolean> = _isConnected

    private val _filterPrefix = MutableStateFlow("BT")
    val filterPrefix: StateFlow<String> = _filterPrefix

    private val _readAddress = MutableStateFlow("0xD000")
    val readAddress: StateFlow<String> = _readAddress

    private val _readWords = MutableStateFlow("0x0038")
    val readWords: StateFlow<String> = _readWords

    private val _logToFile = MutableStateFlow(false)
    val logToFile: StateFlow<Boolean> = _logToFile

    private val logFile: File = File(app.filesDir, "notify_log.csv")
    private var polling = false

    fun updateFilter(prefix: String) { _filterPrefix.value = prefix }
    fun updateReadFields(addr: String, words: String) {
        _readAddress.value = addr
        _readWords.value = words
    }

    fun toggleLog(enable: Boolean) {
        _logToFile.value = enable
        if (enable && !logFile.exists()) {
            logFile.writeText("timestamp,dataset,raw\n")
        }
    }

    fun scan() {
        adapter.bluetoothLeScanner?.startScan(scanCallback)
        viewModelScope.launch {
            kotlinx.coroutines.delay(5000)
            adapter.bluetoothLeScanner?.stopScan(scanCallback)
        }
    }

    fun connect(device: AndroidBleDevice) {
        adapter.bluetoothLeScanner?.stopScan(scanCallback)
        gatt = device.peripheral.connectGatt(getApplication(), false, gattCallback)
    }

    fun disconnect() {
        gatt?.close()
        gatt = null
        _isConnected.value = false
    }

    fun sendRead() {
        val addr = readAddress.value.removePrefix("0x").toInt(16)
        val words = readWords.value.removePrefix("0x").toInt(16)
        val data = helper.buildRead(addr, words)
        writeChar?.let { characteristic ->
            characteristic.value = data
            gatt?.writeCharacteristic(characteristic)
        }
    }

    fun togglePolling() {
        polling = !polling
        if (polling) {
            viewModelScope.launch(Dispatchers.IO) {
                while (polling) {
                    sendRead()
                    kotlinx.coroutines.delay(5000)
                }
            }
        }
    }

    private fun appendLog(text: String) {
        viewModelScope.launch(Dispatchers.Main) {
            _logLines.value = listOf(text) + _logLines.value.take(99)
        }
        if (logToFile.value) {
            logFile.appendText("${System.currentTimeMillis()},$text\n")
        }
    }

    private val scanCallback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val name = result.device.name ?: return
            if (_filterPrefix.value.isNotEmpty() && !name.startsWith(_filterPrefix.value)) return
            val list = _devices.value.toMutableList()
            if (list.none { it.address == result.device.address }) {
                list.add(AndroidBleDevice(name, result.device.address, result.device))
                _devices.value = list
            }
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothGatt.STATE_CONNECTED) {
                _isConnected.value = true
                gatt.discoverServices()
            } else if (newState == BluetoothGatt.STATE_DISCONNECTED) {
                _isConnected.value = false
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            val service = gatt.getService(helper.serviceUUID)
            notifyChar = service?.getCharacteristic(helper.notifyUUID)?.also {
                gatt.setCharacteristicNotification(it, true)
            }
            writeChar = service?.getCharacteristic(helper.writeUUID)
        }

        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            if (characteristic.uuid == helper.notifyUUID) {
                helper.parseNotify(characteristic.value)?.let { parsed ->
                    appendLog("${parsed.datasetHint} ${parsed.raw.joinToString(separator = "") { String.format("%02X", it) }}")
                }
            }
        }
    }
}
