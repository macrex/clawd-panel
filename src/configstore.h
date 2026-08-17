#pragma once
#include "app_config.h"

// A config do cartao, guardada TAMBEM na memoria interna do chip.
//
// O painel morria quando o cartao nao montava. Nao degradava, nao avisava com o
// que sabia: parava em SEM CONFIG e ficava ali ate alguem desligar a bateria. E
// o `0x107` desta placa ja fez isso quatro vezes — o suficiente para o cartao
// deixar de ser uma dependencia aceitavel para SUBIR.
//
// A NVS resolve porque ela nao esta no cartao: sobrevive a queda de energia, ao
// FAT corrompido e ao cartao que nao responde. E o mesmo lugar onde mora o piso
// do relogio (ver src/hora.h), pela mesma razao.
//
// O que ela NAO substitui: os sprites. Sem cartao a turma, o Clawd e os fundos
// nao existem, e o painel desenha sem eles — que e exatamente o que ele ja faz
// quando um arquivo de sprite falta. O que se ganha e o painel de pe, com hora,
// limites e agentes, em vez de uma tela parada dizendo SEM CONFIG.
namespace configstore {

// Guarda a config que acabou de ser lida do cartao. Chame SO com uma config
// valida — a reserva nao pode ser pior do que o que ela substitui.
//
// Grava so quando algo mudou: a NVS tem vida finita de escrita e esta funcao
// roda em todo boot.
void guardar(const AppConfig &c);

// A config guardada, ou uma invalida quando nunca houve gravacao.
AppConfig ler();

}  // namespace configstore
