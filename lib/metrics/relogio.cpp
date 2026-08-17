#include "relogio.h"
#include <cstdio>
#include <ctime>

namespace {

// Sem acento e em tres letras: as duas fontes do painel cobrem 0x20..0x7E e
// nada mais, e o cabecalho reserva a largura de tres caracteres.
const char *DIAS[] = {"DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SAB"};

}  // namespace

Clock relogioDe(long epochLocal) {
    Clock c;
    if (epochLocal < EPOCH_MINIMO) return c;

    // gmtime e nao localtime: o fuso JA foi aplicado por quem chamou. Passar
    // por localtime aqui aplicaria o TZ do processo uma segunda vez, e o
    // resultado seria certo na placa (que roda em UTC) e errado no teste
    // nativo (que roda no fuso da maquina).
    const time_t t = (time_t)epochLocal;
    struct tm tm {};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif

    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    c.hm = buf;
    snprintf(buf, sizeof(buf), "%02d/%02d", tm.tm_mday, tm.tm_mon + 1);
    c.date = buf;
    c.weekday = DIAS[tm.tm_wday % 7];
    c.known = true;
    return c;
}

std::string prazoTexto(long segundos) {
    if (segundos <= 0) return "agora";
    if (segundos < 60)  return "<1m";

    char buf[16];
    if (segundos >= 86400) {
        snprintf(buf, sizeof(buf), "%ldd%02ldh",
                 segundos / 86400, (segundos % 86400) / 3600);
    } else if (segundos >= 3600) {
        snprintf(buf, sizeof(buf), "%ldh%02ldm",
                 segundos / 3600, (segundos % 3600) / 60);
    } else {
        snprintf(buf, sizeof(buf), "%ldm", segundos / 60);
    }
    return buf;
}

long prazoRestante(const Metric &m, long idadeSeg) {
    if (!m.known || m.resetsIn <= 0) return 0;
    if (idadeSeg < 0) idadeSeg = 0;
    const long resta = (long)m.resetsIn - idadeSeg;
    return resta > 0 ? resta : 0;
}

std::string prazoDaTela(const Metric &m, long idadeSeg) {
    if (!m.known || m.resetsIn <= 0) return "-";
    return prazoTexto(prazoRestante(m, idadeSeg));
}

std::string atDaTela(const Metric &m, long idadeSeg) {
    // O travessao e o "nao sei" que a propria API manda; ele nao e um instante.
    if (!m.known || m.at.empty() || m.at == "-") return "";
    if (prazoRestante(m, idadeSeg) <= 0) return "";
    return m.at;
}
