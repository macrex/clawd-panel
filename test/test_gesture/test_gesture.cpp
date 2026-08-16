#include <unity.h>
#include "gesture.h"

void setUp(void) {}
void tearDown(void) {}

// O driver de touch real devolve um ponto VAZIO quando o dedo levanta (x=0,
// y=0), nao a ultima posicao. Estes testes modelam isso: varias amostras com o
// dedo apoiado e um release sem posicao.

// ---- swipe ----

void test_arrastar_para_esquerda_gera_swipe_left(void) {
    GestureDetector d;
    TEST_ASSERT_TRUE(GestureKind::None == d.update(true,  300, 100, 0).kind);
    TEST_ASSERT_TRUE(GestureKind::None == d.update(true,  250, 100, 60).kind);
    TEST_ASSERT_TRUE(GestureKind::None == d.update(true,  200, 100, 120).kind);
    TEST_ASSERT_TRUE(GestureKind::SwipeLeft == d.update(false, 0, 0, 140).kind);
}

void test_arrastar_para_direita_gera_swipe_right(void) {
    GestureDetector d;
    d.update(true, 100, 100, 0);
    d.update(true, 250, 100, 100);
    TEST_ASSERT_TRUE(GestureKind::SwipeRight == d.update(false, 0, 0, 120).kind);
}

void test_release_ignora_a_posicao_recebida(void) {
    // Regressao: usar o x=0 do release faria todo gesto virar SwipeLeft.
    GestureDetector d;
    d.update(true, 50, 100, 0);
    d.update(true, 400, 100, 100);
    TEST_ASSERT_TRUE(GestureKind::SwipeRight == d.update(false, 0, 0, 120).kind);
}

void test_arrasto_lento_demais_nao_e_swipe(void) {
    GestureDetector d;
    d.update(true, 300, 100, 0);
    d.update(true, 100, 100, 1500);
    TEST_ASSERT_TRUE(GestureKind::None == d.update(false, 0, 0, 1600).kind);
}

// ---- toque simples ----

void test_toque_curto_e_um_tap_com_posicao(void) {
    GestureDetector d;
    d.update(true, 400, 120, 0);
    d.update(true, 402, 121, 40);
    Gesture g = d.update(false, 0, 0, 60);
    TEST_ASSERT_TRUE(GestureKind::Tap == g.kind);
    // A posicao reportada e a ultima VALIDA, para dar hit-test no menu.
    TEST_ASSERT_EQUAL_INT(402, g.x);
    TEST_ASSERT_EQUAL_INT(121, g.y);
}

void test_dedo_parado_por_muito_tempo_nao_e_tap(void) {
    // Apoiar o dedo na tela nao deve virar clique.
    GestureDetector d;
    d.update(true, 400, 120, 0);
    d.update(true, 400, 120, 900);
    TEST_ASSERT_TRUE(GestureKind::None == d.update(false, 0, 0, 950).kind);
}

void test_arrasto_medio_nao_e_tap_nem_swipe(void) {
    // 35px: passa da tolerancia do tap (20) e nao chega no swipe (60).
    GestureDetector d;
    d.update(true, 200, 100, 0);
    d.update(true, 235, 100, 80);
    TEST_ASSERT_TRUE(GestureKind::None == d.update(false, 0, 0, 100).kind);
}

// ---- toque duplo ----

void test_dois_toques_rapidos_geram_double_tap(void) {
    GestureDetector d;
    d.update(true, 200, 150, 0);
    TEST_ASSERT_TRUE(GestureKind::Tap == d.update(false, 0, 0, 50).kind);
    d.update(true, 205, 152, 200);
    TEST_ASSERT_TRUE(GestureKind::DoubleTap == d.update(false, 0, 0, 250).kind);
}

void test_dois_toques_lentos_sao_dois_taps(void) {
    GestureDetector d;
    d.update(true, 200, 150, 0);
    TEST_ASSERT_TRUE(GestureKind::Tap == d.update(false, 0, 0, 50).kind);
    d.update(true, 200, 150, 1500);
    TEST_ASSERT_TRUE(GestureKind::Tap == d.update(false, 0, 0, 1550).kind);
}

void test_tres_toques_rapidos_nao_encadeiam(void) {
    // Depois de um DoubleTap o contador zera: o terceiro toque recomeca como
    // Tap, em vez de virar um segundo DoubleTap.
    GestureDetector d;
    d.update(true, 200, 150, 0);
    d.update(false, 0, 0, 40);                                  // Tap
    d.update(true, 200, 150, 150);
    TEST_ASSERT_TRUE(GestureKind::DoubleTap == d.update(false, 0, 0, 190).kind);
    d.update(true, 200, 150, 300);
    TEST_ASSERT_TRUE(GestureKind::Tap == d.update(false, 0, 0, 340).kind);
}

void test_swipe_no_meio_nao_vira_double_tap(void) {
    // Tap, depois um swipe rapido: o swipe nao pode ser lido como o segundo
    // toque de um duplo.
    GestureDetector d;
    d.update(true, 200, 150, 0);
    TEST_ASSERT_TRUE(GestureKind::Tap == d.update(false, 0, 0, 40).kind);
    d.update(true, 300, 150, 100);
    d.update(true, 200, 150, 160);
    TEST_ASSERT_TRUE(GestureKind::SwipeLeft == d.update(false, 0, 0, 180).kind);
}

// ---- reporte unico ----

void test_gesto_e_reportado_uma_unica_vez(void) {
    GestureDetector d;
    d.update(true, 300, 100, 0);
    d.update(true, 200, 100, 100);
    TEST_ASSERT_TRUE(GestureKind::SwipeLeft == d.update(false, 0, 0, 120).kind);
    TEST_ASSERT_TRUE(GestureKind::None == d.update(false, 0, 0, 150).kind);
    TEST_ASSERT_TRUE(GestureKind::None == d.update(false, 0, 0, 200).kind);
}

// ---- verticais, para rolar a tela de terminal ----

void test_arrastar_para_cima_gera_swipe_up(void) {
    GestureDetector d;
    d.update(true, 200, 250, 0);
    d.update(true, 200, 100, 100);
    TEST_ASSERT_TRUE(GestureKind::SwipeUp == d.update(false, 0, 0, 120).kind);
}

void test_arrastar_para_baixo_gera_swipe_down(void) {
    GestureDetector d;
    d.update(true, 200, 60, 0);
    d.update(true, 200, 220, 100);
    TEST_ASSERT_TRUE(GestureKind::SwipeDown == d.update(false, 0, 0, 120).kind);
}

void test_o_eixo_maior_decide_o_gesto(void) {
    // Arrasto diagonal com mais deslocamento HORIZONTAL: e swipe horizontal,
    // mesmo o vertical tendo passado do limiar. Sem esta regra o gesto sairia
    // conforme a ordem dos `if`, e trocar de pagina quando se queria rolar so
    // apareceria com o dedo real.
    GestureDetector d;
    d.update(true, 300, 100, 0);
    d.update(true, 100, 190, 100);          // dx = -200, dy = +90
    TEST_ASSERT_TRUE(GestureKind::SwipeLeft == d.update(false, 0, 0, 120).kind);

    GestureDetector e;
    e.update(true, 300, 100, 0);
    e.update(true, 210, 300, 100);          // dx = -90, dy = +200
    TEST_ASSERT_TRUE(GestureKind::SwipeDown == e.update(false, 0, 0, 120).kind);
}

void test_arrasto_vertical_curto_nao_e_swipe(void) {
    GestureDetector d;
    d.update(true, 200, 100, 0);
    d.update(true, 200, 130, 100);          // 30 px: abaixo do minimo
    TEST_ASSERT_TRUE(GestureKind::None == d.update(false, 0, 0, 120).kind);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_arrastar_para_esquerda_gera_swipe_left);
    RUN_TEST(test_arrastar_para_cima_gera_swipe_up);
    RUN_TEST(test_arrastar_para_baixo_gera_swipe_down);
    RUN_TEST(test_o_eixo_maior_decide_o_gesto);
    RUN_TEST(test_arrasto_vertical_curto_nao_e_swipe);
    RUN_TEST(test_arrastar_para_direita_gera_swipe_right);
    RUN_TEST(test_release_ignora_a_posicao_recebida);
    RUN_TEST(test_arrasto_lento_demais_nao_e_swipe);
    RUN_TEST(test_toque_curto_e_um_tap_com_posicao);
    RUN_TEST(test_dedo_parado_por_muito_tempo_nao_e_tap);
    RUN_TEST(test_arrasto_medio_nao_e_tap_nem_swipe);
    RUN_TEST(test_dois_toques_rapidos_geram_double_tap);
    RUN_TEST(test_dois_toques_lentos_sao_dois_taps);
    RUN_TEST(test_tres_toques_rapidos_nao_encadeiam);
    RUN_TEST(test_swipe_no_meio_nao_vira_double_tap);
    RUN_TEST(test_gesto_e_reportado_uma_unica_vez);
    return UNITY_END();
}
