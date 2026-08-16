#include <unity.h>
#include <string>
#include <vector>
#include "status.h"

void setUp(void) {}
void tearDown(void) {}

// Os DOIS payloads sao reais: saíram do `GET /status` desta maquina com um
// agente vivo, e o enxuto e o mesmo documento passado por `painel.enxugar()`
// (server/painel.py). Nao foram escritos a mao — um payload inventado provaria
// que o corte que EU imaginei preserva o que EU lembro que o parser le.
//
// 2.006 bytes viram 1.542: 23% a menos, a cada dois segundos, para sempre.
//
// O que este arquivo prova e uma coisa so, e e a unica que importa: os dois
// produzem o MESMO `Status`. Se algum dia alguem cortar um campo vivo em
// painel.py, e aqui que aparece — e nao na tela, dias depois.

static const char *CHEIO = R"json({"online": true, "sessions": 1, "sessions_active": 1, "blocked": 0, "model": "Opus 5 (1M)", "effort": "Max", "context_pct": 50, "context_repo": "esp32", "context_branch": null, "session_pct": 0, "session_resets_in": 14916, "session_resets_hm": "4h08m", "session_resets_clock": "3:10pm", "week_pct": 4, "week_resets_in": 561516, "week_resets_dh": "6d11h", "week_resets_date": "22/08/2026 (Sabado)", "updated_ago": 9, "limits_fresh": true, "session_known": true, "week_known": true, "session_inferred": false, "session_label": "Session 0% 4h08m", "week_label": "Week 4% 6d11h", "labels": [{"session_id": "12ba1d28", "repo": "esp32", "branch": null, "model": "Opus 5 (1M)", "effort": "Max", "context_pct": 50, "color": "yellow", "age": 9, "state": "working", "done": false, "agent": "claude", "state_age": 44, "event": "UserPromptSubmit", "stale": false, "proc_alive": true, "cost_usd": 85.46394149999995, "lines_added": 2986, "lines_removed": 159, "api_ms": 5988373, "pane_id": "w3G:p2", "line": "❖ esp32 | Opus 5 (1M) Max | Context 50%"}], "bloqueio": null, "captura": null, "arquivo": null, "colors": {"context": "yellow", "session": "green", "week": "green"}, "herdr_online": true, "done": 0, "cleaning": false, "works": {"trabalhos": 2, "seconds": 1491.4, "blocked_seconds": 77.9, "api_seconds": 837.1, "cost_usd": 15.3574, "lines_added": 385, "lines_removed": 14, "blocks": 1, "mediana_seconds": 745.7, "mediana_gap_seconds": 102.5, "marcados": 0}, "uso": {"modelos": [{"id": "claude-opus-5", "rotulo": "Opus 5", "cost_usd": 15.3574, "tokens": 26853845, "turnos": 2, "estimado": false}], "cost_usd": 15.3574, "estimados": 0, "fator": 1.0, "vigente": "2026-08-04", "dia": "2026-08-16"}, "vitalicio": {"turnos": 605, "cost_usd": 1879.79, "desde": "2026-08-08"}, "motor": "herdr", "tag": "WIN", "clock": {"time": "11:01", "date": "16/08", "weekday": "DOM", "epoch": 1786888883}, "weather": {"temp": 20, "min": 19, "max": 30, "code": 0, "text": "estrelado", "night": true, "city": "Brasilia", "age": 25453}})json";

static const char *MAGRO = R"json({"online": true, "sessions": 1, "model": "Opus 5 (1M)", "effort": "Max", "context_pct": 50, "context_repo": "esp32", "context_branch": null, "session_pct": 0, "session_resets_in": 14916, "session_resets_hm": "4h08m", "session_resets_clock": "3:10pm", "week_pct": 4, "week_resets_in": 561516, "week_resets_dh": "6d11h", "week_resets_date": "22/08/2026 (Sabado)", "updated_ago": 9, "limits_fresh": true, "session_known": true, "week_known": true, "session_inferred": false, "labels": [{"session_id": "12ba1d28", "repo": "esp32", "branch": null, "model": "Opus 5 (1M)", "effort": "Max", "context_pct": 50, "color": "yellow", "age": 9, "state": "working", "done": false, "agent": "claude", "stale": false, "cost_usd": 85.46394149999995, "lines_added": 2986, "lines_removed": 159, "api_ms": 5988373, "pane_id": "w3G:p2"}], "bloqueio": null, "captura": null, "arquivo": null, "colors": {"context": "yellow", "session": "green", "week": "green"}, "done": 0, "cleaning": false, "works": {"trabalhos": 2, "seconds": 1491.4, "blocked_seconds": 77.9, "cost_usd": 15.3574, "lines_added": 385, "lines_removed": 14, "mediana_seconds": 745.7}, "uso": {"modelos": [{"id": "claude-opus-5", "rotulo": "Opus 5", "cost_usd": 15.3574, "turnos": 2}], "cost_usd": 15.3574}, "vitalicio": {"turnos": 605, "cost_usd": 1879.79, "desde": "2026-08-08"}, "motor": "herdr", "tag": "WIN", "clock": {"time": "11:01", "date": "16/08", "weekday": "DOM", "epoch": 1786888883}, "weather": {"temp": 20, "min": 19, "max": 30, "text": "estrelado", "city": "Brasilia", "age": 25453}})json";

static void mesmaMetrica(const char *qual, const Metric &a, const Metric &b) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(a.pct, b.pct, qual);
    TEST_ASSERT_EQUAL_INT_MESSAGE(a.resetsIn, b.resetsIn, qual);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)a.level, (int)b.level, qual);
    TEST_ASSERT_EQUAL_INT_MESSAGE(a.known, b.known, qual);
    TEST_ASSERT_EQUAL_INT_MESSAGE(a.inferred, b.inferred, qual);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(a.resets.c_str(), b.resets.c_str(), qual);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(a.at.c_str(), b.at.c_str(), qual);
}

void test_o_enxuto_e_de_fato_menor(void) {
    // Sem isto o teste passaria com um `enxugar` que nao enxuga nada.
    TEST_ASSERT_TRUE(std::string(MAGRO).size() < std::string(CHEIO).size());
    const size_t corte = std::string(CHEIO).size() - std::string(MAGRO).size();
    TEST_ASSERT_TRUE(corte > 400);
}

void test_o_topo_sobrevive_ao_corte(void) {
    const Status c = parseStatus(CHEIO);
    const Status m = parseStatus(MAGRO);
    TEST_ASSERT_TRUE(c.valid);
    TEST_ASSERT_TRUE(m.valid);

    TEST_ASSERT_EQUAL_INT(c.online, m.online);
    TEST_ASSERT_EQUAL_INT(c.sessions, m.sessions);
    TEST_ASSERT_EQUAL_INT(c.hooksEngine, m.hooksEngine);
    TEST_ASSERT_EQUAL_INT(c.cleaning, m.cleaning);
    TEST_ASSERT_EQUAL_INT(c.updated_ago, m.updated_ago);
    TEST_ASSERT_EQUAL_STRING(c.tag.c_str(), m.tag.c_str());
    TEST_ASSERT_EQUAL_STRING(c.model.c_str(), m.model.c_str());
    TEST_ASSERT_EQUAL_STRING(c.repo.c_str(), m.repo.c_str());
    TEST_ASSERT_EQUAL_STRING(c.branch.c_str(), m.branch.c_str());
}

void test_os_limites_sobrevivem(void) {
    const Status c = parseStatus(CHEIO);
    const Status m = parseStatus(MAGRO);
    mesmaMetrica("context", c.context, m.context);
    mesmaMetrica("session", c.session, m.session);
    mesmaMetrica("week", c.week, m.week);
}

void test_o_relogio_sobrevive_com_o_epoch(void) {
    // O `epoch` e o campo que acerta o relogio da placa quando o SNTP nao
    // alcanca a internet. Corta-lo por engano deixaria o cache inacessivel numa
    // rede sem saida — e nada na tela diria por que.
    const Status c = parseStatus(CHEIO);
    const Status m = parseStatus(MAGRO);
    TEST_ASSERT_EQUAL_INT(c.clock.known, m.clock.known);
    TEST_ASSERT_EQUAL_STRING(c.clock.hm.c_str(), m.clock.hm.c_str());
    TEST_ASSERT_EQUAL_STRING(c.clock.date.c_str(), m.clock.date.c_str());
    TEST_ASSERT_EQUAL_STRING(c.clock.weekday.c_str(), m.clock.weekday.c_str());
    TEST_ASSERT_EQUAL_INT(c.clock.epoch, m.clock.epoch);
    TEST_ASSERT_TRUE(m.clock.epoch > 0);
}

void test_os_agentes_sobrevivem_inteiros(void) {
    const Status c = parseStatus(CHEIO);
    const Status m = parseStatus(MAGRO);
    TEST_ASSERT_EQUAL_INT(c.agents.size(), m.agents.size());
    for (size_t i = 0; i < c.agents.size(); i++) {
        const Agent &a = c.agents[i], &b = m.agents[i];
        TEST_ASSERT_EQUAL_STRING(a.id.c_str(), b.id.c_str());
        TEST_ASSERT_EQUAL_STRING(a.repo.c_str(), b.repo.c_str());
        TEST_ASSERT_EQUAL_STRING(a.branch.c_str(), b.branch.c_str());
        TEST_ASSERT_EQUAL_STRING(a.agent.c_str(), b.agent.c_str());
        TEST_ASSERT_EQUAL_STRING(a.model.c_str(), b.model.c_str());
        TEST_ASSERT_EQUAL_STRING(a.effort.c_str(), b.effort.c_str());
        TEST_ASSERT_EQUAL_STRING(a.paneId.c_str(), b.paneId.c_str());
        TEST_ASSERT_EQUAL_INT(a.contextPct, b.contextPct);
        TEST_ASSERT_EQUAL_INT(a.hasContext, b.hasContext);
        TEST_ASSERT_EQUAL_INT((int)a.level, (int)b.level);
        TEST_ASSERT_EQUAL_INT(a.age, b.age);
        TEST_ASSERT_EQUAL_INT(a.stale, b.stale);
        TEST_ASSERT_EQUAL_INT((int)a.state, (int)b.state);
        TEST_ASSERT_EQUAL_INT(a.done, b.done);
        TEST_ASSERT_EQUAL_INT(a.hasCost, b.hasCost);
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, a.costUsd, b.costUsd);
        TEST_ASSERT_EQUAL_INT(a.hasLines, b.hasLines);
        TEST_ASSERT_EQUAL_INT(a.linesAdded, b.linesAdded);
        TEST_ASSERT_EQUAL_INT(a.linesRemoved, b.linesRemoved);
        TEST_ASSERT_EQUAL_INT(a.hasApiMs, b.hasApiMs);
        TEST_ASSERT_EQUAL_INT(a.apiMs, b.apiMs);
    }
}

void test_o_livro_caixa_sobrevive(void) {
    const Status c = parseStatus(CHEIO);
    const Status m = parseStatus(MAGRO);

    TEST_ASSERT_EQUAL_INT(c.works.known, m.works.known);
    TEST_ASSERT_EQUAL_INT(c.works.trabalhos, m.works.trabalhos);
    TEST_ASSERT_EQUAL_INT(c.works.seconds, m.works.seconds);
    TEST_ASSERT_EQUAL_INT(c.works.blocked, m.works.blocked);
    TEST_ASSERT_EQUAL_INT(c.works.mediana, m.works.mediana);
    TEST_ASSERT_EQUAL_INT(c.works.hasCost, m.works.hasCost);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, c.works.costUsd, m.works.costUsd);

    TEST_ASSERT_EQUAL_INT(c.uso.known, m.uso.known);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, c.uso.costUsd, m.uso.costUsd);
    TEST_ASSERT_EQUAL_INT(c.uso.modelos.size(), m.uso.modelos.size());
    for (size_t i = 0; i < c.uso.modelos.size(); i++) {
        TEST_ASSERT_EQUAL_STRING(c.uso.modelos[i].rotulo.c_str(),
                                 m.uso.modelos[i].rotulo.c_str());
        TEST_ASSERT_EQUAL_INT(c.uso.modelos[i].hasCost, m.uso.modelos[i].hasCost);
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, c.uso.modelos[i].costUsd,
                                 m.uso.modelos[i].costUsd);
    }

    TEST_ASSERT_EQUAL_INT(c.vitalicio.known, m.vitalicio.known);
    TEST_ASSERT_EQUAL_INT(c.vitalicio.turnos, m.vitalicio.turnos);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, c.vitalicio.costUsd, m.vitalicio.costUsd);
    TEST_ASSERT_EQUAL_STRING(c.vitalicio.desde.c_str(), m.vitalicio.desde.c_str());
}

void test_o_clima_sobrevive(void) {
    const Status c = parseStatus(CHEIO);
    const Status m = parseStatus(MAGRO);
    TEST_ASSERT_EQUAL_INT(c.weather.known, m.weather.known);
    TEST_ASSERT_EQUAL_INT(c.weather.temp, m.weather.temp);
    TEST_ASSERT_EQUAL_INT(c.weather.tmin, m.weather.tmin);
    TEST_ASSERT_EQUAL_INT(c.weather.tmax, m.weather.tmax);
    TEST_ASSERT_EQUAL_INT(c.weather.age, m.weather.age);
    TEST_ASSERT_EQUAL_STRING(c.weather.text.c_str(), m.weather.text.c_str());
    TEST_ASSERT_EQUAL_STRING(c.weather.city.c_str(), m.weather.city.c_str());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_o_enxuto_e_de_fato_menor);
    RUN_TEST(test_o_topo_sobrevive_ao_corte);
    RUN_TEST(test_os_limites_sobrevivem);
    RUN_TEST(test_o_relogio_sobrevive_com_o_epoch);
    RUN_TEST(test_os_agentes_sobrevivem_inteiros);
    RUN_TEST(test_o_livro_caixa_sobrevive);
    RUN_TEST(test_o_clima_sobrevive);
    return UNITY_END();
}
