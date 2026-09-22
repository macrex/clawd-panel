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

// O PC2 so assume a tela inteira depois de o master ficar calado por
// RESERVA_APOS_MS. Antes disso a volta conta como falha e a placa segue com o
// ultimo dado do master, esmaecido e com a idade no rodape.
//
// Com o Wi-Fi fraco (-80 a -88 dBm) o master perde uma volta com frequencia
// (timeout de leitura de 3 s), e o fallback imediato trocava a tela pelo
// /status do PC2 nesse soluco: os cartoes do master sumiam e, com o PC2
// ocioso, `semSessao` acendia a tela de offline com um dado de 6 s.
//
// 20 s sao varias voltas seguidas perdidas, e nao um soluco. E fica abaixo dos
// 45 s da tela de servidor fora, senao a reserva nunca chegaria a aparecer.
const uint32_t RESERVA_APOS_MS = 20000;
bool usarReserva(uint32_t masterCaladoMs);

// A resposta INTEIRA veio de uma fonte so (fallback do master caido): carimba a
// origem em todos os agentes e no bloqueio.
void marcarOrigem(Status &s, int origem);
