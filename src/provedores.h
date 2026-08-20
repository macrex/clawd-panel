#pragma once
#include <stdint.h>
#include <string>

// O icone e a cor de cada CLI, para o cabecalho de grupo do card de sessoes.
//
// POR QUE ICONE E NAO O NOME ESCRITO
// O rotulo textual custava 36 px ("CLAUDE") numa faixa de 12 px de altura, num
// card com 292 px de largura onde o plano tambem precisa caber. O icone faz o
// mesmo trabalho em 9 px, e o olho o reconhece sem ler.
//
// POR QUE 1 BIT E NAO ARTE COLORIDA
// A faixa tem 12 px. Um logotipo colorido nesse tamanho vira papa — medido: o
// knot da OpenAI, reduzido do SVG oficial, e ilegivel em 7, 9, 11, 13, 15 e 17
// px, com e sem engrossamento de traco. Um bitmap de 1 bit pintado na cor da
// marca carrega a identidade sem depender de detalhe que a tela nao resolve.
namespace provedores {

// Uma matriz de bits, linha por linha, '#' aceso.
struct Icone {
    const char *const *linhas;
    int                w;
    int                h;
};

// O icone de um agente do herdr ("claude", "codex", "agy"). Devolve um icone
// com `w == 0` quando o nome nao tem arte — e ai quem desenha cai no texto.
Icone iconeDe(const std::string &agente);

// A cor da marca daquele fornecedor. Cinza para quem nao tem uma.
uint16_t corDe(const std::string &agente);

// O nome de EXIBICAO. E aqui, e so aqui, que `agy` vira "Antigravity": o
// identificador do herdr atravessa o servidor e o firmware inteiros sem
// traducao — casar plano com agente depende disso —, e a unica coisa que le
// "agy" e uma pessoa olhando a tela. Quem nao tem nome proprio cai no
// identificador em maiusculas, que e o que `grupos::Linha::rotulo` ja traz.
std::string nomeDe(const std::string &agente);

}
