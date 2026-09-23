#pragma once
#include <vector>
#include "status.h"

// O que ACONTECEU entre dois polls, sessao a sessao.
//
// A comemoracao da turma ja nascia desta comparacao, dentro do laco, e o som
// do painel precisa da mesma borda com mais uma pergunta: o turno acabou ou
// parou pedindo resposta? Os dois tocam coisas diferentes, e errar a borda nao
// da erro — da um apito a toa, ou um silencio quando alguem esperava por voce.
//
// So conta quem CONTINUA na lista. Uma sessao que termina e some no mesmo poll
// nao tem linha nem estado a acompanhar; e uma que aparece agora e nova, nao
// mudou — no boot a lista inteira apareceria de uma vez.
struct Eventos {
    int prontos      = 0;   // saiu de Working para parada (nao Blocked): o turno acabou
    int esperas      = 0;   // entrou em Blocked: parou pedindo resposta
    int comemoracoes = 0;   // saiu de Working, para onde for: a festa da turma
};

Eventos eventosDoPoll(const std::vector<Agent> &antes,
                      const std::vector<Agent> &agora);
