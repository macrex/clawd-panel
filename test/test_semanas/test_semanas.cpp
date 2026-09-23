#include <unity.h>
#include "semanas.h"

void setUp(void) {}
void tearDown(void) {}

// Os instantes da maquete, em epoch LOCAL (fuso ja aplicado).
static const long TER_22_09 = 1790105100L;   // terca 22/09/2026 19:25
static const long SAB_26_09 = 1790424000L;   // sabado 26/09/2026 12:00
static const long DOM_20_09 = 1789862400L;   // domingo 20/09/2026 00:00
static const long QUA_01_03 = 1835517600L;   // quarta 01/03/2028, ano bissexto

// Os 35 dias valendo o proprio indice: a soma diz quais entraram.
static Historico sequencia() {
    Historico h;
    h.known = true;
    for (int i = 0; i < HISTORICO_DIAS; i++) h.dias[i] = i;
    return h;
}

// ---- O dia de hoje ----

void test_dia_da_semana_comeca_no_domingo(void) {
    TEST_ASSERT_EQUAL_INT(2, diaDaSemana(TER_22_09));
    TEST_ASSERT_EQUAL_INT(6, diaDaSemana(SAB_26_09));
    TEST_ASSERT_EQUAL_INT(0, diaDaSemana(DOM_20_09));   // meia-noite em ponto
}

void test_placa_sem_hora_nao_sabe_o_dia(void) {
    // Um ESP32 sem sincronia liga em 1970: nada de calendario.
    TEST_ASSERT_EQUAL_INT(-1, diaDaSemana(0));
    TEST_ASSERT_EQUAL_INT(0, diaDoMes(0, 0));
}

void test_dia_do_mes_volta_atravessando_o_mes(void) {
    TEST_ASSERT_EQUAL_INT(22, diaDoMes(TER_22_09, 0));
    TEST_ASSERT_EQUAL_INT(23, diaDoMes(TER_22_09, 30));   // 23/08, a 1a celula
    TEST_ASSERT_EQUAL_INT(29, diaDoMes(QUA_01_03, 1));    // 29/02/2028
}

// ---- O mapeamento dia -> celula ----

void test_numa_terca_hoje_e_a_terceira_celula_da_ultima_linha(void) {
    // A maquete: terca 22 em (linha 4, coluna 2), e a celula 0 e 30 dias atras.
    TEST_ASSERT_EQUAL_INT(0, atrasDaCelula(30, 2));
    TEST_ASSERT_EQUAL_INT(30, atrasDaCelula(0, 2));
    // O que vem depois de hoje e futuro.
    TEST_ASSERT_TRUE(atrasDaCelula(31, 2) < 0);
    TEST_ASSERT_TRUE(atrasDaCelula(34, 2) < 0);
}

void test_so_no_sabado_os_35_dias_cabem(void) {
    TEST_ASSERT_EQUAL_INT(HISTORICO_DIAS - 1, atrasDaCelula(0, 6));
    TEST_ASSERT_EQUAL_INT(0, atrasDaCelula(34, 6));
}

void test_no_domingo_hoje_abre_a_ultima_linha(void) {
    TEST_ASSERT_EQUAL_INT(0, atrasDaCelula(28, 0));
    TEST_ASSERT_TRUE(atrasDaCelula(29, 0) < 0);
    TEST_ASSERT_EQUAL_INT(28, atrasDaCelula(0, 0));
}

// ---- As somas ----

void test_esta_semana_vai_de_domingo_ate_hoje(void) {
    const Historico h = sequencia();
    // Terca: domingo (32), segunda (33) e hoje (34).
    TEST_ASSERT_EQUAL_INT(32 + 33 + 34, somaDaSemana(h, 2, 0));
    // Domingo: so hoje.
    TEST_ASSERT_EQUAL_INT(34, somaDaSemana(h, 0, 0));
}

void test_semana_passada_e_domingo_a_sabado_inteiros(void) {
    const Historico h = sequencia();
    // Terca: o domingo passado e o indice 25, o sabado o 31.
    TEST_ASSERT_EQUAL_INT(25 + 26 + 27 + 28 + 29 + 30 + 31, somaDaSemana(h, 2, 1));
    // Sabado: a passada vai de 21 a 27.
    TEST_ASSERT_EQUAL_INT(21 + 22 + 23 + 24 + 25 + 26 + 27, somaDaSemana(h, 6, 1));
}

void test_sem_historico_ou_sem_dia_a_soma_e_zero(void) {
    Historico vazio;
    TEST_ASSERT_EQUAL_INT(0, somaDaSemana(vazio, 2, 0));
    TEST_ASSERT_EQUAL_INT(0, somaDaSemana(sequencia(), -1, 0));
}

// ---- A cor ----

void test_calor_vai_de_zero_a_cem_com_gama(void) {
    TEST_ASSERT_EQUAL_INT(0, calorDoDia(0, 308));
    TEST_ASSERT_EQUAL_INT(100, calorDoDia(308, 308));
    // Um decimo do maximo fica em ~20%, e nao nos 10% da escala linear.
    TEST_ASSERT_EQUAL_INT(19, calorDoDia(30, 300));
    // Mes todo zerado nao divide por zero.
    TEST_ASSERT_EQUAL_INT(0, calorDoDia(5, 0));
}

void test_maior_dia_e_a_escala(void) {
    TEST_ASSERT_EQUAL_INT(34, maiorDia(sequencia()));
    TEST_ASSERT_EQUAL_INT(0, maiorDia(Historico()));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_dia_da_semana_comeca_no_domingo);
    RUN_TEST(test_placa_sem_hora_nao_sabe_o_dia);
    RUN_TEST(test_dia_do_mes_volta_atravessando_o_mes);
    RUN_TEST(test_numa_terca_hoje_e_a_terceira_celula_da_ultima_linha);
    RUN_TEST(test_so_no_sabado_os_35_dias_cabem);
    RUN_TEST(test_no_domingo_hoje_abre_a_ultima_linha);
    RUN_TEST(test_esta_semana_vai_de_domingo_ate_hoje);
    RUN_TEST(test_semana_passada_e_domingo_a_sabado_inteiros);
    RUN_TEST(test_sem_historico_ou_sem_dia_a_soma_e_zero);
    RUN_TEST(test_calor_vai_de_zero_a_cem_com_gama);
    RUN_TEST(test_maior_dia_e_a_escala);
    return UNITY_END();
}
