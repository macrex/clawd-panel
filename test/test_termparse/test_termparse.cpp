#include <unity.h>
#include <string.h>

#include "term_parse.h"

// O parser ANSI/SGR que veio do herdr-assist (MIT, ver lib/termparse/README.md).
//
// POR QUE TESTAR CODIGO DE TERCEIRO
// Nao para conferir o autor: lá ele tem teste próprio. E para travar o CONTRATO
// que este projeto passou a depender — os tetos foram reduzidos para a tela em
// pé, e a API daqui manda linhas já cortadas em 52 colunas com o SGR
// reconstruído no começo de cada uma (ver server/sgr.py). Se qualquer um dos
// dois lados mudar, é aqui que aparece.
//
// A paleta é a do painel, e não a do projeto de origem: as cores do herdr que
// já estão em src/ui.cpp.

void setUp(void) {}
void tearDown(void) {}

static const term_palette_t PAL = {
    // 8 normais + 8 bright, RGB888. Aproxima a paleta que o Claude Code usa no
    // Windows Terminal — é ela que chega nos codigos 30..37 e 90..97.
    {
        0x0c0e12, 0xeb5555, 0x40c878, 0xe6be46,
        0x6f9fd8, 0x9a7fd0, 0x5f9ea8, 0xebeef2,
        0x303642, 0xff7b7b, 0x6fe89c, 0xffd76b,
        0x93bdf0, 0xb9a1e0, 0x87c4cd, 0xffffff,
    },
    0xebeef2,   // default_fg: o FG do painel
    0x0c0e12,   // default_bg: o BG do painel
};

static term_grid_t g;

static const term_run_t *run(int linha, int i) {
    TEST_ASSERT_TRUE(linha < g.line_count);
    TEST_ASSERT_TRUE(i < g.lines[linha].run_count);
    return &g.runs[g.lines[linha].run_start + i];
}

static const char *texto(const term_run_t *r) { return &g.text[r->text_off]; }

// ---- o que a API manda ----

void test_texto_sem_escape_vira_um_run(void) {
    TEST_ASSERT_EQUAL_INT(1, term_parse("clawd-panel", &g, &PAL));
    TEST_ASSERT_EQUAL_INT(1, g.lines[0].run_count);
    TEST_ASSERT_EQUAL_STRING("clawd-panel", texto(run(0, 0)));
    TEST_ASSERT_EQUAL_UINT32(PAL.default_fg, run(0, 0)->fg);
    TEST_ASSERT_EQUAL_INT(11, g.lines[0].cols);
}

void test_a_cor_separa_os_runs(void) {
    // Uma linha com duas cores tem que virar dois runs, cada um com a sua — e
    // o texto de cada um sem nenhum byte de escape.
    TEST_ASSERT_EQUAL_INT(1, term_parse("\x1b[32mok\x1b[0m fim", &g, &PAL));
    TEST_ASSERT_EQUAL_INT(2, g.lines[0].run_count);
    TEST_ASSERT_EQUAL_STRING("ok", texto(run(0, 0)));
    TEST_ASSERT_EQUAL_UINT32(PAL.palette16[2], run(0, 0)->fg);
    TEST_ASSERT_EQUAL_STRING(" fim", texto(run(0, 1)));
    TEST_ASSERT_EQUAL_UINT32(PAL.default_fg, run(0, 1)->fg);
}

// O herdr manda truecolor: as reguas do Claude Code vem em 38;2;136;136;136.
void test_truecolor_chega_inteiro(void) {
    TEST_ASSERT_EQUAL_INT(1, term_parse("\x1b[38;2;136;136;136m---", &g, &PAL));
    TEST_ASSERT_EQUAL_UINT32(0x888888u, run(0, 0)->fg);
}

void test_cor_de_256_usa_a_paleta(void) {
    TEST_ASSERT_EQUAL_INT(1, term_parse("\x1b[38;5;2mverde", &g, &PAL));
    TEST_ASSERT_EQUAL_UINT32(PAL.palette16[2], run(0, 0)->fg);
}

// O `server/sgr.py` reconstrói o SGR no começo de CADA linha, para que a placa
// possa desenhar qualquer uma sem ter visto as anteriores. Este teste é o outro
// lado desse contrato.
void test_cada_linha_pode_abrir_a_propria_cor(void) {
    const char *duas = "\x1b[0m\x1b[31mvermelho\n\x1b[0m\x1b[32mverde";
    TEST_ASSERT_EQUAL_INT(2, term_parse(duas, &g, &PAL));
    TEST_ASSERT_EQUAL_UINT32(PAL.palette16[1], run(0, 0)->fg);
    TEST_ASSERT_EQUAL_UINT32(PAL.palette16[2], run(1, 0)->fg);
}

// E o inverso: sem reset, o estado ATRAVESSA a linha. É o que garante que uma
// linha cortada ao meio pelo `sgr.quebrar` continue na cor certa.
void test_o_estado_atravessa_a_quebra_de_linha(void) {
    TEST_ASSERT_EQUAL_INT(2, term_parse("\x1b[31mcima\nbaixo", &g, &PAL));
    TEST_ASSERT_EQUAL_UINT32(PAL.palette16[1], run(1, 0)->fg);
}

void test_bold_e_dim_viram_flags(void) {
    TEST_ASSERT_EQUAL_INT(1, term_parse("\x1b[1mforte", &g, &PAL));
    TEST_ASSERT_TRUE(run(0, 0)->flags & TERM_F_BOLD);
    TEST_ASSERT_EQUAL_INT(1, term_parse("\x1b[2mfraco", &g, &PAL));
    TEST_ASSERT_TRUE(run(0, 0)->flags & TERM_F_DIM);
}

// O reverso é o cursor e a seleção do Claude Code: sem ele o texto some no
// próprio fundo.
void test_reverso_troca_frente_e_fundo(void) {
    TEST_ASSERT_EQUAL_INT(1, term_parse("\x1b[7mrev", &g, &PAL));
    TEST_ASSERT_EQUAL_UINT32(PAL.default_bg, run(0, 0)->fg);
    TEST_ASSERT_TRUE(run(0, 0)->flags & TERM_F_HAS_BG);
    TEST_ASSERT_EQUAL_UINT32(PAL.default_fg, run(0, 0)->bg);
}

// Fundo diferente do padrão precisa de retângulo antes do texto — e o parser
// avisa isso pela flag, para o laço de desenho não pintar a tela inteira.
void test_fundo_proprio_e_anunciado(void) {
    TEST_ASSERT_EQUAL_INT(1, term_parse("\x1b[41merro", &g, &PAL));
    TEST_ASSERT_TRUE(run(0, 0)->flags & TERM_F_HAS_BG);
    TEST_ASSERT_EQUAL_UINT32(PAL.palette16[1], run(0, 0)->bg);
}

void test_sequencia_que_nao_e_sgr_e_descartada(void) {
    // O `sgr.py` já tira o que não é SGR, mas o parser não pode confiar nisso:
    // um escape que passe não pode virar texto na tela.
    TEST_ASSERT_EQUAL_INT(1, term_parse("\x1b[2J\x1b[Hoi", &g, &PAL));
    TEST_ASSERT_EQUAL_STRING("oi", texto(run(0, 0)));
}

void test_escape_truncado_no_fim_nao_estoura(void) {
    TEST_ASSERT_EQUAL_INT(1, term_parse("oi\x1b[3", &g, &PAL));
    TEST_ASSERT_EQUAL_STRING("oi", texto(run(0, 0)));
}

// String vazia e caso normal (a tela do agente ainda nao chegou). NULL NAO e:
// medido aqui, `term_parse(NULL, ...)` derruba o processo com violacao de
// acesso, porque o parser desreferencia o ponteiro sem checar. O contrato e do
// chamador, e `drawTerminal` o cumpre — este teste existe para que a regra
// fique escrita em algum lugar que falha se alguem a esquecer.
void test_entrada_vazia_nao_estoura(void) {
    TEST_ASSERT_EQUAL_INT(0, term_parse("", &g, &PAL));
}

// A tela em pé tem 52 colunas e os tetos foram reduzidos para ela. Uma linha
// mais larga que o teto não pode escrever fora do grid — ela é cortada e a
// bandeira de overflow sobe.
void test_linha_larga_demais_e_cortada_sem_estourar(void) {
    char longa[TERM_MAX_COLS * 3];
    memset(longa, 'x', sizeof(longa) - 1);
    longa[sizeof(longa) - 1] = 0;
    TEST_ASSERT_EQUAL_INT(1, term_parse(longa, &g, &PAL));
    TEST_ASSERT_TRUE(g.lines[0].cols <= TERM_MAX_COLS);
    TEST_ASSERT_TRUE(g.overflow);
}

void test_mais_linhas_que_o_teto_param_no_teto(void) {
    static char muitas[TERM_MAX_LINES * 4 + 8];
    int n = 0;
    for (int i = 0; i < TERM_MAX_LINES + 10; i++) {
        muitas[n++] = 'a';
        muitas[n++] = '\n';
    }
    muitas[n] = 0;
    const int linhas = term_parse(muitas, &g, &PAL);
    TEST_ASSERT_TRUE(linhas <= TERM_MAX_LINES);
    TEST_ASSERT_TRUE(g.overflow);
}

// Uma tela de verdade do Claude Code, como a API a entrega: já em ASCII, já
// cortada em 52 colunas, com o SGR reaberto no começo de cada linha.
void test_uma_tela_como_a_api_entrega(void) {
    const char *tela =
        "\x1b[0m\x1b[38;2;136;136;136m" "--------------------------" "\n"
        "\x1b[0m\x1b[38;5;7m" "* clawd-panel  main | Opus 5 (1M)" "\n"
        "\x1b[0m" "  " "\x1b[38;5;2m" "OK" "\x1b[0m" " 389 testes passam" "\n";
    TEST_ASSERT_EQUAL_INT(3, term_parse(tela, &g, &PAL));
    TEST_ASSERT_FALSE(g.overflow);
    TEST_ASSERT_EQUAL_UINT32(0x888888u, run(0, 0)->fg);
    TEST_ASSERT_EQUAL_STRING("* clawd-panel  main | Opus 5 (1M)", texto(run(1, 0)));
    // A terceira linha tem três runs: o espaço, o OK verde e o resto.
    TEST_ASSERT_EQUAL_INT(3, g.lines[2].run_count);
    TEST_ASSERT_EQUAL_UINT32(PAL.palette16[2], run(2, 1)->fg);
    TEST_ASSERT_EQUAL_STRING("OK", texto(run(2, 1)));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_texto_sem_escape_vira_um_run);
    RUN_TEST(test_a_cor_separa_os_runs);
    RUN_TEST(test_truecolor_chega_inteiro);
    RUN_TEST(test_cor_de_256_usa_a_paleta);
    RUN_TEST(test_cada_linha_pode_abrir_a_propria_cor);
    RUN_TEST(test_o_estado_atravessa_a_quebra_de_linha);
    RUN_TEST(test_bold_e_dim_viram_flags);
    RUN_TEST(test_reverso_troca_frente_e_fundo);
    RUN_TEST(test_fundo_proprio_e_anunciado);
    RUN_TEST(test_sequencia_que_nao_e_sgr_e_descartada);
    RUN_TEST(test_escape_truncado_no_fim_nao_estoura);
    RUN_TEST(test_entrada_vazia_nao_estoura);
    RUN_TEST(test_linha_larga_demais_e_cortada_sem_estourar);
    RUN_TEST(test_mais_linhas_que_o_teto_param_no_teto);
    RUN_TEST(test_uma_tela_como_a_api_entrega);
    return UNITY_END();
}
