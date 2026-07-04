// ============================================================
//  wifi_ota.h  —  WiFi, mDNS, OTA, and web file manager
//  wifi-ota branch — sleep_controller_sd
//
//  Public API (call from main sketch):
//    wifiInit()    — setup(): quick connect or captive portal on first boot
//    wifiToggle()  — triple-click: enable/disable WiFi
//    wifiLoop()    — loop(): drives ArduinoOTA.handle()
//
//  Web interface at http://sleep-controller.local :
//    /             — file list, upload form, OTA form
//    /dl/<name>    — download file from SD root
//    /rm/<name>    — delete file from SD root
//    /upload       — POST multipart file upload to SD root
//    /update       — POST multipart firmware OTA (.bin)
//    /reboot       — reboot device
//
//  Audio continues on Core 0 during all WiFi operations.
//  SD_MMC concurrent access is safe via FatFS reentrancy.
// ============================================================
#pragma once

#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <SD_MMC.h>

#define MDNS_HOSTNAME "sleep-controller"
#define WIFI_AP_NAME  "SleepController-Setup"

bool   wifiActive = false;
String wifiIP     = "";

static AsyncWebServer webServer(80);
static WiFiManager    wm;
static bool           routesRegistered = false;

// ── File list page ────────────────────────────────────────────
static String buildPage() {
  String h = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<title>Sleep Controller</title>"
    "<style>"
    "body{font-family:monospace;margin:2em;max-width:800px}"
    "h2{margin-bottom:.2em}h3{margin-top:1.5em}"
    "table{border-collapse:collapse;width:100%}"
    "th,td{padding:.4em .8em;text-align:left;border-bottom:1px solid #ccc}"
    "a{color:#0077cc;text-decoration:none}a:hover{text-decoration:underline}"
    "input[type=submit]{margin-top:.5em;padding:.3em 1em}"
    "</style></head><body>");

  h += "<h2>" MDNS_HOSTNAME ".local</h2>";
  h += "<small>IP: " + WiFi.localIP().toString() + "</small>";
  h += F("<h3>SD Card Files</h3>"
         "<table><tr><th>Name</th><th>Size</th><th colspan='2'></th></tr>");

  File root = SD_MMC.open("/");
  if (root) {
    File f = root.openNextFile();
    while (f) {
      if (!f.isDirectory()) {
        String name = String(f.name());
        String path = name.startsWith("/") ? name : "/" + name;
        size_t sz   = f.size();
        String szStr = sz >= 1048576 ? String(sz / 1048576) + " MB"
                     : sz >= 1024    ? String(sz / 1024)    + " KB"
                                     : String(sz)           + " B";
        h += "<tr><td>" + name + "</td><td>" + szStr + "</td>";
        h += "<td><a href='/dl" + path + "'>Download</a></td>";
        h += "<td><a href='/rm" + path + "' "
             "onclick=\"return confirm('Delete " + name + "?')\">Delete</a></td></tr>";
      }
      f.close();
      f = root.openNextFile();
    }
    root.close();
  }

  h += F("</table>"
    "<h3>Upload File</h3>"
    "<form method='POST' action='/upload' enctype='multipart/form-data'>"
    "<input type='file' name='file'>&nbsp;"
    "<input type='submit' value='Upload'></form>"
    "<h3>Firmware Update</h3>"
    "<form method='POST' action='/update' enctype='multipart/form-data'>"
    "<input type='file' name='firmware' accept='.bin'>&nbsp;"
    "<input type='submit' value='Flash Firmware'></form>"
    "<p><a href='/reboot'>Reboot device</a></p>"
    "</body></html>");
  return h;
}

// ── Web routes ────────────────────────────────────────────────
static void setupWebRoutes() {
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/html", buildPage());
  });

  // File download
  webServer.on("/dl/*", HTTP_GET, [](AsyncWebServerRequest *req) {
    String path = req->url().substring(3);   // strip "/dl"
    if (SD_MMC.exists(path)) {
      req->send(SD_MMC, path, "application/octet-stream", true);
    } else {
      req->send(404, "text/plain", "Not found");
    }
  });

  // File delete
  webServer.on("/rm/*", HTTP_GET, [](AsyncWebServerRequest *req) {
    String path = req->url().substring(3);   // strip "/rm"
    if (SD_MMC.exists(path)) {
      SD_MMC.remove(path);
      req->redirect("/");
    } else {
      req->send(404, "text/plain", "Not found");
    }
  });

  // File upload (multipart POST)
  webServer.on("/upload", HTTP_POST,
    [](AsyncWebServerRequest *req) {
      req->redirect("/");
    },
    [](AsyncWebServerRequest *req, String filename, size_t index,
       uint8_t *data, size_t len, bool final) {
      static File upFile;
      if (index == 0) {
        String path = "/" + filename;
        if (SD_MMC.exists(path)) SD_MMC.remove(path);
        upFile = SD_MMC.open(path, FILE_WRITE);
        if (!upFile)
          Serial.printf("Upload: open failed: %s\n", path.c_str());
      }
      if (upFile) upFile.write(data, len);
      if (final && upFile) {
        upFile.close();
        Serial.printf("Upload OK: /%s (%u bytes)\n", filename.c_str(), index + len);
      }
    }
  );

  // Firmware OTA via web
  webServer.on("/update", HTTP_POST,
    [](AsyncWebServerRequest *req) {
      bool ok = !Update.hasError();
      AsyncWebServerResponse *resp = req->beginResponse(200, "text/plain",
        ok ? "Update OK — rebooting..." : "Update FAILED — see serial log");
      resp->addHeader("Connection", "close");
      req->send(resp);
      if (ok) { delay(500); ESP.restart(); }
    },
    [](AsyncWebServerRequest *req, String filename, size_t index,
       uint8_t *data, size_t len, bool final) {
      if (index == 0) {
        Serial.printf("OTA start: %s\n", filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
      }
      if (Update.write(data, len) != len) Update.printError(Serial);
      if (final) {
        if (Update.end(true)) {
          Serial.printf("OTA OK: %u bytes\n", index + len);
        } else {
          Update.printError(Serial);
        }
      }
    }
  );

  webServer.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/plain", "Rebooting...");
    delay(500);
    ESP.restart();
  });
}

// ── Internal: activate services after WiFi connects ───────────
static void onConnected() {
  wifiIP = WiFi.localIP().toString();
  MDNS.begin(MDNS_HOSTNAME);
  ArduinoOTA.setHostname(MDNS_HOSTNAME);
  ArduinoOTA.begin();
  if (!routesRegistered) {
    setupWebRoutes();
    routesRegistered = true;
  }
  webServer.begin();
  wifiActive = true;
  Serial.printf("WiFi: %s  http://%s.local\n", wifiIP.c_str(), MDNS_HOSTNAME);
}

// ── Public API ────────────────────────────────────────────────

// Call from setup().
// - First boot (no stored credentials): launches captive portal (blocks until
//   configured or 2-minute timeout). Audio continues on Core 0.
// - Subsequent boots: quick 10-second connect attempt with stored credentials.
//   If router unreachable, proceeds offline (triple-click to retry).
void wifiInit() {
  wm.setConnectTimeout(10);        // seconds to try stored creds
  wm.setConfigPortalTimeout(120);  // captive portal timeout (first boot)
  wm.setSaveConfigCallback([]() {
    Serial.println("WiFi: credentials saved");
  });

  bool connected = wm.autoConnect(WIFI_AP_NAME);
  if (!connected) {
    Serial.println("WiFi: not connected — triple-click to retry");
    return;
  }
  onConnected();
}

// Call on triple-click. Toggles WiFi on/off.
// When enabling: 20-second connect attempt then 2-minute captive portal.
void wifiToggle() {
  if (wifiActive) {
    webServer.end();
    MDNS.end();
    WiFi.disconnect(true);
    wifiActive = false;
    wifiIP     = "";
    Serial.println("WiFi: disabled");
  } else {
    wm.setConnectTimeout(20);
    wm.setConfigPortalTimeout(120);
    bool connected = wm.autoConnect(WIFI_AP_NAME);
    if (!connected) {
      Serial.println("WiFi: connection failed");
      return;
    }
    onConnected();
  }
}

// Call from loop(). Drives ArduinoOTA polling.
void wifiLoop() {
  if (wifiActive) ArduinoOTA.handle();
}
