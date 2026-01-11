package com.example.bmsdebug

import android.Manifest
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp

class MainActivity : ComponentActivity() {
    private val viewModel: BleViewModel by viewModels()

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        permissionLauncher.launch(
            arrayOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT,
                Manifest.permission.ACCESS_FINE_LOCATION
            )
        )
        setContent {
            MaterialTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    BleScreen(viewModel)
                }
            }
        }
    }
}

@Composable
fun BleScreen(viewModel: BleViewModel) {
    val devices by viewModel.devices.collectAsState()
    val logLines by viewModel.logLines.collectAsState()
    val isConnected by viewModel.isConnected.collectAsState()
    val filterPrefix by viewModel.filterPrefix.collectAsState()
    val readAddr by viewModel.readAddress.collectAsState()
    val readWords by viewModel.readWords.collectAsState()
    val logToFile by viewModel.logToFile.collectAsState()

    var prefixInput by remember { mutableStateOf(filterPrefix) }
    var addrInput by remember { mutableStateOf(readAddr) }
    var wordsInput by remember { mutableStateOf(readWords) }

    Column(modifier = Modifier.padding(16.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                value = prefixInput,
                onValueChange = { prefixInput = it },
                label = { Text("前缀") },
                modifier = Modifier.weight(1f)
            )
            Spacer(modifier = Modifier.width(8.dp))
            Button(onClick = {
                viewModel.updateFilter(prefixInput)
                viewModel.scan()
            }) { Text("扫描") }
        }
        Spacer(modifier = Modifier.height(8.dp))
        LazyColumn(modifier = Modifier.height(180.dp)) {
            items(devices) { device ->
                Card(modifier = Modifier.fillMaxWidth().padding(4.dp)) {
                    Row(modifier = Modifier.padding(8.dp), horizontalArrangement = Arrangement.SpaceBetween) {
                        Column {
                            Text(device.name, style = MaterialTheme.typography.titleMedium)
                            Text(device.address, style = MaterialTheme.typography.bodySmall)
                        }
                        Button(onClick = { viewModel.connect(device) }) { Text("连接") }
                    }
                }
            }
        }
        Spacer(modifier = Modifier.height(8.dp))
        Row {
            Button(onClick = { viewModel.disconnect() }, enabled = isConnected) { Text("断开") }
            Spacer(modifier = Modifier.width(8.dp))
            Switch(checked = logToFile, onCheckedChange = { viewModel.toggleLog(it) })
            Text(text = "保存日志", modifier = Modifier.padding(start = 4.dp))
        }
        Spacer(modifier = Modifier.height(8.dp))
        OutlinedTextField(value = addrInput, onValueChange = { addrInput = it }, label = { Text("地址 0xD000") })
        OutlinedTextField(value = wordsInput, onValueChange = { wordsInput = it }, label = { Text("数量") })
        Row {
            Button(onClick = {
                viewModel.updateReadFields(addrInput, wordsInput)
                viewModel.sendRead()
            }) { Text("读寄存器") }
            Spacer(modifier = Modifier.width(8.dp))
            Button(onClick = { viewModel.togglePolling() }) { Text("轮询") }
        }
        Spacer(modifier = Modifier.height(8.dp))
        Text("日志：")
        LazyColumn(modifier = Modifier.weight(1f)) {
            items(logLines) { line ->
                Text(line, fontFamily = FontFamily.Monospace, fontSize = MaterialTheme.typography.bodySmall.fontSize)
            }
        }
    }
}
