#include <unity.h>
#include "limpeza.h"

void setUp(void) {}
void tearDown(void) {}

// A API apagada nao varre, e o relogio nem comeca a contar.
void test_apagada_nao_varre(void) {
    LimpezaWatch w;
    TEST_ASSERT_FALSE(varrendo(w, false, 1000));
    TEST_ASSERT_EQUAL_UINT32(0, w.desdeMs);
}

void test_acende_e_varre(void) {
    LimpezaWatch w;
    TEST_ASSERT_TRUE(varrendo(w, true, 1000));
    TEST_ASSERT_NOT_EQUAL(0, w.desdeMs);
}

// Dentro do prazo a placa NAO discute com a API: se ela diz que esta limpando,
// esta limpando.
void test_dentro_do_prazo_continua_varrendo(void) {
    LimpezaWatch w;
    varrendo(w, true, 1000);
    TEST_ASSERT_TRUE(varrendo(w, true, 1000 + LIMPEZA_TETO_MS - 1));
}

// O caso que motivou tudo: o servidor morre no meio de um compact. `cleaning`
// congela aceso no ultimo status bom e a turma varre para sempre.
void test_alem_do_teto_para_de_varrer(void) {
    LimpezaWatch w;
    varrendo(w, true, 1000);
    TEST_ASSERT_FALSE(varrendo(w, true, 1000 + LIMPEZA_TETO_MS));
    // E continua parada: o teto nao pisca.
    TEST_ASSERT_FALSE(varrendo(w, true, 1000 + LIMPEZA_TETO_MS * 3));
}

// Limpeza nova depois de uma que estourou o teto: a segunda tem prazo proprio.
// Sem isto, uma sessao travada de manha deixaria o painel surdo o dia inteiro.
void test_apaga_e_acende_de_novo_ganha_prazo_novo(void) {
    LimpezaWatch w;
    varrendo(w, true, 1000);
    TEST_ASSERT_FALSE(varrendo(w, true, 1000 + LIMPEZA_TETO_MS));

    TEST_ASSERT_FALSE(varrendo(w, false, 500000));       // a API apagou
    TEST_ASSERT_TRUE(varrendo(w, true, 501000));         // e acendeu outra vez
    TEST_ASSERT_TRUE(varrendo(w, true, 501000 + LIMPEZA_TETO_MS - 1));
}

// O teto daqui e MAIOR que o do servidor (LIMPEZA_MAX_S = 240 s em
// claude_metrics_api.py). Com o servidor vivo quem apaga e ele, e a placa nunca
// chega a opinar; este teto so existe para o servidor MORTO. Invertida, a ordem
// faria a placa contradizer uma API que esta certa.
void test_teto_local_e_mais_folgado_que_o_do_servidor(void) {
    TEST_ASSERT_GREATER_THAN_UINT32(240u * 1000u, LIMPEZA_TETO_MS);
}

// `millis()` da a volta em 49 dias. A conta e feita em aritmetica sem sinal, que
// atravessa a virada; um `agora - desde` com sinal daria um numero negativo
// gigante e apagaria a varredura na hora.
void test_a_virada_do_millis_nao_apaga_cedo(void) {
    LimpezaWatch w;
    const uint32_t quaseOFim = 0xFFFFFF00u;
    TEST_ASSERT_TRUE(varrendo(w, true, quaseOFim));
    // 1000 ms depois, ja do outro lado da virada.
    TEST_ASSERT_TRUE(varrendo(w, true, quaseOFim + 1000));
    TEST_ASSERT_FALSE(varrendo(w, true, quaseOFim + LIMPEZA_TETO_MS));
}

// Acender exatamente em millis()==0 acontece uma vez por boot e dura 1 ms. O
// zero e a sentinela de "apagada", entao ele nao pode virar estado guardado.
void test_acender_no_instante_zero(void) {
    LimpezaWatch w;
    TEST_ASSERT_TRUE(varrendo(w, true, 0));
    TEST_ASSERT_TRUE(varrendo(w, true, LIMPEZA_TETO_MS - 2));
    TEST_ASSERT_FALSE(varrendo(w, true, LIMPEZA_TETO_MS + 1));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_apagada_nao_varre);
    RUN_TEST(test_acende_e_varre);
    RUN_TEST(test_dentro_do_prazo_continua_varrendo);
    RUN_TEST(test_alem_do_teto_para_de_varrer);
    RUN_TEST(test_apaga_e_acende_de_novo_ganha_prazo_novo);
    RUN_TEST(test_teto_local_e_mais_folgado_que_o_do_servidor);
    RUN_TEST(test_a_virada_do_millis_nao_apaga_cedo);
    RUN_TEST(test_acender_no_instante_zero);
    return UNITY_END();
}
