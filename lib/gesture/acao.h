#pragma once
#include "gesture.h"

// O que o painel FAZ com um gesto — separado de como o gesto foi reconhecido.
//
// `GestureDetector` responde "isto foi um duplo toque em (x, y)". Esta parte
// responde a pergunta seguinte, que e a dificil: com esta pagina, esta
// orientacao, esta tela por cima e este dedo aqui, o duplo toque significa
// GIRAR, DISPENSAR, TROCAR DE TEMA ou APROVAR?
//
// A resposta e uma cadeia de quatorze `else if` cuja ORDEM carrega as regras, e
// ela morava dentro do `loop()`, onde nenhum teste alcanca. A ordem ja custou um
// defeito real: a confirmacao de um botao e, para o detector, um duplo toque — e
// na pagina de contexto o duplo toque ja significava "proximo agente". Sem a
// ordem certa, confirmar uma limpeza trocava de agente em vez de limpar. Foi
// assim que o botao de limpeza quebrou quando nasceu.
//
// O que NAO mora aqui: geometria. Quem sabe onde cada alvo esta e a `ui`, que
// precisa do canvas e das fontes. Os hit-tests chegam ja resolvidos, como
// booleanos — e o que fica testavel e exatamente o que quebrou.

enum class Acao {
    Nada,

    // Modo terminal. Ele consome o gesto ANTES de todo o resto: la os swipes
    // horizontais nao trocam de pagina (nao ha para onde ir) e os verticais,
    // que fora dali nao significam nada, rolam o texto.
    TerminalFechar,
    TerminalRolarCima,
    TerminalRolarBaixo,

    // Navegacao entre as quatro paginas, nas duas orientacoes.
    PaginaProxima,
    PaginaAnterior,

    // Duplo toque, na ordem em que sao disputados.
    GirarTela,
    DispensarReset,
    DispensarOffline,
    EnsaiarKenny,
    EnsaiarReset,
    DispensarToken,
    AbrirToken,
    TrocarTema,
    AbrirTerminal,
    BotaoLimpeza,
    ProximoAgente,
    TrocarTrabalho,

    // Estas duas carregam um numero em `Decisao::n`.
    TocarOpcao,          // n = a opcao tocada (1..4)
    SelecionarAgente,    // n = o indice na lista
};

struct Decisao {
    Acao acao = Acao::Nada;
    int  n    = 0;
};

// Tudo o que a decisao precisa saber, ja resolvido por quem tem acesso a isso.
//
// Os campos `no*`/`na*` sao hit-tests: quem os calcula e o laco, chamando a `ui`.
// Eles chegam prontos porque a geometria pede canvas e fonte, e arrastar isso
// para ca traria o Arduino junto — e ai nao haveria teste nenhum.
struct Contexto {
    bool modoTerminal = false;
    int  page         = 0;
    int  paginas      = 4;      // ui::PAGES, para o swipe saber onde acaba
    bool retrato      = false;
    bool haveLast     = false;
    bool temBloqueio  = false;

    // As telas que tomam o painel inteiro, na ordem em que disputam o gesto.
    bool telaReset    = false;
    bool clawdDorme   = false;
    bool telaToken    = false;

    // Hit-tests ja resolvidos.
    bool noIconeCabecalho = false;
    bool noSairTerminal   = false;
    bool noRotuloSemana   = false;
    bool noPctSessao      = false;
    bool noPctSemana      = false;
    bool naTurma          = false;
    bool noBotaoTerminal  = false;
    bool noBotaoLimpeza   = false;
    int  opcaoP0          = 0;   // 0 = o dedo nao caiu em opcao nenhuma
    int  opcaoP1          = 0;
    int  agenteIndex      = -1;  // -1 = fora da lista
};

Decisao decidirGesto(GestureKind k, const Contexto &c);
