#include "limpeza.h"

bool varrendo(LimpezaWatch &w, bool cleaningDaApi, uint32_t agoraMs) {
    if (!cleaningDaApi) {
        // A API desligou: esquece o prazo. E o que faz a limpeza SEGUINTE ganhar
        // um teto proprio em vez de herdar o que ja estourou.
        w.desdeMs = 0;
        return false;
    }

    if (!w.desdeMs) {
        // Acendeu agora. `millis()` vale zero durante 1 ms a cada boot, e zero e
        // a sentinela de "apagada" — o 1 no lugar dele custa um milissegundo de
        // imprecisao e evita um estado que nao se distingue do vazio.
        w.desdeMs = agoraMs ? agoraMs : 1;
        return true;
    }

    // Sem sinal de proposito: `millis()` da a volta em 49 dias, e a subtracao
    // sem sinal atravessa a virada certa.
    return (uint32_t)(agoraMs - w.desdeMs) < LIMPEZA_TETO_MS;
}
