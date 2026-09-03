#include "sprite.h"

namespace {

const size_t HEADER = 4 + 16;      // "CLWD" + oito campos de 16 bits

uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Em quantos quadros cada pixel aparece opaco. Base das duas funcoes que
// precisam separar o personagem do enfeite transitorio.
bool contarOpacos(const Sprite &s, uint8_t *scratch, size_t scratchLen) {
    const int    w  = s.width;
    const int    h  = s.height;
    const size_t px = (size_t)w * h;
    if (scratchLen < px) return false;

    for (size_t i = 0; i < px; i++) scratch[i] = 0;

    for (int f = 0; f < (int)s.frames; f++) {
        const uint32_t first = s.offsets[f];
        const uint32_t last  = s.offsets[f + 1];
        if (last > s.runCount || first > last) return false;

        int u = 0, v = 0;
        for (uint32_t r = first; r < last; r++) {
            const uint16_t value = s.data[r * 2];
            const uint32_t count = s.data[r * 2 + 1];
            const bool     opaco = (value != s.key);
            for (uint32_t k = 0; k < count; k++) {
                if (v >= h) break;
                // Satura em 255: uma animacao com mais de 255 quadros contaria
                // errado, e o menor limiar util ja e bem abaixo disso.
                if (opaco && scratch[(size_t)v * w + u] < 255) scratch[(size_t)v * w + u]++;
                if (++u == w) { u = 0; v++; }
            }
        }
    }
    return true;
}

// 60% dos quadros. Abaixo disso o balao de alerta entraria; muito acima, uma
// perna que se mexe sairia.
int limiarCorpo(const Sprite &s) { return (int)((s.frames * 3 + 4) / 5); }  // ceil(0.6)

}  // namespace

Sprite parseSprite(const uint8_t *blob, size_t len) {
    Sprite s;
    if (!blob || len < HEADER) return s;
    if (blob[0] != 'C' || blob[1] != 'L' || blob[2] != 'W' || blob[3] != 'D') return s;
    if (rd16(blob + 4) != 1) return s;          // versao desconhecida

    const uint16_t frames = rd16(blob + 6);
    const uint16_t w      = rd16(blob + 8);
    const uint16_t h      = rd16(blob + 10);
    const uint16_t scale  = rd16(blob + 16);
    if (!frames || !w || !h || !scale) return s;

    // Tabela de deslocamentos + pelo menos as corridas que ela promete.
    const size_t tableBytes = (size_t)(frames + 1) * 4;
    if (len < HEADER + tableBytes) return s;

    const uint8_t *table = blob + HEADER;
    const uint32_t last  = rd32(table + (size_t)frames * 4);
    // Um arquivo cortado pela metade tem que ser recusado inteiro. Desenhar
    // "o que deu" viraria lixo na tela sem nenhuma pista da causa.
    if (len < HEADER + tableBytes + (size_t)last * 4) return s;

    s.valid    = true;
    s.frames   = frames;
    s.width    = w;
    s.height   = h;
    s.key      = rd16(blob + 12);
    s.frameMs  = rd16(blob + 14);
    s.scale    = scale;
    s.offsets  = reinterpret_cast<const uint32_t *>(table);
    s.data     = reinterpret_cast<const uint16_t *>(table + tableBytes);
    s.runCount = last;
    return s;
}

int spriteBufW(const Sprite &s) { return s.width * s.scale; }
int spriteBufH(const Sprite &s) { return s.height * s.scale; }

size_t spriteBufPixels(const Sprite &s) {
    return (size_t)spriteBufW(s) * (size_t)spriteBufH(s);
}

bool spriteBodyAnchor(const Sprite &s, uint8_t *scratch, size_t scratchLen,
                      int &centerX, int &bottom) {
    if (!s.valid || !scratch) return false;
    if (!contarOpacos(s, scratch, scratchLen)) return false;

    const int w = s.width;
    const int h = s.height;

    const int limiar = limiarCorpo(s);
    int x0 = w, x1 = -1, y1 = -1;
    for (int v = 0; v < h; v++)
        for (int u = 0; u < w; u++)
            if (scratch[(size_t)v * w + u] >= limiar) {
                if (u < x0) x0 = u;
                if (u > x1) x1 = u;
                if (v > y1) y1 = v;
            }

    if (x1 < 0) {                      // nenhum pixel estavel: cai no quadro
        centerX = w / 2;
        bottom  = h;
        return true;
    }
    centerX = (x0 + x1 + 1) / 2;
    bottom  = y1 + 1;
    return true;
}

bool spriteBodyBox(const Sprite &s, uint8_t *scratch, size_t scratchLen,
                   SpriteBox &box) {
    if (!s.valid || !scratch) return false;
    if (!contarOpacos(s, scratch, scratchLen)) return false;

    const int w = s.width;
    const int h = s.height;
    const int limiar = limiarCorpo(s);

    // Maior faixa CONTIGUA de linhas que tem algum pixel estavel. E aqui que o
    // enfeite cai fora: ele e uma ilha de poucas linhas, separada do
    // personagem por um vao de linhas totalmente vazias.
    int melhorIni = -1, melhorLen = 0, ini = -1;
    for (int v = 0; v <= h; v++) {
        bool cheia = false;
        if (v < h)
            for (int u = 0; u < w; u++)
                if (scratch[(size_t)v * w + u] >= limiar) { cheia = true; break; }

        if (cheia && ini < 0) ini = v;
        if (!cheia && ini >= 0) {
            if (v - ini > melhorLen) { melhorIni = ini; melhorLen = v - ini; }
            ini = -1;
        }
    }

    if (melhorLen <= 0) {           // nenhum pixel estavel: cai no quadro inteiro
        box.x = 0; box.y = 0; box.w = w; box.h = h;
        return true;
    }

    int x0 = w, x1 = -1;
    for (int v = melhorIni; v < melhorIni + melhorLen; v++)
        for (int u = 0; u < w; u++)
            if (scratch[(size_t)v * w + u] >= limiar) {
                if (u < x0) x0 = u;
                if (u > x1) x1 = u;
            }

    box.x = x0;
    box.y = melhorIni;
    box.w = x1 - x0 + 1;
    box.h = melhorLen;
    return true;
}

bool spriteUnionBox(const Sprite &s, SpriteBox &box) {
    if (!s.valid) return false;

    const int w = s.width, h = s.height;
    int x0 = w, y0 = h, x1 = -1, y1 = -1;

    for (int f = 0; f < (int)s.frames; f++) {
        const uint32_t first = s.offsets[f], last = s.offsets[f + 1];
        if (last > s.runCount || first > last) return false;

        int u = 0, v = 0;
        for (uint32_t r = first; r < last; r++) {
            const uint16_t value = s.data[r * 2];
            const uint32_t count = s.data[r * 2 + 1];

            if (value == s.key) {              // transparente: so avanca
                for (uint32_t k = 0; k < count && v < h; k++)
                    if (++u == w) { u = 0; v++; }
                continue;
            }

            for (uint32_t k = 0; k < count; k++) {
                if (v >= h) break;
                if (u < x0) x0 = u;
                if (u > x1) x1 = u;
                if (v < y0) y0 = v;
                if (v > y1) y1 = v;
                if (++u == w) { u = 0; v++; }
            }
        }
    }

    if (x1 < 0) {                              // animacao inteiramente vazia
        box.x = 0; box.y = 0; box.w = w; box.h = h;
        return true;
    }
    box.x = x0;
    box.y = y0;
    box.w = x1 - x0 + 1;
    box.h = y1 - y0 + 1;
    return true;
}

uint32_t spriteRearme(uint32_t ultimo, uint32_t agora, uint16_t frameMs) {
    if (!frameMs) return agora;
    // Subtracao em aritmetica de 32 bits sem sinal: ela sobrevive a virada do
    // millis(), e e por isso que a comparacao e sobre a DIFERENCA e nunca sobre
    // os dois instantes.
    const uint32_t atraso = agora - ultimo;
    if (atraso > (uint32_t)frameMs * 2) return agora;
    return ultimo + frameMs;
}

bool coalescerEnvio(bool algumAvancou, uint32_t nowMs, uint32_t envioMinMs,
                    bool &pendente, uint32_t &ultimoEnvio) {
    if (algumAvancou) pendente = true;
    if (!pendente || (nowMs - ultimoEnvio) < envioMinMs) return false;
    pendente    = false;
    ultimoEnvio = nowMs;
    return true;
}

int spriteBestFrame(const Sprite &s) {
    if (!s.valid) return 0;
    int melhor = 0;
    uint32_t maisOpacos = 0;
    for (int f = 0; f < (int)s.frames; f++) {
        const uint32_t first = s.offsets[f], last = s.offsets[f + 1];
        if (last > s.runCount || first > last) continue;
        uint32_t opacos = 0;
        for (uint32_t r = first; r < last; r++)
            if (s.data[r * 2] != s.key) opacos += s.data[r * 2 + 1];
        if (opacos > maisOpacos) { maisOpacos = opacos; melhor = f; }
    }
    return melhor;
}

int spriteDownW(const Sprite &s, int div) {
    return (div > 0 && s.width  >= div) ? s.width  / div : 1;
}
int spriteDownH(const Sprite &s, int div) {
    return (div > 0 && s.height >= div) ? s.height / div : 1;
}
size_t spriteDownPixels(const Sprite &s, int div) {
    return (size_t)spriteDownW(s, div) * (size_t)spriteDownH(s, div);
}

bool decodeFrameDown(const Sprite &s, int frame, int div, uint16_t *out,
                     size_t outPixels, uint16_t bg) {
    if (!s.valid || !out || div < 1) return false;
    if (frame < 0 || frame >= (int)s.frames) return false;

    const int ow = spriteDownW(s, div);
    const int oh = spriteDownH(s, div);
    if (outPixels < (size_t)ow * oh) return false;

    for (size_t i = 0; i < outPixels; i++) out[i] = bg;

    const uint32_t first = s.offsets[frame], last = s.offsets[frame + 1];
    if (last > s.runCount || first > last) return false;

    const int w = s.width, h = s.height;
    int u = 0, v = 0;
    for (uint32_t r = first; r < last; r++) {
        const uint16_t value = s.data[r * 2];
        const uint32_t count = s.data[r * 2 + 1];
        for (uint32_t k = 0; k < count; k++) {
            if (v >= h) return true;
            // Amostra so nos multiplos de `div`: escrever todos faria o ultimo
            // pixel de cada bloco vencer, em vez do representante do bloco.
            if (value != s.key && (u % div) == 0 && (v % div) == 0) {
                const int ox = u / div, oy = v / div;
                if (ox < ow && oy < oh) out[(size_t)oy * ow + ox] = value;
            }
            if (++u == w) { u = 0; v++; }
        }
    }
    return true;
}

int boxDownW(const SpriteBox &box, int div) {
    return (div > 0 && box.w >= div) ? box.w / div : 1;
}
int boxDownH(const SpriteBox &box, int div) {
    return (div > 0 && box.h >= div) ? box.h / div : 1;
}

bool decodeCropDown(const Sprite &s, int frame, const SpriteBox &box, int div,
                    uint16_t *out, size_t outPixels, uint16_t bg) {
    if (!s.valid || !out || div < 1 || box.w <= 0 || box.h <= 0) return false;
    if (frame < 0 || frame >= (int)s.frames) return false;

    const int ow = boxDownW(box, div);
    const int oh = boxDownH(box, div);
    if (outPixels < (size_t)ow * oh) return false;

    for (size_t i = 0; i < outPixels; i++) out[i] = bg;

    const uint32_t first = s.offsets[frame], last = s.offsets[frame + 1];
    if (last > s.runCount || first > last) return false;

    const int w = s.width, h = s.height;
    int u = 0, v = 0;
    for (uint32_t r = first; r < last; r++) {
        const uint16_t value = s.data[r * 2];
        const uint32_t count = s.data[r * 2 + 1];
        for (uint32_t k = 0; k < count; k++) {
            if (v >= h) return true;
            if (value != s.key) {
                // Coordenada relativa ao recorte, e so depois a amostragem —
                // amostrar antes de recortar deslocaria a grade e escolheria
                // outros pixels representantes.
                const int cu = u - box.x, cv = v - box.y;
                if (cu >= 0 && cv >= 0 && cu < box.w && cv < box.h &&
                    (cu % div) == 0 && (cv % div) == 0) {
                    const int ox = cu / div, oy = cv / div;
                    if (ox < ow && oy < oh) out[(size_t)oy * ow + ox] = value;
                }
            }
            if (++u == w) { u = 0; v++; }
        }
    }
    return true;
}

bool decodeFullOpaque(const Sprite &s, int frame, uint16_t *out,
                      size_t outPixels, uint16_t bg) {
    if (!s.valid || !out) return false;
    if (frame < 0 || frame >= (int)s.frames) return false;

    const size_t total = (size_t)s.width * s.height;
    if (outPixels < total) return false;

    const uint32_t first = s.offsets[frame], last = s.offsets[frame + 1];
    if (last > s.runCount || first > last) return false;

    uint16_t *p = out;
    uint16_t *const fim = out + total;
    for (uint32_t r = first; r < last && p < fim; r++) {
        const uint16_t value = s.data[r * 2];
        uint32_t count = s.data[r * 2 + 1];
        // Corrida que passa do fim do quadro: o arquivo esta mentindo sobre o
        // proprio tamanho. Escrever o que cabe e parar e melhor do que confiar
        // no cabecalho e passar por cima do buffer.
        if ((size_t)(fim - p) < count) count = (uint32_t)(fim - p);
        const uint16_t cor = (value == s.key) ? bg : value;
        for (uint32_t k = 0; k < count; k++) *p++ = cor;
    }
    // Quadro que nao cobriu o buffer inteiro — pelo mesmo motivo acima. O resto
    // fica no fundo, e nao no que sobrou do quadro anterior.
    while (p < fim) *p++ = bg;
    return true;
}

bool decodeFrame(const Sprite &s, int frame, uint16_t *out, size_t outPixels,
                 uint16_t bg) {
    if (!s.valid || !out) return false;
    if (frame < 0 || frame >= (int)s.frames) return false;
    if (outPixels < spriteBufPixels(s)) return false;

    const int    w   = s.width;
    const int    h   = s.height;
    const int    sc  = s.scale;
    const int    bw  = w * sc;              // largura do buffer, em desenho
    const size_t px  = (size_t)w * h;       // pixels de um frame, em nativo

    for (size_t i = 0; i < outPixels; i++) out[i] = bg;

    const uint32_t first = s.offsets[frame];
    const uint32_t last  = s.offsets[frame + 1];
    if (last > s.runCount || first > last) return false;

    // u,v andam junto com a posicao linear em vez de sair de uma divisao por
    // pixel: este laco roda ate 25 mil vezes por frame na placa.
    int u = 0, v = 0;
    for (uint32_t r = first; r < last; r++) {
        const uint16_t value = s.data[r * 2];
        uint32_t       count = s.data[r * 2 + 1];

        if (value == s.key) {               // transparente: ja e o fundo
            for (uint32_t k = 0; k < count && v < h; k++)
                if (++u == w) { u = 0; v++; }
            continue;
        }

        for (uint32_t k = 0; k < count; k++) {
            // Arquivo corrompido pode prometer mais pixels do que o frame tem.
            // Para na borda em vez de escrever fora do buffer de quem chamou.
            if (v >= h) return true;

            const int row0 = v * sc;
            const int col0 = u * sc;
            for (int dy = 0; dy < sc; dy++) {
                uint16_t *line = out + (size_t)(row0 + dy) * bw;
                for (int dx = 0; dx < sc; dx++) line[col0 + dx] = value;
            }

            if (++u == w) { u = 0; v++; }
        }
    }
    return true;
}
