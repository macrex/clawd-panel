#pragma once
#include <string>

// O cartao, e a REGRA que passou a valer sobre ele.
//
// EM OPERACAO NORMAL, O CARTAO SO E LIDO.
//
// Ele guardava tres coisas que eram reescritas sozinhas: a config (foi para a
// NVS em src/configstore.h), o nivel a cada 60 s (src/nivel_nvs.cpp) e o
// retrato do ultimo status a cada 5 min (src/retrato.h). Hoje nenhuma escreve
// aqui. O UNICO escritor que sobrou e `gravarArquivoBaixado` (main.cpp), a
// atualizacao de sprite pelo ar, que nao tem para onde ir — sprite mora no
// cartao — mas e sob demanda e quem dispara sabe que disparou.
//
// POR QUE ELAS SAIRAM, E O QUE ISSO NAO RESOLVEU
// A intencao era prevenir o 0x107: como um cartao travado nesta placa nao volta
// por software (tres mecanismos medidos e reprovados, ver storage.cpp) e a
// JC3248W535 nao tem GPIO que corte o VDD do cartao, so restava nao expor o
// cartao a uma escrita interrompida por um reset de gravacao.
//
// MEDIDO DEPOIS, E A HIPOTESE CAIU: com o cartao recem-recuperado por corte
// fisico e ja rodando o firmware que NAO escreve nele, uma gravacao de firmware
// travou o cartao em 0x107 do mesmo jeito. A escrita periodica nao era o
// gatilho. O que trava esta em outro ponto do processo de gravacao — a suspeita
// que sobrou sao os ~20 s em que CLK/CMD/D0 ficam flutuando enquanto o chip esta
// em download mode, mas isso NAO foi testado e nao deve ser escrito aqui como se
// tivesse sido.
//
// A mudanca ficou por um ganho que ela de fato entrega e que e visivel: com o
// cartao travado, o nivel e o retrato continuam vivos e continuam sendo
// atualizados, em vez de congelarem junto com o cartao.
//
// Antes de trazer uma escrita periodica de volta para ca, releia este bloco.
namespace storage {
bool begin();

// O cartao montou? Falso ate `begin()` rodar, e falso para sempre depois de um
// `begin()` que falhou.
//
// SEM CHAMADOR HOJE, e de proposito. Quem perguntava era o rodape, para
// escrever SEM CARTAO na tela — um aviso que existia porque cartao fora
// significava sprite faltando. Com os sprites na flash (src/assets.h) isso
// deixou de ser verdade, o aviso saiu da tela (ver src/ui.cpp) e a pergunta
// ficou sem quem a fizesse.
//
// Fica porque e a unica forma de o resto do firmware saber o estado do cartao,
// e porque `g_montado` precisa existir de qualquer jeito para as guardas de
// leitura aqui dentro. Se daqui a algumas versoes ninguem tiver perguntado,
// apague sem cerimonia.
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
