#include "config_portal.h"

#include "WiFi.h"

// HTML for configuration page
static const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Tilted Gateway Configuration</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }
    .form-group { margin-bottom: 15px; }
    label { display: block; margin-bottom: 5px; }
    input[type="text"], input[type="password"] { width: 100%; padding: 8px; box-sizing: border-box; }
    button { background-color: #4CAF50; color: white; padding: 10px 15px; border: none; cursor: pointer; }
    fieldset { margin-bottom: 20px; }
    .section { margin-bottom: 30px; }
  </style>
</head>
<body>
  <h1>Tilted Gateway Configuration</h1>
  <form action="/save" method="post">
    <div class="section">
      <fieldset>
        <legend>Device Settings</legend>
        <div class="form-group">
          <label for="deviceName">Device Name:</label>
          <input type="text" id="deviceName" name="deviceName" value="%DEVICE_NAME%">
        </div>
      </fieldset>
    </div>

    <div class="section">
      <fieldset>
        <legend>WiFi Settings</legend>
        <div class="form-group">
          <label for="wifiSSID">WiFi SSID:</label>
          <input type="text" id="wifiSSID" name="wifiSSID" value="%WIFI_SSID%">
        </div>
        <div class="form-group">
          <label for="wifiPassword">WiFi Password:</label>
          <input type="password" id="wifiPassword" name="wifiPassword" value="%WIFI_PASSWORD%">
        </div>
      </fieldset>
    </div>

    <div class="section">
      <fieldset>
        <legend>Calibration</legend>
        <div class="form-group">
          <label for="polynomial">Polynomial:</label>
          <input type="text" id="polynomial" name="polynomial" value="%POLYNOMIAL%">
        </div>
      </fieldset>
    </div>

    <div class="section">
      <fieldset>
        <legend>Brewfather Settings</legend>
        <div class="form-group">
          <label for="brewfatherURL">Brewfather URL:</label>
          <input type="text" id="brewfatherURL" name="brewfatherURL" value="%BREWFATHER_URL%">
        </div>
      </fieldset>
    </div>

    <button type="submit">Save Configuration</button>
  </form>
</body>
</html>
)rawliteral";

String ConfigPortal::processTemplate(const String& deviceName,
                                    const String& wifiSSID,
                                    const String& wifiPassword,
                                    const String& polynomial,
                                    const String& brewfatherURL) const
{
    String html = CONFIG_HTML;
    html.replace("%DEVICE_NAME%", deviceName);
    html.replace("%WIFI_SSID%", wifiSSID);
    html.replace("%WIFI_PASSWORD%", wifiPassword);
    html.replace("%POLYNOMIAL%", polynomial);
    html.replace("%BREWFATHER_URL%", brewfatherURL);
    return html;
}

void ConfigPortal::saveSettings(const String& deviceName,
                               const String& wifiSSID,
                               const String& wifiPassword,
                               const String& polynomial,
                               const String& brewfatherURL)
{
    preferences_.begin("tilted", false);
    preferences_.putString("deviceName", deviceName);
    preferences_.putString("wifiSSID", wifiSSID);
    preferences_.putString("wifiPassword", wifiPassword);
    preferences_.putString("polynomial", polynomial);
    preferences_.putString("brewfatherURL", brewfatherURL);
    preferences_.end();

    Serial.println("Settings saved");
}

void ConfigPortal::start(String& deviceName,
                         String& wifiSSID,
                         String& wifiPassword,
                         String& polynomial,
                         String& brewfatherURL)
{
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSSID_, apPassword_);

    Serial.println("AP Started");
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());

  server_.on("/", HTTP_GET, [&]() {
    server_.send(200, "text/html", processTemplate(deviceName, wifiSSID, wifiPassword, polynomial, brewfatherURL));
  });

    server_.on("/status", HTTP_GET, [&]() {
        String body;
        body.reserve(256);
        body += "OK\n";
        body += "ip=" + WiFi.softAPIP().toString() + "\n";
        body += "ssid=" + String(apSSID_) + "\n";
        server_.send(200, "text/plain", body);
    });

    auto redirectToRoot = [&]() {
        Serial.printf("[CAPTIVE] %s %s\n", server_.method() == HTTP_GET ? "GET" : "REQ", server_.uri().c_str());
        server_.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
        server_.send(302, "text/plain", "Redirecting to setup...");
    };

    server_.on("/generate_204", HTTP_GET, redirectToRoot);
    server_.on("/gen_204", HTTP_GET, redirectToRoot);
    server_.on("/hotspot-detect.html", HTTP_GET, redirectToRoot);
    server_.on("/library/test/success.html", HTTP_GET, redirectToRoot);
    server_.on("/ncsi.txt", HTTP_GET, redirectToRoot);

    server_.onNotFound([&]() {
        Serial.printf("[CAPTIVE] 404 %s\n", server_.uri().c_str());
        server_.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
        server_.send(302, "text/plain", "Redirecting to setup...");
    });

    server_.on("/save", HTTP_POST, [&]() {
        deviceName = server_.arg("deviceName");
        wifiSSID = server_.arg("wifiSSID");
        wifiPassword = server_.arg("wifiPassword");
        polynomial = server_.arg("polynomial");
        brewfatherURL = server_.arg("brewfatherURL");

        saveSettings(deviceName, wifiSSID, wifiPassword, polynomial, brewfatherURL);

        server_.send(200,
                     "text/html",
                     "<html><head><meta http-equiv='refresh' content='5;url=/'></head>"
                     "<body><h1>Configuration Saved</h1>"
                     "<p>The device will restart in 5 seconds.</p></body></html>");

        delay(5000);
        ESP.restart();
    });

    server_.begin();

    dnsServer_.stop();
    dnsServer_.start(53, "*", WiFi.softAPIP());

    Serial.println("Configuration mode started");
}

void ConfigPortal::handle()
{
    dnsServer_.processNextRequest();
    server_.handleClient();
}
