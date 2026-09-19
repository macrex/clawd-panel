#include "neturl.h"
#include <cstring>

namespace {
// O pedido de payload enxuto. Mora aqui e em server/painel.py, e os dois tem
// que casar: e um contrato de rede, nao uma constante interna.
const char *CAMPOS_PAINEL = "campos=painel";

const char *SUFIXO_LOCAL = ".local";

// Onde o host comeca e onde ele acaba nesta URL. O fim e o primeiro ':', '/'
// ou '?' depois do esquema — os tres separam o host do que vem depois, e qual
// deles aparece primeiro depende da URL ter porta, caminho, os dois ou nenhum.
void faixaDoHost(const std::string &url, size_t &ini, size_t &fim) {
    const size_t esquema = url.find("://");
    ini = (esquema == std::string::npos) ? 0 : esquema + 3;
    fim = url.size();
    for (size_t i = ini; i < url.size(); i++) {
        const char c = url[i];
        if (c == ':' || c == '/' || c == '?') { fim = i; break; }
    }
}
}  // namespace

std::string urlIrma(const std::string &base, const char *segmento) {
    const size_t esquema = base.find("://");
    const size_t inicio = (esquema == std::string::npos) ? 0 : esquema + 3;
    const size_t barra = base.find('/', inicio);
    const std::string host = (barra == std::string::npos) ? base
                                                          : base.substr(0, barra);
    return host + "/" + segmento;
}

std::string nomeLocalDaUrl(const std::string &url) {
    size_t ini = 0, fim = 0;
    faixaDoHost(url, ini, fim);
    const std::string host = url.substr(ini, fim - ini);
    const size_t suf = strlen(SUFIXO_LOCAL);
    if (host.size() <= suf ||
        host.compare(host.size() - suf, suf, SUFIXO_LOCAL) != 0)
        return std::string();
    return host.substr(0, host.size() - suf);
}

std::string urlComHost(const std::string &url, const std::string &host) {
    if (host.empty()) return url;
    size_t ini = 0, fim = 0;
    faixaDoHost(url, ini, fim);
    return url.substr(0, ini) + host + url.substr(fim);
}

std::string urlDoStatus(const std::string &base) {
    if (base.empty()) return base;
    // Ja pedido: chamar duas vezes sobre a mesma base nao pode acumular
    // parametros. Custa uma busca por volta, e a volta acontece uma vez no
    // begin — mas quem mexer nisto depois nao vai reler este comentario antes
    // de mover a chamada para dentro do laco.
    if (base.find(CAMPOS_PAINEL) != std::string::npos) return base;
    const char separador = (base.find('?') == std::string::npos) ? '?' : '&';
    return base + separador + CAMPOS_PAINEL;
}

std::string urlDoStatusComFrio(const std::string &base,
                               const std::string &resumo) {
    const std::string url = urlDoStatus(base);
    if (url.empty() || resumo.empty()) return url;
    // Sem checar duplicata como o `campos=painel` faz, e de proposito: esta
    // funcao e chamada A CADA POLL, com um resumo que muda, entao a base que ela
    // recebe nunca pode ser a saida da chamada anterior. Quem a chama monta
    // sempre a partir da URL do cartao.
    return url + "&frio=" + resumo;
}
