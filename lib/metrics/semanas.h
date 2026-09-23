#pragma once
#include "status.h"

// A aritmetica da pagina SEMANAS: o calendario de 5 linhas x 7 colunas que
// comeca no DOMINGO — o limite semanal zera sabado 23h, entao a linha do
// calendario e a mesma semana que o limite conta.
//
// HOJE MORA NA ULTIMA LINHA, na coluna do dia da semana dele, e o que fica a
// direita e futuro. Por isso os 35 dias da API nao cabem sempre inteiros: numa
// terca o calendario mostra 31 (quatro celulas sao futuro) e os quatro mais
// antigos ficam de fora. So num sabado os 35 aparecem.
//
// As celulas sao numeradas linha a linha: 0 e o domingo do alto, 34 o sabado
// de baixo.

// Epoch LOCAL -> dia da semana, 0 = domingo. -1 quando a placa nao sabe que
// dia e (epoch anterior a EPOCH_MINIMO, ver relogio.h).
int diaDaSemana(long epochLocal);

// Epoch LOCAL -> o dia do mes de `atras` dias antes. 0 sem hora.
int diaDoMes(long epochLocal, int atras);

// Quantos dias antes de hoje cai a celula `c`, com hoje na coluna `dow`.
// Negativo = futuro. Nunca passa de 34, entao o dia dela em `Historico::dias`
// e sempre `HISTORICO_DIAS - 1 - atras`.
int atrasDaCelula(int c, int dow);

// A soma em US$ de uma semana de domingo a sabado: 0 = a corrente (de domingo
// ate hoje), 1 = a anterior inteira.
int somaDaSemana(const Historico &h, int dow, int semanasAtras);

// O dia mais caro dos 35. E a escala da cor: o calendario compara os dias entre
// si, e nao contra um teto fixo que envelheceria com o uso.
int maiorDia(const Historico &h);

// A cor da celula, de 0 (SUBCARD) a 100 (LARANJA).
//
// Gama 0,7 e nao linear: com a escala no dia mais caro, um dia de US$ 30 num
// mes com um de US$ 300 ficaria a 10% — indistinguivel do fundo. A curva
// levanta a ponta de baixo sem mexer no topo.
int calorDoDia(int custo, int maximo);
