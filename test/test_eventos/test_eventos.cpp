#include <unity.h>
#include "eventos.h"

void setUp(void) {}
void tearDown(void) {}

static Agent ag(const char *id, AgentState st) {
    Agent a;
    a.id = id;
    a.state = st;
    return a;
}

// O caso que o som existe para avisar: o turno acabou e a bola voltou.
void test_trabalhando_para_ocioso_e_pronto(void) {
    const Eventos e = eventosDoPoll({ag("a", AgentState::Working)},
                                    {ag("a", AgentState::Idle)});
    TEST_ASSERT_EQUAL(1, e.prontos);
    TEST_ASSERT_EQUAL(0, e.esperas);
    TEST_ASSERT_EQUAL(1, e.comemoracoes);
}

// Parar numa pergunta NAO e turno pronto: toca a espera. A turma comemora
// assim mesmo, que e o que ela sempre fez com esta borda.
void test_trabalhando_para_bloqueado_e_espera(void) {
    const Eventos e = eventosDoPoll({ag("a", AgentState::Working)},
                                    {ag("a", AgentState::Blocked)});
    TEST_ASSERT_EQUAL(0, e.prontos);
    TEST_ASSERT_EQUAL(1, e.esperas);
    TEST_ASSERT_EQUAL(1, e.comemoracoes);
}

// A espera vale de qualquer estado, e nao so do trabalho.
void test_ocioso_para_bloqueado_e_espera(void) {
    const Eventos e = eventosDoPoll({ag("a", AgentState::Idle)},
                                    {ag("a", AgentState::Blocked)});
    TEST_ASSERT_EQUAL(1, e.esperas);
    TEST_ASSERT_EQUAL(0, e.comemoracoes);
}

// Continuar no mesmo estado nao e evento: o som tocaria a cada 2 s.
void test_estado_parado_nao_e_evento(void) {
    const Eventos e = eventosDoPoll(
        {ag("a", AgentState::Blocked), ag("b", AgentState::Idle),
         ag("c", AgentState::Working)},
        {ag("a", AgentState::Blocked), ag("b", AgentState::Idle),
         ag("c", AgentState::Working)});
    TEST_ASSERT_EQUAL(0, e.prontos);
    TEST_ASSERT_EQUAL(0, e.esperas);
    TEST_ASSERT_EQUAL(0, e.comemoracoes);
}

// Sessao nova nao mudou de estado: no boot a lista inteira seria "nova", e o
// painel apitaria por tudo o que ja estava la.
void test_sessao_nova_nao_e_evento(void) {
    const Eventos e = eventosDoPoll({}, {ag("a", AgentState::Blocked),
                                         ag("b", AgentState::Idle)});
    TEST_ASSERT_EQUAL(0, e.prontos);
    TEST_ASSERT_EQUAL(0, e.esperas);
}

// Quem sumiu no mesmo poll nao tem estado a acompanhar.
void test_sessao_que_sumiu_nao_e_evento(void) {
    const Eventos e = eventosDoPoll({ag("a", AgentState::Working)}, {});
    TEST_ASSERT_EQUAL(0, e.prontos);
    TEST_ASSERT_EQUAL(0, e.comemoracoes);
}

// O casamento e pelo ID, e nao pela posicao: a lista vem ordenada por contexto
// e reordena sozinha entre dois polls.
void test_casa_por_id_mesmo_reordenada(void) {
    const Eventos e = eventosDoPoll(
        {ag("a", AgentState::Working), ag("b", AgentState::Idle)},
        {ag("b", AgentState::Idle), ag("a", AgentState::Idle)});
    TEST_ASSERT_EQUAL(1, e.prontos);
}

// Dois turnos terminando juntos sao duas comemoracoes, uma por sessao.
void test_conta_por_sessao(void) {
    const Eventos e = eventosDoPoll(
        {ag("a", AgentState::Working), ag("b", AgentState::Working),
         ag("c", AgentState::Idle)},
        {ag("a", AgentState::Idle), ag("b", AgentState::Unknown),
         ag("c", AgentState::Blocked)});
    TEST_ASSERT_EQUAL(2, e.prontos);
    TEST_ASSERT_EQUAL(1, e.esperas);
    TEST_ASSERT_EQUAL(2, e.comemoracoes);
}

// Voltar a trabalhar nao avisa nada: foi voce quem respondeu.
void test_voltar_a_trabalhar_nao_e_evento(void) {
    const Eventos e = eventosDoPoll({ag("a", AgentState::Blocked)},
                                    {ag("a", AgentState::Working)});
    TEST_ASSERT_EQUAL(0, e.prontos);
    TEST_ASSERT_EQUAL(0, e.esperas);
    TEST_ASSERT_EQUAL(0, e.comemoracoes);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_trabalhando_para_ocioso_e_pronto);
    RUN_TEST(test_trabalhando_para_bloqueado_e_espera);
    RUN_TEST(test_ocioso_para_bloqueado_e_espera);
    RUN_TEST(test_estado_parado_nao_e_evento);
    RUN_TEST(test_sessao_nova_nao_e_evento);
    RUN_TEST(test_sessao_que_sumiu_nao_e_evento);
    RUN_TEST(test_casa_por_id_mesmo_reordenada);
    RUN_TEST(test_conta_por_sessao);
    RUN_TEST(test_voltar_a_trabalhar_nao_e_evento);
    return UNITY_END();
}
