import SwiftUI

struct ContentView: View {
    @EnvironmentObject var vm: BleViewModel

    var body: some View {
        NavigationView {
            VStack {
                HStack {
                    TextField("名称前缀", text: $vm.filterPrefix)
                        .textFieldStyle(RoundedBorderTextFieldStyle())
                    Button(vm.isScanning ? "停止" : "扫描") {
                        vm.isScanning ? vm.stopScan() : vm.startScan()
                    }
                }
                List {
                    Section(header: Text("设备")) {
                        ForEach(vm.devices) { device in
                            HStack {
                                VStack(alignment: .leading) {
                                    Text(device.name).bold()
                                    Text(device.peripheral.identifier.uuidString).font(.caption)
                                }
                                Spacer()
                                Button("连接") { vm.connect(device) }
                            }
                        }
                    }
                }
                .frame(height: 200)

                HStack {
                    Button("断开") { vm.disconnect() }
                    Toggle("自动重连", isOn: $vm.autoReconnect)
                }

                Form {
                    Section(header: Text("读寄存器")) {
                        TextField("地址 0xD000", text: $vm.readAddress)
                            .keyboardType(.asciiCapable)
                        TextField("数量", text: $vm.readWords)
                            .keyboardType(.numbersAndPunctuation)
                        Button("发送读命令") { vm.sendReadCommand() }
                    }
                    Section(header: Text("轮询")) {
                        Toggle("每5秒轮询", isOn: Binding(
                            get: { vm.isPolling },
                            set: { vm.togglePolling(enable: $0) }
                        ))
                    }
                }

                List(vm.logLines, id: \.self) { line in
                    Text(line).font(.system(size: 12, design: .monospaced))
                }
            }
            .padding()
            .navigationTitle("BMS BLE 调试")
            .alert(item: Binding(get: {
                vm.errorMessage.map { ErrorWrapper(message: $0) }
            }, set: { _ in vm.errorMessage = nil })) { wrapper in
                Alert(title: Text("错误"), message: Text(wrapper.message), dismissButton: .default(Text("确定")))
            }
        }
    }
}

private struct ErrorWrapper: Identifiable {
    let id = UUID()
    let message: String
}
