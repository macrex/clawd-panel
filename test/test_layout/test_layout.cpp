#include <unity.h>
#include "layout.h"

void setUp(void) {}
void tearDown(void) {}

void test_cobre_o_retangulo_mais_a_margem(void) {
    // O selo mais estreito: alert reduzido, 60 px a partir de x=14.
    TEST_ASSERT_EQUAL_INT(80, prefixColumns(14, 60, 6, 480));
}

void test_o_selo_mais_largo_ainda_cabe(void) {
    // idle e 72x51 nativo e cai em divisor 1, entao fica com 72 px de largura.
    // Um prefixo fixo de 80 cortaria o bicho — foi por isso que este calculo
    // deixou de ser uma constante.
    TEST_ASSERT_EQUAL_INT(92, prefixColumns(14, 72, 6, 480));
}

void test_sem_margem_para_no_fim_do_retangulo(void) {
    TEST_ASSERT_EQUAL_INT(74, prefixColumns(14, 60, 0, 480));
}

void test_passar_da_tela_vira_a_tela_inteira(void) {
    // Pedir mais que a tela nao e erro: e so um flush inteiro.
    TEST_ASSERT_EQUAL_INT(480, prefixColumns(400, 200, 0, 480));
    TEST_ASSERT_EQUAL_INT(480, prefixColumns(0, 480, 6, 480));
}

void test_largura_zero_nao_manda_nada(void) {
    // Sem cartao, crewW() devolve 0. Enviar prefixo nenhum e o
    // certo — enviar "so a margem" pintaria uma tira sem motivo.
    TEST_ASSERT_EQUAL_INT(0, prefixColumns(14, 0, 6, 480));
    TEST_ASSERT_EQUAL_INT(0, prefixColumns(14, -3, 6, 480));
}

void test_valores_negativos_nao_estouram(void) {
    TEST_ASSERT_EQUAL_INT(20, prefixColumns(-5, 20, 0, 480));   // x negativo vira 0
    TEST_ASSERT_EQUAL_INT(74, prefixColumns(14, 60, -3, 480));  // margem negativa vira 0
    TEST_ASSERT_EQUAL_INT(0, prefixColumns(14, 60, 6, 0));      // tela sem largura
}

// ---- Fileira do rodape ----

void test_fileira_soma_slots_e_vaos(void) {
    // As tres larguras reais do trio: estado (43), companheiro do meio (62) e
    // da direita (69), com 6 px de vao.
    const int w[3] = {43, 62, 69};
    TEST_ASSERT_EQUAL_INT(186, rowWidth(w, 3, 6));
    TEST_ASSERT_EQUAL_INT(14,       rowSlotX(w, 3, 6, 0, 14));
    TEST_ASSERT_EQUAL_INT(14 + 49,  rowSlotX(w, 3, 6, 1, 14));
    TEST_ASSERT_EQUAL_INT(14 + 117, rowSlotX(w, 3, 6, 2, 14));
}

void test_slot_vazio_some_da_fileira(void) {
    // Uma animacao que faltou no cartao nao pode deixar um buraco e um vao
    // sobrando: os que restaram se juntam.
    const int w[3] = {43, 0, 69};
    TEST_ASSERT_EQUAL_INT(118, rowWidth(w, 3, 6));
    TEST_ASSERT_EQUAL_INT(14 + 49, rowSlotX(w, 3, 6, 2, 14));

    const int so_um[3] = {0, 0, 69};
    TEST_ASSERT_EQUAL_INT(69, rowWidth(so_um, 3, 6));
    TEST_ASSERT_EQUAL_INT(14, rowSlotX(so_um, 3, 6, 2, 14));
}

void test_fileira_degenerada(void) {
    const int w[2] = {10, 20};
    TEST_ASSERT_EQUAL_INT(0,  rowWidth(nullptr, 2, 6));
    TEST_ASSERT_EQUAL_INT(0,  rowWidth(w, 0, 6));
    TEST_ASSERT_EQUAL_INT(30, rowWidth(w, 2, -4));       // vao negativo vira 0
    TEST_ASSERT_EQUAL_INT(14, rowSlotX(w, 2, 6, 5, 14)); // indice fora da fileira
}

void test_desenho_fica_centrado_no_slot(void) {
    // typing mede 43 e sleeping 31: os dois tem que pisar no mesmo centro,
    // senao o bicho pula de lado quando o turno comeca.
    TEST_ASSERT_EQUAL_INT(14, centerIn(14, 43, 43));
    TEST_ASSERT_EQUAL_INT(20, centerIn(14, 43, 31));
    // Maior que o slot: encosta na esquerda em vez de invadir o vizinho de tras.
    TEST_ASSERT_EQUAL_INT(14, centerIn(14, 43, 60));
}

void test_texto_que_cabe_nao_quebra(void) {
    // "7:20pm" no card de 15 caracteres: uma linha so.
    TEST_ASSERT_EQUAL_INT(6, breakAt("7:20pm", 15));
    // Exatamente na medida tambem nao quebra.
    TEST_ASSERT_EQUAL_INT(15, breakAt("123456789012345", 15));
}

void test_data_quebra_antes_do_dia_da_semana(void) {
    // O caso real: 19 caracteres num card que comporta 15. A quebra cai no
    // espaco, deixando "01/08/2026" em cima e "(Sabado)" embaixo — e nao um
    // "01/08/2026 (Sa" cortado no meio da palavra.
    TEST_ASSERT_EQUAL_INT(10, breakAt("01/08/2026 (Sabado)", 15));
}

void test_espaco_bem_no_limite_e_aproveitado(void) {
    // O espaco no indice 15 fecha a linha sem sobrar nada pendurado. Buscar so
    // ate 14 quebraria antes, desperdicando uma coluna inteira.
    TEST_ASSERT_EQUAL_INT(15, breakAt("123456789012345 xyz", 15));
}

void test_palavra_unica_longa_e_cortada_e_nao_escapa(void) {
    // Nenhum dos formatos de hoje chega aqui, mas uma linha cortada e melhor do
    // que escrita passando por cima do card vizinho.
    TEST_ASSERT_EQUAL_INT(15, breakAt("012345678901234567890", 15));
}

void test_entradas_degeneradas_nao_estouram(void) {
    TEST_ASSERT_EQUAL_INT(0, breakAt(nullptr, 15));
    TEST_ASSERT_EQUAL_INT(0, breakAt("qualquer", 0));
    TEST_ASSERT_EQUAL_INT(0, breakAt("qualquer", -3));
    TEST_ASSERT_EQUAL_INT(0, breakAt("", 15));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_texto_que_cabe_nao_quebra);
    RUN_TEST(test_data_quebra_antes_do_dia_da_semana);
    RUN_TEST(test_espaco_bem_no_limite_e_aproveitado);
    RUN_TEST(test_palavra_unica_longa_e_cortada_e_nao_escapa);
    RUN_TEST(test_entradas_degeneradas_nao_estouram);
    RUN_TEST(test_cobre_o_retangulo_mais_a_margem);
    RUN_TEST(test_o_selo_mais_largo_ainda_cabe);
    RUN_TEST(test_sem_margem_para_no_fim_do_retangulo);
    RUN_TEST(test_passar_da_tela_vira_a_tela_inteira);
    RUN_TEST(test_largura_zero_nao_manda_nada);
    RUN_TEST(test_valores_negativos_nao_estouram);
    RUN_TEST(test_fileira_soma_slots_e_vaos);
    RUN_TEST(test_slot_vazio_some_da_fileira);
    RUN_TEST(test_fileira_degenerada);
    RUN_TEST(test_desenho_fica_centrado_no_slot);
    return UNITY_END();
}
