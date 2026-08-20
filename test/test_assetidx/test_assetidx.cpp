#include <unity.h>
#include "assetidx.h"
#include <cstring>
#include <vector>

void setUp(void) {}
void tearDown(void) {}

namespace {

// Monta um container na mao. Duas entradas bastam para provar que a busca
// distingue nomes e devolve o offset certo — com uma so, um `return tabela[0]`
// passaria.
std::vector<uint8_t> build(bool magicBom = true) {
    std::vector<uint8_t> b;
    auto u16 = [&](uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); };
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((v >> (8 * i)) & 0xFF); };
    auto nome = [&](const char *s) {
        char n[24] = {0};
        strncpy(n, s, 23);
        for (int i = 0; i < 24; i++) b.push_back((uint8_t)n[i]);
    };

    const char *m = magicBom ? "CLWA" : "XXXX";
    for (int i = 0; i < 4; i++) b.push_back((uint8_t)m[i]);
    u16(1);          // versao
    u16(2);          // n
    u32(0);          // reservado

    nome("alert");   u32(84); u32(10); u32(30);
    nome("mago");    u32(94); u32(20); u32(60);

    b.resize(114);   // 12 + 2*36 = 84 de cabecalho+tabela, mais 30 de dados
    return b;
}

}

void test_acha_o_primeiro(void) {
    const std::vector<uint8_t> b = build();
    AssetIdx idx = parseAssetIdx(b.data(), b.size());
    TEST_ASSERT_TRUE(idx.valid);
    TEST_ASSERT_EQUAL_UINT16(2, idx.count);

    AssetEntry e;
    TEST_ASSERT_TRUE(acharAsset(idx, "alert", e));
    TEST_ASSERT_EQUAL_UINT32(84, e.offset);
    TEST_ASSERT_EQUAL_UINT32(10, e.comprimido);
    TEST_ASSERT_EQUAL_UINT32(30, e.cru);
}

void test_acha_o_segundo(void) {
    const std::vector<uint8_t> b = build();
    AssetIdx idx = parseAssetIdx(b.data(), b.size());
    AssetEntry e;
    TEST_ASSERT_TRUE(acharAsset(idx, "mago", e));
    TEST_ASSERT_EQUAL_UINT32(94, e.offset);
    TEST_ASSERT_EQUAL_UINT32(60, e.cru);
}

// O firmware inteiro pede "/clawd/nome.clw". Se a busca nao souber recortar
// isso sozinha, cada chamador teria que recortar — e um que esquecesse falharia
// silenciosamente, caindo para o cartao sem ninguem notar.
void test_aceita_o_caminho_completo(void) {
    const std::vector<uint8_t> b = build();
    AssetIdx idx = parseAssetIdx(b.data(), b.size());
    AssetEntry e;
    TEST_ASSERT_TRUE(acharAsset(idx, "/clawd/mago.clw", e));
    TEST_ASSERT_EQUAL_UINT32(94, e.offset);
}

void test_nome_ausente(void) {
    const std::vector<uint8_t> b = build();
    AssetIdx idx = parseAssetIdx(b.data(), b.size());
    AssetEntry e;
    TEST_ASSERT_FALSE(acharAsset(idx, "/clawd/nao_existe.clw", e));
}

// Particao virgem: tudo 0xFF. Nao pode virar um indice com milhares de
// entradas lidas de lixo.
void test_magic_errado(void) {
    const std::vector<uint8_t> b = build(false);
    AssetIdx idx = parseAssetIdx(b.data(), b.size());
    TEST_ASSERT_FALSE(idx.valid);
}

void test_curto_demais(void) {
    const std::vector<uint8_t> b = build();
    AssetIdx idx = parseAssetIdx(b.data(), 8);
    TEST_ASSERT_FALSE(idx.valid);
}

// A tabela declara 2 entradas mas o buffer nao as comporta: recusar inteiro, e
// nao ler metade.
void test_tabela_truncada(void) {
    const std::vector<uint8_t> b = build();
    AssetIdx idx = parseAssetIdx(b.data(), 12 + 36);
    TEST_ASSERT_FALSE(idx.valid);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_acha_o_primeiro);
    RUN_TEST(test_acha_o_segundo);
    RUN_TEST(test_aceita_o_caminho_completo);
    RUN_TEST(test_nome_ausente);
    RUN_TEST(test_magic_errado);
    RUN_TEST(test_curto_demais);
    RUN_TEST(test_tabela_truncada);
    return UNITY_END();
}
