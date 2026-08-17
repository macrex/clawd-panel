#include "neturl.h"

namespace {
// O pedido de payload enxuto. Mora aqui e em server/painel.py, e os dois tem
// que casar: e um contrato de rede, nao uma constante interna.
const char *CAMPOS_PAINEL = "campos=painel";
}  // namespace

std::string urlIrma(const std::string &base, const char *segmento) {
    const size_t esquema = base.find("://");
    const size_t inicio = (esquema == std::string::npos) ? 0 : esquema + 3;
    const size_t barra = base.find('/', inicio);
    const std::string host = (barra == std::string::npos) ? base
                                                          : base.substr(0, barra);
    return host + "/" + segmento;
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
