#include "hora.h"
#include "relogio.h"

#include <Arduino.h>
#include <ctime>
#include <sys/time.h>

namespace {

// Quantos segundos o relogio local esta a frente do UTC. Descoberto uma vez,
// na primeira leitura boa, e guardado: `localtime_r` faz a conta a cada
// chamada, e o cabecalho a faria a 50 Hz por nada.
long     g_offset = 0;
bool     g_offsetLido = false;
uint32_t g_offsetMs = 0;      // millis da ultima reavaliacao

// O ultimo estado conhecido da sincronia. Uma vez sincronizado, nao volta
// atras: o contador do ESP32 continua andando com a rede fora, e "perdi o NTP"
// nao e motivo para o painel ficar sem hora.
bool g_ok = false;

// Quantos segundos o fuso configurado esta a frente do UTC.
//
// A newlib desta toolchain nao tem `tm_gmtoff` (o campo existe na glibc e no
// BSD, nao no padrao), entao a diferenca sai da comparacao dos dois calendarios
// do MESMO instante. O acerto de dia cobre o fuso que atravessa a meia-noite: a
// hora local pode estar no dia seguinte ou no anterior ao UTC, e ai a diferenca
// de horas sozinha erra por 24.
long offsetDoFuso(time_t t) {
    struct tm loc {}, utc {};
    localtime_r(&t, &loc);
    gmtime_r(&t, &utc);

    long off = (loc.tm_hour - utc.tm_hour) * 3600L
             + (loc.tm_min - utc.tm_min) * 60L
             + (loc.tm_sec - utc.tm_sec);
    const int dias = loc.tm_yday - utc.tm_yday;
    // `tm_yday` vira 0 no Ano Novo, entao a diferenca de um dia aparece como
    // +364/+365 ou -364/-365 uma vez por ano. O sinal e o que importa.
    if (dias == 1 || dias < -1)      off += 86400L;
    else if (dias == -1 || dias > 1) off -= 86400L;
    return off;
}

}  // namespace

namespace hora {

void begin(const AppConfig &c) {
    // `configTzTime` aceita ser chamada antes de o Wi-Fi associar: o cliente do
    // lwIP so comeca a mandar pacote quando a interface sobe, e a partir dai
    // ele tenta sozinho, com backoff, para sempre. Nao ha o que esperar aqui —
    // e por isso esta funcao nao bloqueia.
    //
    // Tres servidores porque o primeiro pode simplesmente nao resolver numa
    // rede com DNS local capenga, e a alternativa a um segundo nome e um painel
    // sem hora ate alguem reiniciar.
    configTzTime(c.tz.c_str(), c.ntp.c_str(), "pool.ntp.org", "time.google.com");
    Serial.printf("hora: SNTP armado (tz=%s, servidor=%s)\n",
                  c.tz.c_str(), c.ntp.c_str());
}

bool sincronizada() {
    if (g_ok) return true;
    g_ok = time(nullptr) >= EPOCH_MINIMO;
    if (g_ok) {
        g_offset = offsetDoFuso(time(nullptr));
        g_offsetLido = true;
        Serial.printf("hora: sincronizada por SNTP (offset %+ld s)\n", g_offset);
    }
    return g_ok;
}

void semear(long epochUtc) {
    if (epochUtc < EPOCH_MINIMO) return;   // API antiga, sem o campo
    if (sincronizada()) return;            // o SNTP chegou primeiro, e manda

    struct timeval tv { (time_t)epochUtc, 0 };
    settimeofday(&tv, nullptr);
    g_ok         = true;
    g_offset     = offsetDoFuso((time_t)epochUtc);
    g_offsetLido = true;
    Serial.printf("hora: acertada pelo carimbo da API (offset %+ld s)\n",
                  g_offset);
}

long agoraLocal() {
    if (!sincronizada()) return 0;

    // O offset e reavaliado de hora em hora, e nao lido uma vez na vida.
    //
    // Com `<-03>3` ele nunca muda — Sao Paulo nao tem horario de verao desde
    // 2019. Mas `tz` vem do cartao, e um `EST5EDT` ou `CET-1CEST` troca de
    // offset no meio do ano: lido uma vez, o painel erraria UMA HORA em silencio
    // ate alguem reiniciar. Uma conta por hora e barata demais para valer a
    // aposta de que ninguem vai mudar o campo.
    const uint32_t agoraMs = millis();
    if (!g_offsetLido || (agoraMs - g_offsetMs) >= 3600UL * 1000UL) {
        g_offset     = offsetDoFuso(time(nullptr));
        g_offsetLido = true;
        g_offsetMs   = agoraMs;
    }
    return (long)time(nullptr) + g_offset;
}

Clock daTela(const Clock &daApi) {
    if (!sincronizada()) return daApi;
    return relogioDe(agoraLocal());
}

}  // namespace hora
