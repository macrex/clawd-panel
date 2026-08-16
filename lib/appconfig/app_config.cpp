#include "app_config.h"
#include <ArduinoJson.h>

AppConfig parseConfig(const char *json) {
    AppConfig c;
    JsonDocument doc;
    if (deserializeJson(doc, json)) { c.error = "JSON invalido"; return c; }

    // password pode ser vazia (rede aberta), mas precisa existir.
    if (!doc["wifi"]["ssid"].is<const char *>())     { c.error = "wifi.ssid ausente";     return c; }
    if (!doc["wifi"]["password"].is<const char *>()) { c.error = "wifi.password ausente"; return c; }
    if (!doc["api"]["url"].is<const char *>())       { c.error = "api.url ausente";       return c; }

    c.ssid     = doc["wifi"]["ssid"].as<const char *>();
    c.password = doc["wifi"]["password"].as<const char *>();
    c.url      = doc["api"]["url"].as<const char *>();
    // Opcional: ausente deixa url2 vazia, e ai a placa tem uma fonte so.
    if (doc["api"]["url2"].is<const char *>()) c.url2 = doc["api"]["url2"].as<const char *>();
    if (doc["poll_ms"].is<uint32_t>())    c.pollMs     = doc["poll_ms"].as<uint32_t>();
    if (doc["brightness"].is<uint8_t>())  c.brightness = doc["brightness"].as<uint8_t>();
    // Opcionais com padrao util: um cartao antigo, sem estes campos, continua
    // ligando com o fuso e o NTP certos para esta mesa.
    if (doc["tz"].is<const char *>())     c.tz  = doc["tz"].as<const char *>();
    if (doc["ntp"].is<const char *>())    c.ntp = doc["ntp"].as<const char *>();
    c.valid = true;
    return c;
}
