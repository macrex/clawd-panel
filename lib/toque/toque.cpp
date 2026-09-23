#include "toque.h"
#include <math.h>

namespace toque {

namespace {

// Notas na faixa de 500 Hz a 1 kHz de proposito: um alto-falante pequeno quase
// nao reproduz abaixo disso, e acima o som fica estridente.
const Nota NOTAS_PRONTO[] = {
    {784, 110},    // sol
    {0, 25},
    {1047, 190},   // do, uma quarta acima: a segunda mais longa "resolve"
};
const Nota NOTAS_ESPERA[] = {
    {587, 110},    // re, abaixo de tudo no Pronto
    {0, 70},
    {587, 110},
};

// Ataque curto: rapido o bastante para soar como um toque, longo o bastante
// para a onda nao partir de um degrau (que e o que o ouvido escuta como estalo).
const uint32_t ATAQUE = TAXA * 4 / 1000;

const float DOIS_PI = 6.2831853f;

uint32_t amostras(uint16_t ms) { return TAXA * ms / 1000; }

}

const Melodia PRONTO = {NOTAS_PRONTO, sizeof(NOTAS_PRONTO) / sizeof(NOTAS_PRONTO[0])};
const Melodia ESPERA = {NOTAS_ESPERA, sizeof(NOTAS_ESPERA) / sizeof(NOTAS_ESPERA[0])};

uint32_t duracao(const Melodia &m) {
    uint32_t n = 0;
    for (int k = 0; k < m.n; k++) n += amostras(m.notas[k].ms);
    return n;
}

int16_t amostra(const Melodia &m, uint32_t i) {
    for (int k = 0; k < m.n; k++) {
        const uint32_t len = amostras(m.notas[k].ms);
        if (i >= len) {
            i -= len;
            continue;
        }
        const uint16_t hz = m.notas[k].hz;
        if (hz == 0) return 0;

        // Decaimento quadratico ate ZERO no fim da nota, como um sino: o fim
        // chega em silencio e a proxima nota (ou o nada) nao encontra degrau.
        // Cada nota tambem parte da fase zero, entao comeca em silencio.
        const float x = (float)i / len;
        float env = (1.0f - x) * (1.0f - x);
        if (i < ATAQUE) env *= (float)i / ATAQUE;

        // env <= 1 e |sin| <= 1: o resultado nunca passa de PICO.
        return (int16_t)lroundf(PICO * env * sinf(DOIS_PI * hz * i / TAXA));
    }
    return 0;
}

}
