#include <unity.h>
#include <cstring>
#include "protocolo.h"

void setUp(void) {}
void tearDown(void) {}

static uint32_t crcDe(const char *txt) {
    return crc32Passo(0, (const uint8_t *)txt, std::strlen(txt));
}

// ---- CRC-32 ----

// Os vetores canonicos do CRC-32 IEEE. Do outro lado quem calcula e o zlib do
// Python (`binascii.crc32`), e um desacordo aqui rejeitaria um sprite perfeito
// — falha que aparece como "a atualizacao nao funciona" e nao como erro de CRC.
void test_crc_bate_com_os_vetores_conhecidos(void) {
    TEST_ASSERT_EQUAL_UINT32(0x00000000u, crcDe(""));
    TEST_ASSERT_EQUAL_UINT32(0xE8B7BE43u, crcDe("a"));
    TEST_ASSERT_EQUAL_UINT32(0xCBF43926u, crcDe("123456789"));
    TEST_ASSERT_EQUAL_UINT32(0x414FA339u, crcDe("The quick brown fox jumps over the lazy dog"));
}

// O download chega em pedacos e o CRC e encadeado — se ele nao fosse
// associativo, um arquivo de 600 KB seria rejeitado por causa do tamanho do
// buffer de leitura, que nao tem nada a ver com o conteudo.
void test_crc_em_pedacos_e_igual_ao_de_uma_vez(void) {
    const char *todo = "123456789";
    uint32_t passo = 0;
    passo = crc32Passo(passo, (const uint8_t *)"1234", 4);
    passo = crc32Passo(passo, (const uint8_t *)"5", 1);
    passo = crc32Passo(passo, (const uint8_t *)"6789", 4);
    TEST_ASSERT_EQUAL_UINT32(crcDe(todo), passo);
}

void test_crc_de_pedaco_vazio_nao_muda_nada(void) {
    const uint32_t antes = crcDe("clawd");
    TEST_ASSERT_EQUAL_UINT32(antes, crc32Passo(antes, (const uint8_t *)"", 0));
}

// Bytes acima de 0x7F: o sprite e binario, e um CRC que so funcionasse em ASCII
// so falharia no arquivo de verdade.
void test_crc_cobre_byte_alto(void) {
    const uint8_t bin[] = {0x00, 0xFF, 0x80, 0x7F, 0x01};
    const uint32_t a = crc32Passo(0, bin, sizeof(bin));
    const uint8_t outro[] = {0x00, 0xFF, 0x80, 0x7F, 0x02};
    TEST_ASSERT_NOT_EQUAL(a, crc32Passo(0, outro, sizeof(outro)));
}

// ---- O escape do corpo do POST ----

void test_escape_deixa_texto_comum_intacto(void) {
    TEST_ASSERT_EQUAL_STRING("Yes, proceed", jsonEscapado("Yes, proceed").c_str());
    TEST_ASSERT_EQUAL_STRING("", jsonEscapado("").c_str());
}

// O caso real que motivou a funcao: um rotulo de aprovacao com aspas dentro.
void test_escape_das_aspas_e_da_barra(void) {
    TEST_ASSERT_EQUAL_STRING(
        "Yes, and don't ask again for \\\"rm\\\" commands",
        jsonEscapado("Yes, and don't ask again for \"rm\" commands").c_str());
    TEST_ASSERT_EQUAL_STRING("C:\\\\workspace", jsonEscapado("C:\\workspace").c_str());
}

void test_escape_das_quebras_de_linha(void) {
    TEST_ASSERT_EQUAL_STRING("a\\nb\\rc\\td", jsonEscapado("a\nb\rc\td").c_str());
}

// Controle sem sequencia curta vira \u00XX. Sem isto ele entraria cru no corpo e
// o servidor devolveria 400 — que na placa aparece como uma aprovacao que
// simplesmente nao aconteceu.
void test_escape_de_controle_vira_unicode(void) {
    const std::string bell(1, (char)0x07);
    TEST_ASSERT_EQUAL_STRING("\\u0007", jsonEscapado(bell).c_str());
    const std::string esc(1, (char)0x1B);
    TEST_ASSERT_EQUAL_STRING("\\u001b", jsonEscapado(esc).c_str());
}

// Acentos e UTF-8 passam byte a byte: o JSON aceita UTF-8 cru, e escapar aqui
// produziria mojibake do outro lado.
void test_escape_nao_mexe_em_utf8(void) {
    TEST_ASSERT_EQUAL_STRING("naÌƒo", jsonEscapado("naÌƒo").c_str());
}

// ---- Pedidos de carona ----

void test_primeiro_pedido_de_uma_fonte_e_sempre_novo(void) {
    IdsPorFonte s;
    TEST_ASSERT_TRUE(pedidoNovo(s, 0, 1));
    TEST_ASSERT_TRUE(pedidoNovo(s, 1, 1));   // outra fonte, mesmo numero
}

void test_o_mesmo_id_nao_repete(void) {
    IdsPorFonte s;
    TEST_ASSERT_TRUE(pedidoNovo(s, 0, 7));
    TEST_ASSERT_FALSE(pedidoNovo(s, 0, 7));
    TEST_ASSERT_FALSE(pedidoNovo(s, 0, 7));
}

// O contador da API reinicia a cada logon, entao a comparacao e por
// DESIGUALDADE. Com "maior que o ultimo", um id que volta atras congelaria o
// fluxo ate o contador alcancar o valor antigo.
void test_id_que_volta_atras_e_pedido_novo(void) {
    IdsPorFonte s;
    TEST_ASSERT_TRUE(pedidoNovo(s, 0, 90));
    TEST_ASSERT_TRUE(pedidoNovo(s, 0, 1));
}

// Zero e um id como qualquer outro: a API reiniciada pode publicar o primeiro
// pedido com ele, e trata-lo como "nunca houve" perderia o pedido.
void test_id_zero_conta(void) {
    IdsPorFonte s;
    TEST_ASSERT_TRUE(pedidoNovo(s, 0, 0));
    TEST_ASSERT_FALSE(pedidoNovo(s, 0, 0));
    TEST_ASSERT_TRUE(pedidoNovo(s, 0, 1));
}

// As duas fontes nao se atrapalham: o mesmo numero em duas maquinas sao pedidos
// diferentes, e atender um nao pode calar o outro.
void test_as_fontes_sao_independentes(void) {
    IdsPorFonte s;
    TEST_ASSERT_TRUE(pedidoNovo(s, 0, 5));
    TEST_ASSERT_TRUE(pedidoNovo(s, 1, 5));
    TEST_ASSERT_FALSE(pedidoNovo(s, 0, 5));
    TEST_ASSERT_FALSE(pedidoNovo(s, 1, 5));
}

// Uma API futura mandando origem 2 nao pode indexar fora do vetor.
void test_origem_desconhecida_cai_no_master(void) {
    IdsPorFonte s;
    TEST_ASSERT_TRUE(pedidoNovo(s, 9, 3));
    TEST_ASSERT_FALSE(pedidoNovo(s, 0, 3));    // foi para a fonte do master
    TEST_ASSERT_FALSE(pedidoNovo(s, -1, 3));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_crc_bate_com_os_vetores_conhecidos);
    RUN_TEST(test_crc_em_pedacos_e_igual_ao_de_uma_vez);
    RUN_TEST(test_crc_de_pedaco_vazio_nao_muda_nada);
    RUN_TEST(test_crc_cobre_byte_alto);
    RUN_TEST(test_escape_deixa_texto_comum_intacto);
    RUN_TEST(test_escape_das_aspas_e_da_barra);
    RUN_TEST(test_escape_das_quebras_de_linha);
    RUN_TEST(test_escape_de_controle_vira_unicode);
    RUN_TEST(test_escape_nao_mexe_em_utf8);
    RUN_TEST(test_primeiro_pedido_de_uma_fonte_e_sempre_novo);
    RUN_TEST(test_o_mesmo_id_nao_repete);
    RUN_TEST(test_id_que_volta_atras_e_pedido_novo);
    RUN_TEST(test_id_zero_conta);
    RUN_TEST(test_as_fontes_sao_independentes);
    RUN_TEST(test_origem_desconhecida_cai_no_master);
    return UNITY_END();
}
