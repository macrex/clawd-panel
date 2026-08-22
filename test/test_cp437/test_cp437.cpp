#include <unity.h>
#include <string.h>

#include "cp437.h"

// A ponte entre o UTF-8 que a API manda e a fonte CP437 que a placa desenha.
//
// O contrato tem DOIS lados e os dois quebram em silencio: se o mapa errar, a
// tabela do Claude Code sai com o glifo errado; se a contagem de bytes errar, a
// linha inteira desalinha a partir dali — e alinhamento por coluna e a unica
// razao de existir esta tela.

void setUp(void) {}
void tearDown(void) {}

static uint8_t um(const char *s, int &n) {
    return cp437::proximo(s, strlen(s), n);
}

void test_ascii_passa_intacto(void) {
    int n = 0;
    TEST_ASSERT_EQUAL_UINT8('A', um("A", n));
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_UINT8(' ', um(" ", n));
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_UINT8('~', um("~", n));
}

// O caractere mais frequente da tela: 688 de 779 nao-ASCII medidos numa tela.
void test_a_regua_horizontal_vira_o_glifo_da_fonte(void) {
    int n = 0;
    TEST_ASSERT_EQUAL_UINT8(0xC4, um("─", n));
    TEST_ASSERT_EQUAL_INT(3, n);
}

void test_os_quatro_cantos_e_o_cruzamento(void) {
    int n = 0;
    TEST_ASSERT_EQUAL_UINT8(0xDA, um("┌", n));   // ┌
    TEST_ASSERT_EQUAL_UINT8(0xBF, um("┐", n));   // ┐
    TEST_ASSERT_EQUAL_UINT8(0xC0, um("└", n));   // └
    TEST_ASSERT_EQUAL_UINT8(0xD9, um("┘", n));   // ┘
    TEST_ASSERT_EQUAL_UINT8(0xC5, um("┼", n));   // ┼
}

// O Claude Code desenha as molduras dele com canto ARREDONDADO, e a CP437 nao
// tem esse glifo. O reto e o mesmo desenho com o canto vivo — melhor que um `?`
// em cada quina de cada caixa.
void test_canto_arredondado_cai_no_reto(void) {
    int n = 0;
    TEST_ASSERT_EQUAL_UINT8(0xDA, um("╭", n));   // ╭ -> ┌
    TEST_ASSERT_EQUAL_UINT8(0xBF, um("╮", n));   // ╮ -> ┐
    TEST_ASSERT_EQUAL_UINT8(0xD9, um("╯", n));   // ╯ -> ┘
    TEST_ASSERT_EQUAL_UINT8(0xC0, um("╰", n));   // ╰ -> └
}

void test_blocos_e_sombras(void) {
    int n = 0;
    TEST_ASSERT_EQUAL_UINT8(0xDB, um("█", n));   // █
    TEST_ASSERT_EQUAL_UINT8(0xB1, um("▒", n));   // ▒
    TEST_ASSERT_EQUAL_UINT8(0xDF, um("▀", n));   // ▀
}

// Fora da tabela vira '?' VISIVEL, e nao nada: um caractere que some do meio de
// uma tabela empurra o resto da linha uma coluna para a esquerda.
void test_fora_da_tabela_vira_interrogacao_e_gasta_os_bytes(void) {
    int n = 0;
    TEST_ASSERT_EQUAL_UINT8('?', um("☃", n));    // boneco de neve
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_UINT8('?', um("ç", n));    // c-cedilha (2 bytes)
    TEST_ASSERT_EQUAL_INT(2, n);
}

// UMA COLUNA POR CARACTERE. Andar a quantidade errada de bytes desalinha tudo
// dali para a frente, que e o defeito mais caro possivel nesta tela.
void test_a_contagem_de_colunas_bate_com_a_do_terminal(void) {
    const char *linha = "┌──┐ ok";   // ┌──┐ ok
    int cols = 0;
    size_t i = 0, n = strlen(linha);
    while (i < n) {
        int gasto = 0;
        cp437::proximo(linha + i, n - i, gasto);
        i += gasto;
        cols++;
    }
    TEST_ASSERT_EQUAL_INT(7, cols);   // ┌ ─ ─ ┐ espaco o k
}

void test_entradas_degeneradas_nao_travam(void) {
    int n = 0;
    TEST_ASSERT_EQUAL_UINT8(0, cp437::proximo(nullptr, 5, n));
    TEST_ASSERT_EQUAL_UINT8(0, cp437::proximo("a", 0, n));

    // Sequencia TRUNCADA: dois bytes de uma de tres. Tem que gastar ao menos um
    // byte, senao o laco de quem chama nunca sai do lugar.
    char truncada[3] = {(char)0xE2, (char)0x94, 0};
    TEST_ASSERT_EQUAL_UINT8('?', cp437::proximo(truncada, 2, n));
    TEST_ASSERT_TRUE(n >= 1);

    // Byte de continuacao solto, sem o lider.
    char solto[2] = {(char)0x94, 0};
    TEST_ASSERT_EQUAL_UINT8('?', cp437::proximo(solto, 1, n));
    TEST_ASSERT_EQUAL_INT(1, n);
}

// Uma linha inteira como o Claude Code a desenha, com moldura arredondada.
void test_uma_moldura_do_claude_code(void) {
    const char *m = "╭───╮";
    const uint8_t esperado[] = {0xDA, 0xC4, 0xC4, 0xC4, 0xBF};
    size_t i = 0, n = strlen(m);
    int k = 0;
    while (i < n) {
        int gasto = 0;
        TEST_ASSERT_EQUAL_UINT8(esperado[k], cp437::proximo(m + i, n - i, gasto));
        i += gasto;
        k++;
    }
    TEST_ASSERT_EQUAL_INT(5, k);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_ascii_passa_intacto);
    RUN_TEST(test_a_regua_horizontal_vira_o_glifo_da_fonte);
    RUN_TEST(test_os_quatro_cantos_e_o_cruzamento);
    RUN_TEST(test_canto_arredondado_cai_no_reto);
    RUN_TEST(test_blocos_e_sombras);
    RUN_TEST(test_fora_da_tabela_vira_interrogacao_e_gasta_os_bytes);
    RUN_TEST(test_a_contagem_de_colunas_bate_com_a_do_terminal);
    RUN_TEST(test_entradas_degeneradas_nao_travam);
    RUN_TEST(test_uma_moldura_do_claude_code);
    return UNITY_END();
}
