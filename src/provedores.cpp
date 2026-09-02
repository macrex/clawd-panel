#include "provedores.h"
#include "display.h"

namespace provedores {
namespace {

// A arte destes tres icones sai dos SVGs oficiais, versionados em
// `sprites/sources/marcas/`. Nenhum deles foi redesenhado a mao: os arrays
// abaixo SAO a saida de `python tools\build_icone_provedor.py`, e mexer neles
// aqui e perder a mudanca no proximo compilador que alguem rodar — a receita de
// cada marca (como recortar, em que altura, com qual reducao) mora la, porque
// as tres sao diferentes e o porque de cada diferenca esta comentado nos dois
// lugares.
//
// O que esta guardado aqui e a MASCARA, nao a cor: quem desenha mistura a cor
// da marca com o fundo na proporcao de cada byte (ver `misturaNaFaixa` em
// src/ui.cpp).

// ---- Claude ----
// O logotipo do Claude Code, e nao o asterisco da Anthropic. Ele ja nasce em
// grade: o SVG e um retangulo de 16x5 celulas, entao a reducao e EXATA e a
// mascara so tem 0 e 255 — nenhum meio-tom, nenhuma franja. Por isso ele
// continua nitido numa altura em que os outros dois precisam de alfa.
//
// POR QUE 10 LINHAS PARA 5 CELULAS
// As celulas nao sao quadradas: o SVG e 512x321, entao cada uma mede 32x64 —
// duas vezes mais alta que larga. Guardado em 16x5 o icone saia com METADE da
// altura que devia, e ao lado dos outros dois (12 px) parecia um logotipo
// menor, nao um logotipo mais largo. Cada celula ocupa 2 linhas aqui, que e a
// proporcao real, e o unico fator inteiro possivel: 5 celulas nao dividem os
// 12 px dos vizinhos, e qualquer fator quebrado traria de volta a franja que
// a grade exata evita.
const uint8_t CLAUDE[] = {
      0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
      0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
      0,   0, 255, 255,   0, 255, 255, 255, 255, 255, 255,   0, 255, 255,   0,   0,
      0,   0, 255, 255,   0, 255, 255, 255, 255, 255, 255,   0, 255, 255,   0,   0,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
      0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
      0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
      0,   0,   0, 255,   0, 255,   0,   0,   0,   0, 255,   0, 255,   0,   0,   0,
      0,   0,   0, 255,   0, 255,   0,   0,   0,   0, 255,   0, 255,   0,   0,   0,
};

// ---- Antigravity ----
// A silhueta do icone oficial: corpo cheio afunilando para cima, base abrindo
// nas duas pontas. A curva das pernas e o que o meio-tom preserva e o 1 bit
// destruia — sem ele as duas pontas viravam escada.
const uint8_t AGY[] = {
      0,   0,   2,   2,  20, 189, 247, 194,  24,   1,   2,   0,   0,
      0,   0,   3,   0, 169, 255, 250, 255, 178,   0,   2,   0,   0,
      0,   2,   0,  47, 255, 252, 254, 252, 255,  54,   0,   3,   0,
      0,   4,   0, 135, 255, 251, 255, 251, 254, 143,   0,   4,   0,
      0,   1,   1, 207, 255, 249, 251, 249, 255, 213,   3,   1,   0,
      2,   0,  35, 250, 253, 255, 255, 255, 253, 252,  42,   0,   2,
      4,   1,  99, 254, 255, 176, 120, 172, 255, 254, 109,   1,   4,
      3,   0, 176, 255, 137,   0,   0,   0, 129, 255, 185,   0,   2,
      2,  23, 246, 219,   4,   3,   6,   4,   1, 212, 249,  29,   2,
      0, 133, 255,  70,   0,   3,   0,   3,   0,  62, 255, 143,   0,
     71, 255, 141,   0,   3,   0,   0,   0,   3,   0, 134, 255,  80,
    233, 148,   0,   3,   1,   0,   0,   0,   1,   3,   0, 142, 232,
};

// ---- Codex ----
// O knot da OpenAI, do SVG oficial, tirado do quadrado branco por luminancia:
// no arquivo ele e o desenho ESCURO sobre fundo claro, e aqui e a cor da marca
// sobre o fundo do painel.
//
// Este e o icone que obrigou a troca de 1 bit por alfa. A versao anterior era
// um desenho a mao — um anel hexagonal com vazio central — porque a reducao
// automatica do knot era ilegivel em qualquer tamanho que coubesse na faixa.
// Ela era ilegivel por falta de meio-tom, nao por falta de pixels: com alfa, o
// knot inteiro se le em 12 px. O desenho a mao saiu.
//
// Duas escolhas que os outros dois nao precisaram, e que valem so aqui:
// reducao por MEDIA DE AREA e nao por LANCZOS (o ringing do LANCZOS cava um
// halo escuro colado no traco, e num desenho onde o vao entre duas alcas tem
// 1 px isso fecha o vao), e um realce de contraste de 1.6x em torno do meio
// antes de guardar (sem ele, traco e vao caem os dois na faixa dos 100 e o
// entrelacado vira malha). Ambos foram comparados a olho contra 12, 13 e 14 px
// das tres reducoes; esta e a unica combinacao em que as seis alcas se contam.
const uint8_t CODEX[] = {
      0,   0,   0,  69, 253, 252, 229,   2,   0,   0,   0,   0,
      0,   0,  34, 210,   0,   0, 181, 255, 228, 234,   0,   0,
      0, 138, 255,   0,  68, 255,  79,   0,   0,   0, 255,   0,
    138, 125, 212,   0, 195,   0,  91, 195, 239,  18, 135,  45,
    255,   0, 208,   0, 213, 170, 197,  40,   0, 192, 255,  44,
    255,   0, 207,   0, 171,   0,   0, 205, 168,   0,  74, 171,
    171,  77,   0, 168, 205,   0,   0, 171,   0, 207,   0, 255,
     44, 255, 195,   0,  40, 197, 170, 213,   0, 208,   0, 255,
     47, 133,  18, 239, 195,  91,   0, 195,   0, 212, 125, 136,
      0, 255,   0,   0,   0,  80, 255,  69,   0, 255, 138,   0,
      0,   0, 231, 228, 255, 179,   0,   0, 212,  32,   0,   0,
      0,   0,   0,   0,   3, 229, 250, 252,  66,   0,   0,   0,
};

// ---- Ollama (o reserva) ----
// A lhama do Ollama vale por QUALQUER CLI que nao seja uma das tres de cima:
// nao ha arte por marca para as dezenove que o herdr reconhece, e um grupo sem
// icone nenhum lia como defeito do painel (medido com uma sessao do qwen).
//
// Aqui o que esta guardado nao e o desenho oficial, e a SILHUETA dele. A arte
// do Ollama e line art: em 512 px o traco tem 14, ou seja um terco de pixel na
// altura de 12 em que este icone vive — some inteiro na reducao e deixa
// chuvisco. Preenchido, o mesmo desenho se le: as duas orelhas dizem bicho, e
// os olhos e o focinho vazados (as ilhas que `preencher` mantem furadas)
// dizem qual. Mesmo precedente do Antigravity, que ja entra so pela silhueta.
const uint8_t OLLAMA[] = {
      0, 156, 193,   1,   0,   1, 194, 156,   0,
      1, 247, 255,  94, 117,  95, 255, 246,   1,
      3, 253, 255, 255, 255, 255, 255, 253,   3,
     82, 253, 255, 255, 255, 255, 255, 253,  82,
    221, 255, 255, 255, 255, 255, 255, 255, 220,
    247, 218, 119, 126, 136, 125, 119, 218, 247,
    181, 255, 174, 207, 143, 207, 175, 255, 181,
    216, 255, 240, 120, 134, 120, 240, 255, 215,
    232, 255, 255, 255, 255, 255, 255, 255, 231,
    172, 255, 255, 255, 255, 255, 255, 255, 172,
    182, 255, 255, 255, 255, 255, 255, 255, 182,
    207, 255, 255, 255, 255, 255, 255, 255, 206,
};

// Laranja da Anthropic (#D97757), azul do Google (#8AB4F8) e verde da OpenAI
// (#10A37F). Sao as cores das proprias marcas, e nao a paleta do painel: e o
// que faz o olho separar os grupos sem ler nada.
const uint16_t COR_CLAUDE = RGB565(217, 119,  87);
const uint16_t COR_AGY    = RGB565(138, 180, 248);
const uint16_t COR_CODEX  = RGB565( 16, 163, 127);
const uint16_t COR_OUTRO  = RGB565(130, 140, 155);   // o MUTED do painel

}   // namespace

Icone iconeDe(const std::string &agente) {
    if (agente == "claude" || agente.empty()) return {CLAUDE, 16, 10};
    if (agente == "agy")                      return {AGY,    13, 12};
    if (agente == "codex")                    return {CODEX,  12, 12};
    // Qualquer outra CLI: a lhama, e nao o vazio. Um grupo sem icone abria a
    // linha num buraco, e quem olha nao tem como saber se aquilo e "uma CLI
    // que o painel nao conhece" ou "o painel esqueceu de desenhar".
    return {OLLAMA, 9, 12};
}

uint16_t corDe(const std::string &agente) {
    if (agente == "claude" || agente.empty()) return COR_CLAUDE;
    if (agente == "agy")                      return COR_AGY;
    if (agente == "codex")                    return COR_CODEX;
    return COR_OUTRO;
}

std::string nomeDe(const std::string &agente) {
    if (agente == "claude" || agente.empty()) return "Claude";
    // O unico lugar do projeto onde `agy` deixa de ser `agy`. O herdr chama a
    // CLI do Antigravity assim, e esse identificador viaja intacto ate aqui
    // porque e ele que casa o agente com o plano da conta. Na TELA, quem le e
    // uma pessoa, e "agy" nao diz nada a ela.
    if (agente == "agy")   return "Antigravity";
    if (agente == "codex") return "Codex";
    return agente;
}

}   // namespace provedores
