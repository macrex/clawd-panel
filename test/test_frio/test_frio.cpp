#include <unity.h>
#include "frio.h"
#include "neturl.h"

void setUp(void) {}
void tearDown(void) {}

// Um payload com os tres blocos frios preenchidos.
static Status comBlocos(const char *resumo) {
    Status s;
    s.valid = true;
    s.frio  = resumo;

    s.works.known     = true;
    s.works.trabalhos = 7;
    s.works.seconds   = 4200;

    s.uso.known   = true;
    s.uso.costUsd = 1.25f;
    UsoModelo m;
    m.rotulo  = "Opus 5";
    m.hasCost = true;
    m.costUsd = 1.25f;
    s.uso.modelos.push_back(m);

    s.vitalicio.known   = true;
    s.vitalicio.turnos  = 900;
    s.vitalicio.costUsd = 310.0f;
    s.vitalicio.desde   = "2026-06-01";
    return s;
}

// O mesmo payload com os blocos OMITIDOS, como o servidor os manda quando o
// resumo bate.
static Status semBlocos(const char *resumo) {
    Status s;
    s.valid = true;
    s.frio  = resumo;
    return s;
}

// ---- O ciclo normal ----

void test_o_primeiro_poll_guarda_os_blocos(void) {
    BlocosFrios f;
    Status s = comBlocos("abc123abc123");
    aplicarFrios(f, s);

    TEST_ASSERT_EQUAL_STRING("abc123abc123", f.resumo.c_str());
    TEST_ASSERT_EQUAL_INT(7, f.works.trabalhos);
    TEST_ASSERT_EQUAL_INT(900, (int)f.vitalicio.turnos);
    // E o proprio status nao foi tocado.
    TEST_ASSERT_TRUE(s.works.known);
    TEST_ASSERT_EQUAL_INT(7, s.works.trabalhos);
}

void test_o_poll_seguinte_reaproveita_o_que_foi_guardado(void) {
    BlocosFrios f;
    Status primeiro = comBlocos("abc123abc123");
    aplicarFrios(f, primeiro);

    Status segundo = semBlocos("abc123abc123");
    TEST_ASSERT_FALSE(segundo.works.known);      // veio vazio do fio
    aplicarFrios(f, segundo);

    TEST_ASSERT_TRUE(segundo.works.known);
    TEST_ASSERT_EQUAL_INT(7, segundo.works.trabalhos);
    TEST_ASSERT_EQUAL_INT(4200, segundo.works.seconds);
    TEST_ASSERT_TRUE(segundo.uso.known);
    TEST_ASSERT_EQUAL_INT(1, (int)segundo.uso.modelos.size());
    TEST_ASSERT_EQUAL_STRING("Opus 5", segundo.uso.modelos[0].rotulo.c_str());
    TEST_ASSERT_TRUE(segundo.vitalicio.known);
    TEST_ASSERT_EQUAL_STRING("2026-06-01", segundo.vitalicio.desde.c_str());
}

// O turno terminou: o conteudo mudou, o resumo mudou, o servidor mandou os
// blocos de novo. E o que a placa guarda passa a ser o novo.
void test_conteudo_novo_substitui_o_guardado(void) {
    BlocosFrios f;
    Status primeiro = comBlocos("aaaaaaaaaaaa");
    aplicarFrios(f, primeiro);

    Status segundo = comBlocos("bbbbbbbbbbbb");
    segundo.works.trabalhos = 8;
    aplicarFrios(f, segundo);

    TEST_ASSERT_EQUAL_STRING("bbbbbbbbbbbb", f.resumo.c_str());
    TEST_ASSERT_EQUAL_INT(8, f.works.trabalhos);
}

// ---- O que NAO pode acontecer ----

// Resumo diferente com os blocos omitidos e uma resposta incoerente (a placa
// mandou um resumo e o servidor respondeu com outro sem mandar o conteudo).
// Preencher com o que estava guardado seria mostrar o dia de ontem.
void test_resumo_que_nao_bate_nao_inventa_nada(void) {
    BlocosFrios f;
    Status primeiro = comBlocos("aaaaaaaaaaaa");
    aplicarFrios(f, primeiro);

    Status segundo = semBlocos("zzzzzzzzzzzz");
    aplicarFrios(f, segundo);

    TEST_ASSERT_FALSE(segundo.works.known);
    TEST_ASSERT_FALSE(segundo.uso.known);
    TEST_ASSERT_FALSE(segundo.vitalicio.known);
}

// Boot: a placa nao tem nada guardado. Ate o primeiro payload completo chegar,
// os cards simplesmente nao aparecem — que e o que eles ja faziam contra uma API
// antiga.
void test_sem_nada_guardado_nao_preenche(void) {
    BlocosFrios f;
    Status s = semBlocos("abc123abc123");
    aplicarFrios(f, s);
    TEST_ASSERT_FALSE(s.works.known);
}

// API antiga, que nao publica o campo: o mecanismo fica desligado inteiro.
void test_api_sem_o_campo_desliga_o_mecanismo(void) {
    BlocosFrios f;
    Status s = comBlocos("");
    aplicarFrios(f, s);
    TEST_ASSERT_TRUE(f.resumo.empty());
    TEST_ASSERT_EQUAL_INT(0, f.works.trabalhos);   // nada foi guardado
}

// Um bloco so presente ainda conta como "o payload trouxe": o servidor omite os
// tres juntos ou nenhum, e um dia sem livro-caixa tem `works` ausente de
// legitimo.
void test_um_bloco_presente_ja_atualiza_o_guardado(void) {
    BlocosFrios f;
    Status s;
    s.valid = true;
    s.frio  = "ddddddddddd1";
    s.vitalicio.known  = true;
    s.vitalicio.turnos = 12;
    aplicarFrios(f, s);

    TEST_ASSERT_EQUAL_STRING("ddddddddddd1", f.resumo.c_str());
    TEST_ASSERT_EQUAL_INT(12, (int)f.vitalicio.turnos);
    TEST_ASSERT_FALSE(f.works.known);
}

// ---- A URL que carrega o resumo ----

void test_a_url_leva_o_resumo(void) {
    TEST_ASSERT_EQUAL_STRING(
        "http://h:8787/status?campos=painel&frio=abc123",
        urlDoStatusComFrio("http://h:8787/status", "abc123").c_str());
}

// No primeiro poll depois do boot nao ha o que reaproveitar, e pedir tudo e o
// certo.
void test_sem_resumo_a_url_e_a_de_sempre(void) {
    TEST_ASSERT_EQUAL_STRING(
        "http://h:8787/status?campos=painel",
        urlDoStatusComFrio("http://h:8787/status", "").c_str());
    TEST_ASSERT_EQUAL_STRING("", urlDoStatusComFrio("", "abc").c_str());
}

// Base do cartao que ja traz query propria: o separador continua sendo escolhido
// pela `urlDoStatus`, e o `frio` entra depois dela.
void test_base_com_query_propria(void) {
    TEST_ASSERT_EQUAL_STRING(
        "http://h:8787/status?x=1&campos=painel&frio=ff",
        urlDoStatusComFrio("http://h:8787/status?x=1", "ff").c_str());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_o_primeiro_poll_guarda_os_blocos);
    RUN_TEST(test_o_poll_seguinte_reaproveita_o_que_foi_guardado);
    RUN_TEST(test_conteudo_novo_substitui_o_guardado);
    RUN_TEST(test_resumo_que_nao_bate_nao_inventa_nada);
    RUN_TEST(test_sem_nada_guardado_nao_preenche);
    RUN_TEST(test_api_sem_o_campo_desliga_o_mecanismo);
    RUN_TEST(test_um_bloco_presente_ja_atualiza_o_guardado);
    RUN_TEST(test_a_url_leva_o_resumo);
    RUN_TEST(test_sem_resumo_a_url_e_a_de_sempre);
    RUN_TEST(test_base_com_query_propria);
    return UNITY_END();
}
