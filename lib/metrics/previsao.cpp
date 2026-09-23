#include "previsao.h"
#include <cstdio>
#include "relogio.h"

Previsao preverJanela(const Metric &m, long janelaSeg) {
    Previsao p;
    if (!m.known || m.memoria || janelaSeg <= 0 || m.resetsIn <= 0) return p;
    if (m.pct <= 0 || m.pct >= 100) return p;

    const long correu = janelaSeg - m.resetsIn;
    if (correu * 100 < janelaSeg * PREVISAO_BASE_PCT) return p;

    // No ritmo de agora a janela inteira gastaria pct * janela / correu.
    const long final = (long)m.pct * janelaSeg / correu;
    if (final < 100) {
        p.tipo     = Previsao::Fecha;
        p.pctFinal = (int)final;
        return p;
    }
    // O que falta ate 100 na mesma velocidade. Cai sempre antes da virada:
    // `final >= 100` e exatamente a condicao de isto ser <= resetsIn.
    p.tipo   = Previsao::Estoura;
    p.seg100 = (100L - m.pct) * correu / m.pct;
    return p;
}

std::string textoDaPrevisao(const Metric &m, long janelaSeg, const Clock &rel,
                            bool &perigo) {
    const Previsao p = preverJanela(m, janelaSeg);
    perigo = p.tipo == Previsao::Estoura;
    if (p.tipo == Previsao::Fecha) {
        char buf[16];
        snprintf(buf, sizeof(buf), "fecha %d%%", p.pctFinal);
        return buf;
    }
    if (!perigo) return "";

    std::string t = "100%";
    if (janelaSeg > 24L * 3600) {
        const std::string dia = diaDaquiA(rel, p.seg100);
        if (!dia.empty()) t += " " + dia;
    }
    const std::string hora = horaDaquiA(rel, p.seg100);
    if (!hora.empty()) t += " " + hora;
    return t;
}
