#include <unity.h>
#include <string>
#include <vector>
#include "reset_watch.h"

void setUp(void) {}
void tearDown(void) {}

// As cinco fases da virada da janela de 5h, na ordem em que acontecem. NAO sao
// payloads escritos a mao: sairam do `build_status()` do proprio servidor
// (server/claude_metrics_api.py), rodado com o carimbo da sessao empurrado para
// cada momento da linha do tempo. Sao os campos de limite exatamente como
// chegam ao painel.
//
// O que eles mostram, e que nenhum teste via antes: entre a virada e a proxima
// chamada de API o prazo NAO salta — ele vai a ZERO e fica la, porque o
// servidor se recusa a inventar o horario de uma janela que ninguem carimbou
// ainda. O painel esperava um salto que so acontece depois desse zero, e
// comparar com zero era justamente o que o gatilho descartava.
static const char *P_CORRENDO = R"json({"session_pct": 88, "session_resets_in": 119,
"session_resets_hm": "0h01m", "session_known": true, "session_inferred": false,
"week_pct": 50, "week_resets_in": 259199, "week_resets_dh": "2d23h", "week_known": true,
"limits_fresh": true, "colors": {"context": "green", "session": "red", "week": "yellow"}})json";

static const char *P_QUASE = R"json({"session_pct": 88, "session_resets_in": 3,
"session_resets_hm": "0h00m", "session_known": true, "session_inferred": false,
"week_pct": 50, "week_resets_in": 259199, "week_resets_dh": "2d23h", "week_known": true,
"limits_fresh": true, "colors": {"context": "green", "session": "red", "week": "yellow"}})json";

static const char *P_VIROU = R"json({"session_pct": 0, "session_resets_in": 0,
"session_resets_hm": "-", "session_known": true, "session_inferred": true,
"week_pct": 50, "week_resets_in": 259199, "week_resets_dh": "2d23h", "week_known": true,
"limits_fresh": false, "colors": {"context": "green", "session": "green", "week": "yellow"}})json";

static const char *P_ESPERANDO = R"json({"session_pct": 0, "session_resets_in": 0,
"session_resets_hm": "-", "session_known": true, "session_inferred": true,
"week_pct": 50, "week_resets_in": 259199, "week_resets_dh": "2d23h", "week_known": true,
"limits_fresh": false, "colors": {"context": "green", "session": "green", "week": "yellow"}})json";

static const char *P_CARIMBO_NOVO = R"json({"session_pct": 2, "session_resets_in": 17699,
"session_resets_hm": "4h54m", "session_known": true, "session_inferred": false,
"week_pct": 50, "week_resets_in": 259199, "week_resets_dh": "2d23h", "week_known": true,
"limits_fresh": true, "colors": {"context": "green", "session": "green", "week": "yellow"}})json";

// Roda uma sequencia de polls e devolve o que a tela anunciou em cada um
// ("" = nao armou). Um vetor, e nao um contador: o teste precisa dizer QUANDO
// disparou, senao "armou uma vez" nao distingue a virada de um susto no poll
// seguinte.
static std::vector<std::string> rodar(const std::vector<const char *> &polls) {
    ResetWatch w;
    std::vector<std::string> saidas;
    for (const char *json : polls) {
        const Status      s = parseStatus(json);
        const char *const q = detectarReset(w, s);
        saidas.push_back(q ? q : "");
    }
    return saidas;
}

static int quantasVezesArmou(const std::vector<std::string> &saidas) {
    int n = 0;
    for (const std::string &s : saidas) n += !s.empty();
    return n;
}

// O boot nao e uma virada: no primeiro poll nao ha valor anterior, e anunciar
// reset ali seria chute — inclusive quando a placa liga com a janela ja virada.
void test_primeiro_poll_nunca_dispara(void) {
    TEST_ASSERT_EQUAL_STRING("", rodar({P_VIROU})[0].c_str());
    TEST_ASSERT_EQUAL_STRING("", rodar({P_CORRENDO})[0].c_str());
}

void test_prazo_encolhendo_nao_dispara(void) {
    const auto saidas = rodar({P_CORRENDO, P_QUASE, P_QUASE});
    TEST_ASSERT_EQUAL_INT(0, quantasVezesArmou(saidas));
}

// O CASO QUE FALTAVA, e o motivo desta suite existir: a virada de verdade,
// poll a poll, com os payloads que o servidor publica.
void test_a_virada_real_arma_a_tela(void) {
    const auto saidas = rodar({P_CORRENDO, P_QUASE, P_VIROU, P_ESPERANDO,
                               P_CARIMBO_NOVO});
    TEST_ASSERT_EQUAL_STRING("SESSAO", saidas[2].c_str());
}

// Uma virada, um aviso. O prazo continua zerado por polls a fio ate alguem
// chamar a API, e uma tela de 6 s reaparecendo a cada dois segundos durante
// esse tempo seria pior do que nao avisar.
void test_a_virada_arma_uma_vez_so(void) {
    const auto saidas = rodar({P_CORRENDO, P_QUASE, P_VIROU, P_ESPERANDO,
                               P_ESPERANDO, P_CARIMBO_NOVO});
    TEST_ASSERT_EQUAL_INT(1, quantasVezesArmou(saidas));
}

// O caminho raro: a janela vira e alguem chama a API DENTRO do mesmo intervalo
// de poll, entao o prazo pula direto de um numero pequeno para uma janela
// inteira, sem passar pelo zero. E o unico caso que o gatilho antigo pegava, e
// ele continua valendo.
void test_carimbo_novo_no_mesmo_poll_ainda_dispara(void) {
    const auto saidas = rodar({P_QUASE, P_CARIMBO_NOVO});
    TEST_ASSERT_EQUAL_STRING("SESSAO", saidas[1].c_str());
}

// A janela de 7 dias desliza e o servidor nao infere virada nenhuma para ela:
// o salto do prazo e o unico sinal que existe, e ele continua de pe.
void test_salto_da_semana_dispara_semana(void) {
    static const char *SEMANA_CURTA = R"json({"session_pct": 10, "session_resets_in": 9000,
"session_known": true, "week_pct": 99, "week_resets_in": 120, "week_known": true})json";
    static const char *SEMANA_NOVA = R"json({"session_pct": 10, "session_resets_in": 9000,
"session_known": true, "week_pct": 3, "week_resets_in": 604000, "week_known": true})json";
    const auto saidas = rodar({SEMANA_CURTA, SEMANA_NOVA});
    TEST_ASSERT_EQUAL_STRING("SEMANA", saidas[1].c_str());
}

// Uma API antiga nao publica `session_inferred`. Sem o campo o painel nao
// inventa virada: fica com o salto, que e o que ele sempre teve.
void test_api_sem_o_campo_novo_nao_inventa_virada(void) {
    static const char *ANTIGA_CORRENDO = R"json({"session_pct": 88,
"session_resets_in": 300, "session_known": true, "week_known": false})json";
    static const char *ANTIGA_ZERADA = R"json({"session_pct": 0,
"session_resets_in": 0, "session_known": true, "week_known": false})json";
    const auto saidas = rodar({ANTIGA_CORRENDO, ANTIGA_ZERADA});
    TEST_ASSERT_EQUAL_INT(0, quantasVezesArmou(saidas));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_primeiro_poll_nunca_dispara);
    RUN_TEST(test_prazo_encolhendo_nao_dispara);
    RUN_TEST(test_a_virada_real_arma_a_tela);
    RUN_TEST(test_a_virada_arma_uma_vez_so);
    RUN_TEST(test_carimbo_novo_no_mesmo_poll_ainda_dispara);
    RUN_TEST(test_salto_da_semana_dispara_semana);
    RUN_TEST(test_api_sem_o_campo_novo_nao_inventa_virada);
    return UNITY_END();
}
