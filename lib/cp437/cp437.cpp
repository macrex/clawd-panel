#include "cp437.h"

namespace {

// A faixa U+2500..U+259F (box drawing + block elements) inteira cabe em tres
// bytes UTF-8 que comecam com E2 94 ou E2 95 ou E2 96. Guardar o code point e
// comparar contra a tabela e mais barato — e mais legivel — do que casar bytes.
uint32_t codePoint3(const char *s) {
    return ((uint32_t)(uint8_t)s[0] & 0x0F) << 12 |
           ((uint32_t)(uint8_t)s[1] & 0x3F) << 6 |
           ((uint32_t)(uint8_t)s[2] & 0x3F);
}

struct Par { uint16_t uni; uint8_t cp; };

// So o que o Claude Code de fato desenha. A lista saiu de contar os caracteres
// nao-ASCII de uma tela real; o resto da faixa nao entra porque a CP437 nao tem
// equivalente e uma tabela grande de nada gastaria flash.
//
// OS CANTOS ARREDONDADOS (╭╮╯╰) VIRAM OS RETOS. O Claude Code usa os
// arredondados nas molduras dele e a CP437 nao os tem: o reto e o mesmo desenho
// com o canto vivo, e e melhor que um `?` no canto de toda caixa.
const Par TABELA[] = {
    {0x2500, 0xC4},   // ─  horizontal          — 688 de 779 nao-ASCII medidos
    {0x2501, 0xCD},   // ━  horizontal grossa   -> a dupla, que e o que ha
    {0x2502, 0xB3},   // │  vertical
    {0x2503, 0xBA},   // ┃  vertical grossa
    {0x250C, 0xDA},   // ┌  canto superior esquerdo
    {0x2510, 0xBF},   // ┐  canto superior direito
    {0x2514, 0xC0},   // └  canto inferior esquerdo
    {0x2518, 0xD9},   // ┘  canto inferior direito
    {0x251C, 0xC3},   // ├  T para a direita
    {0x2524, 0xB4},   // ┤  T para a esquerda
    {0x252C, 0xC2},   // ┬  T para baixo
    {0x2534, 0xC1},   // ┴  T para cima
    {0x253C, 0xC5},   // ┼  cruzamento
    {0x2550, 0xCD},   // ═  horizontal dupla
    {0x2551, 0xBA},   // ║  vertical dupla
    {0x256D, 0xDA},   // ╭  canto arredondado -> reto
    {0x256E, 0xBF},   // ╮
    {0x256F, 0xD9},   // ╯
    {0x2570, 0xC0},   // ╰
    {0x2580, 0xDF},   // ▀  meio bloco de cima
    {0x2584, 0xDC},   // ▄  meio bloco de baixo
    {0x2588, 0xDB},   // █  bloco cheio
    {0x258C, 0xDD},   // ▌  meio bloco esquerdo
    {0x2590, 0xDE},   // ▐  meio bloco direito
    {0x2591, 0xB0},   // ░  sombra clara
    {0x2592, 0xB1},   // ▒  sombra media
    {0x2593, 0xB2},   // ▓  sombra escura
    {0x25CF, 0x07},   // ●  bolinha -> o bullet da CP437
    {0x25AA, 0xFE},   // ▪  quadrado pequeno
    {0x2022, 0x07},   // •  bullet
    {0x2192, 0x1A},   // →  seta direita
    {0x2190, 0x1B},   // ←  seta esquerda
    {0x2191, 0x18},   // ↑
    {0x2193, 0x19},   // ↓
    {0x00B7, 0xFA},   // ·  ponto do meio
};
const int N = (int)(sizeof(TABELA) / sizeof(TABELA[0]));

}   // namespace

namespace cp437 {

uint8_t proximo(const char *s, size_t restam, int &consumidos) {
    consumidos = 1;
    if (!s || !restam) return 0;

    const uint8_t b = (uint8_t)s[0];
    if (b < 0x80) return b;                    // ASCII passa direto

    // Continuacao solta ou sequencia truncada: consome UM byte e devolve '?'.
    // Devolver zero faria o chamador parar de andar e travar o laco.
    if (b < 0xC0) return '?';

    int n = (b >= 0xF0) ? 4 : (b >= 0xE0) ? 3 : 2;
    if ((size_t)n > restam) return '?';
    consumidos = n;

    if (n == 3) {
        const uint32_t cp = codePoint3(s);
        for (int i = 0; i < N; i++)
            if (TABELA[i].uni == cp) return TABELA[i].cp;
    } else if (n == 2) {
        const uint32_t cp = ((uint32_t)(b & 0x1F) << 6) |
                            ((uint32_t)(uint8_t)s[1] & 0x3F);
        for (int i = 0; i < N; i++)
            if (TABELA[i].uni == cp) return TABELA[i].cp;
        // Latin-1 acentuado nao chega aqui — a API ja o reduziu (texto.py).
    }
    return '?';
}

}
