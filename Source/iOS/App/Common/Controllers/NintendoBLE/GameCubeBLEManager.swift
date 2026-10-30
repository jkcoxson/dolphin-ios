// Copyright 2026 DolphiniOS Project
// SPDX-License-Identifier: GPL-2.0-or-later

import Combine
import CoreBluetooth
import Foundation
import NintendoBLE
import UIKit

@MainActor
final class GameCubeBLEManager: ObservableObject {
    static let shared = GameCubeBLEManager()

    @Published var autoConnect: Bool = UserDefaults.standard.bool(forKey: "NSOAutoConnect") {
        didSet {
            UserDefaults.standard.set(autoConnect, forKey: "NSOAutoConnect")
            errorMessage = nil
            if autoConnect { client.prepareBluetooth(); resumeDiscovery() }
            else { stop() }
        }
    }
    @Published private(set) var bluetoothState: CBManagerState = .unknown
    @Published private(set) var slotStates = Array(repeating: ControllerConnectionState.disconnected, count: 4)
    @Published private(set) var batteries: [Int: Int] = [:]
    @Published private(set) var errorMessage: String?

    private let client = NintendoControllerManager(options: .init(sources: [.nsoGameCube]))
    private var players: [UUID: UInt8] = [:]
    private var handles: [UUID: UInt64] = [:]
    private var rumble: [UUID: Bool] = [:]
    private var retryAfter: [UUID: TimeInterval] = [:]
    private var scanTask: Task<Void, Never>?
    private var outputTimer: Timer?
    private var observers: [NSObjectProtocol] = []
    private var inForeground = true

    private init() {
        // NSO controllers must advertise in pairing mode each time. Never select
        // a remembered UUID: a newly paired controller may have a different ID.
        UserDefaults.standard.removeObject(forKey: "NSORememberedControllers")
        UserDefaults.standard.removeObject(forKey: "NSOAutoReconnect")
        client.onDiscovered = { [weak self] candidate in
            // Match the driver: initiate connection after didDiscover returns.
            Task { @MainActor [weak self] in self?.connect(candidate) }
        }
        client.onBluetoothStateChanged = { [weak self] state in
            guard let self else { return }
            self.bluetoothState = state
            self.resumeDiscovery()
        }
        client.onConnectionChanged = { [weak self] id, state in self?.connectionChanged(id, state) }
        client.onGameCubeInput = { [weak self] id, input in
            guard let self, let handle = self.handles[id], let player = self.players[id] else { return }
            let buttons: [GameCubeButton] = [.a, .b, .x, .y, .l, .r, .z, .zl, .start,
                                             .home, .capture, .up, .down, .left, .right]
            var state = DOLGameCubeState()
            for (bit, button) in buttons.enumerated() where input.buttons.contains(button) {
                state.buttons |= UInt32(1) << bit
            }
            state.stick_x = input.leftStick.x; state.stick_y = input.leftStick.y
            state.cstick_x = input.rightStick.x; state.cstick_y = input.rightStick.y
            state.trigger_l = input.leftTrigger; state.trigger_r = input.rightTrigger
            DOLGameCubeBLEUpdate(handle, state)
            if let level = input.battery?.percentage, self.batteries[Int(player)] != level {
                self.batteries[Int(player)] = level
            }
        }
        observers.append(NotificationCenter.default.addObserver(
            forName: UIApplication.didEnterBackgroundNotification, object: nil, queue: .main
        ) { [weak self] _ in
            Task { @MainActor [weak self] in
                self?.inForeground = false
                self?.stop()
            }
        })
        observers.append(NotificationCenter.default.addObserver(
            forName: UIApplication.didBecomeActiveNotification, object: nil, queue: .main
        ) { [weak self] _ in
            Task { @MainActor [weak self] in
                self?.inForeground = true
                self?.resumeDiscovery()
            }
        })
        if autoConnect { client.prepareBluetooth() }
    }

    private var canDiscover: Bool {
        autoConnect && inForeground && bluetoothState == .poweredOn && players.count < 4
    }
    private func connect(_ candidate: ControllerCandidate) {
        guard canDiscover, candidate.source == .nsoGameCube, players[candidate.id] == nil,
              ProcessInfo.processInfo.systemUptime >= (retryAfter[candidate.id] ?? 0),
              let player = (UInt8(0)...UInt8(3)).first(where: { !players.values.contains($0) }) else { return }
        // Reserve the lowest vacancy before starting an asynchronous handshake.
        // Leaving another slot empty must not move existing players.
        players[candidate.id] = player
        slotStates[Int(player)] = .connecting
        scanTask?.cancel()
        client.stopDiscovery()
        do { try client.connect(to: candidate.id) }
        catch {
            release(candidate.id)
            retryAfter[candidate.id] = ProcessInfo.processInfo.systemUptime + 2
            errorMessage = error.localizedDescription
            resumeDiscovery(after: 2)
        }
    }
    private func connectionChanged(_ id: UUID, _ state: ControllerConnectionState) {
        guard let player = players[id] else { return }
        switch state {
        case .ready:
            guard autoConnect, inForeground else { client.disconnect(id); return }
            let handle = DOLGameCubeBLEAdd(player)
            guard handle != 0 else {
                errorMessage = "Could not assign NSO GC Controller \(Int(player) + 1)."
                client.disconnect(id)
                return
            }
            handles[id] = handle
            slotStates[Int(player)] = .ready
            retryAfter[id] = nil
            errorMessage = nil
            do { try client.setPlayerLED(player, for: id) }
            catch { errorMessage = "Could not set player LED: \(error.localizedDescription)" }
            startOutput()
            resumeDiscovery()
        case .disconnected, .failed:
            release(id)
            if case .failed(let message) = state {
                errorMessage = message
                retryAfter[id] = ProcessInfo.processInfo.systemUptime + 2
                resumeDiscovery(after: 2)
            } else {
                resumeDiscovery()
            }
        case .connecting:
            slotStates[Int(player)] = .connecting
        }
    }
    private func release(_ id: UUID) {
        if let handle = handles.removeValue(forKey: id) { DOLGameCubeBLERemove(handle) }
        if let player = players.removeValue(forKey: id) {
            slotStates[Int(player)] = .disconnected
            batteries[Int(player)] = nil
        }
        rumble[id] = nil
        if handles.isEmpty { outputTimer?.invalidate(); outputTimer = nil }
    }
    private func stop() {
        scanTask?.cancel()
        scanTask = nil
        client.stop()
        for id in Array(players.keys) { release(id) }
        retryAfter.removeAll()
    }
    private func resumeDiscovery(after delay: UInt64 = 0) {
        scanTask?.cancel()
        client.stopDiscovery()
        guard canDiscover else { return }
        scanTask = Task { [weak self] in
            if delay > 0 {
                do { try await Task.sleep(nanoseconds: delay * 1_000_000_000) }
                catch { return }
            }
            while !Task.isCancelled {
                guard let self, self.canDiscover else { return }
                self.retryAfter = self.retryAfter.filter { $0.value > ProcessInfo.processInfo.systemUptime }
                self.client.startDiscovery()
                // As in nso_gc_driver, scan continuously when empty, and use
                // 1.5s on / 8.5s off while playing to limit radio contention.
                guard !self.players.isEmpty else { return }
                do { try await Task.sleep(nanoseconds: 1_500_000_000) }
                catch { return }
                guard !Task.isCancelled else { return }
                self.client.stopDiscovery()
                do { try await Task.sleep(nanoseconds: 8_500_000_000) }
                catch { return }
            }
        }
    }
    private func startOutput() {
        guard outputTimer == nil else { return }
        let timer = Timer(timeInterval: 0.01, repeats: true) { [weak self] _ in
            Task { @MainActor [weak self] in self?.drainOutput() }
        }
        outputTimer = timer
        RunLoop.main.add(timer, forMode: .common)
    }
    private func drainOutput() {
        for (id, handle) in handles {
            let active = DOLGameCubeBLERumble(handle)
            guard rumble[id] != active else { continue }
            do { try client.setRumble(active, for: id); rumble[id] = active }
            catch NintendoBLEError.writeQueueFull { continue }
            catch { errorMessage = "Controller output failed: \(error.localizedDescription)" }
        }
    }
}
