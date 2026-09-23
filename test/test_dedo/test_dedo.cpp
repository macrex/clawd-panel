#include <unity.h>
#include "acao.h"

// O DEDO, simulado ponta a ponta.
//
// `test_gesture` prova o detector; `test_acao` prova a decisao. Este arquivo
// prova os DOIS JUNTOS, com a mesma sequencia de amostras que o `loop()`
// entrega: uma leitura a cada ~20 ms enquanto o dedo esta apoiado, e um release
// SEM posicao valida — que e como o driver desta placa se comporta.
//
// Ele existe porque a placa esta com o cartao travado em 0x107 e nao sobe, e
// tocar a tela com o dedo deixou de ser possivel. Mas o gesto e codigo
// deterministico: o que o dedo faria, uma sequencia de amostras faz igual — e
// faz para TODAS as paginas, todos os alvos e todas as cadencias, que nenhuma
// sessao de dedo cobriria.

void setUp(void) {}
void tearDown(void) {}

namespace {

// O relogio do laco, em ms. Anda como `millis()` andaria.
uint32_t g_agora = 1000;

// Um toque completo: o dedo desce em (x0,y0), arrasta ate (x1,y1) em
// `duracaoMs`, e levanta.
Gesture toque(GestureDetector &det, int x0, int y0, int x1, int y1,
              uint32_t duracaoMs) {
    const int PASSOS = 5;
    for (int i = 0; i <= PASSOS; i++) {
        det.update(true, x0 + (x1 - x0) * i / PASSOS,
                   y0 + (y1 - y0) * i / PASSOS, g_agora);
        g_agora += duracaoMs / PASSOS;
    }
    // O release NAO traz posicao: o driver devolve um ponto vazio, e quem
    // guarda a ultima valida e o detector. Passar (0,0) aqui e de proposito —
    // e o que fazia todo gesto horizontal parecer "para a esquerda" antes de
    // isso ser tratado.
    const Gesture g = det.update(false, 0, 0, g_agora);
    g_agora += 20;
    return g;
}

Gesture tap(GestureDetector &det, int x, int y) {
    return toque(det, x, y, x, y, 100);
}

// Espera o suficiente para a cadeia de duplo toque expirar.
void esperar(uint32_t ms) { g_agora += ms; }

Contexto base() {
    Contexto c;
    c.haveLast = true;
    return c;
}

}   // namespace

// ---- O sensor que pisca (medido na placa em 22/09) ----
// O dedo apoiado chega ao laco como "um ponto" e "nenhum ponto" alternados. O
// filtro fica entre o sensor e o detector, como no laco.

// Um dedo parado em (x,y) por `ms`, lido a cada 9 ms, com o sensor devolvendo
// "nenhum ponto" em leitura sim, leitura nao. Devolve a ULTIMA acao que saiu.
Acao dedoPiscando(GestureDetector &det, FiltroDeSoltura &f, const Contexto &c,
                  int x, int y, uint32_t ms, int &acoes) {
    Acao ultima = Acao::Nada;
    acoes = 0;
    for (uint32_t t = 0; t < ms; t += 9) {
        const bool leu = ((t / 9) % 2) == 0;
        const Leitura l = f.update(leu, leu ? x : 0, leu ? y : 0, g_agora);
        const Gesture g = det.update(l.pressed, l.x, l.y, g_agora);
        const Acao a = decidirGesto(g.kind, c).acao;
        if (a != Acao::Nada) { ultima = a; acoes++; }
        g_agora += 9;
    }
    // O dedo sai de verdade: o sensor para de ver.
    for (int i = 0; i < 20; i++) {
        const Leitura l = f.update(false, 0, 0, g_agora);
        const Gesture g = det.update(l.pressed, l.x, l.y, g_agora);
        const Acao a = decidirGesto(g.kind, c).acao;
        if (a != Acao::Nada) { ultima = a; acoes++; }
        g_agora += 9;
    }
    return ultima;
}

// O defeito da placa: na pagina de contexto (2) o dedo parado trocava de agente
// sem parar, e na do Clawd (3) trocava o bicho. Filtrado, dedo parado e so um
// toque longo — nao e toque, nao e duplo toque, nao faz nada.
void test_dedo_parado_com_sensor_piscando_nao_troca_nada(void) {
    Contexto c = base();
    for (int pg = 2; pg <= 3; pg++) {
        c.page = pg;
        GestureDetector det;
        FiltroDeSoltura f;
        int acoes = 0;
        dedoPiscando(det, f, c, 456, 225, 1500, acoes);
        TEST_ASSERT_EQUAL_INT(0, acoes);
    }
}

// E sem o filtro o mesmo dedo gera a enxurrada que a serial mostrou: o sensor
// direto no detector, como o laco fazia.
void test_sem_filtro_o_dedo_parado_vira_duplos_toques(void) {
    Contexto c = base();
    c.page = 2;
    GestureDetector det;
    int acoes = 0;
    Acao ultima = Acao::Nada;
    for (uint32_t t = 0; t < 600; t += 9) {
        const bool leu = ((t / 9) % 2) == 0;
        const Gesture g = det.update(leu, 456, 225, g_agora);
        const Acao a = decidirGesto(g.kind, c).acao;
        if (a != Acao::Nada) { ultima = a; acoes++; }
        g_agora += 9;
    }
    TEST_ASSERT_TRUE(acoes > 5);
    TEST_ASSERT_EQUAL(Acao::ProximoAgente, ultima);
}

// Um arrasto com o sensor falhando no meio continua sendo UM swipe.
void test_arrasto_com_falhas_ainda_e_swipe(void) {
    GestureDetector det;
    FiltroDeSoltura f;
    Contexto c = base();
    c.page = 3;
    Acao saiu = Acao::Nada;
    int n = 0;
    // 300 ms indo de x=420 a x=180, lido a cada 9 ms, com uma leitura em cada
    // tres vazia e um buraco de 50 ms no meio.
    for (uint32_t t = 0; t <= 300; t += 9, n++) {
        const bool buraco = t >= 120 && t < 170;
        const bool leu = !buraco && (n % 3) != 2;
        const int x = 420 - (int)(240 * t / 300);
        const Leitura l = f.update(leu, leu ? x : 0, leu ? 160 : 0, g_agora);
        const Gesture g = det.update(l.pressed, l.x, l.y, g_agora);
        if (g.kind != GestureKind::None) saiu = decidirGesto(g.kind, c).acao;
        g_agora += 9;
    }
    for (int i = 0; i < 20; i++) {
        const Leitura l = f.update(false, 0, 0, g_agora);
        const Gesture g = det.update(l.pressed, l.x, l.y, g_agora);
        if (g.kind != GestureKind::None) saiu = decidirGesto(g.kind, c).acao;
        g_agora += 9;
    }
    TEST_ASSERT_EQUAL(Acao::PaginaProxima, saiu);
}

// O duplo toque de verdade continua existindo: dois toques de 60 ms com
// 150 ms entre eles.
void test_duplo_toque_de_verdade_passa_pelo_filtro(void) {
    GestureDetector det;
    FiltroDeSoltura f;
    Contexto c = base();
    c.page = 3;
    Acao saiu = Acao::Nada;
    auto amostra = [&](bool dedo) {
        const Leitura l = f.update(dedo, dedo ? 240 : 0, dedo ? 160 : 0, g_agora);
        const Gesture g = det.update(l.pressed, l.x, l.y, g_agora);
        if (g.kind != GestureKind::None) saiu = decidirGesto(g.kind, c).acao;
        g_agora += 9;
    };
    for (int k = 0; k < 2; k++) {
        for (int i = 0; i < 7; i++) amostra(true);     // ~60 ms apoiado
        for (int i = 0; i < 17; i++) amostra(false);   // ~150 ms solto
    }
    TEST_ASSERT_EQUAL(Acao::TrocarTrabalho, saiu);
}

// ---- O que o dedo faz, virando acao ----

void test_arrastar_para_a_esquerda_avanca_a_pagina(void) {
    GestureDetector det;
    Contexto c = base();

    const Gesture g = toque(det, 300, 160, 150, 160, 300);
    TEST_ASSERT_EQUAL(GestureKind::SwipeLeft, g.kind);
    TEST_ASSERT_EQUAL(Acao::PaginaProxima, decidirGesto(g.kind, c).acao);
}

// A volta e circular: da primeira pagina, a direita leva a ultima.
void test_arrastar_para_a_direita_na_primeira_pagina_vai_para_a_ultima(void) {
    GestureDetector det;
    Contexto c = base();

    const Gesture g = toque(det, 150, 160, 300, 160, 300);
    TEST_ASSERT_EQUAL(GestureKind::SwipeRight, g.kind);
    TEST_ASSERT_EQUAL(Acao::PaginaAnterior, decidirGesto(g.kind, c).acao);
}

// O arrasto vertical: do cabecalho para baixo abre os ajustes; do miolo para
// cima entra na fila. Quem diz de onde o dedo saiu e a ORIGEM do gesto.
void test_arrasto_vertical_ajustes_e_fila(void) {
    GestureDetector det;
    Contexto c = base();

    Gesture g = toque(det, 240, 12, 240, 200, 300);
    TEST_ASSERT_EQUAL(GestureKind::SwipeDown, g.kind);
    c.inicioNoCabecalho = g.y0 < 42;
    TEST_ASSERT_EQUAL(Acao::AbrirAjustes, decidirGesto(g.kind, c).acao);

    g = toque(det, 240, 250, 240, 100, 300);
    TEST_ASSERT_EQUAL(GestureKind::SwipeUp, g.kind);
    c.inicioNoCabecalho = g.y0 < 42;
    TEST_ASSERT_EQUAL(Acao::AbrirFila, decidirGesto(g.kind, c).acao);
}

// O release sem posicao: o gesto tem que sair com a ULTIMA posicao apoiada, e
// nao com o (0,0) do driver. Sem isso o dx daria sempre negativo.
void test_o_release_sem_posicao_nao_inverte_o_sentido(void) {
    GestureDetector det;
    const Gesture g = toque(det, 100, 160, 260, 160, 300);
    TEST_ASSERT_EQUAL(GestureKind::SwipeRight, g.kind);
    TEST_ASSERT_EQUAL_INT(260, g.x);
    TEST_ASSERT_EQUAL_INT(160, g.y);
}

// ---- A cadencia dos toques ----

void test_dois_toques_rapidos_viram_duplo(void) {
    GestureDetector det;
    Contexto c = base();
    c.page = 3;                        // a pagina do Clawd troca o trabalhador

    TEST_ASSERT_EQUAL(GestureKind::Tap, tap(det, 160, 240).kind);
    const Gesture g = tap(det, 160, 240);
    TEST_ASSERT_EQUAL(GestureKind::DoubleTap, g.kind);
    TEST_ASSERT_EQUAL(Acao::TrocarTrabalho, decidirGesto(g.kind, c).acao);
}

void test_dois_toques_lentos_sao_dois_toques(void) {
    GestureDetector det;
    TEST_ASSERT_EQUAL(GestureKind::Tap, tap(det, 160, 240).kind);
    esperar(600);                      // acima do doubleTapMs padrao (400)
    TEST_ASSERT_EQUAL(GestureKind::Tap, tap(det, 160, 240).kind);
}

// O terceiro toque nao encadeia com o duplo que acabou de sair — senao um dedo
// nervoso viraria duplo toque a cada toque.
void test_o_terceiro_toque_recomeca_a_contagem(void) {
    GestureDetector det;
    TEST_ASSERT_EQUAL(GestureKind::Tap, tap(det, 160, 240).kind);
    TEST_ASSERT_EQUAL(GestureKind::DoubleTap, tap(det, 160, 240).kind);
    TEST_ASSERT_EQUAL(GestureKind::Tap, tap(det, 160, 240).kind);
}

// Um swipe no meio interrompe a cadeia: o toque seguinte e simples.
void test_um_arrasto_no_meio_quebra_o_duplo_toque(void) {
    GestureDetector det;
    TEST_ASSERT_EQUAL(GestureKind::Tap, tap(det, 160, 240).kind);
    TEST_ASSERT_EQUAL(GestureKind::SwipeLeft, toque(det, 300, 160, 150, 160, 300).kind);
    TEST_ASSERT_EQUAL(GestureKind::Tap, tap(det, 160, 240).kind);
}

// Arrasto diagonal: o EIXO MAIOR decide. Trocar de pagina quando se queria rolar
// e o erro que so aparece com o dedo real — na horizontal perfeita do teste
// escrito de proposito ele nunca acontece.
void test_o_arrasto_diagonal_escolhe_um_eixo_so(void) {
    GestureDetector det;
    Contexto c = base();
    c.modoTerminal = true;

    // Mais vertical que horizontal: rola o terminal, nao troca de pagina.
    const Gesture sobe = toque(det, 160, 300, 200, 140, 300);
    TEST_ASSERT_EQUAL(GestureKind::SwipeUp, sobe.kind);
    TEST_ASSERT_EQUAL(Acao::TerminalRolarCima, decidirGesto(sobe.kind, c).acao);

    // Mais horizontal que vertical: sai do terminal.
    const Gesture sai = toque(det, 140, 200, 300, 240, 300);
    TEST_ASSERT_EQUAL(GestureKind::SwipeRight, sai.kind);
    TEST_ASSERT_EQUAL(Acao::TerminalFechar, decidirGesto(sai.kind, c).acao);
}

// Arrasto curto demais nao e swipe nem tap: e o dedo escorregando.
void test_arrasto_curto_nao_e_gesto_nenhum(void) {
    GestureDetector det;
    const Gesture g = toque(det, 160, 240, 190, 250, 300);
    TEST_ASSERT_EQUAL(GestureKind::None, g.kind);
}

// Dedo apoiado por muito tempo tambem nao: sem isso, apoiar a mao na moldura
// viraria um toque quando ela saisse.
void test_dedo_apoiado_por_muito_tempo_nao_e_toque(void) {
    GestureDetector det;
    const Gesture g = toque(det, 160, 240, 160, 240, 1200);
    TEST_ASSERT_EQUAL(GestureKind::None, g.kind);
}

// ---- A cadeia inteira, alvo por alvo ----
//
// Cada um destes e um duplo toque real (duas amostras completas) caindo num
// alvo diferente, com o contexto que aquela tela teria.

void test_duplo_toque_no_cabecalho_gira_a_tela(void) {
    GestureDetector det;
    Contexto c = base();
    c.noIconeCabecalho = true;
    c.page = 3;                        // vale em QUALQUER pagina

    tap(det, 30, 20);
    const Gesture g = tap(det, 30, 20);
    TEST_ASSERT_EQUAL(GestureKind::DoubleTap, g.kind);
    TEST_ASSERT_EQUAL(Acao::GirarTela, decidirGesto(g.kind, c).acao);
}

void test_duplo_toque_dispensa_a_tela_do_clawd_dormindo(void) {
    GestureDetector det;
    Contexto c = base();
    c.clawdDorme = true;
    c.retrato = true;

    tap(det, 160, 300);
    const Gesture g = tap(det, 160, 300);
    TEST_ASSERT_EQUAL(Acao::DispensarOffline, decidirGesto(g.kind, c).acao);
}

// O defeito historico, com dedo: confirmar a limpeza e um duplo toque, e na
// pagina de contexto o duplo toque tambem significa "proximo agente".
void test_confirmar_a_limpeza_nao_troca_de_agente(void) {
    GestureDetector det;
    Contexto c = base();
    c.page = 2;              // contexto
    c.noBotaoLimpeza = true;

    tap(det, 250, 200);
    const Gesture g = tap(det, 250, 200);
    TEST_ASSERT_EQUAL(GestureKind::DoubleTap, g.kind);
    TEST_ASSERT_EQUAL(Acao::BotaoLimpeza, decidirGesto(g.kind, c).acao);

    // E fora do botao, deitado, o mesmo duplo toque avanca o agente.
    Contexto fora = base();
    fora.page = 2;
    esperar(600);
    tap(det, 100, 200);
    const Gesture g2 = tap(det, 100, 200);
    TEST_ASSERT_EQUAL(Acao::ProximoAgente, decidirGesto(g2.kind, fora).acao);
}

// Toque SIMPLES na lista seleciona; o duplo na mesma posicao nao seleciona de
// novo — ele cai no alvo do duplo toque daquela pagina.
void test_toque_simples_seleciona_e_o_duplo_faz_outra_coisa(void) {
    GestureDetector det;
    Contexto c = base();
    c.page = 2;              // contexto
    c.agenteIndex = 2;

    const Gesture um = tap(det, 400, 120);
    TEST_ASSERT_EQUAL(Acao::SelecionarAgente, decidirGesto(um.kind, c).acao);
    TEST_ASSERT_EQUAL_INT(2, decidirGesto(um.kind, c).n);

    const Gesture dois = tap(det, 400, 120);
    TEST_ASSERT_EQUAL(GestureKind::DoubleTap, dois.kind);
    TEST_ASSERT_EQUAL(Acao::ProximoAgente, decidirGesto(dois.kind, c).acao);
}

// A pergunta em tela cheia responde ao toque SIMPLES, e o alvo e a opcao.
void test_responder_a_pergunta_com_um_toque(void) {
    GestureDetector det;
    Contexto c = base();
    c.temBloqueio = true;
    c.opcaoP0 = 2;

    const Gesture g = tap(det, 160, 260);
    TEST_ASSERT_EQUAL(GestureKind::Tap, g.kind);
    const Decisao d = decidirGesto(g.kind, c);
    TEST_ASSERT_EQUAL(Acao::TocarOpcao, d.acao);
    TEST_ASSERT_EQUAL_INT(2, d.n);
}

// Navegar as quatro paginas com quatro arrastos, e voltar.
void test_atravessar_as_quatro_paginas_e_voltar(void) {
    GestureDetector det;
    Contexto c = base();

    for (int p = 0; p < 3; p++) {
        c.page = p;
        const Gesture g = toque(det, 300, 160, 150, 160, 300);
        TEST_ASSERT_EQUAL(Acao::PaginaProxima, decidirGesto(g.kind, c).acao);
    }
    // Da ultima, a esquerda da a volta.
    c.page = 3;
    TEST_ASSERT_EQUAL(Acao::PaginaProxima,
                      decidirGesto(toque(det, 300, 160, 150, 160, 300).kind, c).acao);

    for (int p = 3; p > 0; p--) {
        c.page = p;
        const Gesture g = toque(det, 150, 160, 300, 160, 300);
        TEST_ASSERT_EQUAL(Acao::PaginaAnterior, decidirGesto(g.kind, c).acao);
    }
    c.page = 0;
    TEST_ASSERT_EQUAL(Acao::PaginaAnterior,
                      decidirGesto(toque(det, 150, 160, 300, 160, 300).kind, c).acao);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_arrastar_para_a_esquerda_avanca_a_pagina);
    RUN_TEST(test_arrastar_para_a_direita_na_primeira_pagina_vai_para_a_ultima);
    RUN_TEST(test_arrasto_vertical_ajustes_e_fila);
    RUN_TEST(test_o_release_sem_posicao_nao_inverte_o_sentido);
    RUN_TEST(test_dois_toques_rapidos_viram_duplo);
    RUN_TEST(test_dois_toques_lentos_sao_dois_toques);
    RUN_TEST(test_o_terceiro_toque_recomeca_a_contagem);
    RUN_TEST(test_um_arrasto_no_meio_quebra_o_duplo_toque);
    RUN_TEST(test_o_arrasto_diagonal_escolhe_um_eixo_so);
    RUN_TEST(test_arrasto_curto_nao_e_gesto_nenhum);
    RUN_TEST(test_dedo_apoiado_por_muito_tempo_nao_e_toque);
    RUN_TEST(test_duplo_toque_no_cabecalho_gira_a_tela);
    RUN_TEST(test_duplo_toque_dispensa_a_tela_do_clawd_dormindo);
    RUN_TEST(test_confirmar_a_limpeza_nao_troca_de_agente);
    RUN_TEST(test_toque_simples_seleciona_e_o_duplo_faz_outra_coisa);
    RUN_TEST(test_responder_a_pergunta_com_um_toque);
    RUN_TEST(test_atravessar_as_quatro_paginas_e_voltar);
    RUN_TEST(test_dedo_parado_com_sensor_piscando_nao_troca_nada);
    RUN_TEST(test_sem_filtro_o_dedo_parado_vira_duplos_toques);
    RUN_TEST(test_arrasto_com_falhas_ainda_e_swipe);
    RUN_TEST(test_duplo_toque_de_verdade_passa_pelo_filtro);
    return UNITY_END();
}
