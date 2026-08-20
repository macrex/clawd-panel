#include "nivel.h"
#include "storage.h"
#include <Arduino.h>
#include <Preferences.h>
#include <cstdio>
#include <cstdlib>
#include <string>

// A metade do nivel que PERSISTE. A aritmetica mora em lib/nivel/nivel.cpp e
// roda em qualquer lugar; esta aqui depende do chip e so existe na placa.
//
// Separadas porque juntas elas travavam o teste: `pio test -e native` nao
// conseguia ligar a conta sem arrastar a persistencia junto.
//
// POR QUE ISTO SAIU DO CARTAO (o arquivo se chamava nivel_sd.cpp)
// O cartao desta placa trava em 0x107 e, quando trava, so volta com corte
// FISICO de energia. A aposta era que a escrita a cada 60 SEGUNDOS que este
// arquivo fazia fosse o gatilho — uma gravacao de firmware reseta o ESP32 no
// instante em que o esptool quiser, e quem escreve pode ser interrompido no
// meio. A aposta foi medida depois e PERDEU: o cartao travou numa gravacao com
// este firmware, ja sem escrever nele (ver src/storage.h).
//
// O que sobrou e o ganho que nao dependia da aposta, e ele e real: aqui o
// convivio sobrevive ao cartao. Antes, cartao travado significava um contador
// que parava de ser gravado e voltava ao valor do ultimo boot bom; agora ele
// continua contando com o cartao fora, que e o que este numero precisa fazer —
// ele e o unico dado do painel que nao se recupera sozinho.
namespace {

// A MESMA area da config e do piso do relogio. Ver src/configstore.cpp.
const char *NVS_AREA = "clawd";

// O arquivo antigo, no cartao. Existe SO para a migracao logo abaixo.
const char *ARQ_ANTIGO = "/clawd/nivel.json";

// Le um inteiro nao-negativo de um JSON simples, sem carregar um parser. Estas
// tres funcoes sobraram do formato antigo e servem apenas para ler UMA vez o
// arquivo que ficou no cartao; nada novo e escrito nesse formato.
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

// O nivel guardado no cartao pela versao anterior, se ainda estiver la.
//
// Isto roda no maximo uma vez na vida de cada placa, e existe por um motivo
// concreto: no momento da mudanca o cartao desta placa tinha 186 HORAS de
// convivio acumulado. Comecar do zero seria jogar fora o unico dado do painel
// que nao se recupera sozinho — os outros a API remanda no proximo poll, este
// so existe aqui.
bool migrarDoCartao(nivel::Estado &e) {
    std::string s;
    if (!storage::readFile(ARQ_ANTIGO, s)) return false;
    const long c = campo(s, "contato_s", -1);
    const long m = campo(s, "nivel_max", -1);
    if (c < 0 || m < 0) return false;
    e.contatoS   = (uint32_t)c;
    e.nivelMax   = (int)(m > nivel::MAX ? nivel::MAX : m);
    e.xpNaVirada = campoF(s, "xp_virada", 0.0f);
    campoS(s, "dia", e.dia, sizeof(e.dia));
    return true;
}

}

namespace nivel {

bool carregar(Estado &e) {
    Preferences p;
    if (!p.begin(NVS_AREA, true)) return false;      // somente leitura
    // `contato` ausente e a unica prova de que nunca houve gravacao: o nivel
    // maximo pode ser zero legitimamente (placa nova), o convivio tambem, mas
    // a CHAVE so existe depois de um `salvar`.
    const bool temNvs = p.isKey("nvl_contato");
    if (temNvs) {
        e.contatoS   = p.getUInt ("nvl_contato", 0);
        e.nivelMax   = p.getInt  ("nvl_max", 0);
        e.xpNaVirada = p.getFloat("nvl_xpvirada", 0.0f);
        const String dia = p.getString("nvl_dia", "");
        snprintf(e.dia, sizeof(e.dia), "%s", dia.c_str());
    }
    p.end();
    if (temNvs) {
        if (e.nivelMax > MAX) e.nivelMax = MAX;
        return true;
    }

    // Nada na NVS: ou e placa nova, ou e a primeira vez que este firmware roda
    // numa placa que ja tinha o arquivo no cartao. Sem cartao montado a
    // migracao simplesmente nao encontra nada e o nivel comeca do zero, que e o
    // mesmo que acontecia antes quando o arquivo faltava.
    if (!migrarDoCartao(e)) return false;
    Serial.printf("nivel: migrado do cartao para a NVS (%lus de convivio)\n",
                  (unsigned long)e.contatoS);
    salvar(e);
    return true;
}

bool salvar(const Estado &e) {
    Preferences p;
    if (!p.begin(NVS_AREA, false)) return false;
    // Sem comparar antes: `nvs_set_*` ja nao regrava valor igual, e o unico
    // campo que muda de verdade a cada gravacao e o convivio — comparar os
    // quatro para poupar um deles seria trabalho puro. O custo real e uma
    // entrada de 32 bytes por minuto, que a NVS absorve por decadas.
    p.putUInt  ("nvl_contato",  e.contatoS);
    p.putInt   ("nvl_max",      e.nivelMax);
    p.putFloat ("nvl_xpvirada", e.xpNaVirada);
    p.putString("nvl_dia",      e.dia);
    p.end();
    return true;
}

}
