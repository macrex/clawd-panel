#pragma once
#include "status.h"
#include <cstdint>
#include <string>

std::string formatAge(int seconds);        // "agora", "ha 2s", "ha 1m", "ha 1h"
std::string pctText(const Metric &m);      // "26%" ou "-"
Level       worstLevel(const Status &s);   // pior nivel entre as metricas conhecidas
int         barWidth(int pct, int fullWidth);

// Um estado so para a tela inteira, escolhido pela urgencia: um agente
// bloqueado governa o painel mesmo que outros nove estejam trabalhando.
// Sem agente nenhum devolve Unknown — quem chama distingue "ninguem conectado"
// olhando se a lista esta vazia.
AgentState  overallState(const Status &s);

// Ninguem trabalhando: nenhuma sessao do Claude Code publicando E nenhum agente
// na lista. E a pergunta que liga a tela do Clawd DORMINDO.
//
// As tres condicoes importam, e cada uma ja custou um bug:
//   `!online`      — quem define ausencia e o servidor, e nao a placa;
//   lista vazia    — `online: false` COM agentes quer dizer ocioso ou
//                    bloqueado, e esconder esses agentes foi exatamente o bug
//                    que a marcacao veio corrigir;
//   `!doCache`     — um retrato restaurado do cartao nao guarda lista de
//                    agentes, entao ele nao tem como AFIRMAR que nao ha
//                    ninguem; ele so tem os limites que acabou de trazer.
bool        semSessao(const Status &s);

// Qual agente esta puxando o estado agregado. Vazio quando nao ha nenhum, ou
// quando o estado nao vem de um agente especifico.
std::string overallAgent(const Status &s);

// O MESMO agente, por indice, ou -1. A pagina do Clawd escreve o nome dele e
// agora tambem os numeros dele — e os dois precisam sair do mesmo lugar, senao
// a tela mostra o custo de uma sessao debaixo do nome de outra.
int overallIndex(const Status &s);

// Tempo acumulado de chamada de API, pronto para imprimir: "9h09", "13min",
// "45s". Milissegundos porque e assim que o Claude Code reporta.
//
// Nunca mostra segundos junto de horas: numa linha de rodape o que importa e a
// ordem de grandeza, e "9h09m32s" gasta o dobro do espaco para dizer o mesmo.
std::string formatApiTime(uint32_t ms);

// Duracao em segundos, curta: "45s", "12min", "3h20". Mesma familia do
// formatApiTime, mas partindo de segundos — o livro-caixa conta assim.
std::string formatDuration(int seconds);
