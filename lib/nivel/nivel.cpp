#include "nivel.h"
#include <cmath>
#include <cstring>

// So a ARITMETICA. A persistencia no cartao mora em src/nivel_sd.cpp, porque
// depende de SD_MMC e so existe na placa — esta metade roda em qualquer lugar, e
// e por isso que ela pode ser testada com `pio test -e native`.

namespace nivel {

float xp(long turnos, float custoUsd, float horasContato) {
    // Piso em zero POR CONTADOR, e nao no total: um custo negativo vindo de
    // dado corrompido nao pode apagar turnos que de fato existiram.
    const float t = turnos > 0 ? (float)turnos : 0.0f;
    const float c = custoUsd > 0 ? custoUsd : 0.0f;
    const float h = horasContato > 0 ? horasContato : 0.0f;
    return t * PESO_TURNO + c * PESO_CUSTO + h * PESO_HORA;
}

int calcular(long turnos, float custoUsd, float horasContato) {
    float x = xp(turnos, custoUsd, horasContato);
    // Sem este teto o nivel passaria de 99 e o sprite correspondente nao
    // existiria no cartao.
    if (x > XP_99) x = XP_99;

    // O epsilon nao e supersticao: com o XP EXATAMENTE no limiar de um nivel, a
    // conta em float devolve 21,99999... e o truncamento derruba para 21. Quem
    // sente isso e a barra de progresso da quarta tela, que compara `calcular`
    // com `xpDoNivel`: no instante da virada ela mostraria fracao negativa.
    //
    // 1e-3 de NIVEL vale cerca de 0,2 XP no pior ponto da curva — bem abaixo de
    // uma unidade, entao ele absorve o erro de arredondamento sem nunca
    // promover um XP que de fato ainda nao chegou la.
    const int n = (int)(1.0f + 98.0f * sqrtf(x / XP_99) + 1e-3f);
    if (n < MIN) return MIN;
    if (n > MAX) return MAX;
    return n;
}

float xpDoNivel(int n) {
    if (n <= MIN) return 0.0f;
    if (n > MAX) n = MAX;
    const float f = (float)(n - 1) / 98.0f;
    return XP_99 * f * f;
}

bool virada(Estado &e, float xpAgora, const char *dataApi, float &xpDoDia) {
    xpDoDia = 0.0f;

    // Sem data nao ha o que decidir: marcar seria chutar de que dia e a marca,
    // e um chute errado zera o contador na hora errada.
    if (!dataApi || !dataApi[0]) {
        if (e.dia[0]) {
            const float d = xpAgora - e.xpNaVirada;
            xpDoDia = d > 0 ? d : 0.0f;    // a marca antiga ainda serve
        }
        return false;
    }

    const bool primeira = (e.dia[0] == '\0');
    const bool mudou    = primeira || strncmp(e.dia, dataApi, sizeof(e.dia) - 1) != 0;

    if (mudou) {
        // Virada (ou primeira marca): o dia recomeca daqui. Nao ha como saber
        // quanto ja se ganhou antes de existir uma marca, entao o dia comeca
        // com zero em vez de um numero inventado.
        e.xpNaVirada = xpAgora;
        strncpy(e.dia, dataApi, sizeof(e.dia) - 1);
        e.dia[sizeof(e.dia) - 1] = '\0';
        return true;
    }

    // Mesmo dia: so subtrai. Negativo vira zero — se a API perder historia, "0"
    // informa mais do que "-40 XP", que nao significa nada para quem olha.
    const float d = xpAgora - e.xpNaVirada;
    xpDoDia = d > 0 ? d : 0.0f;
    return false;
}

}
