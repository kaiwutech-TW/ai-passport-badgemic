import BadgeMicCore
import CoreBluetooth
import Foundation

enum BLEError: Error, LocalizedError {
    case notConnected, writeFailed(String)
    var errorDescription: String? {
        switch self {
        case .notConnected: return "裝置未連線"
        case .writeFailed(let m): return "寫入失敗:\(m)"
        }
    }
}

struct DiscoveredDevice: Identifiable, Equatable {
    var id: UUID
    var name: String
    var rssi: Int
}

enum LinkState: Equatable {
    case unavailable(String)  // Bluetooth off / unauthorized
    case idle
    case scanning
    case connecting
    case discovering          // connected, setting up GATT / waiting for pairing
    case subscribed           // notifications on; waiting for hello
}

/// CoreBluetooth central for the Nordic UART Service. All callbacks arrive on the main queue.
final class BLETransport: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    static let nus = CBUUID(string: "6e400001-b5a3-f393-e0a9-e50e24dcca9e")
    static let rxUUID = CBUUID(string: "6e400002-b5a3-f393-e0a9-e50e24dcca9e")
    static let txUUID = CBUUID(string: "6e400003-b5a3-f393-e0a9-e50e24dcca9e")
    static let namePrefix = "Passport-"

    var onState: ((LinkState) -> Void)?
    var onDiscovered: (([DiscoveredDevice]) -> Void)?
    var onFrame: ((Frame) -> Void)?
    var onConnected: ((String) -> Void)?   // peripheral name; subscription is up
    var onDisconnected: (() -> Void)?
    var onLog: ((String) -> Void)?
    var onRememberDevice: ((UUID) -> Void)?

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var rx: CBCharacteristic?
    private var tx: CBCharacteristic?
    private var reassembler = FrameReassembler()
    private var found: [UUID: (CBPeripheral, DiscoveredDevice)] = [:]
    private var wantedID: UUID?
    private var userDisconnected = false
    private var subscribeRetries = 0

    private struct PendingWrite { var data: Data; var cont: CheckedContinuation<Void, Error> }
    private var writeQueue: [PendingWrite] = []
    private var writing = false

    private(set) var state: LinkState = .idle { didSet { onState?(state) } }

    func start(lastDevice: UUID?) {
        wantedID = lastDevice
        central = CBCentralManager(delegate: self, queue: .main)
    }

    // MARK: Actions

    func connect(to id: UUID) {
        guard let p = found[id]?.0 ?? central.retrievePeripherals(withIdentifiers: [id]).first else { return }
        userDisconnected = false
        wantedID = id
        central.stopScan()
        attach(p)
        state = .connecting
        central.connect(p, options: nil)
    }

    func disconnect() {
        userDisconnected = true
        if let p = peripheral { central.cancelPeripheralConnection(p) }
        beginScanIfPossible()
    }

    func rescan() { beginScanIfPossible() }

    private func attach(_ p: CBPeripheral) {
        peripheral = p
        p.delegate = self
    }

    private func beginScanIfPossible() {
        guard central.state == .poweredOn else { return }
        guard peripheral == nil || peripheral?.state == .disconnected else { return }
        if state != .scanning { state = .scanning }
        // No service filter: the 128-bit UUID may be only in the scan response; filter ourselves.
        central.scanForPeripherals(withServices: nil, options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
    }

    // MARK: Writes (serialised, write-with-response)

    func write(_ bytes: [UInt8]) async throws {
        try await withCheckedThrowingContinuation { (cont: CheckedContinuation<Void, Error>) in
            writeQueue.append(PendingWrite(data: Data(bytes), cont: cont))
            pumpWrites()
        }
    }

    private func pumpWrites() {
        guard !writing, let first = writeQueue.first else { return }
        guard let p = peripheral, p.state == .connected, let rx else {
            failWrites(BLEError.notConnected)
            return
        }
        writing = true
        p.writeValue(first.data, for: rx, type: .withResponse)
        _ = first
    }

    private func failWrites(_ error: Error) {
        let q = writeQueue
        writeQueue.removeAll()
        writing = false
        for w in q { w.cont.resume(throwing: error) }
    }

    // MARK: CBCentralManagerDelegate

    func centralManagerDidUpdateState(_ c: CBCentralManager) {
        switch c.state {
        case .poweredOn:
            state = .idle
            if let id = wantedID, let p = c.retrievePeripherals(withIdentifiers: [id]).first {
                // A pending connect never times out: it completes whenever the badge comes into range.
                attach(p)
                state = .connecting
                c.connect(p, options: nil)
            }
            beginScanIfPossible()
        case .unauthorized: state = .unavailable("未授權藍牙:請到「系統設定 > 隱私權與安全性 > 藍牙」允許 BadgeMic")
        case .poweredOff: state = .unavailable("藍牙已關閉")
        case .unsupported: state = .unavailable("此 Mac 不支援藍牙 LE")
        default: state = .unavailable("藍牙狀態:\(c.state.rawValue)")
        }
    }

    func centralManager(_ c: CBCentralManager, didDiscover p: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? p.name ?? ""
        let uuids = advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID] ?? []
        guard uuids.contains(Self.nus) || name.hasPrefix(Self.namePrefix) else { return }
        found[p.identifier] = (p, DiscoveredDevice(id: p.identifier, name: name.isEmpty ? "Badge" : name, rssi: RSSI.intValue))
        onDiscovered?(found.values.map(\.1).sorted { $0.rssi > $1.rssi })
        // Auto-reconnect to the remembered device as soon as it is seen.
        if p.identifier == wantedID, peripheral == nil || peripheral?.state == .disconnected, !userDisconnected {
            connect(to: p.identifier)
        }
    }

    func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
        onLog?("已連線 \(p.name ?? "?"),探索服務…")
        state = .discovering
        reassembler.reset()
        subscribeRetries = 0
        p.discoverServices([Self.nus])
    }

    func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        onLog?("連線失敗:\(error?.localizedDescription ?? "?")")
        state = .idle
        if !userDisconnected { central.connect(p, options: nil); state = .connecting }
    }

    func centralManager(_ c: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        onLog?("已斷線\(error.map { ":\($0.localizedDescription)" } ?? "")")
        rx = nil; tx = nil
        failWrites(BLEError.notConnected)
        onDisconnected?()
        if userDisconnected {
            peripheral = nil
            state = .idle
            beginScanIfPossible()
        } else {
            // Auto-reconnect: re-issue a pending connect.
            state = .connecting
            central.connect(p, options: nil)
        }
    }

    // MARK: CBPeripheralDelegate

    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        guard let s = p.services?.first(where: { $0.uuid == Self.nus }) else {
            onLog?("找不到 NUS 服務\(error.map { ":\($0.localizedDescription)" } ?? "")")
            return
        }
        p.discoverCharacteristics([Self.rxUUID, Self.txUUID], for: s)
    }

    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        for c in s.characteristics ?? [] {
            if c.uuid == Self.rxUUID { rx = c }
            if c.uuid == Self.txUUID { tx = c }
        }
        guard let tx, rx != nil else { onLog?("缺少 RX/TX 特徵"); return }
        // CoreBluetooth negotiates the ATT MTU itself (there is no API to request one); log what we got.
        onLog?("MTU 可寫入長度:\(p.maximumWriteValueLength(for: .withResponse)) / \(p.maximumWriteValueLength(for: .withoutResponse))")
        // Subscribing to an encrypted characteristic makes macOS show the pairing dialog (6-digit passkey).
        p.setNotifyValue(true, for: tx)
    }

    func peripheral(_ p: CBPeripheral, didUpdateNotificationStateFor c: CBCharacteristic, error: Error?) {
        if let error {
            onLog?("訂閱失敗(可能需要配對):\(error.localizedDescription)")
            if subscribeRetries < 5, let tx {
                subscribeRetries += 1
                DispatchQueue.main.asyncAfter(deadline: .now() + 2) { p.setNotifyValue(true, for: tx) }
            }
            return
        }
        if c.isNotifying {
            state = .subscribed
            if let id = wantedID { onRememberDevice?(id) }
            onConnected?(p.name ?? "Badge")
        }
    }

    func peripheral(_ p: CBPeripheral, didUpdateValueFor c: CBCharacteristic, error: Error?) {
        guard error == nil, c.uuid == Self.txUUID, let data = c.value else { return }
        for f in reassembler.feed(data) { onFrame?(f) }
    }

    func peripheral(_ p: CBPeripheral, didWriteValueFor c: CBCharacteristic, error: Error?) {
        guard !writeQueue.isEmpty else { return }
        let done = writeQueue.removeFirst()
        writing = false
        if let error { done.cont.resume(throwing: BLEError.writeFailed(error.localizedDescription)) } else { done.cont.resume() }
        pumpWrites()
    }
}
