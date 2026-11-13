import AppKit
import BadgeMicCore
import BadgeMicRender
import SwiftUI
import UniformTypeIdentifiers

struct BadgeTab: View {
    @EnvironmentObject var model: AppModel

    var body: some View {
        HStack(alignment: .top, spacing: 18) {
            ScrollView {
                VStack(alignment: .leading, spacing: 12) {
                    GroupBox("內容") {
                        VStack(alignment: .leading, spacing: 8) {
                            TextField("姓名", text: $model.badge.name)
                            TextField("職稱", text: $model.badge.title)
                            TextField("單位/公司", text: $model.badge.organization)
                            TextField("電話", text: $model.badge.phone)
                            TextField("Email", text: $model.badge.email)
                            TextField("網站", text: $model.badge.website)
                            TextField("附加一行", text: $model.badge.extraLine)
                            HStack {
                                Text(model.badge.photoPath.map { URL(fileURLWithPath: $0).lastPathComponent } ?? "未選擇照片")
                                    .foregroundStyle(.secondary).lineLimit(1)
                                Spacer()
                                Button("選擇照片…") { choosePhoto() }
                                if model.badge.photoPath != nil { Button("移除") { model.badge.photoPath = nil } }
                            }
                        }.padding(6)
                    }
                    GroupBox("QR Code") {
                        VStack(alignment: .leading, spacing: 8) {
                            Picker("內容", selection: $model.badge.qrMode) {
                                Text("由欄位產生 vCard").tag(QRMode.vcard)
                                Text("自訂網址/文字").tag(QRMode.custom)
                            }.pickerStyle(.segmented)
                            if model.badge.qrMode == .custom { TextField("網址或文字", text: $model.badge.customQR) }
                        }.padding(6)
                    }
                    GroupBox("上傳到裝置") {
                        VStack(alignment: .leading, spacing: 8) {
                            HStack {
                                Button("上傳全部") { Task { await model.upload(pages: [0, 1]) } }
                                Button("只傳名牌頁") { Task { await model.upload(pages: [0]) } }
                                Button("只傳 QR 頁") { Task { await model.upload(pages: [1]) } }
                                Button("清除 QR 頁") { model.clearPage(1) }
                            }.disabled(!model.ready || model.protoMismatch || model.uploadProgress != nil)
                            if let p = model.uploadProgress { ProgressView(value: p) }
                            if let m = model.uploadMessage { Text(m).font(.callout) }
                            if !model.ready { Text("裝置未就緒,請先到「裝置」分頁連線。").font(.caption).foregroundStyle(.secondary) }
                        }.padding(6)
                    }
                }
            }
            VStack(spacing: 10) {
                Text("預覽 240 × 320").font(.caption).foregroundStyle(.secondary)
                HStack(alignment: .top, spacing: 12) {
                    preview(model.page(0), "名牌")
                    preview(model.page(1), "QR 卡")
                }
                Spacer()
            }
        }
    }

    private func preview(_ page: BadgeRenderer.Page, _ title: String) -> some View {
        VStack {
            Image(nsImage: NSImage(cgImage: page.image, size: NSSize(width: 240, height: 320)))
                .resizable().interpolation(.high).frame(width: 180, height: 240)
                .overlay(RoundedRectangle(cornerRadius: 4).stroke(Color.secondary.opacity(0.5)))
            Text(title).font(.caption)
        }
    }

    private func choosePhoto() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.image]
        panel.allowsMultipleSelection = false
        if panel.runModal() == .OK, let url = panel.url { model.badge.photoPath = url.path }
    }
}
