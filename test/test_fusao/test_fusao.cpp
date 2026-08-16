#include <unity.h>
#include "fusao.h"

void setUp(void) {}
void tearDown(void) {}

static Agent ag(const char *id, AgentState st, int ctx) {
    Agent a;
    a.id = id;
    a.state = st;
    a.hasContext = ctx >= 0;
    a.contextPct = ctx >= 0 ? ctx : 0;
    return a;
}

void test_fusao_concatena_e_carimba_origem(void) {
    Status base, pc2;
    base.agents.push_back(ag("m1", AgentState::Working, 62));
    pc2.agents.push_back(ag("s1", AgentState::Working, 30));
    fundirAgentes(base, pc2);
    TEST_ASSERT_EQUAL_INT(2, (int)base.agents.size());
    // Working 62 vem antes de Working 30; o local continua 0 e o do PC2 vira 1.
    TEST_ASSERT_EQUAL_STRING("m1", base.agents[0].id.c_str());
    TEST_ASSERT_EQUAL_INT(0, base.agents[0].origem);
    TEST_ASSERT_EQUAL_STRING("s1", base.agents[1].id.c_str());
    TEST_ASSERT_EQUAL_INT(1, base.agents[1].origem);
}

void test_bloqueado_do_pc2_vai_para_o_topo(void) {
    // A origem NAO reordena: a regra global (blocked primeiro) vale para a
    // lista fundida, e um bloqueado remoto passa na frente de um working local.
    Status base, pc2;
    base.agents.push_back(ag("m1", AgentState::Working, 90));
    pc2.agents.push_back(ag("s1", AgentState::Blocked, 10));
    fundirAgentes(base, pc2);
    TEST_ASSERT_EQUAL_STRING("s1", base.agents[0].id.c_str());
}

void test_sem_contexto_vai_para_o_fim_do_estado(void) {
    Status base, pc2;
    base.agents.push_back(ag("m1", AgentState::Working, -1));   // nao reporta
    pc2.agents.push_back(ag("s1", AgentState::Working, 5));
    fundirAgentes(base, pc2);
    TEST_ASSERT_EQUAL_STRING("s1", base.agents[0].id.c_str());
    TEST_ASSERT_EQUAL_STRING("m1", base.agents[1].id.c_str());
}

void test_bloqueio_do_master_vence(void) {
    Status base, pc2;
    base.bloqueio.known = true;
    base.bloqueio.paneId = "w0:m";
    pc2.bloqueio.known = true;
    pc2.bloqueio.paneId = "w0:s";
    fundirAgentes(base, pc2);
    TEST_ASSERT_EQUAL_STRING("w0:m", base.bloqueio.paneId.c_str());
    TEST_ASSERT_EQUAL_INT(0, base.bloqueio.origem);
}

void test_bloqueio_do_pc2_quando_master_nao_tem(void) {
    Status base, pc2;
    pc2.bloqueio.known = true;
    pc2.bloqueio.paneId = "w0:s";
    fundirAgentes(base, pc2);
    TEST_ASSERT_TRUE(base.bloqueio.known);
    TEST_ASSERT_EQUAL_STRING("w0:s", base.bloqueio.paneId.c_str());
    TEST_ASSERT_EQUAL_INT(1, base.bloqueio.origem);
}

void test_marcar_origem_carimba_tudo(void) {
    // Fallback: a resposta inteira veio do PC2 e todo mundo e origem 1.
    Status s;
    s.agents.push_back(ag("a", AgentState::Working, 10));
    s.agents.push_back(ag("b", AgentState::Idle, -1));
    s.bloqueio.known = true;
    marcarOrigem(s, 1);
    TEST_ASSERT_EQUAL_INT(1, s.agents[0].origem);
    TEST_ASSERT_EQUAL_INT(1, s.agents[1].origem);
    TEST_ASSERT_EQUAL_INT(1, s.bloqueio.origem);
}

void test_captura_do_pc2_atravessa_a_fusao(void) {
    // O Status do PC2 e DESCARTADO depois da fusao: campo de topo que ninguem
    // copia morre aqui. Sem esta copia, uma foto pedida na segunda maquina
    // nunca chegaria a placa enquanto o master estivesse saudavel.
    Status base, pc2;
    pc2.captura.known = true;
    pc2.captura.id    = 1785439580;
    fundirAgentes(base, pc2);
    TEST_ASSERT_TRUE(base.captura.known);
    TEST_ASSERT_EQUAL_INT(1, base.captura.origem);
}

void test_captura_do_master_vence(void) {
    // Duas maquinas pedindo na mesma volta: atende-se o master agora, e o PC2
    // no proximo poll — dois segundos depois, dentro do prazo de 30 s do pedido.
    Status base, pc2;
    base.captura.known = true;
    base.captura.id    = 100;
    pc2.captura.known  = true;
    pc2.captura.id     = 200;
    fundirAgentes(base, pc2);
    TEST_ASSERT_EQUAL_INT(100, (int)base.captura.id);
    TEST_ASSERT_EQUAL_INT(0, base.captura.origem);
}

void test_marcar_origem_carimba_a_captura(void) {
    // A volta inteira veio do PC2 (o master falhou). A foto tem que voltar para
    // quem a pediu — o master nao a espera.
    Status s;
    s.captura.known = true;
    s.captura.id    = 7;
    marcarOrigem(s, 1);
    TEST_ASSERT_EQUAL_INT(1, s.captura.origem);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_fusao_concatena_e_carimba_origem);
    RUN_TEST(test_bloqueado_do_pc2_vai_para_o_topo);
    RUN_TEST(test_sem_contexto_vai_para_o_fim_do_estado);
    RUN_TEST(test_bloqueio_do_master_vence);
    RUN_TEST(test_bloqueio_do_pc2_quando_master_nao_tem);
    RUN_TEST(test_marcar_origem_carimba_tudo);
    RUN_TEST(test_captura_do_pc2_atravessa_a_fusao);
    RUN_TEST(test_captura_do_master_vence);
    RUN_TEST(test_marcar_origem_carimba_a_captura);
    return UNITY_END();
}
