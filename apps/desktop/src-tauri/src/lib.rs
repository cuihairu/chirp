// Chirp 桌面聊天 App 的 Tauri v2 壳：协议与业务都在前端（@chirp/protocol TS 核），
// 壳只负责窗口与桌面通知插件。
#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_notification::init())
        .run(tauri::generate_context!())
        .expect("error while running chirp desktop");
}
