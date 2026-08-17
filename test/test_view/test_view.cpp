#include <unity.h>
#include "view_model.h"

void setUp(void) {}
void tearDown(void) {}

void test_idade_em_segundos(void) {
    TEST_ASSERT_EQUAL_STRING("agora", formatAge(0).c_str());
    TEST_ASSERT_EQUAL_STRING("ha 2s", formatAge(2).c_str());
    TEST_ASSERT_EQUAL_STRING("ha 59s", formatAge(59).c_str());
}

void test_idade_em_minutos_e_horas(void) {
    TEST_ASSERT_EQUAL_STRING("ha 1m", formatAge(60).c_str());
    TEST_ASSERT_EQUAL_STRING("ha 59m", formatAge(3599).c_str());
    TEST_ASSERT_EQUAL_STRING("ha 1h", formatAge(3600).c_str());
    TEST_ASSERT_EQUAL_STRING("ha 5h", formatAge(3600 * 5 + 61).c_str());
}

void test_percentual_conhecido_e_desconhecido(void) {
    Metric m; m.pct = 26; m.known = true;
    TEST_ASSERT_EQUAL_STRING("26%", pctText(m).c_str());
    m.known = false;
    TEST_ASSERT_EQUAL_STRING("-", pctText(m).c_str());
}

void test_pior_nivel_governa_o_pill(void) {
    Status s;
    s.context.known = s.session.known = s.week.known = true;
    s.context.level = Level::Green;
    s.session.level = Level::Yellow;
    s.week.level    = Level::Red;
    TEST_ASSERT_TRUE(Level::Red == worstLevel(s));

    s.week.level = Level::Green;
    TEST_ASSERT_TRUE(Level::Yellow == worstLevel(s));
}

void test_metrica_desconhecida_nao_conta_para_o_pill(void) {
    // Sem limits_fresh, session/week nao podem pintar o pill de vermelho.
    Status s;
    s.context.known = true;  s.context.level = Level::Green;
    s.session.known = false; s.session.level = Level::Red;
    s.week.known    = false; s.week.level    = Level::Red;
    TEST_ASSERT_TRUE(Level::Green == worstLevel(s));
}

void test_largura_da_barra(void) {
    TEST_ASSERT_EQUAL_INT(0,   barWidth(0, 200));
    TEST_ASSERT_EQUAL_INT(100, barWidth(50, 200));
    TEST_ASSERT_EQUAL_INT(200, barWidth(100, 200));
}

void test_largura_da_barra_satura(void) {
    // A API nunca deveria mandar isso, mas a tela nao pode estourar.
    TEST_ASSERT_EQUAL_INT(200, barWidth(150, 200));
    TEST_ASSERT_EQUAL_INT(0,   barWidth(-5, 200));
}

static Agent ag(const char *repo, AgentState st) {
    Agent a; a.repo = repo; a.state = st; return a;
}

void test_bloqueado_governa_a_tela_inteira(void) {
    // Nove trabalhando e um travado: quem manda no painel e o travado. E o
    // unico dos dois estados que pede uma acao sua.
    Status s;
    for (int i = 0; i < 9; i++) s.agents.push_back(ag("x", AgentState::Working));
    s.agents.push_back(ag("outro-repo", AgentState::Blocked));
    TEST_ASSERT_TRUE(AgentState::Blocked == overallState(s));
    TEST_ASSERT_EQUAL_STRING("outro-repo", overallAgent(s).c_str());
}

void test_ordem_de_urgencia(void) {
    Status s;
    s.agents.push_back(ag("a", AgentState::Unknown));
    TEST_ASSERT_TRUE(AgentState::Unknown == overallState(s));
    s.agents.push_back(ag("b", AgentState::Idle));
    TEST_ASSERT_TRUE(AgentState::Idle == overallState(s));
    s.agents.push_back(ag("c", AgentState::Working));
    TEST_ASSERT_TRUE(AgentState::Working == overallState(s));
}

void test_sem_agentes_nao_inventa_estado(void) {
    Status s;
    TEST_ASSERT_TRUE(AgentState::Unknown == overallState(s));
    TEST_ASSERT_EQUAL_STRING("", overallAgent(s).c_str());
}

static Agent comEstado(const char *repo, AgentState st) {
    Agent a; a.repo = repo; a.state = st; return a;
}

void test_indice_do_agente_agregado_aponta_para_o_mesmo_nome(void) {
    // A pagina do Clawd escreve o NOME de um agente e, embaixo, os NUMEROS
    // dele. Se as duas coisas saissem de buscas separadas, um dia a tela
    // mostraria o custo de uma sessao debaixo do nome de outra.
    Status s;
    s.agents.push_back(comEstado("pc", AgentState::Idle));
    s.agents.push_back(comEstado("esp32-s3", AgentState::Blocked));
    s.agents.push_back(comEstado("seeu", AgentState::Working));
    const int i = overallIndex(s);
    TEST_ASSERT_EQUAL_INT(1, i);
    TEST_ASSERT_EQUAL_STRING(s.agents[i].repo.c_str(), overallAgent(s).c_str());
}

void test_sem_agentes_o_indice_e_invalido(void) {
    // -1 e nao 0: com a lista vazia, 0 seria um indice para dentro do nada.
    Status s;
    TEST_ASSERT_EQUAL_INT(-1, overallIndex(s));
}

void test_tempo_de_api_em_segundos_minutos_e_horas(void) {
    TEST_ASSERT_EQUAL_STRING("0s", formatApiTime(0).c_str());
    TEST_ASSERT_EQUAL_STRING("45s", formatApiTime(45999).c_str());
    TEST_ASSERT_EQUAL_STRING("1min", formatApiTime(60000).c_str());
    TEST_ASSERT_EQUAL_STRING("59min", formatApiTime(3599000).c_str());
    TEST_ASSERT_EQUAL_STRING("1h00", formatApiTime(3600000).c_str());
}

void test_minuto_do_tempo_de_api_tem_dois_digitos(void) {
    // "9h9" leria como nove horas e nove alguma coisa.
    TEST_ASSERT_EQUAL_STRING("9h09", formatApiTime(9 * 3600000 + 9 * 60000).c_str());
    TEST_ASSERT_EQUAL_STRING("9h39", formatApiTime(9 * 3600000 + 39 * 60000).c_str());
    // O caso real medido nesta sessao: 32.985.351 ms.
    TEST_ASSERT_EQUAL_STRING("9h09", formatApiTime(32985351u).c_str());
}

// --- O Clawd dormindo: ninguem trabalhando ---
// A tela cheia do bicho dormindo depende desta pergunta, e ela e a mesma que
// desenhava o aviso de texto antes: sem sessao publicando E sem agente nenhum.

void test_sem_sessao_quando_ninguem_publica(void) {
    Status s;
    s.online = false;
    TEST_ASSERT_TRUE(semSessao(s));
}

void test_agente_vivo_nao_e_sem_sessao(void) {
    // `online: false` com agente na lista quer dizer ocioso ou bloqueado, e
    // nao ausente — esconder esse agente foi um bug que ja aconteceu.
    Status s;
    s.online = false;
    Agent a; a.id = "817d452c"; a.repo = "meu-repo";
    s.agents.push_back(a);
    TEST_ASSERT_FALSE(semSessao(s));
}

void test_online_nao_e_sem_sessao(void) {
    Status s;
    s.online = true;
    TEST_ASSERT_FALSE(semSessao(s));
}

void test_retrato_do_cartao_nao_afirma_ausencia(void) {
    // O cache nem guarda lista de agentes: ele nao tem como dizer que nao ha
    // sessao, e por isso nunca dispara a tela.
    Status s;
    s.online = false;
    s.doCache = true;
    TEST_ASSERT_FALSE(semSessao(s));
}

// ---- A linha de idade do dado, no rodape ----

void test_dado_fresco_mostra_so_a_idade(void) {
    Status s;
    s.updated_ago = 12;
    TEST_ASSERT_EQUAL_STRING("ha 12s",
                             textoVetustez(s, 0, "SEM CONTATO", false).c_str());
    TEST_ASSERT_EQUAL_STRING("ha 12s",
                             textoVetustez(s, 0, "SEM CONTATO", true).c_str());
}

void test_dado_velho_traz_o_motivo_e_o_tempo(void) {
    Status s;
    TEST_ASSERT_EQUAL_STRING("API FORA HA 40s",
                             textoVetustez(s, 40, "API FORA", false).c_str());
    TEST_ASSERT_EQUAL_STRING("SEM WIFI HA 40s",
                             textoVetustez(s, 40, "SEM WIFI", false).c_str());
}

// Acima de uma hora vira horas: em segundos, um atraso de dias empurra o texto
// para a esquerda ate encostar no trio de bichos.
void test_acima_de_uma_hora_o_tempo_vira_horas(void) {
    Status s;
    TEST_ASSERT_EQUAL_STRING("SEM CONTATO HA 3599s",
                             textoVetustez(s, 3599, "SEM CONTATO", false).c_str());
    TEST_ASSERT_EQUAL_STRING("SEM CONTATO HA 1h",
                             textoVetustez(s, 3600, "SEM CONTATO", false).c_str());
    TEST_ASSERT_EQUAL_STRING("SEM CONTATO HA 25h",
                             textoVetustez(s, 90000, "SEM CONTATO", false).c_str());
}

// Motivo ausente cai no texto de sempre, em vez de deixar a linha capenga.
void test_sem_motivo_o_texto_e_o_de_sempre(void) {
    Status s;
    TEST_ASSERT_EQUAL_STRING("SEM CONTATO HA 5s",
                             textoVetustez(s, 5, nullptr, false).c_str());
}

// O motor de reserva e prefixo, e vale nos tres ramos.
void test_o_motor_de_reserva_prefixa_tudo(void) {
    Status s;
    s.hooksEngine = true;
    s.updated_ago = 3;
    TEST_ASSERT_EQUAL_STRING("HOOKS  ha 3s",
                             textoVetustez(s, 0, nullptr, false).c_str());
    TEST_ASSERT_EQUAL_STRING("HOOKS  API TRAVADA HA 8s",
                             textoVetustez(s, 8, "API TRAVADA", false).c_str());
    TEST_ASSERT_EQUAL_STRING("HOOKS  S/CONTATO 8s",
                             textoVetustezCurto(s, 8).c_str());
}

// Em contato, mas nao com quem manda. So a tela em pe mostra este ramo: deitado
// o rodape divide a largura com a fileira de bichos.
void test_o_master_fora_so_aparece_onde_ha_espaco(void) {
    Status s;
    s.viaSlave = true;
    s.masterSemContatoS = 74;
    s.tag = "MAC";
    TEST_ASSERT_EQUAL_STRING("MASTER OFF HA 74s - VIA MAC",
                             textoVetustez(s, 0, nullptr, true).c_str());
    TEST_ASSERT_EQUAL_STRING("agora", textoVetustez(s, 0, nullptr, false).c_str());
}

// A reserva sem nome ainda diz que e a reserva.
void test_reserva_sem_tag_tem_nome_generico(void) {
    Status s;
    s.viaSlave = true;
    s.masterSemContatoS = 5;
    TEST_ASSERT_EQUAL_STRING("MASTER OFF HA 5s - VIA RESERVA",
                             textoVetustez(s, 0, nullptr, true).c_str());
}

// Dado velho ganha do master fora: nao adianta dizer de qual maquina veio um
// numero que ninguem sabe se ainda vale.
void test_dado_velho_ganha_do_master_fora(void) {
    Status s;
    s.viaSlave = true;
    s.masterSemContatoS = 74;
    TEST_ASSERT_EQUAL_STRING("SEM WIFI HA 9s",
                             textoVetustez(s, 9, "SEM WIFI", true).c_str());
}

// A forma curta e sempre MENOR que a longa — e a razao de ela existir.
void test_a_forma_curta_cabe_onde_a_longa_nao_cabe(void) {
    Status s;
    TEST_ASSERT_EQUAL_STRING("S/CONTATO 40s", textoVetustezCurto(s, 40).c_str());
    TEST_ASSERT_EQUAL_STRING("S/CONTATO 2h", textoVetustezCurto(s, 7200).c_str());
    TEST_ASSERT_TRUE(textoVetustezCurto(s, 40).size() <
                     textoVetustez(s, 40, "SEM CONTATO", false).size());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_sem_sessao_quando_ninguem_publica);
    RUN_TEST(test_agente_vivo_nao_e_sem_sessao);
    RUN_TEST(test_online_nao_e_sem_sessao);
    RUN_TEST(test_retrato_do_cartao_nao_afirma_ausencia);
    RUN_TEST(test_indice_do_agente_agregado_aponta_para_o_mesmo_nome);
    RUN_TEST(test_sem_agentes_o_indice_e_invalido);
    RUN_TEST(test_tempo_de_api_em_segundos_minutos_e_horas);
    RUN_TEST(test_minuto_do_tempo_de_api_tem_dois_digitos);
    RUN_TEST(test_bloqueado_governa_a_tela_inteira);
    RUN_TEST(test_ordem_de_urgencia);
    RUN_TEST(test_sem_agentes_nao_inventa_estado);
    RUN_TEST(test_idade_em_segundos);
    RUN_TEST(test_idade_em_minutos_e_horas);
    RUN_TEST(test_percentual_conhecido_e_desconhecido);
    RUN_TEST(test_pior_nivel_governa_o_pill);
    RUN_TEST(test_metrica_desconhecida_nao_conta_para_o_pill);
    RUN_TEST(test_largura_da_barra);
    RUN_TEST(test_largura_da_barra_satura);
    RUN_TEST(test_dado_fresco_mostra_so_a_idade);
    RUN_TEST(test_dado_velho_traz_o_motivo_e_o_tempo);
    RUN_TEST(test_acima_de_uma_hora_o_tempo_vira_horas);
    RUN_TEST(test_sem_motivo_o_texto_e_o_de_sempre);
    RUN_TEST(test_o_motor_de_reserva_prefixa_tudo);
    RUN_TEST(test_o_master_fora_so_aparece_onde_ha_espaco);
    RUN_TEST(test_reserva_sem_tag_tem_nome_generico);
    RUN_TEST(test_dado_velho_ganha_do_master_fora);
    RUN_TEST(test_a_forma_curta_cabe_onde_a_longa_nao_cabe);
    return UNITY_END();
}
