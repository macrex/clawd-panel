#include "relogio.h"
#include <cctype>
#include <cstdio>
#include <ctime>

namespace {

// Sem acento e em tres letras: as duas fontes do painel cobrem 0x20..0x7E e
// nada mais, e o cabecalho reserva a largura de tres caracteres.
const char *DIAS[] = {"DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SAB"};

// Por extenso, e em ASCII pelo mesmo motivo: "Sabado" com acento sairia com
// lixo no lugar dele.
const char *DIAS_EXTENSO[] = {"Domingo", "Segunda", "Terca", "Quarta",
                              "Quinta",  "Sexta",   "Sabado"};

// O minuto do dia daqui a `segundos`, contado do relogio do cabecalho e
// arredondado ao multiplo de cinco (passa de 1440 quando a conta vira o dia).
// E a conta UNICA da hora e do dia de um instante futuro: dia e hora escritos
// lado a lado tem que concordar na meia-noite.
bool minutoDaquiA(const Clock &c, long segundos, long &tot) {
    int hh = 0, mm = 0;
    if (!c.known || segundos < 0 ||
        sscanf(c.hm.c_str(), "%d:%d", &hh, &mm) != 2) return false;
    tot = (long)hh * 60 + mm + (segundos + 30) / 60;
    tot = ((tot + 2) / 5) * 5;
    return true;
}

}  // namespace

// Epoch LOCAL -> calendario. Falso quando a placa nao sabe que dia e.
//
// gmtime e nao localtime: o fuso JA foi aplicado por quem chamou. Passar por
// localtime aqui aplicaria o TZ do processo uma segunda vez, e o resultado seria
// certo na placa (que roda em UTC) e errado no teste nativo (que roda no fuso da
// maquina).
bool calendarioDe(long epochLocal, struct tm &tm) {
    if (epochLocal < EPOCH_MINIMO) return false;
    const time_t t = (time_t)epochLocal;
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    return true;
}

bool epochAceitavel(long candidato, long piso) {
    if (candidato < EPOCH_MINIMO) return false;
    if (piso > 0 && candidato < piso) return false;
    return true;
}

Clock relogioDe(long epochLocal) {
    Clock c;
    struct tm tm {};
    if (!calendarioDe(epochLocal, tm)) return c;

    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    c.hm = buf;
    snprintf(buf, sizeof(buf), "%02d/%02d", tm.tm_mday, tm.tm_mon + 1);
    c.date = buf;
    c.weekday = DIAS[tm.tm_wday % 7];
    c.known = true;
    return c;
}

std::string horaDaquiA(const Clock &c, long segundos) {
    long tot = 0;
    if (!minutoDaquiA(c, segundos, tot)) return "";
    tot %= 24 * 60;
    char buf[16];
    if (tot % 60) snprintf(buf, sizeof(buf), "%02ld:%02ldh", tot / 60, tot % 60);
    else          snprintf(buf, sizeof(buf), "%ldh", tot / 60);
    return buf;
}

std::string diaDaquiA(const Clock &c, long segundos) {
    long tot = 0;
    if (!minutoDaquiA(c, segundos, tot)) return "";
    // Pelo NOME, e nao pela posicao: a API numera a semana a partir da segunda
    // e a placa a partir do domingo, mas os dois escrevem as mesmas tres letras.
    int hoje = -1;
    for (int i = 0; i < 7; i++)
        if (c.weekday == DIAS[i]) hoje = i;
    if (hoje < 0) return "";

    std::string d = DIAS[(hoje + tot / (24 * 60)) % 7];
    for (size_t i = 1; i < d.size(); i++) d[i] = (char)tolower(d[i]);
    return d;
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

std::string instanteDe(long epochLocal) {
    struct tm tm {};
    if (!calendarioDe(epochLocal - 1, tm)) return "";

    const int h = (tm.tm_hour % 12) ? (tm.tm_hour % 12) : 12;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d%s", h, tm.tm_min,
             tm.tm_hour >= 12 ? "pm" : "am");
    return buf;
}

std::string dataDe(long epochLocal) {
    struct tm tm {};
    if (!calendarioDe(epochLocal, tm)) return "";

    char buf[32];
    snprintf(buf, sizeof(buf), "%02d/%02d/%d (%s)", tm.tm_mday, tm.tm_mon + 1,
             tm.tm_year + 1900, DIAS_EXTENSO[tm.tm_wday % 7]);
    return buf;
}

namespace {

// O epoch em que a janela vira, ou zero quando nao da para saber.
long viradaEm(const Metric &m, long idadeSeg, long agoraLocal) {
    if (agoraLocal <= 0) return 0;              // a placa ainda nao tem hora
    const long resta = prazoRestante(m, idadeSeg);
    if (resta <= 0) return 0;                   // venceu, ou nunca houve prazo
    return agoraLocal + resta;
}

}  // namespace

std::string horaDaVirada(const Metric &m, long idadeSeg, long agoraLocal) {
    const long em = viradaEm(m, idadeSeg, agoraLocal);
    return em ? instanteDe(em) : "";
}

std::string dataDaVirada(const Metric &m, long idadeSeg, long agoraLocal) {
    const long em = viradaEm(m, idadeSeg, agoraLocal);
    return em ? dataDe(em) : "";
}
