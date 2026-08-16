#include "cache.h"
#include <ArduinoJson.h>

namespace {

// Sobe quando o formato mudar de um jeito que a versao antiga leria errado.
// Uma versao que nao e esta e DESCARTADA em vez de adivinhada: um retrato mal
// interpretado e pior do que um retrato ausente, porque ele aparece na tela com
// a mesma cara dos outros.
const int VERSAO = 1;

int daLevel(Level l) { return (int)l; }

Level paraLevel(int v) {
    switch (v) {
        case 1:  return Level::Yellow;
        case 2:  return Level::Red;
        default: return Level::Green;
    }
}

void gravarMetric(JsonObject o, const Metric &m, long agoraLocal) {
    o["pct"]   = m.pct;
    o["nivel"] = daLevel(m.level);
    o["sabe"]  = m.known;
    // O INSTANTE da virada, e nao quanto falta para ela. Zero quando a API nao
    // carimbou a janela — e ai o cache tambem nao carimba, em vez de inventar
    // um horario que a volta leria como "vira agora".
    o["vira"]  = m.resetsIn > 0 ? (agoraLocal + m.resetsIn) : 0;
    o["at"]    = m.at;
}

void lerMetric(JsonObjectConst o, Metric &m, long agoraLocal) {
    m.pct   = o["pct"] | 0;
    m.level = paraLevel(o["nivel"] | 0);
    m.known = o["sabe"] | false;
    m.at    = o["at"].is<const char *>() ? o["at"].as<const char *>() : "";

    const long vira = o["vira"] | 0L;
    const long resta = vira > 0 ? (vira - agoraLocal) : 0;
    m.resetsIn = resta > 0 ? (int)resta : 0;
    // `resets` fica VAZIO de proposito: o texto do prazo e reescrito a cada
    // segundo por quem desenha (ver lib/metrics/relogio.h), e gravar uma string
    // pronta aqui so criaria uma segunda verdade para o mesmo numero.
}

}  // namespace

std::string serializarCache(const Status &s, long agoraLocal) {
    if (agoraLocal <= 0) return "";

    JsonDocument doc;
    doc["v"]  = VERSAO;
    doc["em"] = agoraLocal;

    gravarMetric(doc["sessao"].to<JsonObject>(), s.session, agoraLocal);
    gravarMetric(doc["semana"].to<JsonObject>(), s.week, agoraLocal);

    if (s.works.known) {
        JsonObject w = doc["works"].to<JsonObject>();
        w["trabalhos"] = s.works.trabalhos;
        w["seconds"]   = s.works.seconds;
        w["blocked"]   = s.works.blocked;
        w["mediana"]   = s.works.mediana;
        if (s.works.hasCost) w["cost"] = s.works.costUsd;
    }

    if (s.uso.known) {
        JsonObject u = doc["uso"].to<JsonObject>();
        u["cost"] = s.uso.costUsd;
        JsonArray ms = u["modelos"].to<JsonArray>();
        for (const UsoModelo &m : s.uso.modelos) {
            JsonObject o = ms.add<JsonObject>();
            o["rotulo"] = m.rotulo;
            // Sem custo o campo simplesmente NAO existe: nulo e diferente de
            // zero, e o card desenha "-" para o modelo fora da tabela de preco.
            if (m.hasCost) o["cost"] = m.costUsd;
        }
    }

    if (s.vitalicio.known) {
        JsonObject v = doc["vitalicio"].to<JsonObject>();
        v["turnos"] = s.vitalicio.turnos;
        v["cost"]   = s.vitalicio.costUsd;
        v["desde"]  = s.vitalicio.desde;
    }

    std::string out;
    serializeJson(doc, out);
    return out;
}

bool lerCache(const char *json, long agoraLocal, Status &out, int &idadeSeg) {
    if (!json || !*json || agoraLocal <= 0) return false;

    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;
    if ((doc["v"] | 0) != VERSAO) return false;

    const long em = doc["em"] | 0L;
    if (em <= 0) return false;
    const long idade = agoraLocal - em;
    // Retrato do futuro: o relogio da placa voltou atras, ou o cartao veio de
    // outra placa. Uma idade negativa ESTICARIA os prazos em vez de encolhe-los.
    if (idade < 0) return false;

    Status s;
    s.valid   = true;
    // A marca de procedencia. `online` falso com a lista vazia e o mesmo estado
    // que a API publica quando nao ha sessao nenhuma, e os dois recados sao
    // opostos: um afirma sobre o presente, o outro admite nao saber dele. Quem
    // desenha precisa distinguir (ver ui.cpp).
    s.doCache = true;
    // `online` fica falso e a lista de agentes vazia porque isto e um retrato do
    // passado. O que existia naquele instante nao existe agora, e o painel nao
    // pode afirmar o contrario.
    lerMetric(doc["sessao"], s.session, agoraLocal);
    lerMetric(doc["semana"], s.week, agoraLocal);

    JsonObjectConst w = doc["works"];
    if (!w.isNull()) {
        s.works.known     = true;
        s.works.trabalhos = w["trabalhos"] | 0;
        s.works.seconds   = w["seconds"] | 0;
        s.works.blocked   = w["blocked"] | 0;
        s.works.mediana   = w["mediana"] | 0;
        s.works.hasCost   = w["cost"].is<float>();
        s.works.costUsd   = s.works.hasCost ? w["cost"].as<float>() : 0;
    }

    JsonObjectConst u = doc["uso"];
    if (!u.isNull()) {
        s.uso.known   = true;
        s.uso.costUsd = u["cost"] | 0.0f;
        for (JsonVariantConst m : u["modelos"].as<JsonArrayConst>()) {
            UsoModelo um;
            um.rotulo  = m["rotulo"].is<const char *>()
                             ? m["rotulo"].as<const char *>() : "?";
            um.hasCost = m["cost"].is<float>();
            um.costUsd = um.hasCost ? m["cost"].as<float>() : 0;
            s.uso.modelos.push_back(um);
        }
    }

    JsonObjectConst v = doc["vitalicio"];
    if (!v.isNull()) {
        s.vitalicio.known   = true;
        s.vitalicio.turnos  = v["turnos"] | 0L;
        s.vitalicio.costUsd = v["cost"] | 0.0f;
        s.vitalicio.desde   = v["desde"].is<const char *>()
                                  ? v["desde"].as<const char *>() : "";
    }

    out = s;
    idadeSeg = (int)idade;
    return true;
}
