#pragma once
#include "status.h"

// Ordena como a API ordena: quem precisa de voce primeiro (blocked), depois
// working, depois o resto; no mesmo estado, maior contexto primeiro e quem nao
// reporta contexto por ultimo. A regra morava SO no servidor ("o firmware nao
// reordena") — com duas fontes isso acabou: concatenar duas listas ordenadas
// nao da uma lista ordenada, e o merge tem que morar em quem ve as duas.
void ordenarAgentes(std::vector<Agent> &agents);

// Funde o /status do PC2 no do master, in-place na base:
//  - agentes do PC2 entram com origem=1 e a lista e reordenada pela regra acima
//  - bloqueio: o do master tem prioridade; sem bloqueio la, vale o do PC2
// O resto do Status (cabecalho, relogio, limites, works) fica o da base: sao
// da CONTA ou do MUNDO, e o master e quem manda neles enquanto responde.
void fundirAgentes(Status &base, const Status &pc2);

// A resposta INTEIRA veio de uma fonte so (fallback do master caido): carimba a
// origem em todos os agentes e no bloqueio.
void marcarOrigem(Status &s, int origem);
