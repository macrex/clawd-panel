#include "retrato.h"
#include <Arduino.h>
#include <Preferences.h>

namespace {

// A MESMA area da config, do piso do relogio e do nivel: as quatro coisas
// guardadas aqui existem pela mesma razao — sobreviver ao cartao — e nao valem
// uma particao cada.
const char *NVS_AREA = "clawd";
const char *CHAVE    = "retrato";

// O teto de uma string na NVS e 4000 bytes. O retrato serializado desta placa
// mede algumas centenas, entao a recusa aqui seria sinal de que o /status
// cresceu muito — e a serial precisa dizer isso, porque o sintoma na tela
// (nenhum retrato no proximo boot) aparece horas depois e longe da causa.
const size_t TETO = 3900;

}

namespace retrato {

bool guardar(const std::string &json) {
    if (json.empty()) return false;
    if (json.size() > TETO) {
        Serial.printf("retrato: %u bytes nao cabem na NVS (teto %u)\n",
                      (unsigned)json.size(), (unsigned)TETO);
        return false;
    }

    Preferences p;
    if (!p.begin(NVS_AREA, false)) return false;
    const size_t gravados = p.putString(CHAVE, json.c_str());
    p.end();

    if (!gravados) {
        Serial.println("retrato: a NVS recusou a gravacao");
        return false;
    }
    return true;
}

bool ler(std::string &json) {
    Preferences p;
    if (!p.begin(NVS_AREA, true)) return false;      // somente leitura
    const String s = p.getString(CHAVE, "");
    p.end();
    if (!s.length()) return false;
    json = s.c_str();
    return true;
}

}
