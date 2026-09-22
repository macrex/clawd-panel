#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

// O que a placa precisa para FALAR com a API, sem depender do Arduino.
//
// Estas tres coisas moravam dentro do `net.cpp` — quase 900 linhas que nenhum
// teste alcanca, porque tudo ali pede Wi-Fi, HTTPClient e FreeRTOS. Elas nao
// pedem nada disso: sao aritmetica e texto.
//
// Saem pelo motivo de sempre nesta casa: errar em qualquer uma NAO da erro
// visivel. Um CRC errado rejeita um sprite que estava perfeito; um escape errado
// vira um 400 silencioso e uma aprovacao que nunca aconteceu; um id repetido faz
// a placa ignorar um pedido para sempre.
//
// As URLs sao da lib irma, `neturl` — elas sairam antes, pela mesma razao.

// CRC-32 (IEEE, o mesmo do zlib do outro lado), em passos.
//
// Sem tabela: a folga de tempo e enorme (o download leva segundos) e a tabela
// custaria 1 KB de RAM para um fluxo que roda uma vez por atualizacao.
//
// Comece com `crc32Passo(0, ...)` e va encadeando o resultado — e o que permite
// conferir 600 KB que chegam em pedacos sem guardar o arquivo inteiro.
uint32_t crc32Passo(uint32_t crc, const uint8_t *dados, size_t n);

// Escapa uma string para dentro de um JSON escrito a mao.
//
// O comando nao precisa disto — sao um id hexadecimal e um nome de lista branca.
// O ROTULO da opcao precisa: e texto que veio da tela de um terminal, e
// `Yes, and don't ask again for "rm" commands` quebraria o corpo no meio.
std::string jsonEscapado(const std::string &s);

// ---- Pedidos que viajam de carona no /status ----
//
// A captura de tela e a atualizacao de arquivo chegam como um `id` dentro do
// payload, e a placa precisa saber se ja atendeu aquele.
//
// O id e guardado POR FONTE, e comparado por DESIGUALDADE. Duas regras, e as
// duas custaram investigacao: cada API tem o seu contador (o mesmo numero em
// duas maquinas sao pedidos diferentes), e ela reinicia a cada logon — "maior
// que o ultimo" congelaria o fluxo ate o contador alcancar o valor antigo.
struct IdsPorFonte {
    long ultimo[2] = {0, 0};
    bool visto[2]  = {false, false};
};

// Este pedido e novo? Quando for, marca como atendido e devolve true.
//
// Marcar AQUI, e nao depois de o trabalho dar certo, e deliberado: um envio que
// falha nao volta a ser tentado. A API deixa o pedido expirar, e uma foto que
// chega tres segundos depois descreve uma tela que ja mudou.
//
// A primeira vez que se ve uma fonte tambem conta como pedido novo: a API so
// publica o campo quando alguem pediu, entao nao ha caso em que ignorar o
// primeiro seja o certo.
//
// `origem` fora de {0, 1} cai no master — a config so tem duas fontes, e um
// numero novo vindo de uma API futura nao pode indexar fora do vetor.
bool pedidoNovo(IdsPorFonte &s, int origem, long id);

// Escreve `n` bytes em blocos de ate `bloco`, e DESISTE quando `agora()` passa
// de `fimMs`. Devolve quanto saiu; menos que `n` e falha.
//
// Existe por causa da foto da tela: o HTTPClient manda o corpo inteiro numa
// chamada so, e o NetworkClient so desiste depois de 10 s SEM progresso. Num
// Wi-Fi fraco os 300 KB saem aos pingos, cada pingo zera essa paciencia, e a
// tarefa de rede ficava presa ate o vigia de 60 s reiniciar a placa. O bloco
// pequeno limita quanto uma escrita presa passa do prazo.
//
// Para tambem quando `escrever` devolve 0 (o socket desistiu). A comparacao do
// prazo e com sinal, para atravessar a virada do millis().
using Escritor = std::function<size_t(const uint8_t *, size_t)>;
size_t escreverComPrazo(const uint8_t *buf, size_t n, size_t bloco, uint32_t fimMs,
                        const Escritor &escrever,
                        const std::function<uint32_t()> &agora);
