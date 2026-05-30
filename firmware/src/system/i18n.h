#pragma once

#include <Arduino.h>

enum class Lang : uint8_t { ZH = 0, EN = 1 };

void i18n_init(Lang lang = Lang::ZH);
void i18n_set_lang(Lang lang);
Lang i18n_get_lang();
const char *i18n(const char *key);

// ── String Keys ──
// OOBE
#define S_WELCOME             "welcome"
#define S_SELECT_LANGUAGE     "select_lang"
#define S_CHINESE             "chinese"
#define S_ENGLISH             "english"
#define S_SCAN_WIFI           "scan_wifi"
#define S_SCANNING            "scanning"
#define S_SELECT_NETWORK      "select_net"
#define S_ENTER_PASSWORD      "enter_pass"
#define S_CONNECTING_WIFI     "conn_wifi"
#define S_WIFI_CONNECTED      "wifi_ok"
#define S_WIFI_FAILED         "wifi_fail"
#define S_PAIRING             "pairing"
#define S_PAIR_CODE           "pair_code"
#define S_PAIR_WAITING        "pair_wait"
#define S_PAIR_SUCCESS        "pair_ok"
#define S_PAIR_FAILED         "pair_fail"
#define S_PAIR_TIMEOUT        "pair_timeout"
#define S_SYNCING             "syncing"
#define S_SETUP_COMPLETE      "setup_done"
#define S_SWIPE_HINT          "swipe_hint"

// Dashboard
#define S_IDLE                "idle"
#define S_NO_ACTIVE_TOOLS     "no_tools"
#define S_ACTIVE              "active"
#define S_INACTIVE            "inactive"
#define S_TOKENS              "tokens"
#define S_SESSIONS            "sessions"
#define S_UPTIME              "uptime"
#define S_FINDING_HOST        "find_host"
#define S_RECONNECTING        "reconnecting"
#define S_WAITING_DATA        "wait_data"

// System page
#define S_SYSTEM              "system"
#define S_CPU                 "cpu"
#define S_MEMORY              "memory"
#define S_NETWORK             "network"

// Settings
#define S_SETTINGS            "settings"
#define S_DISPLAY             "display"
#define S_BRIGHTNESS          "brightness"
#define S_SLEEP_TIMEOUT       "sleep_to"
#define S_THEME               "theme"
#define S_NETWORK_SETTINGS    "net_set"
#define S_NOTIFICATIONS       "notif"
#define S_USAGE_ALERT         "usage_alert"
#define S_DISCONNECT_ALERT    "disc_alert"
#define S_COLLECTORS          "collectors"
#define S_SYSTEM_SETTINGS     "sys_set"
#define S_LANGUAGE            "language"
#define S_TIMEZONE            "timezone"
#define S_DEVICE_NAME         "dev_name"
#define S_ABOUT               "about"
#define S_FIRMWARE_VER        "fw_ver"
#define S_FIRMWARE_UPDATE     "fw_update"
#define S_HARDWARE_INFO       "hw_info"
#define S_MAC_ADDRESS         "mac_addr"
#define S_PAIR_STATUS         "pair_stat"

// OTA
#define S_UPDATE_AVAILABLE    "update_avail"
#define S_DOWNLOADING         "downloading"
#define S_INSTALLING          "installing"
#define S_UPDATE_SUCCESS      "update_ok"
#define S_UPDATE_FAILED       "update_fail"
#define S_UP_TO_DATE          "up_to_date"

// Reset
#define S_RESET               "reset"
#define S_SOFT_RESTART        "soft_restart"
#define S_DISPLAY_RESET       "disp_reset"
#define S_NETWORK_RESET       "net_reset"
#define S_FACTORY_RESET       "factory_reset"
#define S_RESET_CONFIRM       "reset_confirm"
#define S_RESETTING           "resetting"

// Diagnostics
#define S_DIAGNOSTICS         "diagnostics"
#define S_SELF_TEST           "self_test"
#define S_ALL_OK              "all_ok"
#define S_ERROR_FOUND         "error_found"
#define S_SAFE_MODE           "safe_mode"

// Boot
#define S_BOOTING             "booting"
