#include <unity.h>
#include <string>
#include "cache.h"

void setUp(void) {}
void tearDown(void) {}

// 2026-08-15 10:00 local. O reset da semana deste painel cai as 10:59.
static const long AGORA   = 1786788000L;
static const long DEPOIS  = AGORA + 3600L;   // uma hora mais tarde

static Status retratoCheio() {
    Status s;
    s.valid = true;

    s.session.known    = true;
    s.session.pct      = 63;
    s.session.level    = Level::Yellow;
    s.session.resetsIn = 7200;               // vira as 12:00
    s.session.at       = "12:00pm";

    s.week.known    = true;
    s.week.pct      = 91;
    s.week.level    = Level::Red;
    s.week.resetsIn = 3540;                  // vira as 10:59
    s.week.at       = "15/08/2026 (Sabado)";

    s.works.known     = true;
    s.works.trabalhos = 12;
    s.works.seconds   = 4200;
    s.works.blocked   = 600;
    s.works.mediana   = 300;
    s.works.hasCost   = true;
    s.works.costUsd   = 3.75f;

    s.uso.known   = true;
    s.uso.costUsd = 3.75f;
    UsoModelo opus;  opus.rotulo = "Opus 5";  opus.costUsd = 3.0f; opus.hasCost = true;
    UsoModelo outro; outro.rotulo = "Fable 5"; outro.hasCost = false;
    s.uso.modelos.push_back(opus);
    s.uso.modelos.push_back(outro);

    s.vitalicio.known   = true;
    s.vitalicio.turnos  = 4210;
    s.vitalicio.costUsd = 812.5f;
    s.vitalicio.desde   = "2026-06-01";

    // O que NAO deve voltar: retratos do agora.
    s.sessions = 3;
    Agent a; a.id = "abc12345"; a.repo = "esp32-s3";
    s.agents.push_back(a);
    s.bloqueio.known = true;
    s.bloqueio.pergunta = "posso apagar?";
    return s;
}

void test_ida_e_volta_preserva_os_limites(void) {
    const std::string txt = serializarCache(retratoCheio(), AGORA);
    TEST_ASSERT_FALSE(txt.empty());

    Status v;
    int idade = -1;
    TEST_ASSERT_TRUE(lerCache(txt.c_str(), AGORA, v, idade));
    TEST_ASSERT_EQUAL_INT(0, idade);

    TEST_ASSERT_TRUE(v.session.known);
    TEST_ASSERT_EQUAL_INT(63, v.session.pct);
    TEST_ASSERT_EQUAL_INT((int)Level::Yellow, (int)v.session.level);
    TEST_ASSERT_EQUAL_INT(7200, v.session.resetsIn);
    TEST_ASSERT_EQUAL_STRING("12:00pm", v.session.at.c_str());

    TEST_ASSERT_TRUE(v.week.known);
    TEST_ASSERT_EQUAL_INT(91, v.week.pct);
    TEST_ASSERT_EQUAL_INT((int)Level::Red, (int)v.week.level);
    TEST_ASSERT_EQUAL_INT(3540, v.week.resetsIn);
}

void test_o_prazo_e_um_instante_e_nao_uma_duracao(void) {
    // Gravado as 10:00 com 59 minutos para a virada; lido as 11:00, quando ela
    // JA passou. Um cache que guardasse "faltam 3540 s" diria, uma hora depois,
    // que ainda faltam 59 minutos — que e exatamente o defeito que motivou isto.
    const std::string txt = serializarCache(retratoCheio(), AGORA);

    Status v;
    int idade = 0;
    TEST_ASSERT_TRUE(lerCache(txt.c_str(), DEPOIS, v, idade));
    TEST_ASSERT_EQUAL_INT(3600, idade);
    TEST_ASSERT_EQUAL_INT(0, v.week.resetsIn);        // venceu
    TEST_ASSERT_EQUAL_INT(3600, v.session.resetsIn);  // 7200 - 3600
}

void test_o_que_e_do_agora_nao_volta(void) {
    const std::string txt = serializarCache(retratoCheio(), AGORA);
    Status v;
    int idade = 0;
    TEST_ASSERT_TRUE(lerCache(txt.c_str(), AGORA, v, idade));

    // Uma sessao viva num PC desligado nao e dado velho, e mentira.
    TEST_ASSERT_EQUAL_INT(0, v.sessions);
    TEST_ASSERT_EQUAL_INT(0, (int)v.agents.size());
    TEST_ASSERT_FALSE(v.bloqueio.known);
    TEST_ASSERT_FALSE(v.online);

    // E a marca de PROCEDENCIA, sem a qual este retrato e indistinguivel de um
    // /status dizendo "nao ha sessao nenhuma agora" — e a tela respondia aos
    // dois com "CLAUDIO OFFLINE", escondendo os limites que o cache trouxe.
    TEST_ASSERT_TRUE(v.doCache);
}

void test_status_da_rede_nao_se_diz_do_cache(void) {
    // A outra ponta da mesma marca: o padrao e falso, entao qualquer Status que
    // nao passou por lerCache continua se apresentando como o que e.
    Status daRede;
    TEST_ASSERT_FALSE(daRede.doCache);
}

void test_o_livro_caixa_volta_inteiro(void) {
    const std::string txt = serializarCache(retratoCheio(), AGORA);
    Status v;
    int idade = 0;
    TEST_ASSERT_TRUE(lerCache(txt.c_str(), AGORA, v, idade));

    TEST_ASSERT_TRUE(v.works.known);
    TEST_ASSERT_EQUAL_INT(12, v.works.trabalhos);
    TEST_ASSERT_EQUAL_INT(4200, v.works.seconds);
    TEST_ASSERT_EQUAL_INT(600, v.works.blocked);
    TEST_ASSERT_EQUAL_INT(300, v.works.mediana);
    TEST_ASSERT_TRUE(v.works.hasCost);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.75f, v.works.costUsd);

    TEST_ASSERT_TRUE(v.uso.known);
    TEST_ASSERT_EQUAL_INT(2, (int)v.uso.modelos.size());
    TEST_ASSERT_EQUAL_STRING("Opus 5", v.uso.modelos[0].rotulo.c_str());
    TEST_ASSERT_TRUE(v.uso.modelos[0].hasCost);
    // Modelo fora da tabela de preco: nulo e diferente de zero, e o card
    // desenha "-" no lugar de "US$ 0,00".
    TEST_ASSERT_FALSE(v.uso.modelos[1].hasCost);

    TEST_ASSERT_TRUE(v.vitalicio.known);
    TEST_ASSERT_EQUAL_INT(4210, (int)v.vitalicio.turnos);
    TEST_ASSERT_EQUAL_STRING("2026-06-01", v.vitalicio.desde.c_str());
}

void test_sem_relogio_nao_grava_nem_le(void) {
    // Sem hora nao da para carimbar o retrato, e um retrato sem data nao pode
    // ser envelhecido depois — melhor nao existir do que existir sem idade.
    TEST_ASSERT_TRUE(serializarCache(retratoCheio(), 0).empty());

    const std::string txt = serializarCache(retratoCheio(), AGORA);
    Status v;
    int idade = 0;
    TEST_ASSERT_FALSE(lerCache(txt.c_str(), 0, v, idade));
}

void test_lixo_no_cartao_nao_derruba_o_boot(void) {
    Status v;
    int idade = 0;
    TEST_ASSERT_FALSE(lerCache("", AGORA, v, idade));
    TEST_ASSERT_FALSE(lerCache("{", AGORA, v, idade));
    TEST_ASSERT_FALSE(lerCache("{\"nada\":1}", AGORA, v, idade));
    // Versao futura: melhor ignorar do que adivinhar o formato.
    TEST_ASSERT_FALSE(lerCache("{\"v\":99,\"em\":1786788000}", AGORA, v, idade));
}

void test_cache_do_futuro_e_descartado(void) {
    // O relogio pode ter voltado atras (placa que sincronizou errado uma vez).
    // Uma idade negativa faria os prazos ESTICAREM.
    const std::string txt = serializarCache(retratoCheio(), DEPOIS);
    Status v;
    int idade = 0;
    TEST_ASSERT_FALSE(lerCache(txt.c_str(), AGORA, v, idade));
}

void test_janela_sem_carimbo_continua_sem_carimbo(void) {
    // A API nao sabia o prazo (resetsIn == 0): o cache nao pode inventar um
    // instante para ele, senao a volta diria que a janela vira agora.
    Status s;
    s.valid = true;
    s.session.known = true;
    s.session.pct = 40;
    s.session.resetsIn = 0;
    s.week.known = false;

    const std::string txt = serializarCache(s, AGORA);
    Status v;
    int idade = 0;
    TEST_ASSERT_TRUE(lerCache(txt.c_str(), DEPOIS, v, idade));
    TEST_ASSERT_TRUE(v.session.known);
    TEST_ASSERT_EQUAL_INT(0, v.session.resetsIn);
    TEST_ASSERT_FALSE(v.week.known);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_ida_e_volta_preserva_os_limites);
    RUN_TEST(test_o_prazo_e_um_instante_e_nao_uma_duracao);
    RUN_TEST(test_o_que_e_do_agora_nao_volta);
    RUN_TEST(test_status_da_rede_nao_se_diz_do_cache);
    RUN_TEST(test_o_livro_caixa_volta_inteiro);
    RUN_TEST(test_sem_relogio_nao_grava_nem_le);
    RUN_TEST(test_lixo_no_cartao_nao_derruba_o_boot);
    RUN_TEST(test_cache_do_futuro_e_descartado);
    RUN_TEST(test_janela_sem_carimbo_continua_sem_carimbo);
    return UNITY_END();
}
