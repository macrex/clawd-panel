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
    // A barra de rolagem do rodape, que existe porque em pe o swipe vertical
    // disputa com a troca de pagina. `Decisao::n` traz o botao: 0 topo,
    // 1 uma tela para tras, 2 uma para frente, 3 fim.
    TerminalBotao,

    // Navegacao entre as quatro paginas, nas duas orientacoes.
    PaginaProxima,
    PaginaAnterior,

    // Duplo toque, na ordem em que sao disputados.
    GirarTela,
    DispensarReset,
    DispensarOffline,
    EnsaiarReset,
    DispensarToken,
    AbrirToken,
    // O irmao do AbrirToken, no alvo da SESSAO: abre a tela do Clawd dormindo
    // sem esperar o servidor cair. So deitado — em pe aquele alvo ja e o ensaio
    // da tela de reset, que nasceu primeiro e continua dele.
    AbrirOffline,
    TrocarTema,
    AbrirTerminal,
    BotaoLimpeza,
    ProximoAgente,
    TrocarTrabalho,

    // Estas tres carregam um numero em `Decisao::n`.
    TocarOpcao,          // n = a opcao tocada (1..4)
    SelecionarAgente,    // n = o indice na lista
    // Duplo toque num cartao da lista da primeira tela EM PE: seleciona aquele
    // agente e abre o terminal dele de uma vez. Deitado o terminal tem botao
    // proprio; em pe nao havia como chegar nele, e o cartao era o unico alvo
    // grande da tela que nao respondia a nada.
    AbrirTerminalDoAgente,   // n = o indice na lista

    // A FILA DE ATENCAO: a mesma primeira tela deitada, com as sessoes em
    // linhas de largura inteira. Arrastar para cima entra, para baixo volta.
    AbrirFila,
    FecharFila,

    // O PAINEL DE AJUSTES, que desce do cabecalho. Aberto, ele consome todo
    // gesto: o toque num ajuste age, o toque fora ou o arrasto para cima fecha.
    AbrirAjustes,
    FecharAjustes,
    TocarAjuste,             // n = o ajuste tocado (ver ui::ajusteAt)

    // O MODO NOITE: a tela apagada consome o primeiro gesto so para acordar.
    Acordar,
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
    int  paginas      = 4;      // ui::paginas(), para o swipe saber onde acaba
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
    // Qual botao da barra do terminal esta sob o dedo. -1 = nenhum.
    int  botaoTerminal    = -1;
    // Os dois alvos das janelas de limite. O NOME diz onde eles ficam em pe (o
    // percentual de cada coluna); deitado a mesma pergunta e respondida pelo
    // ANEL inteiro, que e onde o rotulo daquela janela esta escrito. Quem
    // resolve a geometria de cada orientacao e a `ui` — aqui chega so "o dedo
    // caiu no alvo da sessao" e "no alvo da semana".
    bool noPctSessao      = false;
    bool noPctSemana      = false;
    bool naTurma          = false;
    bool noBotaoTerminal  = false;
    bool noBotaoLimpeza   = false;
    int  opcaoP0          = 0;   // 0 = o dedo nao caiu em opcao nenhuma
    int  opcaoP1          = 0;
    int  agenteIndex      = -1;  // -1 = fora da lista (menu da pagina 1 em pe)
    // O cartao da lista da PRIMEIRA tela em pe. -1 = o dedo caiu fora dela.
    int  cartaoSessao     = -1;

    // Os modos que mudam o que a tela e.
    bool noite            = false;   // modo noite com a tela apagada
    bool ajustesAbertos   = false;
    bool fila             = false;   // a primeira tela deitada mostra a fila
    // O arrasto NASCEU no cabecalho (hit-test na origem do gesto, e nao no
    // ponto em que o dedo levantou).
    bool inicioNoCabecalho = false;
    // O ajuste sob o dedo com o painel aberto. -1 = fora de todos; o toque
    // fora do painel e -2 (fecha).
    int  ajusteTocado     = -1;
    // A linha da fila sob o dedo. -1 = fora da lista.
    int  linhaFila        = -1;
};

Decisao decidirGesto(GestureKind k, const Contexto &c);

// A pagina `passo` casas adiante (ou atras, negativo), dando a volta: a
// navegacao e circular, e da ultima a esquerda volta a primeira.
int paginaVizinha(int page, int paginas, int passo);

// A pagina e de BICHO GRANDE — a do Clawd (3) ou a do nivel (4)? Nelas o bicho
// ocupa a tela, a turma do rodape nao e desenhada e um quadro de animacao custa
// o redesenho inteiro. A MESMA numeracao nas duas orientacoes.
//
// Uma funcao so, e nao a conta em cada lugar: quando a tela nova entrou na
// frente das paginas deitadas (02/09), a regra foi atualizada em pe e ficou
// velha deitada em tres dos cinco lugares que a escreviam. A pagina de contexto
// passou a redesenhar a tela inteira a cada passo da turma, o Clawd grande
// parou de andar, e a turma passou a ser desenhada por cima da pagina do nivel.
bool paginaDeBicho(int page);

// ---- O quadro de animacao ----
// O que uma volta do laco repinta quando algo que anima deu um passo: a turma
// (e o bicho do canto do cabecalho, que anda no mesmo relogio) ou o nome
// digitando.
//
// Pelo mesmo motivo de `paginaDeBicho`: a regra morava no laco, uma pagina por
// vez, e cada pagina que ficava de fora dela animava devagar sem ninguem notar —
// a turma da pagina 0 deitada, depois o nome das outras paginas deitadas.
struct CenaAnimacao {
    bool emPe         = false;
    int  page         = 0;
    bool telaBicho    = false;   // a tela do Token ou do servidor fora no ar
    bool reset        = false;   // a danca do reset no ar
    bool avancouFaixa = false;   // a turma (e o bicho do canto) deu um passo
    bool avancouNome  = false;   // o nome digitando deu um passo
};

struct QuadroAnimacao {
    bool inteiro = false;   // so o redesenho completo mostra o passo
    bool parcial = false;   // o quadro barato (ui::redrawAnimacao) mostra
    // Com `parcial`: o nome digitando entra (em pe ele e o canto do cabecalho;
    // sem ele, o canto e o bicho) e a turma entra.
    bool nome  = false;
    bool turma = false;
};

QuadroAnimacao quadroDeAnimacao(const CenaAnimacao &c);
