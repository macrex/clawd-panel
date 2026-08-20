#include "grupos.h"
#include <algorithm>

namespace grupos {

namespace {

std::string maiusculo(const std::string &s) {
    std::string r = s;
    for (char &c : r)
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    return r;
}

std::string minusculo(const std::string &s) {
    std::string r = s;
    for (char &c : r)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return r;
}

// Um agente sem `agent` e Claude: e o que a API mandava antes de publicar o
// campo, e era assim que a lista se comportava. Perder esses agentes agora
// seria regressao.
//
// MINUSCULO na chave: quem publica o campo sao tres CLIs diferentes, e duas
// grafias do mesmo nome dariam dois grupos com o MESMO rotulo na tela — e fora
// de ordem, porque maiuscula ordena antes em ASCII.
std::string nomeDe(const Agent &a) {
    return minusculo(a.agent.empty() ? std::string("claude") : a.agent);
}

// A chave do dicionario de planos vem da mesma fonte e corre o mesmo risco de
// grafia, entao ela e normalizada aqui tambem. `agente` ja vem de `nomeDe`.
std::string planoDe(const std::vector<Plano> &planos, const std::string &agente) {
    for (const Plano &p : planos)
        if (minusculo(p.agente) == agente) return p.rotulo;
    return "";
}

}   // namespace

Lista montarLista(const std::vector<Agent> &agents,
                  const std::vector<Plano> &planos) {
    Lista lista;
    if (agents.empty()) return lista;

    // Os provedores na ordem de PRIMEIRA APARICAO, que depois vira a ordem
    // final. Um vetor e nao um set: sao tres ou quatro nomes, e a ordem
    // importa.
    std::vector<std::string> nomes;
    for (const Agent &a : agents) {
        const std::string n = nomeDe(a);
        if (std::find(nomes.begin(), nomes.end(), n) == nomes.end())
            nomes.push_back(n);
    }

    // Claude primeiro, o resto alfabetico. Sem regra fixa a ordem dos grupos
    // dancaria a cada poll, conforme quem estivesse mais urgente no instante.
    std::sort(nomes.begin(), nomes.end(), [](const std::string &a,
                                             const std::string &b) {
        if (a == "claude") return b != "claude";
        if (b == "claude") return false;
        return a < b;
    });

    const bool comCabecalho = nomes.size() > 1;
    if (!comCabecalho)
        lista.planoUnico = planoDe(planos, nomes[0]);

    for (const std::string &n : nomes) {
        int quantos = 0;
        for (const Agent &a : agents)
            if (nomeDe(a) == n) quantos++;

        if (comCabecalho) {
            Linha c;
            c.cabecalho = true;
            c.rotulo    = maiusculo(n);
            // `n` ja veio de `nomeDe`, entao esta normalizado em minusculo — e
            // e essa forma que a UI usa para achar o icone e a cor da marca.
            c.cli       = n;
            c.plano     = planoDe(planos, n);
            c.contagem  = quantos;
            lista.linhas.push_back(c);
        }
        // A ordem DENTRO do grupo e a que a lista ja tinha: urgencia, decidida
        // no servidor e preservada pela fusao. Agrupar nao reordena.
        for (size_t i = 0; i < agents.size(); i++) {
            if (nomeDe(agents[i]) != n) continue;
            Linha l;
            l.agente = (int)i;
            lista.linhas.push_back(l);
        }
    }
    return lista;
}

int cabemLinhas(const std::vector<Linha> &l, int altura,
                int hCabecalho, int hAgente, int &foraN) {
    int usado = 0, n = 0;
    for (size_t i = 0; i < l.size(); i++) {
        const int h = l[i].cabecalho ? hCabecalho : hAgente;
        if (usado + h > altura) break;
        usado += h;
        n++;
    }

    // Um cabecalho que ficou sem nenhum agente embaixo nao e desenhado: um
    // titulo sozinho parece defeito. A altura dele volta para o rodape.
    while (n > 0 && l[n - 1].cabecalho) n--;

    // So AGENTES contam: "+1" tem que significar "uma sessao que voce nao esta
    // vendo". Como o `while` acima so remove cabecalhos, e cabecalho nao entra
    // nesta conta, tanto faz contar antes ou depois dele.
    foraN = 0;
    for (size_t i = (size_t)n; i < l.size(); i++)
        if (!l[i].cabecalho) foraN++;
    return n;
}

}   // namespace grupos
