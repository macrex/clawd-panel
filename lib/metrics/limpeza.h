#pragma once
#include <cstdint>

// Ate quando a placa acredita que uma limpeza ainda esta acontecendo.
//
// Quem acende e apaga o `cleaning` e a API: ela liga quando aceita o comando e
// desliga quando o contexto encolhe OU quando o proprio prazo dela estoura
// (`LIMPEZA_MAX_S = 240` em claude_metrics_api.py). O painel so obedecia.
//
// O buraco: se o servidor MORRE no meio de um compact, o ultimo status bom fica
// congelado com `cleaning` aceso — e a turma do rodape varre para sempre, sem
// nada que a desligue a nao ser um reboot. O teto do servidor existe, mas a
// placa nunca o ve.
//
// Este teto e o mesmo prazo visto do outro lado. Ele e mais FOLGADO que o do
// servidor de proposito: com a API viva quem manda e ela, e a placa nunca chega
// a opinar. Invertida, a ordem faria o painel contradizer um servidor certo.
const uint32_t LIMPEZA_TETO_MS = 300000;   // 5 min; o servidor apaga aos 4

// Quando a limpeza acendeu, em millis(). Zero = apagada.
struct LimpezaWatch {
    uint32_t desdeMs = 0;
};

// A turma deve varrer AGORA? Recebe o que a API disse (ou o que o ultimo status
// bom guarda) e devolve o que a tela faz com isso.
//
// Chame a cada volta do laco, e nao so quando chega poll novo: o caso que isto
// resolve e justamente o poll que NAO chega mais.
bool varrendo(LimpezaWatch &w, bool cleaningDaApi, uint32_t agoraMs);
