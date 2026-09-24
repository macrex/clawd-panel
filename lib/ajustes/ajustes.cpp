#include "ajustes.h"
#include <cstdlib>
#include "relogio.h"   // EPOCH_MINIMO: a mesma regra de "a placa sabe que horas sao"

namespace {

// Quase geometrica, razao ~2 (ver ajustes.h). O 255 fecha a escala: o degrau de
// cima e o maximo que o backlight da.
const uint8_t TABELA[ajustes::DEGRAUS] = {12, 25, 50, 100, 170, 255};

}  // namespace

namespace ajustes {

uint8_t brilhoDoDegrau(int d) {
    if (d < 0) d = 0;
    if (d >= DEGRAUS) d = DEGRAUS - 1;
    return TABELA[d];
}

int degrauDoBrilho(uint8_t v) {
    int melhor = 0;
    for (int d = 1; d < DEGRAUS; d++)
        if (std::abs(v - TABELA[d]) < std::abs(v - TABELA[melhor])) melhor = d;
    return melhor;
}

bool horaDaNoite(long epochLocal) {
    if (epochLocal < EPOCH_MINIMO) return false;
    const int h = (int)((epochLocal % 86400L) / 3600L);
    // A janela atravessa a meia-noite (23h-07h): o "dentro" e a UNIAO das duas
    // pontas, e nao a intersecao.
    return h >= NOITE_INICIO_H || h < NOITE_FIM_H;
}

bool telaDeNoite(bool ligado, long epochLocal, bool acordada, bool semSessao) {
    return ligado && !acordada && semSessao && horaDaNoite(epochLocal);
}

bool noPrazo(uint32_t ateMs, uint32_t agoraMs) {
    return ateMs != 0 && (int32_t)(ateMs - agoraMs) > 0;
}

uint32_t prazoVivo(uint32_t ateMs, uint32_t agoraMs) {
    return noPrazo(ateMs, agoraMs) ? ateMs : 0;
}

Aviso avisoDoPoll(int prontos, int esperas, bool somLigado, bool emSilencio) {
    if (!somLigado || emSilencio) return Aviso::Nenhum;
    if (esperas) return Aviso::Espera;
    return prontos ? Aviso::Pronto : Aviso::Nenhum;
}

int minutosAte(uint32_t ateMs, uint32_t agoraMs) {
    if (!noPrazo(ateMs, agoraMs)) return 0;
    return (int)((ateMs - agoraMs + 59999UL) / 60000UL);
}

bool sinalFraco(bool conectado, int rssi) {
    return !conectado || rssi < SINAL_FRACO_DBM;
}

std::string textoDoSinal(bool conectado, int rssi) {
    if (!conectado) return "sem Wi-Fi";
    return std::to_string(rssi) + " dBm" + (sinalFraco(true, rssi) ? " fraco" : "");
}

std::string linhaDaNoite(const Status &s) {
    int rodando = 0, esperando = 0;
    for (const Agent &a : s.agents) {
        if (a.state == AgentState::Working) rodando++;
        if (a.state == AgentState::Blocked) esperando++;
    }
    std::string r = rodando == 0 ? "nenhuma sessao rodando"
                  : rodando == 1 ? "1 sessao rodando"
                                 : std::to_string(rodando) + " sessoes rodando";
    std::string e = esperando == 0 ? "nenhuma esperando"
                                   : std::to_string(esperando) + " esperando";
    return r + "  -  " + e;
}

// ---- Geometria ----
// Deitado sao os numeros da maquete aprovada do painel. Em pe, a mesma margem
// de 14 px dentro de um painel de 300.

Alvo painel(bool retrato) {
    return retrato ? Alvo{10, 0, 300, 320} : Alvo{14, 0, 452, 212};
}

Alvo quadradoDoDegrau(int d, bool retrato) {
    return retrato ? Alvo{24 + 46 * d, 62, 38, 26} : Alvo{132 + 36 * d, 32, 30, 22};
}

Alvo botao(int id, bool retrato) {
    const int k = id - SOM;
    if (retrato) return Alvo{24 + 140 * (k % 2), 102 + 70 * (k / 2), 132, 62};
    return Alvo{28 + 144 * (k % 3), 72 + 70 * (k / 3), 136, 62};
}

int noPonto(int x, int y, bool retrato) {
    if (!dentro(painel(retrato), x, y)) return -2;
    for (int d = 0; d < DEGRAUS; d++) {
        const Alvo q = quadradoDoDegrau(d, retrato);
        if (dentro(Alvo{q.x - 4, q.y - 10, q.w + 8, q.h + 20}, x, y)) return d;
    }
    for (int id = SOM; id < SOM + BOTOES; id++)
        if (dentro(botao(id, retrato), x, y)) return id;
    return -1;
}

}  // namespace ajustes
