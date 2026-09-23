#include <unity.h>
#include "previsao.h"
#include "relogio.h"

void setUp(void) {}
void tearDown(void) {}

static const long JANELA_5H = 5 * 3600;
static const long JANELA_7D = 7 * 24 * 3600;

static Metric janela(int pct, int resetsIn) {
    Metric m;
    m.known    = true;
    m.pct      = pct;
    m.resetsIn = resetsIn;
    return m;
}

static Clock relogio(const char *hm, const char *dia) {
    Clock c;
    c.known   = true;
    c.hm      = hm;
    c.weekday = dia;
    return c;
}

// ---- A previsao ----

// A sessao da maquete: 41% com 57% da janela corrida fecha em 72%.
void test_abaixo_do_ritmo_fecha_antes_de_100(void) {
    const Previsao p = preverJanela(janela(41, 7800), JANELA_5H);
    TEST_ASSERT_EQUAL_INT(Previsao::Fecha, p.tipo);
    TEST_ASSERT_EQUAL_INT(72, p.pctFinal);
}

// A semana da maquete: 52% com 41% corrido estoura antes da virada, e o
// instante sai em segundos daqui — terca 19:25 mais isso e sexta de manha.
void test_acima_do_ritmo_diz_quando_estoura(void) {
    const long correu = JANELA_7D * 41 / 100;
    const Previsao p = preverJanela(janela(52, (int)(JANELA_7D - correu)), JANELA_7D);
    TEST_ASSERT_EQUAL_INT(Previsao::Estoura, p.tipo);
    TEST_ASSERT_EQUAL_INT(48L * correu / 52, p.seg100);
    TEST_ASSERT_TRUE(p.seg100 < JANELA_7D - correu);   // antes da virada
    TEST_ASSERT_EQUAL_STRING("Sex", diaDaquiA(relogio("19:25", "TER"), p.seg100).c_str());
}

// Exatamente no ritmo a janela fecha em 100 no instante da virada: estoura,
// com o instante caindo na propria virada.
void test_no_ritmo_exato_estoura_na_virada(void) {
    const Previsao p = preverJanela(janela(50, JANELA_5H / 2), JANELA_5H);
    TEST_ASSERT_EQUAL_INT(Previsao::Estoura, p.tipo);
    TEST_ASSERT_EQUAL_INT(JANELA_5H / 2, p.seg100);
}

// Sem base, sem previsao: cada um desses projetaria um numero inventado.
void test_sem_base_nao_ha_previsao(void) {
    Metric desconhecida = janela(40, 9000);
    desconhecida.known = false;
    Metric lembranca = janela(40, 9000);
    lembranca.memoria = true;

    TEST_ASSERT_EQUAL_INT(Previsao::Nenhuma, preverJanela(desconhecida, JANELA_5H).tipo);
    TEST_ASSERT_EQUAL_INT(Previsao::Nenhuma, preverJanela(lembranca, JANELA_5H).tipo);
    // Gasto zero nao tem velocidade; 100% ja estourou.
    TEST_ASSERT_EQUAL_INT(Previsao::Nenhuma, preverJanela(janela(0, 9000), JANELA_5H).tipo);
    TEST_ASSERT_EQUAL_INT(Previsao::Nenhuma, preverJanela(janela(100, 9000), JANELA_5H).tipo);
    // Sem prazo nao se sabe quanto correu.
    TEST_ASSERT_EQUAL_INT(Previsao::Nenhuma, preverJanela(janela(40, 0), JANELA_5H).tipo);
    // A API arredonda o prazo para cima e pode passar da janela.
    TEST_ASSERT_EQUAL_INT(Previsao::Nenhuma, preverJanela(janela(40, JANELA_5H + 10), JANELA_5H).tipo);
}

// A janela recem-aberta: abaixo de PREVISAO_BASE_PCT corrido nao sai nada, e
// no limite ja sai.
void test_janela_recem_aberta_espera_a_base(void) {
    const long base = JANELA_5H * PREVISAO_BASE_PCT / 100;       // 30 min
    TEST_ASSERT_EQUAL_INT(Previsao::Nenhuma,
                          preverJanela(janela(5, (int)(JANELA_5H - base + 1)), JANELA_5H).tipo);
    TEST_ASSERT_NOT_EQUAL(Previsao::Nenhuma,
                          preverJanela(janela(5, (int)(JANELA_5H - base)), JANELA_5H).tipo);
}

// ---- O dia da semana daqui a N segundos ----

void test_dia_na_mesma_semana_e_na_virada(void) {
    const Clock ter = relogio("19:25", "TER");
    TEST_ASSERT_EQUAL_STRING("Ter", diaDaquiA(ter, 3600).c_str());
    TEST_ASSERT_EQUAL_STRING("Qua", diaDaquiA(ter, 5 * 3600).c_str());
    // Sabado mais dois dias da a volta na semana.
    TEST_ASSERT_EQUAL_STRING("Seg", diaDaquiA(relogio("10:00", "SAB"), 2 * 86400).c_str());
}

// 23:58 arredonda para 0h, e o dia acompanha a hora escrita ao lado.
void test_dia_arredonda_como_a_hora(void) {
    TEST_ASSERT_EQUAL_STRING("Qua", diaDaquiA(relogio("23:58", "TER"), 0).c_str());
    TEST_ASSERT_EQUAL_STRING("Ter", diaDaquiA(relogio("23:57", "TER"), 0).c_str());
}

void test_dia_sem_relogio_e_vazio(void) {
    Clock c = relogio("19:25", "TER");
    c.known = false;
    TEST_ASSERT_EQUAL_STRING("", diaDaquiA(c, 60).c_str());
    TEST_ASSERT_EQUAL_STRING("", diaDaquiA(relogio("19:25", "???"), 60).c_str());
    TEST_ASSERT_EQUAL_STRING("", diaDaquiA(relogio("", "TER"), 60).c_str());
}

// O texto da tela, com os numeros da maquete: a sessao fecha em 72%, e a
// semana adiantada bate 100% na sexta, com dia e hora da MESMA conta.
void test_o_texto_da_previsao(void) {
    const Clock ter = relogio("19:25", "TER");
    bool perigo = true;
    TEST_ASSERT_EQUAL_STRING("fecha 72%",
        textoDaPrevisao(janela(41, 7800), JANELA_5H, ter, perigo).c_str());
    TEST_ASSERT_FALSE(perigo);

    TEST_ASSERT_EQUAL_STRING("100% Sex 11:05h",
        textoDaPrevisao(janela(52, 99 * 3600), JANELA_7D, ter, perigo).c_str());
    TEST_ASSERT_TRUE(perigo);

    // Na janela de 5h nao ha dia: ela nunca passa da manha seguinte.
    TEST_ASSERT_EQUAL_STRING("100% 20:45h",
        textoDaPrevisao(janela(60, 3 * 3600), JANELA_5H, ter, perigo).c_str());

    // Sem base, nada.
    TEST_ASSERT_EQUAL_STRING("",
        textoDaPrevisao(janela(0, 7800), JANELA_5H, ter, perigo).c_str());
    TEST_ASSERT_FALSE(perigo);
}

// A hora de um instante futuro, a conta que o reset e a previsao dividem.
void test_hora_daqui_a(void) {
    const Clock ter = relogio("19:25", "TER");
    TEST_ASSERT_EQUAL_STRING("21:35h", horaDaquiA(ter, 2 * 3600 + 10 * 60).c_str());
    TEST_ASSERT_EQUAL_STRING("20h", horaDaquiA(ter, 35 * 60).c_str());
    // Atravessa a meia-noite sem sair do relogio de 24h.
    TEST_ASSERT_EQUAL_STRING("01:30h", horaDaquiA(ter, 6 * 3600 + 5 * 60).c_str());
    Clock sem = ter;
    sem.known = false;
    TEST_ASSERT_EQUAL_STRING("", horaDaquiA(sem, 60).c_str());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_abaixo_do_ritmo_fecha_antes_de_100);
    RUN_TEST(test_acima_do_ritmo_diz_quando_estoura);
    RUN_TEST(test_no_ritmo_exato_estoura_na_virada);
    RUN_TEST(test_sem_base_nao_ha_previsao);
    RUN_TEST(test_janela_recem_aberta_espera_a_base);
    RUN_TEST(test_dia_na_mesma_semana_e_na_virada);
    RUN_TEST(test_dia_arredonda_como_a_hora);
    RUN_TEST(test_dia_sem_relogio_e_vazio);
    RUN_TEST(test_o_texto_da_previsao);
    RUN_TEST(test_hora_daqui_a);
    return UNITY_END();
}
