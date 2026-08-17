#pragma once

// Por que o painel esta sem dado — em ate 11 caracteres.
//
// O rodape dizia "SEM CONTATO HA 40s" para tres situacoes que pedem TRES acoes
// diferentes: o processo da API morreu, a maquina inteira esta fora, ou a API
// respondeu erro. O codigo HTTP da ultima tentativa ja distinguia as tres, e o
// firmware o guardava so para o pulso de diagnostico.
//
// Onze caracteres e o teto de largura do rodape: "SEM CONTATO", o texto mais
// longo que ja morava ali, tem exatamente onze. Rotulo maior empurra o aviso
// para a esquerda ate encostar na turma.

// Os codigos do HTTPClient do Arduino, repetidos aqui porque este arquivo e
// compilado TAMBEM no PC, onde HTTPClient.h nao existe. `src/net.cpp` tem um
// static_assert amarrando cada um ao valor de verdade — se o core mudar, a build
// da placa quebra em vez de o painel passar a mentir o motivo.
const int HTTP_RECUSADA     = -1;      // HTTPC_ERROR_CONNECTION_REFUSED
const int HTTP_PERDIDA      = -5;      // HTTPC_ERROR_CONNECTION_LOST
const int HTTP_LEITURA_LENTA = -11;    // HTTPC_ERROR_READ_TIMEOUT

// Nosso, e nao do core: o `http.begin()` recusou a URL antes de qualquer socket.
const int HTTP_URL_RUIM = -1000;

// Abaixo disto, a tentativa fracassou RAPIDO demais para ter sido espera de
// rede: alguem do outro lado respondeu, e um RST de porta fechada volta em
// milissegundos. Acima, ninguem respondeu e a espera correu ate o timeout de
// conexao — que este firmware fixa em 3 s.
//
// E daqui que sai a unica distincao que o codigo sozinho NAO da: o `-1` do
// Arduino cobre tanto "a porta esta fechada" quanto "a maquina nao existe".
//
// Medido na placa, o caminho rapido NAO acontece contra o master Windows: com a
// API derrubada o firewall descarta o SYN em silencio, e a tentativa consome os
// 3 s inteiros. Por isso a espera longa devolve o texto de sempre em vez de
// acusar a maquina — ela pode estar de pe. O caminho rapido continua valendo
// para quem responde RST, que e o caso do slave macOS.
const int LIMIAR_RESPOSTA_MS = 500;

// `duracaoMs` e quanto durou a ultima tentativa; `wifiOk` e o radio associado.
// O ponteiro devolvido e literal e vive para sempre — nao ha o que liberar.
const char *motivoDaFalha(int httpCode, int duracaoMs, bool wifiOk);
