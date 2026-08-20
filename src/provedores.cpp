#include "provedores.h"
#include "display.h"

namespace provedores {
namespace {

// ---- Claude ----
// O mesmo desenho que o painel ja usa no cabecalho da tela (o Clawd), e nao o
// asterisco da Anthropic: quem olha este painel reconhece o caranguejo antes de
// reconhecer a marca, e ele ja estava no firmware.
const char *CLAUDE[] = {
    "...############..",
    "...##.######.##..",
    ".################",
    "...############..",
    "....#.#....#.#...",
};

// ---- Antigravity ----
// Reduzido do PNG oficial (`antigravity-icon__one-color.png`, 540x540, press
// kit do Google) por LANCZOS sobre o canal alfa. A silhueta e solida, entao
// sobrevive: corpo cheio afunilando para cima, base aberta.
const char *AGY[] = {
    "....#....",
    "...###...",
    "..#####..",
    "..#####..",
    "..#####..",
    "..#...#..",
    ".##...##.",
    "##.....##",
    "#.......#",
};

// ---- Codex ----
// NAO e o logotipo reduzido: o knot da OpenAI sao seis alcas finas
// entrelacadas, e em 1 bit elas viram ruido em qualquer tamanho que caiba aqui
// (testado de 7 a 17 px, com e sem engrossamento). Este e um desenho a mao que
// guarda o que o olho reconhece — o anel hexagonal e o vazio central. E uma
// citacao do logo, e o comentario existe para ninguem "corrigir" isto
// substituindo pela reducao automatica, que ja foi tentada e reprovada.
const char *CODEX[] = {
    "..#####..",
    ".#.....#.",
    "#..###..#",
    "#.#...#.#",
    "#.#...#.#",
    "#..###..#",
    ".#.....#.",
    "..#####..",
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
    if (agente == "claude" || agente.empty()) return {CLAUDE, 17, 5};
    if (agente == "agy")                      return {AGY,     9, 9};
    if (agente == "codex")                    return {CODEX,   9, 8};
    return {nullptr, 0, 0};
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
