#include "semanas.h"
#include <cmath>
#include <ctime>
#include "relogio.h"

int diaDaSemana(long epochLocal) {
    struct tm tm {};
    return calendarioDe(epochLocal, tm) ? tm.tm_wday : -1;
}

int diaDoMes(long epochLocal, int atras) {
    // Epoch local nao tem horario de verao: um dia e sempre 86400 s.
    struct tm tm {};
    return calendarioDe(epochLocal - (long)atras * 86400L, tm) ? tm.tm_mday : 0;
}

int atrasDaCelula(int c, int dow) {
    // Hoje e a celula 28 + dow: a ultima linha comeca na 28.
    return 28 + dow - c;
}

int somaDaSemana(const Historico &h, int dow, int semanasAtras) {
    if (!h.known || dow < 0 || dow > 6) return 0;
    // O domingo desta semana e `dow` dias antes de hoje.
    const int hoje    = HISTORICO_DIAS - 1;
    const int domingo = hoje - dow - 7 * semanasAtras;
    const int sabado  = semanasAtras ? domingo + 6 : hoje;
    int soma = 0;
    for (int i = domingo; i <= sabado; i++)
        if (i >= 0) soma += h.dias[i];
    return soma;
}

int maiorDia(const Historico &h) {
    int m = 0;
    for (int v : h.dias) if (v > m) m = v;
    return m;
}

int calorDoDia(int custo, int maximo) {
    if (custo <= 0 || maximo <= 0) return 0;
    if (custo >= maximo) return 100;
    return (int)(100.0 * std::pow((double)custo / maximo, 0.7));
}
