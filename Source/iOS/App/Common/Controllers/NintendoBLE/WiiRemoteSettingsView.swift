// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import CoreBluetooth
import SwiftUI
import NintendoBLE

struct WiiRemoteSettingsView: View {
    @ObservedObject private var bridge = WiiRemoteBridgeManager.shared
    private struct RenameTarget: Identifiable {
        let id = UUID()
        let slot: UInt8?
        let name: String
    }
    @State private var renameTarget: RenameTarget?

    private var canConnectBridge: Bool { bridge.connection != .ready && bridge.connection != .connecting }
    private var sleepingSlots: [UInt8] {
        bridge.playerOrder.filter { bridge.remoteInfo[$0] != nil && !bridge.connectedSlots.contains($0) }
    }
    private var otherBridges: [WiiRemoteBridgeManager.SavedBridge] {
        bridge.savedBridges.filter { $0.id != bridge.selected?.id }
    }
    private var newCandidates: [ControllerCandidate] {
        bridge.candidates.filter { candidate in
            candidate.id != bridge.selected?.id && !bridge.savedBridges.contains { $0.id == candidate.id }
        }
    }

    var body: some View {
        Form {
            Section {
                if let selected = bridge.selected {
                    HStack(spacing: 12) {
                        Button {
                            renameTarget = RenameTarget(slot: nil, name: selected.name)
                        } label: {
                            VStack(alignment: .leading, spacing: 4) {
                                Text(selected.name).foregroundColor(.primary)
                                Text(bridge.connectionDetail).font(.caption).foregroundColor(.secondary)
                            }
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                        .disabled(bridge.connection != .ready || !bridge.managementSupported || bridge.isManaging)
                        .accessibilityHint("Rename bridge")
                        if bridge.connection == .connecting { ProgressView() }
                        if bridge.connection == .ready || bridge.connection == .connecting {
                            Button(bridge.connection == .connecting ? "Cancel" : "Disconnect") {
                                bridge.disconnect()
                            }.buttonStyle(.borderless)
                        } else {
                            Button("Connect") { bridge.connect(selected) }.buttonStyle(.borderless)
                        }
                    }
                    .contextMenu {
                        if let saved = bridge.savedBridges.first(where: { $0.id == selected.id }) {
                            Button("Forget Bridge") { bridge.forget(saved) }
                        }
                    }
                }
                Toggle("Automatically Reconnect", isOn: $bridge.autoReconnect)
            } header: {
                Text("Bridge")
            } footer: {
                if let error = bridge.errorMessage { Text(error).foregroundColor(.red) }
            }

            if bridge.connection == .ready {
                Section {
                    if bridge.connectedSlots.isEmpty {
                        Text("No Wii Remotes connected").foregroundColor(.secondary)
                    }
                    ForEach(bridge.connectedSlots, id: \.self) { slot in
                        remoteRow(slot, connected: true)
                            .moveDisabled(!bridge.remoteControlSupported || bridge.isManaging || bridge.connectedSlots.count < 2)
                    }
                    .onMove { source, destination in
                        var order = bridge.connectedSlots
                        order.move(fromOffsets: source, toOffset: destination)
                        bridge.reorderRemotes(order)
                    }
                } header: {
                    Text("Wii Remotes")
                } footer: {
                    Text("Set each corresponding Wii Remote port to Real Wii Remote in Controller Settings. IR pointing requires a sensor bar.")
                    if bridge.connectedSlots.count > 1 && bridge.remoteControlSupported {
                        Text("Drag to change player order.")
                    }
                }

                if !sleepingSlots.isEmpty {
                    Section(header: Text("Other Remotes")) {
                        ForEach(sleepingSlots, id: \.self) { slot in remoteRow(slot, connected: false) }
                    }
                }

                Section {
                    HStack {
                        Button(bridge.pairingOpen ? "Extend Pairing Window" : "Pair a Wii Remote") { bridge.openPairing() }
                            .disabled(!bridge.managementSupported || bridge.isManaging)
                        Spacer()
                        if bridge.pairingOpen { Text("Pairing…").font(.subheadline).foregroundColor(.secondary) }
                    }
                } footer: {
                    Text(bridge.managementSupported
                         ? "Press the red SYNC button inside the remote’s battery cover within 30 seconds. Press A to wake a remembered remote."
                         : "Update the ESP32 firmware to pair from Settings. With older firmware, reset the bridge and press the remote’s red SYNC button.")
                }
            }

            if !otherBridges.isEmpty {
                Section(header: Text("Remembered Bridges")) {
                    ForEach(otherBridges) { saved in
                        HStack {
                            VStack(alignment: .leading, spacing: 4) {
                                Text(saved.name)
                                Text(String(saved.id.uuidString.suffix(8))).font(.caption).foregroundColor(.secondary)
                            }
                            Spacer()
                            Button("Connect") { bridge.connectSaved(saved) }
                                .buttonStyle(.borderless).disabled(!canConnectBridge)
                        }
                        .contextMenu {
                            Button("Forget") { bridge.forget(saved) }
                        }
                    }
                }
            }

            if canConnectBridge {
                Section(header: Text("Available Bridges")) {
                    if bridge.bluetoothState == .poweredOff {
                        Text("Turn on Bluetooth to find your ESP32.")
                    } else if bridge.bluetoothState == .unauthorized {
                        Text("Allow Bluetooth access for DolphiniOS in the iOS Settings app.")
                    } else if bridge.bluetoothState == .unsupported {
                        Text("Bluetooth is unavailable on this device.")
                    }
                    HStack {
                        Button(bridge.isSearching ? "Stop Searching" : "Find ESP32 Bridge") {
                            if bridge.isSearching { bridge.stopSearching() } else { bridge.search() }
                        }
                        Spacer()
                        if bridge.isSearching { ProgressView() }
                    }
                    ForEach(newCandidates) { candidate in
                        HStack {
                            VStack(alignment: .leading, spacing: 4) {
                                Text(candidate.name)
                                Text(String(candidate.id.uuidString.suffix(8))).font(.caption).foregroundColor(.secondary)
                            }
                            Spacer()
                            Button("Connect") { bridge.connect(candidate) }.buttonStyle(.borderless)
                        }
                    }
                }
            }
            if !bridge.diagnostics.isEmpty {
                Section {
                    DisclosureGroup("Connection Diagnostics") {
                        Text(bridge.diagnostics.joined(separator: "\n"))
                            .font(.system(.caption, design: .monospaced))
                    }
                }
            }
        }
        .environment(\.editMode, .constant(bridge.remoteControlSupported && bridge.connectedSlots.count > 1 ? .active : .inactive))
        .navigationTitle("Wii Remotes")
        .onDisappear { bridge.stopManualSearch() }
        .sheet(item: $renameTarget) { target in
            WiiBridgeRenameView(name: target.name) { name in
                if let slot = target.slot { bridge.renameRemote(slot: slot, name: name) }
                else { bridge.renameBridge(name) }
            }
        }
    }

    private func remoteRow(_ slot: UInt8, connected: Bool) -> some View {
        HStack(spacing: 12) {
            Button {
                renameTarget = RenameTarget(slot: slot, name: bridge.remoteName(slot))
            } label: {
                VStack(alignment: .leading, spacing: 4) {
                    Text(bridge.remoteName(slot)).foregroundColor(.primary)
                    Text(remoteDetail(slot, connected: connected)).font(.caption).foregroundColor(.secondary)

                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .disabled(!bridge.managementSupported || bridge.remoteInfo[slot] == nil || bridge.isManaging)
            .accessibilityHint("Rename Wii Remote")
            if bridge.remoteControlSupported {
                if connected {
                    Button("Disconnect") { bridge.disconnectRemote(slot) }
                        .buttonStyle(.borderless).disabled(bridge.isManaging)
                } else {
                    Button("Connect") { bridge.connectRemote(slot) }
                        .buttonStyle(.borderless).disabled(bridge.isManaging)
                }
            }
        }
    }
    private func remoteDetail(_ slot: UInt8, connected: Bool) -> String {
        guard connected else { return bridge.disconnectedRemoteSlots.contains(slot) ? "Disconnected" : "Asleep" }
        return "Wii Remote \(bridge.playerNumber(for: slot) ?? 1)"
    }
}

private struct WiiBridgeRenameView: View {
    @Environment(\.presentationMode) private var presentationMode
    @State var name: String
    let save: (String) -> Void
    var body: some View {
        NavigationView {
            Form {
                TextField("Name", text: $name)
                if !BridgeDeviceInfo.validName(name) {
                    Text("Enter a shorter, nonempty name without control characters.").font(.caption).foregroundColor(.secondary)
                }
            }
            .navigationTitle("Rename")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Cancel") { presentationMode.wrappedValue.dismiss() } }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Save") { save(name); presentationMode.wrappedValue.dismiss() }.disabled(!BridgeDeviceInfo.validName(name))
                }
            }
        }
    }
}
