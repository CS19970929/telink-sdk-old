import SwiftUI

@main
struct BmsDebugApp: App {
    @StateObject private var viewModel = BleViewModel()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(viewModel)
        }
    }
}
