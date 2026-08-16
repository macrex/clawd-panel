#include "nivel.h"
#include "storage.h"
#include <cstdio>
#include <cstdlib>
#include <string>

// A metade do nivel que TOCA O CARTAO. A aritmetica mora em lib/nivel/nivel.cpp
// e roda em qualquer lugar; esta aqui depende de SD_MMC e so existe na placa.
//
// Separadas porque juntas elas travavam o teste: `pio test -e native` nao
// conseguia ligar a conta sem arrastar o cartao junto.

namespace {
const char *ARQ = "/clawd/nivel.json";

// Le um inteiro nao-negativo de um JSON simples, sem carregar um parser. O
// arquivo e escrito por este mesmo modulo e tem tres campos de formato
// conhecido — trazer o ArduinoJson para ca seria pagar caro por nada.
long campo(const std::string &s, const char *nome, long padrao) {
    const std::string chave = std::string("\"") + nome + "\":";
    const size_t p = s.find(chave);
    if (p == std::string::npos) return padrao;
    return strtol(s.c_str() + p + chave.size(), nullptr, 10);
}

float campoF(const std::string &s, const char *nome, float padrao) {
    const std::string chave = std::string("\"") + nome + "\":";
    const size_t p = s.find(chave);
    if (p == std::string::npos) return padrao;
    return strtof(s.c_str() + p + chave.size(), nullptr);
}

// Le uma string entre aspas. So serve para a data ("DD/MM"), que nao tem aspas
// nem barras invertidas dentro — por isso nao ha tratamento de escape aqui.
void campoS(const std::string &s, const char *nome, char *out, size_t cap) {
    out[0] = '\0';
    const std::string chave = std::string("\"") + nome + "\":\"";
    const size_t p = s.find(chave);
    if (p == std::string::npos) return;
    const size_t ini = p + chave.size();
    const size_t fim = s.find('"', ini);
    if (fim == std::string::npos) return;
    const size_t n = (fim - ini < cap - 1) ? fim - ini : cap - 1;
    s.copy(out, n, ini);
    out[n] = '\0';
}
}

namespace nivel {

bool carregar(Estado &e) {
    std::string s;
    if (!storage::readFile(ARQ, s)) return false;
    const long c = campo(s, "contato_s", -1);
    const long m = campo(s, "nivel_max", -1);
    if (c < 0 || m < 0) return false;          // ilegivel: trata como ausente
    e.contatoS = (uint32_t)c;
    e.nivelMax = (int)(m > MAX ? MAX : m);
    // Ausentes num arquivo gravado antes do XP do dia existir. Sem marca, o
    // primeiro calculo do dia simplesmente comeca do zero — nada quebra.
    e.xpNaVirada = campoF(s, "xp_virada", 0.0f);
    campoS(s, "dia", e.dia, sizeof(e.dia));
    return true;
}

bool salvar(const Estado &e) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"contato_s\":%lu,\"nivel_max\":%d,"
             "\"xp_virada\":%.1f,\"dia\":\"%s\",\"v\":2}",
             (unsigned long)e.contatoS, e.nivelMax, e.xpNaVirada, e.dia);
    return storage::writeFileAtomic(ARQ, std::string(buf));
}

}
