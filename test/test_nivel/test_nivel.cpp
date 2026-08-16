#include <unity.h>
#include "nivel.h"

// Os extremos, que e onde um off-by-one estraga tudo.
void test_sem_nada_e_nivel_um(void) {
    TEST_ASSERT_EQUAL_INT(1, nivel::calcular(0, 0.0f, 0.0f));
}

void test_xp_no_teto_e_nivel_99(void) {
    TEST_ASSERT_EQUAL_INT(99, nivel::calcular(10300, 0.0f, 0.0f));
}

// Sem o teto interno, XP muito acima empurraria o nivel para alem de 99 e o
// sprite correspondente nao existiria no cartao.
void test_xp_muito_acima_do_teto_continua_99(void) {
    TEST_ASSERT_EQUAL_INT(99, nivel::calcular(1000000, 999999.0f, 999999.0f));
}

// Numero negativo nao pode virar raiz de negativo nem nivel zero.
void test_entrada_negativa_nao_desce_do_um(void) {
    TEST_ASSERT_EQUAL_INT(1, nivel::calcular(-5, -100.0f, -100.0f));
}

// A ancora do desenho: 180 dias no ritmo medido tem que dar 99.
void test_ancora_dos_180_dias(void) {
    const float xp = 57.4f * 180.0f;
    TEST_ASSERT_EQUAL_INT(99, nivel::calcular(0, 0.0f, xp / nivel::PESO_HORA));
}

// Primeiro dia ja pula para 8: e o "hype" que o desenho pediu, e um erro no
// expoente apareceria aqui antes de qualquer outro lugar.
void test_primeiro_dia_cai_no_oito(void) {
    TEST_ASSERT_EQUAL_INT(8, nivel::calcular(19, 65.31f, 24.0f));
}

// Os tres pesam praticamente igual: um dia de contato tem que valer quase o
// mesmo que um dia de turnos, que quase o mesmo que um dia de custo. E a
// propriedade que justifica existirem tres contadores.
void test_os_tres_contadores_pesam_parecido(void) {
    const float so_turnos = nivel::xp(19, 0.0f, 0.0f);
    const float so_custo  = nivel::xp(0, 65.31f, 0.0f);
    const float so_horas  = nivel::xp(0, 0.0f, 24.0f);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, so_turnos, so_custo);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, so_turnos, so_horas);
}

// xpDoNivel tem que ser o inverso exato de calcular, senao a barra de progresso
// da quarta tela mostraria fracao errada — ou negativa.
void test_xp_do_nivel_e_o_inverso_de_calcular(void) {
    for (int n = 1; n <= 99; n++) {
        const float x = nivel::xpDoNivel(n);
        TEST_ASSERT_EQUAL_INT(n, nivel::calcular(0, 0.0f, x / nivel::PESO_HORA));
    }
}

// ---- A virada do dia, para o XP ganho hoje ----

void test_primeira_marca_comeca_o_dia_no_zero(void) {
    nivel::Estado e;
    float dia = -1.0f;
    // Marca pela primeira vez: nao ha como saber quanto ja se ganhou antes de a
    // marca existir, entao o dia comeca do zero em vez de um numero inventado.
    TEST_ASSERT_TRUE(nivel::virada(e, 1500.0f, "08/08", dia));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dia);
    TEST_ASSERT_EQUAL_FLOAT(1500.0f, e.xpNaVirada);
    TEST_ASSERT_EQUAL_STRING("08/08", e.dia);
}

void test_mesmo_dia_subtrai_e_nao_remarca(void) {
    nivel::Estado e;
    float dia = 0.0f;
    nivel::virada(e, 1500.0f, "08/08", dia);
    // Mais tarde no mesmo dia: so a subtracao, e a marca NAO se move — se ela
    // andasse, o dia zeraria a cada leitura e nunca acumularia nada.
    TEST_ASSERT_FALSE(nivel::virada(e, 1557.0f, "08/08", dia));
    TEST_ASSERT_EQUAL_FLOAT(57.0f, dia);
    TEST_ASSERT_EQUAL_FLOAT(1500.0f, e.xpNaVirada);
}

void test_dia_novo_remarca_e_zera(void) {
    nivel::Estado e;
    float dia = 0.0f;
    nivel::virada(e, 1500.0f, "08/08", dia);
    nivel::virada(e, 1557.0f, "08/08", dia);
    TEST_ASSERT_TRUE(nivel::virada(e, 1557.0f, "09/08", dia));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dia);
    TEST_ASSERT_EQUAL_FLOAT(1557.0f, e.xpNaVirada);
    TEST_ASSERT_EQUAL_STRING("09/08", e.dia);
}

void test_xp_menor_que_a_marca_nao_vira_negativo(void) {
    // A API perdeu historia (livro-caixa reconstruido menor, por exemplo). "0"
    // informa mais do que "-40 XP", que nao significa nada para quem olha.
    nivel::Estado e;
    float dia = 0.0f;
    nivel::virada(e, 1500.0f, "08/08", dia);
    nivel::virada(e, 1460.0f, "08/08", dia);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dia);
}

void test_sem_data_nao_marca_mas_ainda_calcula(void) {
    // API antiga ou sem relogio: marcar seria chutar de que dia e a marca. A
    // marca ANTERIOR continua servindo para a subtracao.
    nivel::Estado e;
    float dia = 0.0f;
    nivel::virada(e, 1500.0f, "08/08", dia);
    TEST_ASSERT_FALSE(nivel::virada(e, 1590.0f, "", dia));
    TEST_ASSERT_EQUAL_FLOAT(90.0f, dia);
    TEST_ASSERT_EQUAL_STRING("08/08", e.dia);
}

void test_sem_data_e_sem_marca_nao_inventa(void) {
    nivel::Estado e;
    float dia = -1.0f;
    TEST_ASSERT_FALSE(nivel::virada(e, 1500.0f, nullptr, dia));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dia);
    TEST_ASSERT_EQUAL_STRING("", e.dia);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_sem_nada_e_nivel_um);
    RUN_TEST(test_xp_no_teto_e_nivel_99);
    RUN_TEST(test_xp_muito_acima_do_teto_continua_99);
    RUN_TEST(test_entrada_negativa_nao_desce_do_um);
    RUN_TEST(test_ancora_dos_180_dias);
    RUN_TEST(test_primeiro_dia_cai_no_oito);
    RUN_TEST(test_os_tres_contadores_pesam_parecido);
    RUN_TEST(test_xp_do_nivel_e_o_inverso_de_calcular);
    RUN_TEST(test_primeira_marca_comeca_o_dia_no_zero);
    RUN_TEST(test_mesmo_dia_subtrai_e_nao_remarca);
    RUN_TEST(test_dia_novo_remarca_e_zera);
    RUN_TEST(test_xp_menor_que_a_marca_nao_vira_negativo);
    RUN_TEST(test_sem_data_nao_marca_mas_ainda_calcula);
    RUN_TEST(test_sem_data_e_sem_marca_nao_inventa);
    return UNITY_END();
}
