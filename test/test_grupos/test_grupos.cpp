#include <unity.h>
#include "grupos.h"

void setUp(void) {}
void tearDown(void) {}

static Agent ag(const char *agente, const char *repo) {
    Agent a;
    a.agent = agente;
    a.repo  = repo;
    return a;
}

static std::vector<grupos::Plano> tresPlanos() {
    return {{"claude", "Max 5x"}, {"codex", "Free"}, {"agy", "AI Pro"}};
}

// UM provedor: nenhum cabecalho, e o plano vai para o rotulo do card. E o caso
// mais comum e o mais apertado — o card deitado cabe quatro sessoes.
void test_um_provedor_nao_gera_cabecalho(void) {
    std::vector<Agent> as = {ag("claude", "a"), ag("claude", "b")};
    const auto l = grupos::montarLista(as, tresPlanos());
    TEST_ASSERT_EQUAL_INT(2, (int)l.linhas.size());
    TEST_ASSERT_FALSE(l.linhas[0].cabecalho);
    TEST_ASSERT_FALSE(l.linhas[1].cabecalho);
    TEST_ASSERT_EQUAL_STRING("Max 5x", l.planoUnico.c_str());
}

// O `cli` e o nome CRU e minusculo, ao lado do `rotulo` de exibicao. Quem
// desenha escolhe o icone e a cor da marca por ele — casar pelo `rotulo`
// obrigaria a UI a desfazer a capitalizacao que `montarLista` acabou de fazer.
// Com grafia torta na entrada, ele sai normalizado do mesmo jeito.
void test_cabecalho_traz_o_nome_cru_da_cli(void) {
    std::vector<Agent> as = {ag("claude", "a"), ag("CoDeX", "b")};
    const auto l = grupos::montarLista(as, tresPlanos());
    TEST_ASSERT_EQUAL_STRING("claude", l.linhas[0].cli.c_str());
    TEST_ASSERT_EQUAL_STRING("CLAUDE", l.linhas[0].rotulo.c_str());
    TEST_ASSERT_EQUAL_STRING("codex", l.linhas[2].cli.c_str());
    TEST_ASSERT_EQUAL_STRING("CODEX", l.linhas[2].rotulo.c_str());
}

// Linha de agente nao e cabecalho: `cli` fica vazio. Sem isto, quem desenha
// poderia achar que da para pintar o icone na propria linha da sessao.
void test_linha_de_agente_nao_tem_cli(void) {
    std::vector<Agent> as = {ag("claude", "a"), ag("codex", "b")};
    const auto l = grupos::montarLista(as, tresPlanos());
    TEST_ASSERT_EQUAL_STRING("", l.linhas[1].cli.c_str());
}

void test_dois_provedores_geram_cabecalho(void) {
    std::vector<Agent> as = {ag("claude", "a"), ag("codex", "b")};
    const auto l = grupos::montarLista(as, tresPlanos());
    TEST_ASSERT_EQUAL_INT(4, (int)l.linhas.size());
    TEST_ASSERT_TRUE(l.linhas[0].cabecalho);
    TEST_ASSERT_EQUAL_STRING("CLAUDE", l.linhas[0].rotulo.c_str());
    TEST_ASSERT_EQUAL_STRING("Max 5x", l.linhas[0].plano.c_str());
    TEST_ASSERT_EQUAL_INT(1, l.linhas[0].contagem);
    TEST_ASSERT_FALSE(l.linhas[1].cabecalho);
    TEST_ASSERT_EQUAL_INT(0, l.linhas[1].agente);
    TEST_ASSERT_TRUE(l.linhas[2].cabecalho);
    TEST_ASSERT_EQUAL_STRING("CODEX", l.linhas[2].rotulo.c_str());
    TEST_ASSERT_EQUAL_INT(1, l.linhas[3].agente);
    // Com dois grupos cada um mostra o seu: o rotulo do card cala.
    TEST_ASSERT_EQUAL_STRING("", l.planoUnico.c_str());
}

// Claude primeiro mesmo quando nao e o primeiro da lista, e o resto em ordem
// alfabetica. Sem regra fixa a ordem dos grupos dancaria a cada poll, conforme
// quem estivesse mais urgente naquele instante.
void test_claude_primeiro_resto_alfabetico(void) {
    std::vector<Agent> as = {ag("codex", "a"), ag("agy", "b"),
                             ag("claude", "c")};
    const auto l = grupos::montarLista(as, tresPlanos());
    TEST_ASSERT_EQUAL_STRING("CLAUDE", l.linhas[0].rotulo.c_str());
    TEST_ASSERT_EQUAL_STRING("AGY", l.linhas[2].rotulo.c_str());
    TEST_ASSERT_EQUAL_STRING("CODEX", l.linhas[4].rotulo.c_str());
}

// A ordem DENTRO do grupo e a que a lista ja tinha (urgencia, decidida no
// servidor). Agrupar nao pode reordenar quem precisa de voce primeiro.
void test_ordem_dentro_do_grupo_e_preservada(void) {
    std::vector<Agent> as = {ag("claude", "urgente"), ag("codex", "x"),
                             ag("claude", "calmo")};
    const auto l = grupos::montarLista(as, tresPlanos());
    TEST_ASSERT_EQUAL_INT(0, l.linhas[1].agente);
    TEST_ASSERT_EQUAL_INT(2, l.linhas[2].agente);
}

// Provedor sem plano conhecido: o grupo existe, o rotulo do plano fica vazio, e
// quem desenha poe "—". Nunca inventar um plano.
void test_provedor_sem_plano_tem_cabecalho_vazio(void) {
    std::vector<Agent> as = {ag("claude", "a"), ag("cursor", "b")};
    const auto l = grupos::montarLista(as, {{"claude", "Max 5x"}});
    TEST_ASSERT_TRUE(l.linhas[2].cabecalho);
    TEST_ASSERT_EQUAL_STRING("CURSOR", l.linhas[2].rotulo.c_str());
    TEST_ASSERT_EQUAL_STRING("", l.linhas[2].plano.c_str());
}

// Um agente sem `agent` (API antiga) e Claude: era o comportamento anterior, e
// perder esse agente da lista seria uma regressao visivel.
void test_agente_sem_nome_conta_como_claude(void) {
    std::vector<Agent> as = {ag("", "a"), ag("claude", "b")};
    const auto l = grupos::montarLista(as, tresPlanos());
    TEST_ASSERT_EQUAL_INT(2, (int)l.linhas.size());
    TEST_ASSERT_EQUAL_STRING("Max 5x", l.planoUnico.c_str());
}

// A contagem do cabecalho e a do GRUPO, nao a da lista inteira. Com um agente
// por grupo os dois numeros coincidem e o erro passaria despercebido.
void test_contagem_do_cabecalho_e_a_do_grupo(void) {
    std::vector<Agent> as = {ag("claude", "a"), ag("codex", "b"),
                             ag("codex", "c"), ag("claude", "d")};
    const auto l = grupos::montarLista(as, tresPlanos());
    TEST_ASSERT_EQUAL_INT(2, l.linhas[0].contagem);   // CLAUDE
    TEST_ASSERT_EQUAL_INT(2, l.linhas[3].contagem);   // CODEX
}

// Duas grafias do mesmo CLI sao o MESMO grupo. Sem normalizar a chave sairiam
// dois cabecalhos com o mesmo rotulo na tela, e fora de ordem — maiuscula
// ordena antes em ASCII, entao o segundo CODEX cairia depois do AGY.
void test_grafia_diferente_e_o_mesmo_grupo(void) {
    std::vector<Agent> as = {ag("claude", "a"), ag("Codex", "b"),
                             ag("codex", "c")};
    const auto l = grupos::montarLista(as, tresPlanos());
    TEST_ASSERT_EQUAL_INT(5, (int)l.linhas.size());
    TEST_ASSERT_EQUAL_STRING("CODEX", l.linhas[2].rotulo.c_str());
    TEST_ASSERT_EQUAL_INT(2, l.linhas[2].contagem);
}

// A chave do dicionario de planos corre o mesmo risco de grafia que o nome do
// agente, e vem da mesma fonte. Um plano que nao casa por causa da caixa
// apagaria o rotulo do cabecalho sem dizer por que.
void test_plano_casa_com_a_chave_em_outra_caixa(void) {
    std::vector<Agent> as = {ag("claude", "a"), ag("codex", "b")};
    const auto l = grupos::montarLista(as, {{"Claude", "Max 5x"},
                                            {"CODEX", "Free"}});
    TEST_ASSERT_EQUAL_STRING("Max 5x", l.linhas[0].plano.c_str());
    TEST_ASSERT_EQUAL_STRING("Free", l.linhas[2].plano.c_str());
}

void test_lista_vazia_nao_gera_nada(void) {
    std::vector<Agent> as;
    const auto l = grupos::montarLista(as, tresPlanos());
    TEST_ASSERT_EQUAL_INT(0, (int)l.linhas.size());
    TEST_ASSERT_EQUAL_STRING("", l.planoUnico.c_str());
}

// ---- cabemLinhas ----

void test_cabem_todas(void) {
    std::vector<Agent> as = {ag("claude", "a"), ag("codex", "b")};
    const auto l = grupos::montarLista(as, tresPlanos());
    int fora = -1;
    // 2 cabecalhos (12) + 2 agentes (38) = 100
    TEST_ASSERT_EQUAL_INT(4, grupos::cabemLinhas(l.linhas, 100, 12, 38, fora));
    TEST_ASSERT_EQUAL_INT(0, fora);
}

// O `+N` conta AGENTES, nunca linhas: "+1" tem que significar "uma sessao que
// voce nao esta vendo", e nao "uma linha qualquer".
void test_fora_conta_agentes_e_nao_linhas(void) {
    std::vector<Agent> as = {ag("claude", "a"), ag("codex", "b"),
                             ag("codex", "c")};
    const auto l = grupos::montarLista(as, tresPlanos());
    int fora = -1;
    // cabe o cabecalho CLAUDE (12) + agente (38) + cabecalho CODEX (12) = 62
    const int n = grupos::cabemLinhas(l.linhas, 70, 12, 38, fora);
    TEST_ASSERT_EQUAL_INT(2, fora);
    TEST_ASSERT_EQUAL_INT(2, n);   // o cabecalho orfao do CODEX nao entra
}

// Um cabecalho sozinho, sem nenhum agente embaixo, parece defeito. Ele fica de
// fora e a altura dele volta para o rodape.
void test_cabecalho_orfao_nao_e_desenhado(void) {
    std::vector<Agent> as = {ag("claude", "a"), ag("codex", "b")};
    const auto l = grupos::montarLista(as, tresPlanos());
    int fora = -1;
    // 12 + 38 + 12 = 62 cabe, mas o agente do codex (38) nao: para em 2 linhas.
    TEST_ASSERT_EQUAL_INT(2, grupos::cabemLinhas(l.linhas, 62, 12, 38, fora));
    TEST_ASSERT_EQUAL_INT(1, fora);
}

// O caso REAL do card deitado: um provedor so, nenhum cabecalho, 156 px uteis
// e passo de 38. Quatro sessoes cabem e a quinta vira "+1" — que e exatamente o
// que a tela mostra hoje com `(h - 34) / passo`. Este teste e o que impede a
// troca daquela divisao por esta funcao de mexer no que ja esta na tela.
void test_card_deitado_cabe_quatro_e_a_quinta_vira_mais_um(void) {
    std::vector<Agent> as;
    for (int i = 0; i < 5; i++) as.push_back(ag("claude", "r"));
    const auto l = grupos::montarLista(as, tresPlanos());
    int fora = -1;
    TEST_ASSERT_EQUAL_INT(4, grupos::cabemLinhas(l.linhas, 156, 12, 38, fora));
    TEST_ASSERT_EQUAL_INT(1, fora);
}

void test_altura_zero_nao_cabe_nada(void) {
    std::vector<Agent> as = {ag("claude", "a")};
    const auto l = grupos::montarLista(as, tresPlanos());
    int fora = -1;
    TEST_ASSERT_EQUAL_INT(0, grupos::cabemLinhas(l.linhas, 0, 12, 38, fora));
    TEST_ASSERT_EQUAL_INT(1, fora);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_um_provedor_nao_gera_cabecalho);
    RUN_TEST(test_dois_provedores_geram_cabecalho);
    RUN_TEST(test_cabecalho_traz_o_nome_cru_da_cli);
    RUN_TEST(test_linha_de_agente_nao_tem_cli);
    RUN_TEST(test_claude_primeiro_resto_alfabetico);
    RUN_TEST(test_ordem_dentro_do_grupo_e_preservada);
    RUN_TEST(test_provedor_sem_plano_tem_cabecalho_vazio);
    RUN_TEST(test_agente_sem_nome_conta_como_claude);
    RUN_TEST(test_contagem_do_cabecalho_e_a_do_grupo);
    RUN_TEST(test_grafia_diferente_e_o_mesmo_grupo);
    RUN_TEST(test_plano_casa_com_a_chave_em_outra_caixa);
    RUN_TEST(test_lista_vazia_nao_gera_nada);
    RUN_TEST(test_cabem_todas);
    RUN_TEST(test_fora_conta_agentes_e_nao_linhas);
    RUN_TEST(test_cabecalho_orfao_nao_e_desenhado);
    RUN_TEST(test_card_deitado_cabe_quatro_e_a_quinta_vira_mais_um);
    RUN_TEST(test_altura_zero_nao_cabe_nada);
    return UNITY_END();
}
