#include "fusao.h"
#include <algorithm>

namespace {

// A mesma tabela `order` do servidor (build_agents): bloqueado e a informacao
// mais acionavel do painel e vai primeiro; Unknown por ultimo.
int pesoEstado(AgentState st) {
    switch (st) {
        case AgentState::Blocked: return 0;
        case AgentState::Working: return 1;
        case AgentState::Idle:    return 2;
        default:                  return 3;
    }
}

}  // namespace

void ordenarAgentes(std::vector<Agent> &agents) {
    // stable_sort de proposito: empate total mantem a ordem de chegada, que e
    // a ordem que a API de origem ja tinha escolhido.
    std::stable_sort(agents.begin(), agents.end(),
        [](const Agent &a, const Agent &b) {
            const int pa = pesoEstado(a.state), pb = pesoEstado(b.state);
            if (pa != pb) return pa < pb;
            if (a.hasContext != b.hasContext) return a.hasContext;
            return a.contextPct > b.contextPct;
        });
}

void fundirAgentes(Status &base, const Status &pc2) {
    for (const Agent &a : pc2.agents) {
        base.agents.push_back(a);
        base.agents.back().origem = 1;
    }
    ordenarAgentes(base.agents);

    if (!base.bloqueio.known && pc2.bloqueio.known) {
        base.bloqueio = pc2.bloqueio;
        base.bloqueio.origem = 1;
    }

    // A captura atravessa a fusao pela mesma regra do bloqueio: o master ganha,
    // o PC2 entra quando o master nao pediu nada. Sem esta copia, um pedido
    // feito na segunda maquina morreria aqui — todo campo de topo que a fusao
    // nao carrega e descartado com o resto do Status do PC2.
    //
    // Dois pedidos na mesma volta: atende-se o do master agora e o do PC2 no
    // proximo poll, que chega em 2 s. O prazo do pedido e de 30 s dos dois lados.
    if (!base.captura.known && pc2.captura.known) {
        base.captura = pc2.captura;
        base.captura.origem = 1;
    }

    // A atualizacao de arquivo idem: e um campo de topo, e sem a copia um
    // pedido feito no PC2 morreria na fusao.
    if (!base.atualizacao.known && pc2.atualizacao.known) {
        base.atualizacao = pc2.atualizacao;
        base.atualizacao.origem = 1;
    }
    // `sessions` NAO e somado de proposito: nenhuma tela le esse campo — a
    // contagem que aparece no card sai de `agents.size()`, que ja e a fundida.
    // Somar seria manter um numero que ninguem olha.
}

void marcarOrigem(Status &s, int origem) {
    for (Agent &a : s.agents) a.origem = origem;
    if (s.bloqueio.known) s.bloqueio.origem = origem;
    // Sem isto, a foto pedida pelo PC2 numa volta em que o master falhou seria
    // devolvida ao master — que nao a pediu e nao a espera.
    if (s.captura.known) s.captura.origem = origem;
    if (s.atualizacao.known) s.atualizacao.origem = origem;
}
