#include "i18n.h"
#include <string.h>

static Lang currentLang = Lang::ZH;

struct I18nEntry {
    const char *key;
    const char *zh;
    const char *en;
};

static const I18nEntry strings[] = {
    // OOBE
    {"welcome",      "欢迎使用 Vibe Pi",       "Welcome to Vibe Pi"},
    {"select_lang",  "选择语言",               "Select Language"},
    {"chinese",      "中文",                   "中文"},
    {"english",      "English",                "English"},
    {"scan_wifi",    "扫描 WiFi",              "Scan WiFi"},
    {"scanning",     "正在扫描...",             "Scanning..."},
    {"select_net",   "选择网络",               "Select Network"},
    {"enter_pass",   "输入密码",               "Enter Password"},
    {"conn_wifi",    "正在连接 WiFi...",        "Connecting to WiFi..."},
    {"wifi_ok",      "WiFi 已连接",            "WiFi Connected"},
    {"wifi_fail",    "WiFi 连接失败",          "WiFi Connection Failed"},
    {"pairing",      "设备配对",               "Device Pairing"},
    {"pair_code",    "配对码",                 "Pairing Code"},
    {"pair_wait",    "请在主机端输入配对码",     "Enter this code on your host"},
    {"pair_ok",      "配对成功",               "Pairing Successful"},
    {"pair_fail",    "配对失败",               "Pairing Failed"},
    {"pair_timeout", "配对超时",               "Pairing Timed Out"},
    {"syncing",      "正在同步...",             "Syncing..."},
    {"setup_done",   "设置完成",               "Setup Complete"},
    {"swipe_hint",   "左右滑动切换页面",        "Swipe to switch pages"},

    // Dashboard
    {"idle",         "空闲",                   "Idle"},
    {"no_tools",     "无活跃工具",             "No active tools"},
    {"active",       "运行中",                 "Active"},
    {"inactive",     "未运行",                 "Inactive"},
    {"tokens",       "令牌",                   "Tokens"},
    {"sessions",     "会话",                   "Sessions"},
    {"uptime",       "运行时间",               "Uptime"},
    {"find_host",    "正在查找主机...",         "Finding host..."},
    {"reconnecting", "正在重连...",             "Reconnecting..."},
    {"wait_data",    "等待数据...",             "Waiting for data..."},

    // System
    {"system",       "系统",                   "System"},
    {"cpu",          "处理器",                 "CPU"},
    {"memory",       "内存",                   "Memory"},
    {"network",      "网络",                   "Network"},

    // Settings
    {"settings",     "设置",                   "Settings"},
    {"display",      "显示",                   "Display"},
    {"brightness",   "亮度",                   "Brightness"},
    {"sleep_to",     "休眠超时",               "Sleep Timeout"},
    {"theme",        "主题",                   "Theme"},
    {"net_set",      "网络设置",               "Network"},
    {"notif",        "通知",                   "Notifications"},
    {"usage_alert",  "用量预警",               "Usage Alert"},
    {"disc_alert",   "断连提醒",               "Disconnect Alert"},
    {"collectors",   "采集器",                 "Collectors"},
    {"sys_set",      "系统设置",               "System"},
    {"language",     "语言",                   "Language"},
    {"timezone",     "时区",                   "Timezone"},
    {"dev_name",     "设备名称",               "Device Name"},
    {"about",        "关于",                   "About"},
    {"fw_ver",       "固件版本",               "Firmware Version"},
    {"hw_info",      "硬件信息",               "Hardware Info"},
    {"mac_addr",     "MAC 地址",               "MAC Address"},
    {"pair_stat",    "配对状态",               "Pairing Status"},

    // OTA
    {"update_avail", "有新版本可用",           "Update Available"},
    {"downloading",  "正在下载...",             "Downloading..."},
    {"installing",   "正在安装...",             "Installing..."},
    {"update_ok",    "更新成功，即将重启",      "Update OK, restarting..."},
    {"update_fail",  "更新失败",               "Update Failed"},
    {"up_to_date",   "已是最新版本",           "Up to date"},

    // Reset
    {"reset",        "重置",                   "Reset"},
    {"soft_restart",  "软重启",                "Soft Restart"},
    {"disp_reset",   "重置显示设置",           "Reset Display"},
    {"net_reset",    "重置网络设置",           "Reset Network"},
    {"factory_reset","恢复出厂设置",           "Factory Reset"},
    {"reset_confirm","确认重置？",             "Confirm Reset?"},
    {"resetting",    "正在重置...",             "Resetting..."},

    // Diagnostics
    {"diagnostics",  "诊断",                   "Diagnostics"},
    {"self_test",    "自检",                   "Self Test"},
    {"all_ok",       "全部正常",               "All OK"},
    {"error_found",  "发现错误",               "Error Found"},
    {"safe_mode",    "安全模式",               "Safe Mode"},

    // Boot
    {"booting",      "启动中...",               "Booting..."},

    {nullptr, nullptr, nullptr}
};

void i18n_init(Lang lang) {
    currentLang = lang;
}

void i18n_set_lang(Lang lang) {
    currentLang = lang;
}

Lang i18n_get_lang() {
    return currentLang;
}

const char *i18n(const char *key) {
    for (int i = 0; strings[i].key != nullptr; i++) {
        if (strcmp(strings[i].key, key) == 0) {
            return (currentLang == Lang::ZH) ? strings[i].zh : strings[i].en;
        }
    }
    return key;
}
