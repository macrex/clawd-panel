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

void test_swipe_anda_entre_as_paginas(void) {
    Contexto c = base();
    TEST_ASSERT_EQUAL(Acao::PaginaProxima, act(GestureKind::SwipeLeft, c));
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::SwipeRight, c));

    c.page = 3;
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::SwipeLeft, c));
    TEST_ASSERT_EQUAL(Acao::PaginaAnterior, act(GestureKind::SwipeRight, c));
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
    c.noRotuloSemana = c.noPctSessao = c.noPctSemana = c.naTurma = true;
    c.opcaoP0 = c.opcaoP1 = 2;
    c.page = 2;
    TEST_ASSERT_EQUAL(Acao::GirarTela, act(GestureKind::DoubleTap, c));
}

// A tela de reset vem antes dos ensaios: ela esta POR CIMA, e os alvos de
// ensaio pertencem a um layout que nao esta na tela.
void test_a_tela_de_reset_ganha_dos_ensaios(void) {
    Contexto c = base();
    c.telaReset = true;
    c.noRotuloSemana = c.noPctSessao = true;
    TEST_ASSERT_EQUAL(Acao::DispensarReset, act(GestureKind::DoubleTap, c));
}

// O Clawd dormindo idem. Deixar os ensaios na frente faria um toque no lugar
// errado matar o Kenny em vez de sair da tela.
void test_o_clawd_dormindo_ganha_dos_ensaios(void) {
    Contexto c = base();
    c.clawdDorme = true;
    c.noRotuloSemana = c.noPctSessao = c.naTurma = true;
    TEST_ASSERT_EQUAL(Acao::DispensarOffline, act(GestureKind::DoubleTap, c));
}

// Mas o Token vem DEPOIS dos dois ensaios — e assim de proposito, porque os
// atalhos de ensaio moram na mesma tela em que ele pode aparecer.
void test_os_ensaios_ganham_do_token(void) {
    Contexto c = base();
    c.telaToken = true;
    c.noPctSessao = true;
    TEST_ASSERT_EQUAL(Acao::EnsaiarReset, act(GestureKind::DoubleTap, c));

    Contexto d = base();
    d.telaToken = true;
    d.noRotuloSemana = true;
    TEST_ASSERT_EQUAL(Acao::EnsaiarKenny, act(GestureKind::DoubleTap, d));

    // Sem alvo de ensaio, o Token dispensa.
    Contexto e = base();
    e.telaToken = true;
    TEST_ASSERT_EQUAL(Acao::DispensarToken, act(GestureKind::DoubleTap, e));
}

// Com bloqueio na tela os alvos de ensaio nao existem: la quem mora naquele
// canto e a pergunta.
void test_bloqueio_desliga_os_atalhos_de_ensaio(void) {
    Contexto c = base();
    c.temBloqueio = true;
    c.noRotuloSemana = c.noPctSessao = c.noPctSemana = true;
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
    c.page = 1;

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
    c.page = 2;
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
    c.noRotuloSemana = true;
    c.page = 1;
    c.retrato = true;
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::DoubleTap, c));
}

// ---- Toque simples ----

void test_toque_na_lista_seleciona_o_agente(void) {
    Contexto c = base();
    c.page = 1;
    c.agenteIndex = 2;
    c.opcaoP1 = 1;            // a lista ganha da opcao
    TEST_ASSERT_EQUAL(Acao::SelecionarAgente, act(GestureKind::Tap, c));
    TEST_ASSERT_EQUAL_INT(2, decidirGesto(GestureKind::Tap, c).n);
}

void test_toque_fora_da_lista_cai_nos_botoes(void) {
    Contexto c = base();
    c.page = 1;

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

// Gesto que o painel nao usa nunca vira acao.
void test_gesto_nenhum_nao_faz_nada(void) {
    Contexto c = base();
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::None, c));
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::SwipeUp, c));
    TEST_ASSERT_EQUAL(Acao::Nada, act(GestureKind::SwipeDown, c));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_swipe_anda_entre_as_paginas);
    RUN_TEST(test_swipe_vale_em_pe_tambem);
    RUN_TEST(test_o_icone_do_cabecalho_ganha_de_tudo);
    RUN_TEST(test_a_tela_de_reset_ganha_dos_ensaios);
    RUN_TEST(test_o_clawd_dormindo_ganha_dos_ensaios);
    RUN_TEST(test_os_ensaios_ganham_do_token);
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
    RUN_TEST(test_o_terminal_consome_o_gesto_antes_de_tudo);
    RUN_TEST(test_o_terminal_so_fecha_pelo_botao_no_toque);
    RUN_TEST(test_gesto_nenhum_nao_faz_nada);
    return UNITY_END();
}
