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

// O contador de turno: "45s", "2m34", "1h02". Difere de formatDuration porque
// um contador que ANDA precisa dos segundos ("2m34" -> "2m35"); acima de uma
// hora os minutos assumem o papel de "parte que anda". Negativo = sem dado,
// devolve vazio.
std::string formatTurno(int seconds);

// O turno extrapolado entre polls: o state_age do ultimo payload mais o tempo
// que passou desde que ele chegou. `congelado` (dado stale) devolve o valor
// cru — numero que anda sobre payload morto mentiria. -1 atravessa.
int turnoSegundos(int stateAgeS, uint32_t msDesdePoll, bool congelado);

// ---- A memoria que impede o contador de andar para tras ----
//
// A extrapolacao acima e recalculada do zero a cada payload, e os dois lados
// truncam segundos por conta propria: o servidor no state_age, a placa no
// (now - chegada). Os cortes nao se alinham, entao cada sincronismo podia
// recuar o numero exibido — na placa real o contador subia seis segundos e
// voltava cinco, num serrote continuo. O numero que o usuario ve passa por
// aqui e nunca regride dentro do mesmo (sessao, estado).
struct TurnoReg {
    std::string id;
    AgentState  st;
    int         seg;      // o turno CORRENTE, -1 quando nao ha
    int         fim;      // o ultimo turno encerrado, -1 enquanto nao houver
};

// Devolve o valor a exibir e atualiza a memoria. As regras, em ordem:
//   turno encerrado    (estado que nao cronometra) devolve o ultimo WORKING
//                      congelado — quanto durou o turno que acabou de terminar,
//                      que a UI pinta em cinza. Sem working conhecido, -1: uma
//                      sessao encontrada ja parada nao tem o que relatar, e o
//                      relogio do Blocked ("espera voce ha 3m") e outro assunto
//                      e nao vira tempo de turno;
//   seg < 0            zera o corrente e devolve -1 — sem clampar um futuro
//                      turno curto pelo passado;
//   estado trocou      comeca outro relogio (Working -> Blocked);
//   recuo pequeno      segura o maior valor ja mostrado (jitter de poll);
//   recuo grande       aceita: UserPromptSubmit no meio de um working rearma o
//                      state_ts do servidor sem trocar o estado, e ai o recuo
//                      e de minutos — verdade nova, nao ruido.
// A fronteira entre os dois recuos e TURNO_RECUO_TOL_S.
const int TURNO_RECUO_TOL_S = 10;
int turnoMonotonico(std::vector<TurnoReg> &mem, const std::string &id,
                    AgentState st, int seg);

// Tira da memoria as sessoes que sairam da lista. Sem isto o vetor cresce um
// registro por sessao aberta na vida da placa — pouco por dia, mas a placa
// fica ligada na parede por semanas.
void turnoPodar(std::vector<TurnoReg> &mem, const std::vector<Agent> &vivos);

// ---- A linha de idade do dado, no rodape ----
//
// Ela existia duas vezes, palavra por palavra, nas duas orientacoes. As duas
// copias tinham a mesma cadeia, os mesmos formatos e o mesmo prefixo — e uma
// delas tinha um ramo a mais, que e a unica diferenca de verdade.
//
// `motivo` e o que `motivoDaFalha` (falha.h) escolheu, e so aparece com
// `staleSeconds > 0`. Nulo cai em "SEM CONTATO", que e o texto de sempre.
//
// `comMasterOff` liga o terceiro caso — em contato, mas nao com quem manda. Em
// pe ele existe; deitado NAO, e nao por esquecimento: la o rodape divide a
// largura com a fileira de bichos, e o texto ja precisa encolher para nao
// escrever por cima do primeiro deles. Os dois cabecalhos mostram o selo VIA de
// qualquer jeito; o que essa linha acrescenta e HA QUANTO TEMPO.
std::string textoVetustez(const Status &s, int staleSeconds, const char *motivo,
                          bool comMasterOff);

// A MESMA linha, na forma curta, para quando a longa nao cabe.
//
// So o aviso de contato perdido chega ao tamanho que obriga a isto; a idade
// normal ("12s") nao tem como encostar em nada. Aqui o motivo se perde: com a
// largura no limite, dizer HA QUANTO TEMPO vale mais do que dizer por que.
std::string textoVetustezCurto(const Status &s, int staleSeconds);

// "sync 11:15" — a hora em que os limites que estao na tela foram lidos pela
// ultima vez. VAZIO quando eles vem de leitura viva.
//
// Ele mora no rodape, na ponta OPOSTA a da vetustez, e nao dentro da faixa do
// limite: a faixa ja tem prazo e instante da virada, um em cada ponta, e o
// carimbo ali seria a terceira informacao de tempo no mesmo lugar. No rodape
// ele fica ao lado do seu par natural — de um lado quando os LIMITES foram
// lidos, do outro quando o PAYLOAD chegou.
std::string textoSincronia(const Status &s);
