#include <unity.h>
#include <cstring>
#include "jsonmem.h"

void setUp(void) {}
void tearDown(void) {}

// O alocador existe e entrega memoria utilizavel. Parece obvio, e e — o valor do
// teste esta em rodar contra as DUAS implementacoes por tras da mesma assinatura
// (PSRAM na placa, malloc no PC): quem trocar uma delas por engano quebra aqui.
void test_aloca_memoria_gravavel(void) {
    ArduinoJson::Allocator *a = alocadorJson();
    TEST_ASSERT_NOT_NULL(a);

    char *p = (char *)a->allocate(64);
    TEST_ASSERT_NOT_NULL(p);
    std::memset(p, 'x', 64);
    TEST_ASSERT_EQUAL_CHAR('x', p[0]);
    TEST_ASSERT_EQUAL_CHAR('x', p[63]);
    a->deallocate(p);
}

// `reallocate` e obrigatorio no ArduinoJson 7 e e por onde o documento CRESCE
// durante o parse — e o caminho quente, nao o `allocate`. Um realloc que perde o
// conteudo entrega JSON corrompido em vez de erro, que e o pior modo de falha.
void test_realloc_preserva_o_conteudo(void) {
    ArduinoJson::Allocator *a = alocadorJson();

    char *p = (char *)a->allocate(16);
    TEST_ASSERT_NOT_NULL(p);
    std::strcpy(p, "clawd");

    char *q = (char *)a->reallocate(p, 512);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL_STRING("clawd", q);
    a->deallocate(q);
}

// O uso real: um JsonDocument construido com este alocador parseia normalmente.
void test_documento_parseia_com_o_alocador(void) {
    JsonDocument doc(alocadorJson());
    TEST_ASSERT_FALSE(deserializeJson(doc, "{\"a\":1,\"b\":\"dois\"}"));
    TEST_ASSERT_EQUAL_INT(1, doc["a"] | 0);
    TEST_ASSERT_EQUAL_STRING("dois", doc["b"] | "");
}

// Uma unica instancia para o processo inteiro: o alocador nao guarda estado, e
// duas copias so fariam o construtor de JsonDocument decidir qual usar.
void test_e_sempre_o_mesmo(void) {
    TEST_ASSERT_EQUAL_PTR(alocadorJson(), alocadorJson());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_aloca_memoria_gravavel);
    RUN_TEST(test_realloc_preserva_o_conteudo);
    RUN_TEST(test_documento_parseia_com_o_alocador);
    RUN_TEST(test_e_sempre_o_mesmo);
    return UNITY_END();
}
