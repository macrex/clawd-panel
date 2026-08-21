#include <unity.h>
#include "status.h"

void setUp(void) {}
void tearDown(void) {}

// Resposta real capturada da API em 30/07/2026, com `labels` ja no formato de
// LISTA (uma entrada por agente vivo).
//
// Delimitador nomeado R"json( )json" e OBRIGATORIO aqui: o nome do modelo
// termina em "(1M context)" e a sequencia `)"` fecharia um raw string comum
// no meio do JSON, quebrando o arquivo inteiro de forma confusa.
static const char *REAL = R"json({"online": true, "sessions": 3, "model": "Opus 5 (1M context)",
"context_pct": 26, "context_repo": "r36s", "context_branch": null,
"session_pct": 25, "session_resets_in": 9311, "session_resets_hm": "2h35m",
"week_pct": 28, "week_resets_in": 281711, "week_resets_dh": "3d06h",
"updated_ago": 0, "limits_fresh": true,
"session_label": "Session 25% 2h35m", "week_label": "Week 28% 3d06h",
"labels": [
  {"session_id": "817d452c", "repo": "esp32-s3", "branch": "main",
   "model": "Opus 5 (1M context)", "context_pct": 50, "color": "yellow",
   "age": 4, "state": "working", "line": "esp32-s3 main | Opus 5 (1M context) | Context 50%"},
  {"session_id": "91bac378", "repo": "meu-repo", "branch": null,
   "model": "Opus 4.8", "context_pct": 27, "color": "green",
   "age": 3, "state": "idle", "line": "meu-repo | Opus 4.8 | Context 27%"},
  {"session_id": "2bfc2d48", "repo": "outro-repo", "branch": "validacao-arquivos",
   "model": "Opus 5 (1M context)", "context_pct": null, "color": "green",
   "age": 240, "stale": true, "state": "blocked",
   "line": "outro-repo | Opus 5 (1M context) | Context -"}
],
"colors": {"context": "green", "session": "green", "week": "green"}})json";

void test_json_invalido_marca_status_como_invalido(void) {
    TEST_ASSERT_FALSE(parseStatus("isto nao e json").valid);
}

void test_resposta_real_e_lida_por_completo(void) {
    Status s = parseStatus(REAL);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_TRUE(s.online);
    TEST_ASSERT_EQUAL_INT(3, s.sessions);
    TEST_ASSERT_EQUAL_STRING("Opus 5 (1M context)", s.model.c_str());
    TEST_ASSERT_EQUAL_STRING("r36s", s.repo.c_str());
    TEST_ASSERT_EQUAL_INT(26, s.context.pct);
    TEST_ASSERT_EQUAL_INT(25, s.session.pct);
    TEST_ASSERT_EQUAL_STRING("2h35m", s.session.resets.c_str());
    TEST_ASSERT_EQUAL_INT(28, s.week.pct);
    TEST_ASSERT_EQUAL_STRING("3d06h", s.week.resets.c_str());
    // O prazo CRU, que e de onde sai o ritmo da barra. O texto ao lado diz o
    // mesmo instante com resolucao de uma hora, entao ele nao serve de fiador:
    // "3d06h" continuaria igual com o campo em segundos zerado.
    TEST_ASSERT_EQUAL_INT(9311, s.session.resetsIn);
    TEST_ASSERT_EQUAL_INT(281711, s.week.resetsIn);
    TEST_ASSERT_EQUAL_INT(0, s.updated_ago);
}

void test_branch_nulo_vira_string_vazia(void) {
    // A API manda null literal; nao pode virar a string "null" na tela.
    TEST_ASSERT_EQUAL_STRING("", parseStatus(REAL).branch.c_str());
}

void test_cores_vem_da_api_sem_recalcular(void) {
    const char *j = R"({"context_pct":10,"session_pct":95,"week_pct":60,
      "limits_fresh":true,"colors":{"context":"green","session":"red","week":"yellow"}})";
    Status s = parseStatus(j);
    TEST_ASSERT_TRUE(Level::Green  == s.context.level);
    TEST_ASSERT_TRUE(Level::Red    == s.session.level);
    TEST_ASSERT_TRUE(Level::Yellow == s.week.level);
}

void test_limits_fresh_falso_marca_sessao_e_semana_como_desconhecidas(void) {
    // Regra da API: sem sessao viva nas ultimas 5h, os numeros seriam mentira.
    const char *j = R"({"context_pct":26,"session_pct":0,"week_pct":0,
      "limits_fresh":false,"colors":{"context":"green","session":"green","week":"green"}})";
    Status s = parseStatus(j);
    TEST_ASSERT_TRUE(s.context.known);
    TEST_ASSERT_FALSE(s.session.known);
    TEST_ASSERT_FALSE(s.week.known);
}

void test_prazo_ausente_fica_em_zero(void) {
    // Uma API antiga nao manda `session_resets_in`. Zero e o combinado: e assim
    // que a barra sabe que nao pode desenhar ritmo nenhum. Qualquer outro
    // padrao seria uma afirmacao sobre onde a janela esta.
    const char *j = R"({"context_pct":26,"session_pct":40,"week_pct":10,
      "limits_fresh":true,"colors":{"context":"green","session":"green","week":"green"}})";
    Status s = parseStatus(j);
    TEST_ASSERT_EQUAL_INT(0, s.session.resetsIn);
    TEST_ASSERT_EQUAL_INT(0, s.week.resetsIn);
}

void test_lista_de_agentes_e_lida(void) {
    Status s = parseStatus(REAL);
    TEST_ASSERT_EQUAL_INT(3, (int)s.agents.size());

    TEST_ASSERT_EQUAL_STRING("esp32-s3", s.agents[0].repo.c_str());
    TEST_ASSERT_EQUAL_STRING("main", s.agents[0].branch.c_str());
    TEST_ASSERT_EQUAL_STRING("Opus 5 (1M context)", s.agents[0].model.c_str());
    TEST_ASSERT_EQUAL_INT(50, s.agents[0].contextPct);
    TEST_ASSERT_TRUE(s.agents[0].hasContext);
    TEST_ASSERT_TRUE(Level::Yellow == s.agents[0].level);
    TEST_ASSERT_EQUAL_STRING("91bac378", s.agents[1].id.c_str());
}

void test_agente_pode_rodar_modelo_diferente(void) {
    // O que o formato antigo escondia: cada agente tem o SEU modelo.
    Status s = parseStatus(REAL);
    TEST_ASSERT_EQUAL_STRING("Opus 4.8", s.agents[1].model.c_str());
    TEST_ASSERT_EQUAL_STRING("Opus 5 (1M context)", s.agents[0].model.c_str());
}

void test_agente_sem_contexto_nao_vira_zero(void) {
    // context_pct null tem que ficar marcado como desconhecido; virar 0 diria
    // "esse agente esta zerado", que e outra coisa.
    Status s = parseStatus(REAL);
    const Agent &sem = s.agents[2];
    TEST_ASSERT_EQUAL_STRING("outro-repo", sem.repo.c_str());
    TEST_ASSERT_FALSE(sem.hasContext);
}

void test_heartbeat_parado_e_marcado_nao_removido(void) {
    // Um agente BLOQUEADO esperando o usuario para de publicar. Ele nao pode
    // sumir da tela: e o mais vivo de todos. Vem marcado com stale.
    Status s = parseStatus(REAL);
    TEST_ASSERT_FALSE(s.agents[0].stale);        // publicando
    TEST_ASSERT_TRUE(s.agents[2].stale);         // parado ha 240s
    TEST_ASSERT_EQUAL_INT(240, s.agents[2].age);
}

void test_stale_ausente_significa_publicando(void) {
    // Entradas sem o campo contam como publicando, nao paradas.
    Status s = parseStatus(
        R"json({"labels":[{"repo":"x","context_pct":10,"color":"green"}]})json");
    TEST_ASSERT_EQUAL_INT(1, (int)s.agents.size());
    TEST_ASSERT_FALSE(s.agents[0].stale);
}

void test_state_age_do_agente(void) {
    Status s = parseStatus(
        R"json({"labels":[{"session_id":"a","state":"working","state_age":154},
                          {"session_id":"b","state":"idle"}]})json");
    TEST_ASSERT_EQUAL_INT(154, s.agents[0].stateAgeS);
    // Ausente (API antiga ou orfao do herdr) = -1, "nao sei" — nunca zero, que
    // afirmaria "acabou de mudar de estado".
    TEST_ASSERT_EQUAL_INT(-1, s.agents[1].stateAgeS);
}

void test_ordem_da_api_e_preservada(void) {
    // A API ja ordena por contexto decrescente. O firmware nao reordena.
    Status s = parseStatus(REAL);
    TEST_ASSERT_EQUAL_STRING("esp32-s3", s.agents[0].repo.c_str());
    TEST_ASSERT_EQUAL_STRING("meu-repo", s.agents[1].repo.c_str());
    TEST_ASSERT_EQUAL_STRING("outro-repo",    s.agents[2].repo.c_str());
}

void test_sem_labels_a_lista_fica_vazia(void) {
    Status s = parseStatus(R"json({"online":true,"context_pct":10})json");
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_INT(0, (int)s.agents.size());
}

void test_labels_como_lista_nao_afeta_os_campos_de_topo(void) {
    // O firmware nao consome `labels`; le so os campos planos do topo. Este
    // teste trava essa independencia: a lista pode crescer, mudar de forma ou
    // sumir que a tela continua correta. Sem isto, e so uma suposicao.
    Status s = parseStatus(REAL);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_INT(26, s.context.pct);          // do topo, nao de labels[0]
    TEST_ASSERT_EQUAL_STRING("r36s", s.repo.c_str());  // idem
    TEST_ASSERT_EQUAL_INT(3, s.sessions);
}

void test_estado_do_agente_vem_dos_hooks(void) {
    // O campo que substitui a adivinhacao por tempo de silencio.
    Status s = parseStatus(REAL);
    TEST_ASSERT_TRUE(AgentState::Working == s.agents[0].state);
    TEST_ASSERT_TRUE(AgentState::Idle == s.agents[1].state);
    TEST_ASSERT_TRUE(AgentState::Blocked == s.agents[2].state);
}

void test_state_ausente_vira_unknown_e_nao_bloqueado(void) {
    // Servidor antigo, ou sessao sem hooks instalados. O painel precisa ficar
    // DISCRETO nesse caso: chutar "bloqueado" e o bug que estamos consertando.
    Status s = parseStatus(
        R"json({"labels":[{"repo":"x","context_pct":10,"color":"green","stale":true}]})json");
    TEST_ASSERT_TRUE(AgentState::Unknown == s.agents[0].state);
}

void test_nome_antigo_waiting_ainda_e_aceito(void) {
    // A API chamava esse estado de "waiting" antes de o vocabulario ser
    // alinhado com o do usuario. Uma placa gravada precisa continuar
    // funcionando contra um servidor ainda nao atualizado.
    Status s = parseStatus(
        R"json({"labels":[{"repo":"x","state":"waiting"}]})json");
    TEST_ASSERT_TRUE(AgentState::Idle == s.agents[0].state);
}

void test_state_desconhecido_nao_e_confundido_com_blocked(void) {
    // Uma string que a API nunca manda nao pode cair acidentalmente em Blocked.
    Status s = parseStatus(
        R"json({"labels":[{"repo":"x","state":"compactando","color":"green"}]})json");
    TEST_ASSERT_TRUE(AgentState::Unknown == s.agents[0].state);
}

void test_heartbeat_parado_sem_estar_bloqueado(void) {
    // A distincao que motivou tudo isto: parou de publicar, mas o hook diz que
    // apenas terminou o turno. Nao deve pulsar.
    Status s = parseStatus(
        R"json({"labels":[{"repo":"r36s","stale":true,"age":900,"state":"idle"}]})json");
    TEST_ASSERT_TRUE(s.agents[0].stale);
    TEST_ASSERT_TRUE(AgentState::Idle == s.agents[0].state);
}

void test_bloqueado_com_heartbeat_fresco(void) {
    // O inverso: acabou de travar numa pergunta e o heartbeat ainda esta fresco.
    // Precisa pulsar mesmo com idle=false.
    Status s = parseStatus(
        R"json({"labels":[{"repo":"outro-repo","stale":false,"age":2,"state":"blocked"}]})json");
    TEST_ASSERT_FALSE(s.agents[0].stale);
    TEST_ASSERT_TRUE(AgentState::Blocked == s.agents[0].state);
}

void test_offline_ainda_e_um_status_valido(void) {
    // online:false nao e erro de rede; e "nenhuma sessao ativa".
    Status s = parseStatus(R"({"online":false,"sessions":0,"limits_fresh":false})");
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_FALSE(s.online);
}

void test_motor_hooks_e_lido(void) {
    Status s = parseStatus("{\"online\":true,\"motor\":\"hooks\"}");
    TEST_ASSERT_TRUE(s.hooksEngine);
}

void test_motor_herdr_nao_marca(void) {
    Status s = parseStatus("{\"online\":true,\"motor\":\"herdr\"}");
    TEST_ASSERT_FALSE(s.hooksEngine);
}

void test_sem_o_campo_nao_marca(void) {
    // Uma placa gravada com esta versao contra uma API anterior: o campo nao
    // existe, e o comportamento tem que ser o de sempre.
    Status s = parseStatus("{\"online\":true}");
    TEST_ASSERT_FALSE(s.hooksEngine);
}

void test_relogio_e_tempo_sao_lidos(void) {
    Status s = parseStatus(R"json({"online": false, "sessions": 0,
      "clock": {"time": "16:14", "date": "30/07", "weekday": "QUI", "epoch": 1785438841},
      "weather": {"temp": 30, "min": 21, "max": 30, "code": 0,
                  "text": "limpo", "night": false, "city": "Valparaiso", "age": 42}})json");
    TEST_ASSERT_TRUE(s.valid);

    TEST_ASSERT_TRUE(s.clock.known);
    TEST_ASSERT_EQUAL_STRING("16:14", s.clock.hm.c_str());
    TEST_ASSERT_EQUAL_STRING("30/07", s.clock.date.c_str());
    TEST_ASSERT_EQUAL_STRING("QUI", s.clock.weekday.c_str());
    // O epoch e o que acerta o relogio da placa quando o SNTP nao alcanca a
    // internet — ele viajava no payload e era descartado no parse.
    TEST_ASSERT_EQUAL_INT(1785438841L, s.clock.epoch);

    TEST_ASSERT_TRUE(s.weather.known);
    TEST_ASSERT_EQUAL_INT(30, s.weather.temp);
    TEST_ASSERT_EQUAL_INT(21, s.weather.tmin);
    TEST_ASSERT_EQUAL_INT(30, s.weather.tmax);
    TEST_ASSERT_EQUAL_STRING("limpo", s.weather.text.c_str());
    TEST_ASSERT_EQUAL_STRING("Valparaiso", s.weather.city.c_str());
    TEST_ASSERT_EQUAL_INT(42, s.weather.age);
}

void test_tempo_nulo_nao_vira_zero_grau(void) {
    // A API manda weather: null ate a primeira leitura chegar. Zero grau seria
    // uma afirmacao, e nao a ausencia dela.
    Status s = parseStatus(R"json({"online": false, "sessions": 0,
      "clock": {"time": "03:20", "date": "31/07", "weekday": "SEX"},
      "weather": null})json");
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_TRUE(s.clock.known);
    TEST_ASSERT_FALSE(s.weather.known);
    // API antiga, sem o campo: zero, e `hora::semear` sabe nao fazer nada com
    // ele. Um epoch inventado acertaria o relogio da placa para 1970.
    TEST_ASSERT_EQUAL_INT(0L, s.clock.epoch);
}

void test_api_sem_relogio_nao_inventa_hora(void) {
    // Placa gravada com esta versao contra uma API ainda nao atualizada: o
    // cabecalho some com o bloco em vez de mostrar "00:00".
    Status s = parseStatus(REAL);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_FALSE(s.clock.known);
    TEST_ASSERT_FALSE(s.weather.known);
}

void test_esforco_do_agente_e_lido(void) {
    Status s = parseStatus(R"json({"online": true, "sessions": 2,
      "labels": [
        {"session_id": "aaa", "repo": "esp32-s3", "model": "Opus 5 (1M)",
         "effort": "High", "context_pct": 20, "state": "working"},
        {"session_id": "bbb", "repo": "pc", "model": "Opus 4.8",
         "effort": "XHigh", "context_pct": 10, "state": "idle"}
      ]})json");
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_INT(2, (int)s.agents.size());
    // Ja vem capitalizado da API: o firmware nao conhece o vocabulario.
    TEST_ASSERT_EQUAL_STRING("High", s.agents[0].effort.c_str());
    TEST_ASSERT_EQUAL_STRING("XHigh", s.agents[1].effort.c_str());
    TEST_ASSERT_EQUAL_STRING("Opus 5 (1M)", s.agents[0].model.c_str());
}

void test_esforco_ausente_fica_vazio(void) {
    // Sessao publicando por uma statusline anterior ao campo: a linha do modelo
    // some com o sufixo em vez de mostrar "Opus 5 (1M) null".
    Status s = parseStatus(REAL);
    TEST_ASSERT_TRUE(s.valid);
    for (const Agent &a : s.agents)
        TEST_ASSERT_TRUE(a.effort.empty());
}

void test_janelas_de_limite_sao_independentes(void) {
    // A janela de 5h virou e a semana continua valida por dois dias. Antes
    // havia uma flag so e as duas caiam juntas — a semana sumia da tela sem
    // motivo nenhum.
    Status s = parseStatus(R"json({"online": true, "sessions": 1,
      "session_pct": 0, "session_resets_hm": "-",
      "week_pct": 62, "week_resets_dh": "2d03h",
      "limits_fresh": false, "session_known": true, "week_known": true,
      "session_inferred": true,
      "colors": {"context": "green", "session": "green", "week": "yellow"}})json");
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_TRUE(s.session.known);
    TEST_ASSERT_EQUAL_INT(0, s.session.pct);
    TEST_ASSERT_TRUE(s.week.known);
    TEST_ASSERT_EQUAL_INT(62, s.week.pct);
    TEST_ASSERT_EQUAL_STRING("2d03h", s.week.resets.c_str());
}

void test_semana_sozinha_pode_ser_desconhecida(void) {
    Status s = parseStatus(R"json({"online": true, "sessions": 1,
      "session_pct": 41, "session_resets_hm": "2h35m",
      "week_pct": 0, "week_resets_dh": "-",
      "limits_fresh": false, "session_known": true, "week_known": false})json");
    TEST_ASSERT_TRUE(s.session.known);
    TEST_ASSERT_FALSE(s.week.known);
}

void test_api_antiga_cai_no_limits_fresh(void) {
    // Placa gravada com esta versao contra uma API que so manda a flag antiga:
    // ela vale para as duas janelas, como valia antes.
    Status s = parseStatus(REAL);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_TRUE(s.session.known);
    TEST_ASSERT_TRUE(s.week.known);
}

void test_momento_absoluto_do_reset_e_lido(void) {
    // O prazo relativo diz quanto falta; estes dois dizem QUANDO. A API ja
    // manda os dois prontos, no fuso dela: a placa nao tem relogio nem fuso.
    Status s = parseStatus(R"json({"online": true, "sessions": 1,
      "session_pct": 41, "session_resets_hm": "2h35m",
      "session_resets_clock": "7:20pm",
      "week_pct": 62, "week_resets_dh": "1d06h",
      "week_resets_date": "01/08/2026 (Sabado)",
      "limits_fresh": true})json");
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_STRING("7:20pm", s.session.at.c_str());
    TEST_ASSERT_EQUAL_STRING("01/08/2026 (Sabado)", s.week.at.c_str());
}

void test_api_antiga_nao_inventa_o_momento(void) {
    // Placa nova contra API sem os campos novos: fica vazio, e a linha extra
    // simplesmente nao e desenhada. Nada de "null" na tela.
    Status s = parseStatus(REAL);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_TRUE(s.session.at.empty());
    TEST_ASSERT_TRUE(s.week.at.empty());
}

void test_limpeza_em_curso_e_lida(void) {
    // O que faz a turma do rodape varrer. Vem da API, e nao da lembranca do
    // painel de ter mandado o comando.
    Status s = parseStatus(R"json({"online": true, "sessions": 1,
      "cleaning": true, "limits_fresh": true})json");
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_TRUE(s.cleaning);
}

void test_sem_o_campo_ninguem_esta_limpando(void) {
    // API antiga: o padrao e "nao esta", e nao um booleano indefinido.
    TEST_ASSERT_FALSE(parseStatus(REAL).cleaning);
}

void test_cumulativos_da_sessao_sao_lidos(void) {
    Status s = parseStatus(R"json({"online": true, "sessions": 1,
      "labels": [{"session_id": "817d452c", "repo": "esp32-s3",
        "context_pct": 34, "state": "working",
        "cost_usd": 436.04, "lines_added": 20963, "lines_removed": 2208,
        "api_ms": 32985351}]})json");
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_INT(1, (int)s.agents.size());
    const Agent &a = s.agents[0];
    TEST_ASSERT_TRUE(a.hasCost);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 436.04f, a.costUsd);
    TEST_ASSERT_TRUE(a.hasLines);
    TEST_ASSERT_EQUAL_INT(20963, a.linesAdded);
    TEST_ASSERT_EQUAL_INT(2208, a.linesRemoved);
    TEST_ASSERT_TRUE(a.hasApiMs);
    TEST_ASSERT_EQUAL_UINT32(32985351u, a.apiMs);
}

void test_custo_redondo_nao_se_perde(void) {
    // Um custo exato sai do JSON como inteiro. Se a leitura exigisse ponto
    // flutuante, uma sessao que ainda nao gastou nada apareceria como
    // "desconhecida" em vez de zero — que sao coisas diferentes.
    Status s = parseStatus(R"json({"labels": [{"session_id": "a",
      "cost_usd": 0, "lines_added": 0, "lines_removed": 0, "api_ms": 0}]})json");
    TEST_ASSERT_TRUE(s.agents[0].hasCost);
    TEST_ASSERT_TRUE(s.agents[0].hasLines);
    TEST_ASSERT_TRUE(s.agents[0].hasApiMs);
}

void test_cumulativos_ausentes_nao_viram_zero(void) {
    // Statusline anterior a esta versao: os campos nao chegam. Zero seria uma
    // afirmacao — a tela simplesmente nao escreve a linha.
    Status s = parseStatus(REAL);
    for (const Agent &a : s.agents) {
        TEST_ASSERT_FALSE(a.hasCost);
        TEST_ASSERT_FALSE(a.hasLines);
        TEST_ASSERT_FALSE(a.hasApiMs);
    }
}

void test_meia_resposta_de_linhas_nao_conta(void) {
    // So `lines_added`: "+37 linhas, removidas desconhecidas" nao informa nada.
    Status s = parseStatus(R"json({"labels": [{"session_id": "a",
      "lines_added": 37}]})json");
    TEST_ASSERT_FALSE(s.agents[0].hasLines);
}


// ---- A quebra por modelo do dia ----
// `works` diz quanto o dia custou; `uso` diz de QUEM foi o custo.

void test_uso_e_lido_com_os_modelos_na_ordem_da_api(void) {
    Status s = parseStatus(
        "{\"online\":true,\"uso\":{\"dia\":\"2026-08-05\",\"cost_usd\":18.73,"
        "\"modelos\":["
        "{\"id\":\"claude-opus-5\",\"rotulo\":\"Opus 5\",\"cost_usd\":12.5},"
        "{\"id\":\"claude-haiku-4-5\",\"rotulo\":\"Haiku 4.5\",\"cost_usd\":6.23}"
        "]}}");
    TEST_ASSERT_TRUE(s.uso.known);
    TEST_ASSERT_EQUAL_FLOAT(18.73f, s.uso.costUsd);
    TEST_ASSERT_EQUAL_UINT(2, s.uso.modelos.size());
    // A ordem e a da API: o firmware nao reordena.
    TEST_ASSERT_EQUAL_STRING("Opus 5", s.uso.modelos[0].rotulo.c_str());
    TEST_ASSERT_TRUE(s.uso.modelos[0].hasCost);
    TEST_ASSERT_EQUAL_FLOAT(12.5f, s.uso.modelos[0].costUsd);
    TEST_ASSERT_EQUAL_STRING("Haiku 4.5", s.uso.modelos[1].rotulo.c_str());
}

void test_uso_ausente_deixa_known_falso(void) {
    // Placa nova contra API antiga: desenha os dois cards de sempre, e nao um
    // terceiro card vazio afirmando que o dia custou zero.
    Status s = parseStatus("{\"online\":true}");
    TEST_ASSERT_FALSE(s.uso.known);
    TEST_ASSERT_EQUAL_UINT(0, s.uso.modelos.size());
}

void test_uso_com_lista_vazia_e_conhecido(void) {
    // Um dia sem turno nenhum e um fato, e nao ausencia de informacao.
    Status s = parseStatus("{\"online\":true,\"uso\":{\"cost_usd\":0,\"modelos\":[]}}");
    TEST_ASSERT_TRUE(s.uso.known);
    TEST_ASSERT_EQUAL_UINT(0, s.uso.modelos.size());
}

void test_modelo_sem_preco_vem_marcado(void) {
    // Modelo fora da tabela: custo nulo, que e diferente de zero. O card
    // desenha "-" em vez de afirmar que nao custou nada.
    Status s = parseStatus(
        "{\"online\":true,\"uso\":{\"cost_usd\":0,\"modelos\":["
        "{\"id\":\"modelo-novo\",\"rotulo\":\"modelo-novo\",\"cost_usd\":null}]}}");
    TEST_ASSERT_TRUE(s.uso.known);
    TEST_ASSERT_FALSE(s.uso.modelos[0].hasCost);
}

void test_vitalicio_e_lido(void) {
    const char *j = "{\"vitalicio\":{\"turnos\":711,\"cost_usd\":2488.66,"
                    "\"desde\":\"2026-06-30\"}}";
    Status s = parseStatus(j);
    TEST_ASSERT_TRUE(s.vitalicio.known);
    TEST_ASSERT_EQUAL_INT(711, (int)s.vitalicio.turnos);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2488.66f, s.vitalicio.costUsd);
    TEST_ASSERT_EQUAL_STRING("2026-06-30", s.vitalicio.desde.c_str());
}

void test_vitalicio_ausente_deixa_known_falso(void) {
    // API anterior a este recurso: a quarta tela desenha o aviso do que falta
    // em vez de nivel 1, que seria afirmar sobre historia nao contada.
    Status s = parseStatus("{\"online\":true}");
    TEST_ASSERT_FALSE(s.vitalicio.known);
}

void test_vitalicio_com_custo_inteiro(void) {
    // Custo redondo chega como inteiro no JSON. `is<float>()` do ArduinoJson
    // aceita inteiro, entao um total de exatamente 2000 nao pode se perder.
    Status s = parseStatus("{\"vitalicio\":{\"turnos\":10,\"cost_usd\":2000}}");
    TEST_ASSERT_TRUE(s.vitalicio.known);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2000.0f, s.vitalicio.costUsd);
}

void test_vitalicio_com_desde_nulo(void) {
    // Livro-caixa vazio manda `desde: null`. Vira string vazia, e a quarta tela
    // simplesmente nao desenha a linha do periodo.
    Status s = parseStatus("{\"vitalicio\":{\"turnos\":0,\"cost_usd\":0,"
                           "\"desde\":null}}");
    TEST_ASSERT_TRUE(s.vitalicio.known);
    TEST_ASSERT_EQUAL_STRING("", s.vitalicio.desde.c_str());
}

// ---- O bloqueio: a pergunta que trava um agente ----

static const char *BLOQ = R"json({"bloqueio":{"agent_id":"817d452c",
"pane_id":"w0:p1","pergunta":"Do you want to proceed?","seq":3,
"opcoes":[{"n":1,"rotulo":"Yes","texto":false},
          {"n":2,"rotulo":"Yes, and don't ask again","texto":false},
          {"n":3,"rotulo":"No, and tell Claude","texto":true}]}})json";

void test_bloqueio_e_lido(void) {
    Status s = parseStatus(BLOQ);
    TEST_ASSERT_TRUE(s.bloqueio.known);
    TEST_ASSERT_EQUAL_STRING("817d452c", s.bloqueio.agentId.c_str());
    TEST_ASSERT_EQUAL_STRING("w0:p1", s.bloqueio.paneId.c_str());
    TEST_ASSERT_EQUAL_STRING("Do you want to proceed?", s.bloqueio.pergunta.c_str());
    TEST_ASSERT_EQUAL_INT(3, s.bloqueio.seq);
    TEST_ASSERT_EQUAL_INT(3, (int)s.bloqueio.opcoes.size());
    TEST_ASSERT_EQUAL_INT(2, s.bloqueio.opcoes[1].n);
    TEST_ASSERT_EQUAL_STRING("Yes", s.bloqueio.opcoes[0].rotulo.c_str());
    TEST_ASSERT_FALSE(s.bloqueio.opcoes[0].texto);
    // A que abre campo de texto vem marcada: o toque nela nao resolve o
    // bloqueio, so troca de pergunta, e o botao precisa dizer isso.
    TEST_ASSERT_TRUE(s.bloqueio.opcoes[2].texto);
}

void test_captura_e_lida(void) {
    // O pedido de foto da tela. So existe entre alguem pedir e a placa atender,
    // entao a esmagadora maioria das respostas nao traz este campo.
    Status s = parseStatus("{\"captura\":{\"id\":1785439580}}");
    TEST_ASSERT_TRUE(s.captura.known);
    TEST_ASSERT_EQUAL_INT(1785439580, (int)s.captura.id);
    // Sem fusao, o pedido e do master.
    TEST_ASSERT_EQUAL_INT(0, s.captura.origem);
}

void test_captura_ausente_ou_nula_nao_pede_nada(void) {
    // O caso normal das duas formas que a API pode escrever. Um falso positivo
    // aqui faria a placa mandar 300 KB a cada poll.
    TEST_ASSERT_FALSE(parseStatus("{\"online\":true}").captura.known);
    TEST_ASSERT_FALSE(parseStatus("{\"captura\":null}").captura.known);
}

void test_bloqueio_nulo_deixa_known_falso(void) {
    // O caso normal: ninguem bloqueado. A API manda `null`, e a segunda pagina
    // mostra o contexto de sempre.
    Status s = parseStatus("{\"bloqueio\":null}");
    TEST_ASSERT_FALSE(s.bloqueio.known);
    TEST_ASSERT_EQUAL_INT(0, (int)s.bloqueio.opcoes.size());
}

void test_bloqueio_ausente_deixa_known_falso(void) {
    // Placa nova contra API antiga: se comporta como antes de existir o campo.
    TEST_ASSERT_FALSE(parseStatus(REAL).bloqueio.known);
}

void test_bloqueio_sem_opcoes_ainda_e_conhecido(void) {
    // Bloqueado com uma tela que nao e seletor navegavel. A pagina mostra a
    // pergunta SEM botao — e esse e o comportamento certo, nao uma falha.
    Status s = parseStatus("{\"bloqueio\":{\"agent_id\":\"a\",\"pane_id\":\"w0:p1\","
                           "\"pergunta\":\"e ai?\",\"opcoes\":[],\"seq\":1}}");
    TEST_ASSERT_TRUE(s.bloqueio.known);
    TEST_ASSERT_EQUAL_INT(0, (int)s.bloqueio.opcoes.size());
}

void test_bloqueio_sem_pergunta_ainda_e_conhecido(void) {
    // A API nao conseguiu ler a tela. Anunciar o bloqueio sem pergunta e melhor
    // do que sumir: sumindo, o unico estado que exige acao ficaria invisivel.
    Status s = parseStatus("{\"bloqueio\":{\"agent_id\":\"a\",\"pane_id\":\"w0:p1\","
                           "\"pergunta\":\"\",\"opcoes\":[],\"seq\":1}}");
    TEST_ASSERT_TRUE(s.bloqueio.known);
    TEST_ASSERT_EQUAL_STRING("", s.bloqueio.pergunta.c_str());
}

void test_bloqueio_com_opcoes_demais_e_cortado(void) {
    // Guardar o que nao cabe na tela so gasta memoria, e um botao que nao
    // aparece nao pode ser tocado.
    Status s = parseStatus("{\"bloqueio\":{\"agent_id\":\"a\",\"pane_id\":\"p\","
                           "\"pergunta\":\"q\",\"seq\":1,\"opcoes\":["
                           "{\"n\":1,\"rotulo\":\"a\"},{\"n\":2,\"rotulo\":\"b\"},"
                           "{\"n\":3,\"rotulo\":\"c\"},{\"n\":4,\"rotulo\":\"d\"},"
                           "{\"n\":5,\"rotulo\":\"e\"},{\"n\":6,\"rotulo\":\"f\"}]}}");
    TEST_ASSERT_EQUAL_INT((int)BLOQUEIO_MAX_OPCOES, (int)s.bloqueio.opcoes.size());
    TEST_ASSERT_EQUAL_STRING("a", s.bloqueio.opcoes[0].rotulo.c_str());
}

void test_bloqueio_sem_pane_e_ignorado(void) {
    // Sem pane nao ha para onde mandar a resposta. Desenhar botoes que nao
    // podem funcionar seria pior do que nao desenhar nada.
    Status s = parseStatus("{\"bloqueio\":{\"agent_id\":\"a\",\"pergunta\":\"q\","
                           "\"seq\":1,\"opcoes\":[{\"n\":1,\"rotulo\":\"Yes\"}]}}");
    TEST_ASSERT_FALSE(s.bloqueio.known);
}

void test_origem_default_e_master(void) {
    // O parse nao conhece origem: quem carimba e a fusao (net.cpp). Um JSON
    // qualquer produz agentes do master (0) e resposta sem fallback.
    Status s = parseStatus(R"({"online":true,"labels":[{"session_id":"abc","state":"working"}]})");
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_INT(1, (int)s.agents.size());
    TEST_ASSERT_EQUAL_INT(0, s.agents[0].origem);
    TEST_ASSERT_FALSE(s.viaSlave);
    TEST_ASSERT_EQUAL_INT(0, s.bloqueio.origem);
}

void test_tag_da_maquina_desce_para_cada_agente(void) {
    // Quem se identifica e o servico: a tag vem no topo do payload e vale para
    // todo mundo que veio nele, agentes e bloqueio.
    Status s = parseStatus(R"({"online":true,"tag":"WIN",
        "labels":[{"session_id":"abc","state":"working"},
                  {"session_id":"def","state":"idle"}],
        "bloqueio":{"pane_id":"w0:p1","pergunta":"q","seq":1,"opcoes":[]}})");
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_STRING("WIN", s.tag.c_str());
    TEST_ASSERT_EQUAL_STRING("WIN", s.agents[0].tag.c_str());
    TEST_ASSERT_EQUAL_STRING("WIN", s.agents[1].tag.c_str());
    TEST_ASSERT_EQUAL_STRING("WIN", s.bloqueio.tag.c_str());
}

void test_api_antiga_sem_tag_nao_inventa_nome(void) {
    // Um servidor anterior a esta versao nao publica `tag`. Vazio e a resposta
    // honesta: melhor nenhum chip do que um nome que ninguem escolheu.
    Status s = parseStatus(R"({"online":true,"labels":[{"session_id":"abc"}]})");
    TEST_ASSERT_EQUAL_STRING("", s.tag.c_str());
    TEST_ASSERT_EQUAL_STRING("", s.agents[0].tag.c_str());
}

// ---- O plano de cada conta ----

void test_planos_viram_pares(void) {
    const char *j = R"({"planos":{"claude":"Max 5x","codex":"Free"},"labels":[]})";
    const Status s = parseStatus(j);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_INT(2, (int)s.planos.size());
    // A ordem do JSON e preservada; quem ordena os GRUPOS e a lib grupos.
    TEST_ASSERT_EQUAL_STRING("claude", s.planos[0].agente.c_str());
    TEST_ASSERT_EQUAL_STRING("Max 5x", s.planos[0].rotulo.c_str());
}

// Uma API antiga nao manda o campo. A placa tem que subir igual e simplesmente
// nao mostrar plano nenhum.
void test_sem_planos_a_lista_fica_vazia(void) {
    const Status s = parseStatus(R"({"online":true,"labels":[]})");
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_INT(0, (int)s.planos.size());
}

// `planos` com o tipo errado (uma API futura que o transforme em lista) nao
// pode derrubar o parse do resto do documento.
void test_planos_do_tipo_errado_e_ignorado(void) {
    const Status s = parseStatus(R"({"planos":["claude"],"online":true,"labels":[]})");
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_TRUE(s.online);
    TEST_ASSERT_EQUAL_INT(0, (int)s.planos.size());
}

// ---- Limites que a API lembra, sem sessao nenhuma publicando ----
//
// A janela de 5h e a de 7 dias sao da CONTA e continuam correndo com todo
// terminal fechado. A API republica o ultimo snapshot bom marcado como
// lembranca, e a placa precisa saber disso para nao mostrar um numero de uma
// hora atras com a mesma cara de um que acabou de chegar.
void test_limites_lembrados_marcam_so_a_faixa_que_veio_da_memoria(void) {
    const Status s = parseStatus(R"json({"online":false,"sessions":0,
      "session_pct":41,"week_pct":75,"session_known":true,"week_known":true,
      "limites":{"memoria":true,"visto_hm":"11:15","visto_ago":3600,
                 "session":true,"week":false}})json");
    TEST_ASSERT_TRUE(s.session.known);
    TEST_ASSERT_EQUAL_INT(41, s.session.pct);
    TEST_ASSERT_TRUE(s.session.memoria);
    // A semana continua sendo leitura viva: esmaeca-la seria marcar de velho um
    // numero que acabou de chegar.
    TEST_ASSERT_FALSE(s.week.memoria);
    TEST_ASSERT_EQUAL_STRING("11:15", s.limitesVisto.c_str());
}

// Sem o bloco (leitura viva, ou uma API anterior a ele) nada e lembranca — e a
// placa desenha como sempre desenhou.
void test_sem_bloco_limites_nada_vem_da_memoria(void) {
    const Status s = parseStatus(R"json({"session_pct":25,"week_pct":28,
      "limits_fresh":true})json");
    TEST_ASSERT_FALSE(s.session.memoria);
    TEST_ASSERT_FALSE(s.week.memoria);
    TEST_ASSERT_TRUE(s.limitesVisto.empty());
}

// `memoria: false` desliga as duas mesmo com as chaves por janela presentes: o
// bloco inteiro so vale quando ele proprio se declara lembranca.
void test_bloco_limites_desligado_nao_esmaece_nada(void) {
    const Status s = parseStatus(R"json({"session_pct":25,
      "limites":{"memoria":false,"visto_hm":"11:15","session":true,"week":true}})json");
    TEST_ASSERT_FALSE(s.session.memoria);
    TEST_ASSERT_FALSE(s.week.memoria);
    TEST_ASSERT_TRUE(s.limitesVisto.empty());
}

// A lista que as tres trancas compartilham. O teste existe para fixar QUAIS
// caracteres estao nela: era escrita a mao em tres lugares, e acrescentar um
// significava lembrar dos tres.
void test_quais_caracteres_movem_o_cursor(void) {
    TEST_ASSERT_TRUE(eControleDeCursor('\n'));
    TEST_ASSERT_TRUE(eControleDeCursor('\r'));
    TEST_ASSERT_TRUE(eControleDeCursor('\t'));

    // O espaco NAO entra: ele nao move o cursor sozinho, e trata-lo como
    // controle faria o texto perder as separacoes de palavra.
    TEST_ASSERT_FALSE(eControleDeCursor(' '));
    TEST_ASSERT_FALSE(eControleDeCursor('a'));
    TEST_ASSERT_FALSE(eControleDeCursor('\0'));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_cumulativos_da_sessao_sao_lidos);
    RUN_TEST(test_custo_redondo_nao_se_perde);
    RUN_TEST(test_cumulativos_ausentes_nao_viram_zero);
    RUN_TEST(test_meia_resposta_de_linhas_nao_conta);
    RUN_TEST(test_limpeza_em_curso_e_lida);
    RUN_TEST(test_sem_o_campo_ninguem_esta_limpando);
    RUN_TEST(test_janelas_de_limite_sao_independentes);
    RUN_TEST(test_semana_sozinha_pode_ser_desconhecida);
    RUN_TEST(test_api_antiga_cai_no_limits_fresh);
    RUN_TEST(test_momento_absoluto_do_reset_e_lido);
    RUN_TEST(test_api_antiga_nao_inventa_o_momento);
    RUN_TEST(test_esforco_do_agente_e_lido);
    RUN_TEST(test_esforco_ausente_fica_vazio);
    RUN_TEST(test_relogio_e_tempo_sao_lidos);
    RUN_TEST(test_tempo_nulo_nao_vira_zero_grau);
    RUN_TEST(test_api_sem_relogio_nao_inventa_hora);
    RUN_TEST(test_json_invalido_marca_status_como_invalido);
    RUN_TEST(test_resposta_real_e_lida_por_completo);
    RUN_TEST(test_prazo_ausente_fica_em_zero);
    RUN_TEST(test_branch_nulo_vira_string_vazia);
    RUN_TEST(test_cores_vem_da_api_sem_recalcular);
    RUN_TEST(test_limits_fresh_falso_marca_sessao_e_semana_como_desconhecidas);
    RUN_TEST(test_lista_de_agentes_e_lida);
    RUN_TEST(test_agente_pode_rodar_modelo_diferente);
    RUN_TEST(test_agente_sem_contexto_nao_vira_zero);
    RUN_TEST(test_heartbeat_parado_e_marcado_nao_removido);
    RUN_TEST(test_stale_ausente_significa_publicando);
    RUN_TEST(test_ordem_da_api_e_preservada);
    RUN_TEST(test_sem_labels_a_lista_fica_vazia);
    RUN_TEST(test_labels_como_lista_nao_afeta_os_campos_de_topo);
    RUN_TEST(test_estado_do_agente_vem_dos_hooks);
    RUN_TEST(test_state_ausente_vira_unknown_e_nao_bloqueado);
    RUN_TEST(test_nome_antigo_waiting_ainda_e_aceito);
    RUN_TEST(test_state_desconhecido_nao_e_confundido_com_blocked);
    RUN_TEST(test_heartbeat_parado_sem_estar_bloqueado);
    RUN_TEST(test_bloqueado_com_heartbeat_fresco);
    RUN_TEST(test_offline_ainda_e_um_status_valido);
    RUN_TEST(test_motor_hooks_e_lido);
    RUN_TEST(test_motor_herdr_nao_marca);
    RUN_TEST(test_sem_o_campo_nao_marca);
    RUN_TEST(test_uso_e_lido_com_os_modelos_na_ordem_da_api);
    RUN_TEST(test_uso_ausente_deixa_known_falso);
    RUN_TEST(test_uso_com_lista_vazia_e_conhecido);
    RUN_TEST(test_modelo_sem_preco_vem_marcado);
    RUN_TEST(test_vitalicio_e_lido);
    RUN_TEST(test_vitalicio_ausente_deixa_known_falso);
    RUN_TEST(test_vitalicio_com_custo_inteiro);
    RUN_TEST(test_vitalicio_com_desde_nulo);
    RUN_TEST(test_captura_e_lida);
    RUN_TEST(test_captura_ausente_ou_nula_nao_pede_nada);
    RUN_TEST(test_bloqueio_e_lido);
    RUN_TEST(test_bloqueio_nulo_deixa_known_falso);
    RUN_TEST(test_bloqueio_ausente_deixa_known_falso);
    RUN_TEST(test_bloqueio_sem_opcoes_ainda_e_conhecido);
    RUN_TEST(test_bloqueio_sem_pergunta_ainda_e_conhecido);
    RUN_TEST(test_bloqueio_com_opcoes_demais_e_cortado);
    RUN_TEST(test_bloqueio_sem_pane_e_ignorado);
    RUN_TEST(test_origem_default_e_master);
    RUN_TEST(test_state_age_do_agente);
    RUN_TEST(test_tag_da_maquina_desce_para_cada_agente);
    RUN_TEST(test_api_antiga_sem_tag_nao_inventa_nome);
    RUN_TEST(test_planos_viram_pares);
    RUN_TEST(test_sem_planos_a_lista_fica_vazia);
    RUN_TEST(test_planos_do_tipo_errado_e_ignorado);
    RUN_TEST(test_limites_lembrados_marcam_so_a_faixa_que_veio_da_memoria);
    RUN_TEST(test_sem_bloco_limites_nada_vem_da_memoria);
    RUN_TEST(test_bloco_limites_desligado_nao_esmaece_nada);
    RUN_TEST(test_quais_caracteres_movem_o_cursor);
    return UNITY_END();
}
