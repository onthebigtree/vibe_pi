#include "settings_manager.h"
#include "config.h"
#include <Preferences.h>

static DeviceSettings cfg;
static Preferences prefs;

static void save_field_str(const char *key, const char *value) {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString(key, value);
    prefs.end();
}

void settings_init() {
    settings_load();
    cfg.boot_count++;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putUInt(NVS_BOOT_COUNT, cfg.boot_count);
    prefs.end();
    i18n_init(cfg.language);
}

static String safe_get_string(Preferences &p, const char *key, const char *def) {
    if (p.isKey(key)) return p.getString(key, def);
    return String(def);
}

void settings_load() {
    prefs.begin(NVS_NAMESPACE, true);

    cfg.brightness       = prefs.getUChar(NVS_BRIGHTNESS, 80);
    cfg.sleep_timeout_ms = prefs.getUInt(NVS_SLEEP_TIMEOUT, 60000);
    strlcpy(cfg.theme, safe_get_string(prefs, NVS_THEME, "minimal").c_str(), sizeof(cfg.theme));

    strlcpy(cfg.wifi_ssid, safe_get_string(prefs, NVS_WIFI_SSID, "").c_str(), sizeof(cfg.wifi_ssid));
    strlcpy(cfg.wifi_pass, safe_get_string(prefs, NVS_WIFI_PASS, "").c_str(), sizeof(cfg.wifi_pass));
    strlcpy(cfg.host_addr, safe_get_string(prefs, NVS_HOST_ADDR, "").c_str(), sizeof(cfg.host_addr));
    cfg.host_port = prefs.getUShort(NVS_HOST_PORT, 8765);

    cfg.paired = prefs.getBool(NVS_PAIRED, false);
    strlcpy(cfg.pair_token, safe_get_string(prefs, NVS_PAIR_TOKEN, "").c_str(), sizeof(cfg.pair_token));
    strlcpy(cfg.host_name, safe_get_string(prefs, NVS_HOST_NAME, "").c_str(), sizeof(cfg.host_name));

    cfg.language = (Lang)prefs.getUChar(NVS_LANGUAGE, (uint8_t)Lang::ZH);
    strlcpy(cfg.device_name, safe_get_string(prefs, NVS_DEVICE_NAME, "Vibe Pi").c_str(), sizeof(cfg.device_name));
    strlcpy(cfg.timezone, safe_get_string(prefs, NVS_TIMEZONE, "Asia/Shanghai").c_str(), sizeof(cfg.timezone));
    strlcpy(cfg.ota_channel, prefs.getString(NVS_OTA_CHANNEL, "stable").c_str(), sizeof(cfg.ota_channel));

    cfg.alert_usage_pct  = prefs.getUChar(NVS_ALERT_USAGE, 80);
    cfg.alert_disconnect = prefs.getBool(NVS_ALERT_DISCONN, true);

    cfg.oobe_done  = prefs.getBool(NVS_OOBE_DONE, false);
    cfg.boot_count = prefs.getUInt(NVS_BOOT_COUNT, 0);

    prefs.end();
}

void settings_save() {
    prefs.begin(NVS_NAMESPACE, false);

    prefs.putUChar(NVS_BRIGHTNESS, cfg.brightness);
    prefs.putUInt(NVS_SLEEP_TIMEOUT, cfg.sleep_timeout_ms);
    prefs.putString(NVS_THEME, cfg.theme);

    prefs.putString(NVS_WIFI_SSID, cfg.wifi_ssid);
    prefs.putString(NVS_WIFI_PASS, cfg.wifi_pass);
    prefs.putString(NVS_HOST_ADDR, cfg.host_addr);
    prefs.putUShort(NVS_HOST_PORT, cfg.host_port);

    prefs.putBool(NVS_PAIRED, cfg.paired);
    prefs.putString(NVS_PAIR_TOKEN, cfg.pair_token);
    prefs.putString(NVS_HOST_NAME, cfg.host_name);

    prefs.putUChar(NVS_LANGUAGE, (uint8_t)cfg.language);
    prefs.putString(NVS_DEVICE_NAME, cfg.device_name);
    prefs.putString(NVS_TIMEZONE, cfg.timezone);
    prefs.putString(NVS_OTA_CHANNEL, cfg.ota_channel);

    prefs.putUChar(NVS_ALERT_USAGE, cfg.alert_usage_pct);
    prefs.putBool(NVS_ALERT_DISCONN, cfg.alert_disconnect);

    prefs.putBool(NVS_OOBE_DONE, cfg.oobe_done);
    prefs.putUInt(NVS_BOOT_COUNT, cfg.boot_count);

    prefs.end();
}

void settings_save_wifi(const char *ssid, const char *pass) {
    strlcpy(cfg.wifi_ssid, ssid, sizeof(cfg.wifi_ssid));
    strlcpy(cfg.wifi_pass, pass, sizeof(cfg.wifi_pass));
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString(NVS_WIFI_SSID, ssid);
    prefs.putString(NVS_WIFI_PASS, pass);
    prefs.end();
}

void settings_save_pairing(bool paired, const char *token, const char *hostName,
                           const char *hostAddr, uint16_t hostPort) {
    cfg.paired = paired;
    strlcpy(cfg.pair_token, token, sizeof(cfg.pair_token));
    strlcpy(cfg.host_name, hostName, sizeof(cfg.host_name));
    strlcpy(cfg.host_addr, hostAddr, sizeof(cfg.host_addr));
    cfg.host_port = hostPort;

    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBool(NVS_PAIRED, paired);
    prefs.putString(NVS_PAIR_TOKEN, token);
    prefs.putString(NVS_HOST_NAME, hostName);
    prefs.putString(NVS_HOST_ADDR, hostAddr);
    prefs.putUShort(NVS_HOST_PORT, hostPort);
    prefs.end();
}

void settings_mark_oobe_done() {
    cfg.oobe_done = true;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBool(NVS_OOBE_DONE, true);
    prefs.end();
}

DeviceSettings &settings_get() { return cfg; }

void settings_reset_display() {
    cfg.brightness = DEFAULT_BRIGHTNESS;
    cfg.sleep_timeout_ms = SLEEP_TIMEOUT_MS;
    strlcpy(cfg.theme, "minimal", sizeof(cfg.theme));
    settings_save();
    Serial.println("[Settings] L1: Display reset");
}

void settings_reset_network() {
    cfg.wifi_ssid[0] = '\0';
    cfg.wifi_pass[0] = '\0';
    cfg.host_addr[0] = '\0';
    cfg.host_port = WS_DEFAULT_PORT;
    cfg.paired = false;
    cfg.pair_token[0] = '\0';
    cfg.host_name[0] = '\0';
    settings_save();
    Serial.println("[Settings] L2: Network reset");
}

void settings_reset_factory() {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
    cfg = DeviceSettings();
    Serial.println("[Settings] L3: Factory reset");
}

void settings_export_json(JsonDocument &doc) {
    doc["brightness"]       = cfg.brightness;
    doc["sleep_timeout_ms"] = cfg.sleep_timeout_ms;
    doc["theme"]            = cfg.theme;
    doc["language"]         = (cfg.language == Lang::ZH) ? "zh" : "en";
    doc["device_name"]      = cfg.device_name;
    doc["timezone"]         = cfg.timezone;
    doc["ota_channel"]      = cfg.ota_channel;
    doc["alert_usage_pct"]  = cfg.alert_usage_pct;
    doc["alert_disconnect"] = cfg.alert_disconnect;
}

bool settings_import_json(JsonDocument &doc) {
    return settings_apply_sync(doc.as<JsonObject>());
}

bool settings_apply_sync(const JsonObject &p) {
    bool changed = false;

    if (p["brightness"].is<int>()) {
        uint8_t v = p["brightness"].as<uint8_t>();
        if (v <= 100 && v != cfg.brightness) { cfg.brightness = v; changed = true; }
    }
    if (p["sleep_timeout_ms"].is<unsigned int>()) {
        uint32_t v = p["sleep_timeout_ms"];
        if (v >= 5000 && v != cfg.sleep_timeout_ms) { cfg.sleep_timeout_ms = v; changed = true; }
    }
    if (p["theme"].is<const char*>()) {
        const char *v = p["theme"];
        if (strcmp(v, cfg.theme) != 0) { strlcpy(cfg.theme, v, sizeof(cfg.theme)); changed = true; }
    }
    if (p["language"].is<const char*>()) {
        const char *v = p["language"];
        Lang l = (strcmp(v, "en") == 0) ? Lang::EN : Lang::ZH;
        if (l != cfg.language) { cfg.language = l; i18n_set_lang(l); changed = true; }
    }
    if (p["device_name"].is<const char*>()) {
        const char *v = p["device_name"];
        if (strcmp(v, cfg.device_name) != 0) { strlcpy(cfg.device_name, v, sizeof(cfg.device_name)); changed = true; }
    }
    if (p["timezone"].is<const char*>()) {
        const char *v = p["timezone"];
        if (strcmp(v, cfg.timezone) != 0) { strlcpy(cfg.timezone, v, sizeof(cfg.timezone)); changed = true; }
    }
    if (p["ota_channel"].is<const char*>()) {
        const char *v = p["ota_channel"];
        if (strcmp(v, cfg.ota_channel) != 0) { strlcpy(cfg.ota_channel, v, sizeof(cfg.ota_channel)); changed = true; }
    }
    if (p["alert_usage_pct"].is<int>()) {
        uint8_t v = p["alert_usage_pct"];
        if (v <= 100 && v != cfg.alert_usage_pct) { cfg.alert_usage_pct = v; changed = true; }
    }
    if (p["alert_disconnect"].is<bool>()) {
        bool v = p["alert_disconnect"];
        if (v != cfg.alert_disconnect) { cfg.alert_disconnect = v; changed = true; }
    }

    if (changed) settings_save();
    return changed;
}
