#pragma once
#include <string>

namespace storage {
bool begin();

// O cartao montou? Falso ate `begin()` rodar, e falso para sempre depois de um
// `begin()` que falhou.
//
// POR QUE ISTO PRECISA SER VISIVEL
// Com a configuracao guardada na NVS, a placa SOBE sem cartao: associa o Wi-Fi,
// busca o /status e desenha o painel inteiro. O que falta e tudo o que mora no
// cartao — os sprites do rodape, do cabecalho, do clima e dos fundos. Na tela
// isso aparece como bichos que nao trocam, e na serial como uma enxurrada de
// `open(): File system is not mounted`. Sem este estado exposto, o painel
// mostrava o defeito e nao sabia dizer o nome dele.
//
// Quando ele for falso, o remedio nao e software: o cartao trava em 0x107 e so
// um corte REAL de energia o solta (USB fora E chave da bateria desligada) —
// reset por software nao serve, porque com bateria a placa nunca desliga.
bool montado();

bool readFile(const char *path, std::string &out);

// Le um arquivo inteiro para um buffer na PSRAM. Devolve nullptr se faltar
// espaco ou o arquivo nao existir; quem chama vira dono do buffer (free()).
//
// Existe separado de readFile porque um sprite tem ate 92 KB: uma std::string
// desse tamanho iria para a RAM interna, que e escassa e ja sustenta a pilha e
// o Wi-Fi. E le em blocos — byte a byte levaria segundos nesse tamanho.
uint8_t *readFileToPsram(const char *path, size_t &len);

// Grava o arquivo INTEIRO, de um jeito que queda de energia no meio nao
// corrompa o que ja estava la.
//
// Escreve num `<path>.tmp` e so entao renomeia por cima do definitivo. Sem
// isso, um corte no meio da escrita deixaria um arquivo truncado — e quem le
// trataria "existe mas nao parseia" como comeco do zero, jogando fora o
// contador inteiro em vez de perder os ultimos 60 s.
//
// Devolve false se qualquer etapa falhar; quem chama decide se tenta de novo.
bool writeFileAtomic(const char *path, const std::string &dados);

// O mesmo contrato para um buffer grande (um sprite baixado para a PSRAM):
// `.tmp` + rename, escrito em blocos de 16 KB. A std::string de writeFileAtomic
// mora na RAM interna e nao carrega 600 KB.
bool writeBufferAtomic(const char *path, const uint8_t *dados, size_t len);
}
