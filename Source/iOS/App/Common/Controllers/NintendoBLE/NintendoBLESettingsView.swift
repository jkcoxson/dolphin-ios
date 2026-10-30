// Copyright 2026 DolphiniOS Project
// SPDX-License-Identifier: GPL-2.0-or-later

import Combine
import CoreBluetooth
import NintendoBLE
import SwiftUI
import UIKit

@objc final class NintendoBLESettings: NSObject {
    @MainActor @objc static func makeViewController() -> UIViewController {
        NintendoBLESettingsController(style: .insetGrouped)
    }
    @MainActor @objc static func start() {
        _ = WiiRemoteBridgeManager.shared
        _ = GameCubeBLEManager.shared
    }
}

private final class NintendoBLESettingsController: UITableViewController {
    private let gameCube = GameCubeBLEManager.shared
    private var observation: AnyCancellable?

    override func viewDidLoad() {
        super.viewDidLoad()
        title = "Nintendo Bluetooth"
        observation = gameCube.objectWillChange.sink { [weak self] _ in
            DispatchQueue.main.async { self?.tableView.reloadData() }
        }
    }
    override func numberOfSections(in tableView: UITableView) -> Int { 2 }
    override func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        section == 0 ? 1 : (gameCube.autoConnect ? 5 : 1)
    }
    override func tableView(_ tableView: UITableView, titleForHeaderInSection section: Int) -> String? {
        section == 0 ? "Wii Remotes" : "NSO GameCube"
    }
    override func tableView(_ tableView: UITableView, titleForFooterInSection section: Int) -> String? {
        guard section == 1 else { return nil }
        var text = "Hold the controller’s SYNC button each time you connect. DolphiniOS automatically assigns it to the lowest free slot. In a GameCube port’s Configure screen, choose NSO GC Controller 1–4 and load its defaults. Leave sticks and triggers released while connecting."
        if gameCube.autoConnect {
            switch gameCube.bluetoothState {
            case .poweredOff: text += "\n\nTurn on Bluetooth to find controllers."
            case .unauthorized: text += "\n\nAllow Bluetooth access for DolphiniOS in the iOS Settings app."
            case .unsupported: text += "\n\nBluetooth is unavailable on this device."
            default: break
            }
            if let error = gameCube.errorMessage { text += "\n\n" + error }
        }
        return text
    }
    override func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = UITableViewCell(style: .subtitle, reuseIdentifier: nil)
        if indexPath.section == 0 {
            cell.textLabel?.text = "ESP32 Bridge"
            cell.accessoryType = .disclosureIndicator
        } else if indexPath.row == 0 {
            cell.textLabel?.text = "Auto-connect NSO GC"
            let toggle = UISwitch()
            toggle.isOn = gameCube.autoConnect
            toggle.addTarget(self, action: #selector(autoConnectChanged(_:)), for: .valueChanged)
            cell.accessoryView = toggle
            cell.selectionStyle = .none
        } else {
            let slot = indexPath.row - 1
            cell.textLabel?.text = "NSO GC Controller \(slot + 1)"
            switch gameCube.slotStates[slot] {
            case .ready:
                cell.detailTextLabel?.text = gameCube.batteries[slot].map { "Connected · Battery \($0)%" } ?? "Connected"
            case .connecting: cell.detailTextLabel?.text = "Connecting…"
            default: cell.detailTextLabel?.text = "Waiting for controller"
            }
            cell.selectionStyle = .none
        }
        return cell
    }
    @objc private func autoConnectChanged(_ sender: UISwitch) {
        gameCube.autoConnect = sender.isOn
    }
    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: true)
        guard indexPath.section == 0 else { return }
        let controller = UIHostingController(rootView: WiiRemoteSettingsView())
        controller.title = "Wii Remotes"
        navigationController?.pushViewController(controller, animated: true)
    }
}
