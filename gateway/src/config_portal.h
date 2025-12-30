#pragma once

#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>

// Small captive portal + config page for the gateway.
//
// Usage:
//   ConfigPortal portal(preferences);
//   portal.setApCredentials("TiltedGateway-Setup", "tilted123");
//   portal.start(deviceName, wifiSSID, wifiPassword, polynomial, brewfatherURL);
//   ... loop: portal.handle();
//
class ConfigPortal
{
public:
    explicit ConfigPortal(Preferences& preferences)
        : preferences_(preferences)
    {}

    void setApCredentials(const char* ssid, const char* password)
    {
        apSSID_ = ssid;
        apPassword_ = password;
    }

    // Starts AP mode and the captive portal web server.
    void start(String& deviceName,
               String& wifiSSID,
               String& wifiPassword,
               String& polynomial,
               String& brewfatherURL);

    // Must be called frequently from loop() while in config mode.
    void handle();

private:
    String processTemplate(const String& deviceName,
                           const String& wifiSSID,
                           const String& wifiPassword,
                           const String& polynomial,
                           const String& brewfatherURL) const;

    void saveSettings(const String& deviceName,
                      const String& wifiSSID,
                      const String& wifiPassword,
                      const String& polynomial,
                      const String& brewfatherURL);

private:
    Preferences& preferences_;
    WebServer server_{80};
    DNSServer dnsServer_;

    const char* apSSID_ = "TiltedGateway-Setup";
    const char* apPassword_ = "tilted123";
};
