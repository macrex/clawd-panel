#include <unity.h>
#include "acao.h"

void setUp(void) {}
void tearDown(void) {}

// Um painel comum: pagina 0, deitado, com dado bom e nenhuma tela por cima.
static Contexto base() {
    Contexto c;
    c.haveLast = true;
    return c;
}

static Acao act(GestureKind k, const Contexto &c) {
    return decidirGesto(k, c).acao;
}

// ---- Navegacao ----

// A volta e CIRCULAR: da primeira, a direita leva a ultima, e da ultima a
// esquerda volta a primeira. Quem faz a conta do modulo e o laco.
void test_swipe_anda_entre_as_paginas(void) {
    Contexto c = base();
    TEST_ASSERT_EQUAL(Acao::PaginaProxima, act(GestureKind::SwipeLeft, c));
    TEST_ASSERT_EQUAL(Acao::PaginaAnterior, act(GestureKind::SwipeRight, c));

    c.page = 3;
    TEST_ASSERT_EQUAL(Acao::PaginaProxima, act(GestureKind::SwipeLeft, c));
    TEST_ASSERT_EQUAL(Acao::PaginaAnterior, act(GestureKind::SwipeRight, c));

    // Com uma pagina so nao ha para onde ir.
    c.paginas = 1;
    c.page = 0;
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::SwipeLeft, c));
}

// As quatro paginas existem nas duas orientacoes.
void test_swipe_vale_em_pe_tambem(void) {
    Contexto c = base();
    c.retrato = true;
    TEST_ASSERT_EQUAL(Acao::PaginaProxima, act(GestureKind::SwipeLeft, c));
}

// ---- A ordem do duplo toque, que e o que este arquivo existe para travar ----

// O bicho do cabecalho vem PRIMEIRO de todos: em pe ele e a unica saida da
// tela, e uma saida que perde para outro alvo em alguma pagina nao existe.
void test_o_icone_do_cabecalho_ganha_de_tudo(void) {
    Contexto c = base();
    c.noIconeCabecalho = true;
    // Todos os concorrentes ligados ao mesmo tempo.
    c.telaReset = c.clawdDorme = c.telaToken = true;
    c.noPctSessao = c.noPctSemana = c.naTurma = true;
    c.opcaoP0 = c.opcaoP1 = 2;
    c.page = 2;
    TEST_ASSERT_EQUAL(Acao::GirarTela, act(GestureKind::DoubleTap, c));
}

// A tela de reset vem antes dos alvos das janelas: ela esta POR CIMA, e eles
// pertencem a um layout que nao esta na tela.
void test_a_tela_de_reset_ganha_dos_ensaios(void) {
    Contexto c = base();
    c.telaReset = true;
    c.noPctSessao = true;
    TEST_ASSERT_EQUAL(Acao::DispensarReset, act(GestureKind::DoubleTap, c));
}

// O Clawd dormindo idem. Deixar os alvos na frente faria um toque no lugar
// errado abrir outra tela em vez de sair desta.
void test_o_clawd_dormindo_ganha_dos_ensaios(void) {
    Contexto c = base();
    c.clawdDorme = true;
    c.noPctSessao = c.naTurma = true;
    TEST_ASSERT_EQUAL(Acao::DispensarOffline, act(GestureKind::DoubleTap, c));
}

// Mas o Token vem DEPOIS dos dois ensaios — e assim de proposito, porque os
// atalhos de ensaio moram na mesma tela em que ele pode aparecer.
void test_os_ensaios_ganham_do_token(void) {
    Contexto c = base();
    c.telaToken = true;
    c.retrato   = true;              // em pe aquele alvo e o ensaio do reset
    c.noPctSessao = true;
    TEST_ASSERT_EQUAL(Acao::EnsaiarReset, act(GestureKind::DoubleTap, c));

    // Deitado o mesmo alvo abre a tela de offline, e continua ganhando dele.
    Contexto l = c;
    l.retrato = false;
    TEST_ASSERT_EQUAL(Acao::AbrirOffline, act(GestureKind::DoubleTap, l));

    // Sem alvo de ensaio, o Token dispensa.
    Contexto e = base();
    e.telaToken = true;
    TEST_ASSERT_EQUAL(Acao::DispensarToken, act(GestureKind::DoubleTap, e));
}

// DEITADO o alvo da sessao muda de dono: la nao ha tela de reset para ensaiar,
// e o anel da sessao abre a do Clawd dormindo — a irma da do Token, que ate
// entao nao tinha atalho em orientacao nenhuma.
void test_deitado_o_alvo_da_sessao_abre_a_tela_de_offline(void) {
    Contexto c = base();               // base() e deitado
    c.noPctSessao = true;
    TEST_ASSERT_EQUAL(Acao::AbrirOffline, act(GestureKind::DoubleTap, c));

    // Em pe ele continua sendo o ensaio da tela de reset.
    Contexto p = c;
    p.retrato = true;
    TEST_ASSERT_EQUAL(Acao::EnsaiarReset, act(GestureKind::DoubleTap, p));

    // E o da semana abre o Token nas duas.
    Contexto t = base();
    t.noPctSemana = true;
    TEST_ASSERT_EQUAL(Acao::AbrirToken, act(GestureKind::DoubleTap, t));
    t.retrato = true;
    TEST_ASSERT_EQUAL(Acao::AbrirToken, act(GestureKind::DoubleTap, t));
}

// DEITADO os alvos sao dos ANEIS, que moram no miolo da primeira tela — e o
// miolo das outras paginas pertence a outra coisa. Sem a pagina na conta, um
// duplo toque no meio da pagina do Clawd abriria uma tela de bicho em vez de
// trocar o trabalhador.
void test_deitado_os_alvos_de_limite_so_valem_na_primeira_tela(void) {
    Contexto c = base();
    c.page = 3;                        // Clawd
    c.noPctSessao = c.noPctSemana = true;
    TEST_ASSERT_EQUAL(Acao::TrocarTrabalho, act(GestureKind::DoubleTap, c));

    // A pagina 1 e a MESMA tela para os gestos (paginaLogica), entao la valem.
    c.page = 1;
    TEST_ASSERT_EQUAL(Acao::AbrirOffline, act(GestureKind::DoubleTap, c));
}

// Com bloqueio na tela os alvos de ensaio nao existem: la quem mora naquele
// canto e a pergunta.
void test_bloqueio_desliga_os_atalhos_de_ensaio(void) {
    Contexto c = base();
    c.temBloqueio = true;
    c.noPctSessao = c.noPctSemana = true;
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::DoubleTap, c));
}

// A fileira fica ANTES dos alvos de pagina: ela e o unico controle da faixa do
// rodape.
void test_a_turma_ganha_dos_alvos_de_pagina(void) {
    Contexto c = base();
    c.naTurma = true;
    c.page = 1;
    c.opcaoP1 = 3;
    TEST_ASSERT_EQUAL(Acao::TrocarTema, act(GestureKind::DoubleTap, c));
}

// O DEFEITO QUE MOTIVOU A ORDEM: a confirmacao de um botao e, para o detector,
// um duplo toque. Sem estas tres linhas antes do "proximo agente", confirmar uma
// limpeza trocaria de agente em vez de limpar.
void test_os_botoes_da_pagina_1_ganham_do_proximo_agente(void) {
    Contexto c = base();
    c.page = 2;              // contexto

    Contexto opcao = c;  opcao.opcaoP1 = 2;
    TEST_ASSERT_EQUAL(Acao::TocarOpcao, act(GestureKind::DoubleTap, opcao));
    TEST_ASSERT_EQUAL_INT(2, decidirGesto(GestureKind::DoubleTap, opcao).n);

    Contexto term = c;   term.noBotaoTerminal = true;
    TEST_ASSERT_EQUAL(Acao::AbrirTerminal, act(GestureKind::DoubleTap, term));

    Contexto limpa = c;  limpa.noBotaoLimpeza = true;
    TEST_ASSERT_EQUAL(Acao::BotaoLimpeza, act(GestureKind::DoubleTap, limpa));

    // Fora dos botoes, deitado: avanca o agente.
    TEST_ASSERT_EQUAL(Acao::ProximoAgente, act(GestureKind::DoubleTap, c));
}

// Em pe a lista mostra os quatro e o toque simples seleciona, entao o duplo
// toque fica livre para os alvos proprios.
void test_em_pe_a_pagina_1_nao_avanca_o_agente(void) {
    Contexto c = base();
    c.page = 1;
    c.retrato = true;
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::DoubleTap, c));
}

void test_a_pagina_do_clawd_troca_o_trabalhador(void) {
    Contexto c = base();
    c.page = 3;              // Clawd
    TEST_ASSERT_EQUAL(Acao::TrocarTrabalho, act(GestureKind::DoubleTap, c));
}

// A pergunta em tela cheia da P0 vale em pe em qualquer pagina.
void test_a_pergunta_da_p0_confirma_no_duplo_toque(void) {
    Contexto c = base();
    c.opcaoP0 = 3;
    TEST_ASSERT_EQUAL(Acao::TocarOpcao, act(GestureKind::DoubleTap, c));
    TEST_ASSERT_EQUAL_INT(3, decidirGesto(GestureKind::DoubleTap, c).n);

    Contexto emPe = c;
    emPe.page = 3;
    emPe.retrato = true;
    TEST_ASSERT_EQUAL(Acao::TocarOpcao, act(GestureKind::DoubleTap, emPe));
}

// Sem dado nenhum nao ha alvo de conteudo — so o cabecalho e as telas por cima.
void test_sem_dado_o_duplo_toque_nao_toca_conteudo(void) {
    Contexto c;              // haveLast falso
    c.opcaoP0 = c.opcaoP1 = 2;
    c.noPctSemana = true;
    c.page = 1;
    c.retrato = true;
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::DoubleTap, c));
}

// ---- Toque simples ----

void test_toque_na_lista_seleciona_o_agente(void) {
    Contexto c = base();
    c.page = 2;              // contexto: a tela nova empurrou tudo uma casa
    c.agenteIndex = 2;
    c.opcaoP1 = 1;            // a lista ganha da opcao
    TEST_ASSERT_EQUAL(Acao::SelecionarAgente, act(GestureKind::Tap, c));
    TEST_ASSERT_EQUAL_INT(2, decidirGesto(GestureKind::Tap, c).n);
}

void test_toque_fora_da_lista_cai_nos_botoes(void) {
    Contexto c = base();
    c.page = 2;              // contexto

    Contexto opcao = c;  opcao.opcaoP1 = 4;
    TEST_ASSERT_EQUAL(Acao::TocarOpcao, act(GestureKind::Tap, opcao));

    Contexto term = c;   term.noBotaoTerminal = true;
    TEST_ASSERT_EQUAL(Acao::AbrirTerminal, act(GestureKind::Tap, term));

    Contexto limpa = c;  limpa.noBotaoLimpeza = true;
    TEST_ASSERT_EQUAL(Acao::BotaoLimpeza, act(GestureKind::Tap, limpa));

    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::Tap, c));
}

// O toque simples so responde a pergunta quando ela esta na tela — e ai ele nao
// cai para a pagina 1, mesmo estando nela.
void test_a_pergunta_captura_o_toque_simples(void) {
    Contexto c = base();
    c.temBloqueio = true;
    c.retrato = true;
    c.page = 1;
    c.agenteIndex = 0;        // existiria, mas a pergunta esta na frente
    c.opcaoP0 = 1;
    TEST_ASSERT_EQUAL(Acao::TocarOpcao, act(GestureKind::Tap, c));
    TEST_ASSERT_EQUAL_INT(1, decidirGesto(GestureKind::Tap, c).n);

    // Dedo fora das opcoes com a pergunta na tela: nada acontece, e o toque NAO
    // vaza para a lista atras dela.
    c.opcaoP0 = 0;
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::Tap, c));
}

// ---- Modo terminal ----

void test_o_terminal_consome_o_gesto_antes_de_tudo(void) {
    Contexto c = base();
    c.modoTerminal = true;
    c.page = 0;
    c.noIconeCabecalho = true;   // o alvo mais forte de fora nao vale aqui

    TEST_ASSERT_EQUAL(Acao::TerminalRolarCima, act(GestureKind::SwipeUp, c));
    TEST_ASSERT_EQUAL(Acao::TerminalRolarBaixo, act(GestureKind::SwipeDown, c));
    // Swipe para a esquerda nao troca de pagina: nao ha para onde ir.
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::SwipeLeft, c));
    // E o swipe para a direita sai, porque e o gesto que a mao ja aprendeu.
    TEST_ASSERT_EQUAL(Acao::TerminalFechar, act(GestureKind::SwipeRight, c));
}

void test_o_terminal_so_fecha_pelo_botao_no_toque(void) {
    Contexto c = base();
    c.modoTerminal = true;
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::Tap, c));

    c.noSairTerminal = true;
    TEST_ASSERT_EQUAL(Acao::TerminalFechar, act(GestureKind::Tap, c));
    TEST_ASSERT_EQUAL(Acao::TerminalFechar, act(GestureKind::DoubleTap, c));
}

// ---- A tela nova em pe (pagina 0) e o degrau das demais ----
// Em pe as paginas viraram cinco, com a tela nova na frente. Para os gestos a
// nova e a principal (1) sao a MESMA pagina — mesma lista, mesma turma — e as
// outras descem um degrau para casar com a numeracao da paisagem.

void test_em_pe_a_nova_e_a_principal_abrem_o_terminal_do_cartao(void) {
    Contexto c = base();
    c.retrato = true;
    c.cartaoSessao = 2;

    c.page = 0;                    // tela nova
    Decisao d = decidirGesto(GestureKind::DoubleTap, c);
    TEST_ASSERT_EQUAL(Acao::AbrirTerminalDoAgente, d.acao);
    TEST_ASSERT_EQUAL(2, d.n);

    c.page = 1;                    // principal
    d = decidirGesto(GestureKind::DoubleTap, c);
    TEST_ASSERT_EQUAL(Acao::AbrirTerminalDoAgente, d.acao);
}

void test_em_pe_as_paginas_desceram_um_degrau(void) {
    Contexto c = base();
    c.retrato = true;

    // A pagina de contexto agora e a 2: os botoes dela valem la...
    c.page = 2;
    c.noBotaoTerminal = true;
    TEST_ASSERT_EQUAL(Acao::AbrirTerminal,
                      decidirGesto(GestureKind::DoubleTap, c).acao);
    // ...o toque no menu seleciona o agente la...
    c.noBotaoTerminal = false;
    c.agenteIndex = 1;
    TEST_ASSERT_EQUAL(Acao::SelecionarAgente,
                      decidirGesto(GestureKind::Tap, c).acao);
    // ...e NAO na principal, que desceu para a 1 e nao tem menu nenhum.
    c.page = 1;
    TEST_ASSERT_EQUAL(Acao::Nada, decidirGesto(GestureKind::Tap, c).acao);

    // A pagina do Clawd e a 3.
    c = base();
    c.retrato = true;
    c.page = 3;
    TEST_ASSERT_EQUAL(Acao::TrocarTrabalho,
                      decidirGesto(GestureKind::DoubleTap, c).acao);
}

void test_em_pe_o_swipe_alcanca_a_quinta_pagina(void) {
    Contexto c = base();
    c.retrato = true;
    c.paginas = 5;
    c.page    = 4;
    // Da quinta, a esquerda da a volta para a primeira.
    TEST_ASSERT_EQUAL(Acao::PaginaProxima,
                      decidirGesto(GestureKind::SwipeLeft, c).acao);
    c.page = 3;
    TEST_ASSERT_EQUAL(Acao::PaginaProxima,
                      decidirGesto(GestureKind::SwipeLeft, c).acao);
}

// Gesto que o painel nao usa nunca vira acao: fora da primeira tela deitada e
// longe do cabecalho, o arrasto vertical nao significa nada.
void test_gesto_nenhum_nao_faz_nada(void) {
    Contexto c = base();
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::None, c));
    c.page = 2;
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::SwipeUp, c));
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::SwipeDown, c));
}

// ---- A fila de atencao ----

void test_arrastar_para_cima_abre_a_fila_so_na_primeira_deitada(void) {
    Contexto c = base();
    TEST_ASSERT_EQUAL(Acao::AbrirFila, act(GestureKind::SwipeUp, c));

    c.retrato = true;                 // em pe a fila nao existe
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::SwipeUp, c));
    c.retrato = false;
    c.page = 1;                       // nem na principal
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::SwipeUp, c));
    c.page = 0;
    c.temBloqueio = true;             // a pergunta esta por cima
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::SwipeUp, c));
    c.temBloqueio = false;
    c.fila = true;                    // ja esta nela
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::SwipeUp, c));
}

void test_arrastar_para_baixo_volta_da_fila(void) {
    Contexto c = base();
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::SwipeDown, c));
    c.fila = true;
    TEST_ASSERT_EQUAL(Acao::FecharFila, act(GestureKind::SwipeDown, c));
    // Com a pergunta por cima a fila nao e vista, e nao muda as cegas.
    c.temBloqueio = true;
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::SwipeDown, c));
    c.temBloqueio = false;
    // Nas outras paginas a fila nao esta na tela, e o arrasto nao a fecha.
    c.page = 3;
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::SwipeDown, c));
}

// As paginas de bicho grande sao as mesmas nas duas orientacoes: a do Clawd e
// a do nivel. As novas do fim (Semanas e Hoje) tem turma no rodape.
void test_paginas_de_bicho_grande(void) {
    TEST_ASSERT_FALSE(paginaDeBicho(0));
    TEST_ASSERT_FALSE(paginaDeBicho(2));    // contexto: a do defeito de 02/09
    TEST_ASSERT_TRUE(paginaDeBicho(3));
    TEST_ASSERT_TRUE(paginaDeBicho(4));
    TEST_ASSERT_FALSE(paginaDeBicho(5));
    TEST_ASSERT_FALSE(paginaDeBicho(6));
}

// A conta da volta circular, que o laco aplica.
void test_pagina_vizinha_da_a_volta(void) {
    TEST_ASSERT_EQUAL(1, paginaVizinha(0, 7, +1));
    TEST_ASSERT_EQUAL(6, paginaVizinha(0, 7, -1));   // da inicial para Hoje
    TEST_ASSERT_EQUAL(0, paginaVizinha(6, 7, +1));
    TEST_ASSERT_EQUAL(4, paginaVizinha(0, 5, -1));   // em pe
    TEST_ASSERT_EQUAL(0, paginaVizinha(3, 0, +1));
}

void test_na_fila_o_duplo_toque_na_linha_abre_o_terminal(void) {
    Contexto c = base();
    c.fila = true;
    c.linhaFila = 2;
    // Os aneis nao estao na tela: o alvo da sessao nao abre o Clawd dormindo.
    c.noPctSessao = true;
    Decisao d = decidirGesto(GestureKind::DoubleTap, c);
    TEST_ASSERT_EQUAL(Acao::AbrirTerminalDoAgente, d.acao);
    TEST_ASSERT_EQUAL(2, d.n);

    // Fora da fila o mesmo dedo continua sendo o anel da sessao.
    c.fila = false;
    TEST_ASSERT_EQUAL(Acao::AbrirOffline, act(GestureKind::DoubleTap, c));
}

// ---- O painel de ajustes ----

void test_arrasto_do_cabecalho_abre_os_ajustes_em_qualquer_pagina(void) {
    Contexto c = base();
    c.inicioNoCabecalho = true;
    TEST_ASSERT_EQUAL(Acao::AbrirAjustes, act(GestureKind::SwipeDown, c));
    c.fila = true;                    // ganha do "sair da fila"
    TEST_ASSERT_EQUAL(Acao::AbrirAjustes, act(GestureKind::SwipeDown, c));
    c.page = 4;
    TEST_ASSERT_EQUAL(Acao::AbrirAjustes, act(GestureKind::SwipeDown, c));
    c.retrato = true;
    TEST_ASSERT_EQUAL(Acao::AbrirAjustes, act(GestureKind::SwipeDown, c));
}

void test_o_painel_de_ajustes_consome_o_gesto(void) {
    Contexto c = base();
    c.ajustesAbertos = true;
    c.noIconeCabecalho = true;        // nem o giro passa por cima do painel

    c.ajusteTocado = 3;
    Decisao d = decidirGesto(GestureKind::Tap, c);
    TEST_ASSERT_EQUAL(Acao::TocarAjuste, d.acao);
    TEST_ASSERT_EQUAL(3, d.n);
    // O segundo toque de uma dupla nao repete o ajuste que o primeiro ja fez.
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::DoubleTap, c));

    c.ajusteTocado = -1;              // dentro do painel, fora dos ajustes
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::Tap, c));
    c.ajusteTocado = -2;              // fora do painel
    TEST_ASSERT_EQUAL(Acao::FecharAjustes, act(GestureKind::Tap, c));

    TEST_ASSERT_EQUAL(Acao::FecharAjustes, act(GestureKind::SwipeUp, c));
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::SwipeLeft, c));
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::SwipeDown, c));
}

// ---- O modo noite ----

void test_a_noite_so_acorda(void) {
    Contexto c = base();
    c.noite = true;
    c.modoTerminal = true;            // nem o terminal age no escuro
    c.opcaoP0 = 1;
    c.temBloqueio = true;
    TEST_ASSERT_EQUAL(Acao::Acordar, act(GestureKind::Tap, c));
    TEST_ASSERT_EQUAL(Acao::Acordar, act(GestureKind::DoubleTap, c));
    TEST_ASSERT_EQUAL(Acao::Acordar, act(GestureKind::SwipeLeft, c));
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::None, c));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_swipe_anda_entre_as_paginas);
    RUN_TEST(test_swipe_vale_em_pe_tambem);
    RUN_TEST(test_o_icone_do_cabecalho_ganha_de_tudo);
    RUN_TEST(test_a_tela_de_reset_ganha_dos_ensaios);
    RUN_TEST(test_o_clawd_dormindo_ganha_dos_ensaios);
    RUN_TEST(test_os_ensaios_ganham_do_token);
    RUN_TEST(test_deitado_o_alvo_da_sessao_abre_a_tela_de_offline);
    RUN_TEST(test_deitado_os_alvos_de_limite_so_valem_na_primeira_tela);
    RUN_TEST(test_bloqueio_desliga_os_atalhos_de_ensaio);
    RUN_TEST(test_a_turma_ganha_dos_alvos_de_pagina);
    RUN_TEST(test_os_botoes_da_pagina_1_ganham_do_proximo_agente);
    RUN_TEST(test_em_pe_a_pagina_1_nao_avanca_o_agente);
    RUN_TEST(test_a_pagina_do_clawd_troca_o_trabalhador);
    RUN_TEST(test_a_pergunta_da_p0_confirma_no_duplo_toque);
    RUN_TEST(test_sem_dado_o_duplo_toque_nao_toca_conteudo);
    RUN_TEST(test_toque_na_lista_seleciona_o_agente);
    RUN_TEST(test_toque_fora_da_lista_cai_nos_botoes);
    RUN_TEST(test_a_pergunta_captura_o_toque_simples);
    RUN_TEST(test_em_pe_a_nova_e_a_principal_abrem_o_terminal_do_cartao);
    RUN_TEST(test_em_pe_as_paginas_desceram_um_degrau);
    RUN_TEST(test_em_pe_o_swipe_alcanca_a_quinta_pagina);
    RUN_TEST(test_o_terminal_consome_o_gesto_antes_de_tudo);
    RUN_TEST(test_o_terminal_so_fecha_pelo_botao_no_toque);
    RUN_TEST(test_gesto_nenhum_nao_faz_nada);
    RUN_TEST(test_arrastar_para_cima_abre_a_fila_so_na_primeira_deitada);
    RUN_TEST(test_arrastar_para_baixo_volta_da_fila);
    RUN_TEST(test_pagina_vizinha_da_a_volta);
    RUN_TEST(test_paginas_de_bicho_grande);
    RUN_TEST(test_na_fila_o_duplo_toque_na_linha_abre_o_terminal);
    RUN_TEST(test_arrasto_do_cabecalho_abre_os_ajustes_em_qualquer_pagina);
    RUN_TEST(test_o_painel_de_ajustes_consome_o_gesto);
    RUN_TEST(test_a_noite_so_acorda);
    return UNITY_END();
}
