#include "acao.h"

namespace {

Decisao nada() { return Decisao{}; }
Decisao so(Acao a) { return Decisao{a, 0}; }
Decisao com(Acao a, int n) { return Decisao{a, n}; }

// O gesto dentro da tela de terminal. Ela consome tudo e devolve — nao ha
// pagina para trocar nem alvo de status para disputar.
Decisao noTerminal(GestureKind k, const Contexto &c) {
    switch (k) {
        case GestureKind::Tap:
        case GestureKind::DoubleTap:
            if (c.noSairTerminal) return so(Acao::TerminalFechar);
            // A barra do rodape vem DEPOIS do sair: os dois nunca se encostam,
            // mas a saida ganha qualquer disputa por principio — ela e a unica
            // que nao pode falhar.
            if (c.botaoTerminal >= 0)
                return com(Acao::TerminalBotao, c.botaoTerminal);
            return nada();
        // Arrastar para CIMA leva o texto para cima, ou seja, mostra o que esta
        // ABAIXO — e abaixo esta o mais recente. E a mesma direcao de qualquer
        // lista rolavel; inverter aqui brigaria com o dedo.
        case GestureKind::SwipeUp:   return so(Acao::TerminalRolarCima);
        case GestureKind::SwipeDown: return so(Acao::TerminalRolarBaixo);
        // Swipe horizontal tambem sai: e o gesto que a mao ja aprendeu neste
        // painel, e nao ter saida alem de um botao pequeno seria uma armadilha.
        case GestureKind::SwipeRight: return so(Acao::TerminalFechar);
        default: return nada();
    }
}

// A pagina NORMALIZADA: a tela nova (0) e a principal (1) sao a MESMA pagina
// para os gestos — mesma lista de cartoes, mesma turma, mesma pergunta em tela
// cheia —, e as demais descem uma casa. E o que deixa as regras abaixo
// escritas UMA vez, como sempre foram.
//
// Vale nas DUAS orientacoes desde que a tela nova ganhou a irma deitada: antes
// era so em pe, e o `retrato` entrava na conta.
int paginaLogica(const Contexto &c) {
    return c.page == 0 ? 0 : c.page - 1;
}

// A FILA esta na tela: so na primeira pagina deitada, e so com ela ligada.
bool naFila(const Contexto &c) {
    return c.fila && !c.retrato && c.page == 0;
}

// O duplo toque, na ordem em que os alvos disputam. Cada `if` desta cadeia e
// uma regra, e trocar dois de lugar muda o painel.
Decisao duploToque(const Contexto &c) {
    const int p = paginaLogica(c);
    // O BICHO DO CABECALHO vem PRIMEIRO de todos, e vale em qualquer pagina: em
    // pe ele e a unica saida da tela, e uma saida que perde para outro alvo em
    // alguma pagina e uma saida que nao existe.
    if (c.noIconeCabecalho) return so(Acao::GirarTela);

    // Um toque ja dispensa a tela de reset: ela nao pede decisao nenhuma, e
    // exigir dois seria cerimonia para fechar um cartaz.
    if (c.telaReset) return so(Acao::DispensarReset);

    // Com o Clawd dormindo, QUALQUER duplo toque abaixo do cabecalho e "ja vi,
    // devolve o painel". Os alvos das janelas logo abaixo pertencem ao layout
    // da P0, que neste momento nao esta na tela — deixa-los na frente faria um
    // toque no lugar errado abrir outra tela em vez de sair.
    if (c.clawdDorme) return so(Acao::DispensarOffline);

    // Os alvos das duas janelas de limite. Com bloqueio na tela eles nao
    // existem: la quem mora naquele canto e a pergunta.
    //
    // `p == 0` DEITADO e o alvo deitado nascendo com limite: la os aneis moram
    // no miolo da tela, e sem a pagina na conta um duplo toque no meio da
    // pagina do Clawd (que troca o trabalhador) abriria uma tela de bicho. Em
    // pe a regra fica como sempre foi, valendo em qualquer pagina.
    //
    // Com a FILA na primeira tela deitada os aneis nao estao la: as linhas
    // ocupam aquele lugar, e o duplo toque nelas abre o terminal (abaixo).
    const bool alvosDaP0 = c.retrato || (p == 0 && !naFila(c));
    if (alvosDaP0 && c.haveLast && !c.temBloqueio && c.noPctSessao)
        // Em pe aquele alvo ensaia a tela de RESET; deitado ela nao existe, e o
        // que o anel da sessao abre e o Clawd dormindo.
        return so(c.retrato ? Acao::EnsaiarReset : Acao::AbrirOffline);

    // Com o Token na tela, qualquer duplo toque abaixo do cabecalho e "ja vi".
    // Nao ha outro alvo para disputar: a tela e dele.
    if (c.telaToken) return so(Acao::DispensarToken);

    if (alvosDaP0 && c.haveLast && !c.temBloqueio && c.noPctSemana)
        return so(Acao::AbrirToken);

    // A fileira troca o ELENCO. Fica ANTES dos alvos de pagina porque ela e o
    // unico controle daquela faixa: deitado o rodape so tem os bichos e as
    // bolinhas, e em pe a turma mora sozinha entre as duas divisorias.
    if (c.naTurma) return so(Acao::TrocarTema);

    // O CARTAO DE UMA SESSAO, so na primeira tela em pe: abre o terminal dela.
    //
    // Fica DEPOIS do bloqueio (a pergunta em tela cheia toma o painel e o
    // segundo toque ali e "confirmar", nao "abrir") e depois da turma, que e
    // faixa propria. Antes dos alvos de pagina porque a lista ocupa metade da
    // tela: com a pagina ganhando, qualquer toque na lista viraria navegacao.
    if (c.retrato && p == 0 && c.haveLast && !c.temBloqueio &&
        c.cartaoSessao >= 0)
        return com(Acao::AbrirTerminalDoAgente, c.cartaoSessao);

    // A LINHA DA FILA abre o terminal daquela sessao, como o cartao em pe.
    if (naFila(c) && c.haveLast && !c.temBloqueio && c.linhaFila >= 0)
        return com(Acao::AbrirTerminalDoAgente, c.linhaFila);

    // Pergunta em tela cheia da P0: o segundo toque confirma, do mesmo jeito que
    // na aba de contexto.
    if ((p == 0 || c.retrato) && c.haveLast && c.opcaoP0)
        return com(Acao::TocarOpcao, c.opcaoP0);

    if (p == 1 && c.haveLast) {
        if (c.opcaoP1) return com(Acao::TocarOpcao, c.opcaoP1);
        // O duplo toque cai aqui tambem: dois toques rapidos no botao de
        // terminal sao um duplo toque para o detector, e sem esta linha eles
        // trocariam de agente em vez de abrir a tela.
        if (c.noBotaoTerminal) return so(Acao::AbrirTerminal);
        if (c.noBotaoLimpeza)  return so(Acao::BotaoLimpeza);
    }

    // Deitado, o duplo toque em qualquer lugar da pagina 1 avanca o agente. Em
    // pe NAO: la a lista mostra os quatro e o toque simples seleciona, entao o
    // duplo toque fica livre para os alvos proprios.
    if (p == 1 && !c.retrato) return so(Acao::ProximoAgente);

    // Duplo toque na pagina do Clawd troca o trabalhador em cena. Vale a pagina
    // inteira e nao so o retangulo do sprite: nao ha mais nada para tocar aqui,
    // e a moldura varia de 152 a 440 px — exigir o alvo certo faria o gesto
    // falhar justamente com os bichos menores.
    if (p == 2) return so(Acao::TrocarTrabalho);

    return nada();
}

Decisao toqueSimples(const Contexto &c) {
    const int p = paginaLogica(c);
    // Pergunta em tela cheia: na P0 das duas orientacoes, e em pe em QUALQUER
    // pagina — la o bloqueio toma a tela inteira.
    if ((p == 0 || c.retrato) && c.haveLast && c.temBloqueio)
        return c.opcaoP0 ? com(Acao::TocarOpcao, c.opcaoP0) : nada();

    if (p == 1 && c.haveLast) {
        // Toque simples no menu da direita seleciona aquele repo.
        if (c.agenteIndex >= 0) return com(Acao::SelecionarAgente, c.agenteIndex);
        if (c.opcaoP1)          return com(Acao::TocarOpcao, c.opcaoP1);
        // Um toque so, sem armar. Abrir uma tela de LEITURA nao muda nada no
        // agente — a confirmacao existe onde o toque age, e aqui ele nao age.
        if (c.noBotaoTerminal)  return so(Acao::AbrirTerminal);
        if (c.noBotaoLimpeza)   return so(Acao::BotaoLimpeza);
    }

    return nada();
}

// O painel de ajustes aberto consome TODO gesto: por baixo dele a pagina nao
// esta visivel, e um swipe que trocasse de pagina atras do painel seria um
// efeito sem tela. Um toque num ajuste age; fora do painel, fecha.
//
// O DUPLO TOQUE nao faz nada aqui. Para o detector ele e o SEGUNDO toque de
// uma dupla cujo primeiro ja chegou como Tap e ja agiu: repassa-lo desligaria
// o SOM que o primeiro acabou de ligar. E o habito deste painel, onde muita
// coisa pede dois toques, e a mao vai repetir.
Decisao noPainel(GestureKind k, const Contexto &c) {
    switch (k) {
        case GestureKind::Tap:
            if (c.ajusteTocado >= 0) return com(Acao::TocarAjuste, c.ajusteTocado);
            if (c.ajusteTocado == -2) return so(Acao::FecharAjustes);
            return nada();
        case GestureKind::SwipeUp: return so(Acao::FecharAjustes);
        default:                   return nada();
    }
}

}   // namespace

Decisao decidirGesto(GestureKind k, const Contexto &c) {
    if (k == GestureKind::None) return nada();

    // A tela da noite vem antes de tudo: ela esta apagada, e um toque que
    // agisse no escuro (abrir terminal, aprovar uma opcao) seria um toque dado
    // sem ver. O primeiro gesto so acende.
    if (c.noite) return so(Acao::Acordar);
    if (c.modoTerminal) return noTerminal(k, c);
    if (c.ajustesAbertos) return noPainel(k, c);

    switch (k) {
        // As paginas existem nas DUAS orientacoes e a volta e CIRCULAR: da
        // primeira, arrastar para a direita leva a ultima. E o que deixa as
        // paginas do fim (Hoje e Semanas) a um gesto da tela inicial.
        case GestureKind::SwipeLeft:
            return c.paginas > 1 ? so(Acao::PaginaProxima) : nada();
        case GestureKind::SwipeRight:
            return c.paginas > 1 ? so(Acao::PaginaAnterior) : nada();

        // O arrasto vertical nasceu no terminal. Fora dele: do cabecalho para
        // baixo abre os ajustes em qualquer pagina; na primeira tela deitada,
        // para cima entra na fila e para baixo sai dela.
        //
        // Com a PERGUNTA na tela a fila nao liga nem desliga: ela esta por
        // baixo, e trocar um modo que nao se ve seria mudar o painel as cegas.
        case GestureKind::SwipeDown:
            if (c.inicioNoCabecalho) return so(Acao::AbrirAjustes);
            if (naFila(c) && !c.temBloqueio) return so(Acao::FecharFila);
            return nada();
        case GestureKind::SwipeUp:
            if (!c.fila && !c.retrato && c.page == 0 && !c.temBloqueio)
                return so(Acao::AbrirFila);
            return nada();

        case GestureKind::DoubleTap: return duploToque(c);
        case GestureKind::Tap:       return toqueSimples(c);
        default:                     return nada();
    }
}

int paginaVizinha(int page, int paginas, int passo) {
    if (paginas <= 0) return 0;
    return ((page + passo) % paginas + paginas) % paginas;
}

bool paginaDeBicho(int page) { return page == 3 || page == 4; }
