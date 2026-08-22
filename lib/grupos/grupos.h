#pragma once
#include <string>
#include <vector>
#include "status.h"

// A lista de sessoes agrupada por CLI, pronta para desenhar.
//
// POR QUE ISTO EXISTE
// O card mostrava uma lista corrida de agentes e nao dizia de que conta cada um
// vinha. Agrupar por CLI resolve isso e ainda dispensa o chip laranja do agente
// em cada linha — o cabecalho do grupo ja diz o que ele dizia.
//
// POR QUE E POR CLI, E NAO POR PROVEDOR DO MODELO
// O Antigravity (`agy`, para o herdr) roda modelo de outro provedor:
// `claude-opus-4-6-thinking` foi medido rodando dentro dele. Agrupar por
// provedor do modelo poria esse agente sob o rotulo "Max 5x", e ele NAO consome
// o Max 5x — a tela afirmaria falso.
//
// POR QUE A LIB E NAO O SERVIDOR
// A placa funde duas APIs (ver fusao.h). Agrupar antes da fusao daria dois
// grupos "CLAUDE", um por maquina.
namespace grupos {

// O plano de uma conta, como o /status o publica.
struct Plano {
    std::string agente;   // "claude", "codex", "agy" — como o herdr os nomeia
    std::string rotulo;   // "Max 5x", "Free", "AI Pro"
};

// Uma linha da lista: ou um cabecalho de grupo, ou um agente.
//
// Uma estrutura so para as duas coisas, e nao duas listas paralelas: quem
// desenha percorre de cima para baixo somando altura, e com listas separadas
// essa soma precisaria intercalar as duas na mao — que e exatamente o bug de
// contagem que o `+N` do rodape sofreria.
struct Linha {
    bool        cabecalho = false;
    std::string rotulo;              // "CLAUDE", so quando cabecalho
    // O nome CRU da CLI, em minusculo ("claude", "codex", "agy"). So quando
    // cabecalho. Existe ao lado do `rotulo` porque quem desenha escolhe o icone
    // e a cor da marca por ele, e nao pela versao maiuscula de exibicao —
    // casar por `rotulo` obrigaria a UI a desfazer a capitalizacao que esta
    // funcao acabou de fazer.
    std::string cli;
    std::string plano;               // "Max 5x", so quando cabecalho; vazio = desconhecido
    int         contagem  = 0;       // agentes do grupo, so quando cabecalho
    int         agente    = -1;      // indice em Status::agents, so quando !cabecalho
};

struct Lista {
    std::vector<Linha> linhas;
    // O plano a mostrar ao lado do rotulo do card quando ha UM provedor so.
    // Vazio quando ha dois ou mais — ai cada cabecalho mostra o seu.
    std::string        planoUnico;
};

// Monta a lista. Com um provedor so NAO gera cabecalho nenhum: o card fica
// identico ao de hoje e o plano vai para `planoUnico`. O caso de um provedor e
// o mais comum e o mais apertado (o card deitado cabe quatro sessoes), e
// gastar 12 px de cabecalho para dizer "CLAUDE" numa tela onde tudo e Claude
// seria pagar caro por nada.
//
// Ordem dos grupos: claude primeiro, o resto alfabetico. Dentro do grupo, a
// ordem que a lista ja tinha — que e a de urgencia (blocked, working, idle),
// decidida no servidor e preservada pela fusao.
Lista montarLista(const std::vector<Agent> &agents,
                  const std::vector<Plano> &planos);

// Quantas LINHAS de `l` cabem em `altura`, e quantos AGENTES ficaram de fora.
//
// Mora aqui e nao na UI porque as duas telas (deitada e em pe) fazem a mesma
// conta com passos diferentes, e ela tem uma sutileza: um cabecalho cujo grupo
// inteiro ficou de fora NAO e desenhado — um titulo sozinho sem nenhuma linha
// embaixo dele parece defeito.
// ---- A ordem da lista em pe ----
// Devolve os INDICES de `agents` na ordem de desenho, com as sessoes BLOQUEADAS
// no topo e todo o resto na ordem em que a API mandou.
//
// POR QUE SO O BLOQUEADO SOBE
// A lista em pe corta no que cabe (cinco cartoes) e o resto vira "+N". Se a
// sessao que sobrar for a bloqueada, o painel esconde a unica linha que pede
// acao — e responder pergunta de agente parado e a razao de este painel
// existir. Ordenar por estado inteiro faria as sessoes dancarem de lugar a cada
// poll; promover so `Blocked` mantem a lista parada no caso comum, que e
// nenhuma bloqueada.
//
// A ordem RELATIVA e preservada dentro de cada grupo (ordenacao estavel): duas
// bloqueadas continuam entre si na ordem da API, e as demais tambem. Sem isso a
// lista se reembaralharia a cada poll mesmo sem mudanca de estado.
std::vector<int> ordemComBloqueadosNoTopo(const std::vector<Agent> &agents);

int cabemLinhas(const std::vector<Linha> &l, int altura,
                int hCabecalho, int hAgente, int &foraN);

}
