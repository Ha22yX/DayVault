"""Lightweight i18n (English default, Chinese) for the DayVault sync tool."""

TRANSLATIONS: dict[str, dict[str, str]] = {
    "en": {
        "title": "DayVault Sync",
        "device": "Device:",
        "sync_btn": "Sync now",
        "col_name": "Name",
        "col_size": "Size",
        "col_time": "Time",
        "col_status": "Status",
        "folder": "Sync folder:",
        "choose_folder": "Choose...",
        "waiting": "Waiting for device...",
        "device_detected": "Device {} connected, syncing...",
        "device_disconnected": "Device {} disconnected",
        "status_downloaded": "Downloaded",
        "status_not_downloaded": "Not downloaded",
        "status_downloading": "Downloading {}%",
        "status_failed": "Download failed",
        "sync_finished": "{} sync complete",
        "new_downloads": "new downloads {}",
        "failures": "failed {}",
        "sync_error": "{} sync error: {}",
        "no_device": "No device selected",
        "device_idle": "Device idle",
        "manual_sync": "Manual sync {}...",
        "choose_folder_title": "Choose sync folder",
        "folder_changed": "Sync folder changed to {}",
        "lang_label": "Language:",
        "lang_en": "English",
        "lang_zh": "Chinese",
        "tray_tip": "DayVault Sync",
        "tray_show": "Show window",
        "tray_quit": "Quit",
        "tray_hide": "Minimized to the system tray; click the tray icon to reopen.",
    },
    "zh": {
        "title": "DayVault 同步",
        "device": "设备:",
        "sync_btn": "立即同步",
        "col_name": "文件名",
        "col_size": "大小",
        "col_time": "时间",
        "col_status": "状态",
        "folder": "同步文件夹:",
        "choose_folder": "选择...",
        "waiting": "等待设备...",
        "device_detected": "检测到设备 {}，正在同步...",
        "device_disconnected": "设备已断开 {}",
        "status_downloaded": "已下载",
        "status_not_downloaded": "未下载",
        "status_downloading": "下载中 {}%",
        "status_failed": "下载失败",
        "sync_finished": "{} 同步完成",
        "new_downloads": "新下载 {} 个",
        "failures": "失败 {} 个",
        "sync_error": "{} 同步错误: {}",
        "no_device": "没有选中的设备",
        "device_idle": "设备空闲",
        "manual_sync": "手动同步 {}...",
        "choose_folder_title": "选择同步文件夹",
        "folder_changed": "同步文件夹已改为 {}",
        "lang_label": "语言:",
        "lang_en": "英文",
        "lang_zh": "中文",
        "tray_tip": "DayVault 同步",
        "tray_show": "打开主界面",
        "tray_quit": "退出",
        "tray_hide": "已最小化到系统托盘，点击托盘图标可重新打开。",
    },
}


class Lang:
    def __init__(self, lang: str = "en"):
        self.lang = lang if lang in TRANSLATIONS else "en"

    def tr(self, key: str, *args) -> str:
        table = TRANSLATIONS.get(self.lang, TRANSLATIONS["en"])
        s = table.get(key, key)
        if args:
            try:
                return s.format(*args)
            except Exception:
                return s
        return s
