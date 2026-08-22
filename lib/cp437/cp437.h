#pragma once
#include <stddef.h>
#include <stdint.h>

// UTF-8 de desenho de caixa para o byte da fonte embutida.
//
// POR QUE ISTO EXISTE
// Um quinto da tela do Claude Code e caractere de desenho de caixa — medido:
// 779 de 3.891 caracteres visiveis numa tela, 688 deles so o `─`. Ate aqui a
// API os reduzia a `-`, `|` e `+` (ver server/texto.py), e o resultado era uma
// tabela feita de tracinhos onde o terminal mostra linhas continuas.
//
// A FONTE JA TEM OS GLIFOS. A embutida do Arduino_GFX e a CP437 de 256
// caracteres, e o `drawChar` dela indexa direto (`font[c * 5 + i]`), sem o
// deslocamento de compatibilidade que o Adafruit_GFX aplica. O `─` mora em
// 0xC4, o `│` em 0xB3, o `┼` em 0xC5. Nao falta arte: faltava chegar la.
//
// POR QUE NO FIRMWARE E NAO NA API
// Porque o transporte e JSON, que e UTF-8: um byte 0xC4 solto nao e UTF-8
// valido e nao sobreviveria a serializacao. A API deixa o caractere passar
// inteiro e a conversao acontece no ultimo instante, no laco de desenho.
//
// Isto NAO contradiz o `server/texto.py`, que argumenta contra decodificar
// UTF-8 no firmware: la o assunto e o texto geral (acentos, tipografia, setas),
// que continua sendo reduzido na API com uma tabela grande. Aqui sao onze
// caracteres de uma faixa contigua, resolvidos por uma comparacao.
namespace cp437 {

// Le UM caractere a partir de `s`. Devolve o byte a desenhar e escreve em
// `consumidos` quantos bytes de `s` ele gastou (1 para ASCII, 3 para o
// box-drawing UTF-8, e 1 para qualquer lixo que sobre).
//
// O que nao esta na tabela e nao e ASCII vira '?' — visivel, e nao invisivel:
// um caractere que some do meio de uma tabela desalinha a linha inteira, e o
// alinhamento por coluna e o que se veio buscar nesta tela.
uint8_t proximo(const char *s, size_t restam, int &consumidos);

}
