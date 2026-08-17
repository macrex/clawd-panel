#include "configstore.h"
#include <Arduino.h>
#include <Preferences.h>

namespace {

// A MESMA area do piso do relogio: as duas coisas guardadas aqui existem pela
// mesma razao (sobreviver ao cartao) e nao valem uma particao cada.
const char *NVS_AREA = "clawd";

}  // namespace

namespace configstore {

void guardar(const AppConfig &c) {
    if (!c.valid) return;

    Preferences p;
    if (!p.begin(NVS_AREA, false)) return;

    // So escreve o que mudou. `putString` da NVS ja compara antes de gravar,
    // mas a comparacao explicita evita ate abrir a transacao no caso comum, que
    // e o cartao trazendo exatamente o que ja esta guardado.
    if (p.getString("ssid", "") != c.ssid.c_str() ||
        p.getString("pass", "") != c.password.c_str() ||
        p.getString("url",  "") != c.url.c_str() ||
        p.getString("url2", "") != c.url2.c_str() ||
        p.getString("tz",   "") != c.tz.c_str() ||
        p.getString("ntp",  "") != c.ntp.c_str() ||
        p.getUInt("poll", 0)    != c.pollMs ||
        p.getUChar("brilho", 0) != c.brightness) {
        p.putString("ssid", c.ssid.c_str());
        p.putString("pass", c.password.c_str());
        p.putString("url",  c.url.c_str());
        p.putString("url2", c.url2.c_str());
        p.putString("tz",   c.tz.c_str());
        p.putString("ntp",  c.ntp.c_str());
        p.putUInt("poll", c.pollMs);
        p.putUChar("brilho", c.brightness);
        Serial.println("config: reserva da NVS atualizada");
    }
    p.end();
}

AppConfig ler() {
    AppConfig c;

    Preferences p;
    if (!p.begin(NVS_AREA, true)) return c;   // somente leitura

    c.ssid     = p.getString("ssid", "").c_str();
    c.password = p.getString("pass", "").c_str();
    c.url      = p.getString("url", "").c_str();
    c.url2     = p.getString("url2", "").c_str();

    // Os opcionais so sobrescrevem o padrao quando existem de verdade: uma
    // reserva gravada por uma versao anterior pode nao ter todos.
    const String tz = p.getString("tz", "");
    if (tz.length()) c.tz = tz.c_str();
    const String ntp = p.getString("ntp", "");
    if (ntp.length()) c.ntp = ntp.c_str();
    const uint32_t poll = p.getUInt("poll", 0);
    if (poll) c.pollMs = poll;
    const uint8_t brilho = p.getUChar("brilho", 0);
    if (brilho) c.brightness = brilho;

    p.end();

    // As MESMAS tres exigencias do parse do cartao (ver app_config.cpp): sem
    // elas nao ha painel, e uma reserva pela metade so adiaria o SEM CONFIG
    // para depois da tela de boot.
    if (c.ssid.empty() || c.url.empty()) {
        c.error = "reserva da NVS vazia";
        return c;
    }
    c.valid = true;
    return c;
}

}  // namespace configstore
