#include <unity.h>
#include <string>
#include "neturl.h"

void setUp(void) {}
void tearDown(void) {}

// ---- A URL irma ----
// Ela existia dentro do net.cpp e nunca teve teste. Errar aqui nao produz erro
// visivel: manda o POST para o host errado, ou para um pane homonimo de outro
// agente na maquina errada.

void test_irma_troca_o_ultimo_segmento(void) {
    TEST_ASSERT_EQUAL_STRING(
        "http://192.168.0.10:8787/command",
        urlIrma("http://192.168.0.10:8787/status", "command").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "http://192.168.0.11:8787/arquivo/baixar",
        urlIrma("http://192.168.0.11:8787/status", "arquivo/baixar").c_str());
}

void test_irma_descarta_a_query(void) {
    // E O CASO QUE ESTE TRABALHO CRIOU: a URL do polling agora carrega
    // `?campos=painel`, e ela e a base de todas as outras. Se a query
    // atravessasse, o POST /responder chegaria com um parametro que a rota nao
    // espera — e o `/arquivo/baixar` serviria o pedido errado.
    TEST_ASSERT_EQUAL_STRING(
        "http://h:8787/responder",
        urlIrma("http://h:8787/status?campos=painel", "responder").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "http://h:8787/tela",
        urlIrma("http://h:8787/status?x=1&campos=painel", "tela").c_str());
}

void test_irma_sem_caminho_nenhum(void) {
    // Base so com host: nao ha ultimo segmento para trocar, e o segmento vira o
    // primeiro.
    TEST_ASSERT_EQUAL_STRING("http://h:8787/command",
                             urlIrma("http://h:8787", "command").c_str());
}

void test_irma_sem_esquema(void) {
    // Um cartao gravado a mao pode omitir o http://. A busca pela barra nao
    // pode confundir a do esquema com a do caminho.
    TEST_ASSERT_EQUAL_STRING("192.168.0.10:8787/command",
                             urlIrma("192.168.0.10:8787/status", "command").c_str());
}

// ---- A URL do polling ----

void test_status_acrescenta_o_pedido(void) {
    TEST_ASSERT_EQUAL_STRING(
        "http://192.168.0.10:8787/status?campos=painel",
        urlDoStatus("http://192.168.0.10:8787/status").c_str());
}

void test_status_respeita_query_que_ja_existe(void) {
    // Concatenar um segundo `?` produz uma URL que o servidor recusa.
    TEST_ASSERT_EQUAL_STRING(
        "http://h:8787/status?x=1&campos=painel",
        urlDoStatus("http://h:8787/status?x=1").c_str());
}

void test_status_sem_endereco_nao_inventa(void) {
    // Sem url2 configurada a base e vazia, e a segunda fonte simplesmente nao
    // existe. Devolver "?campos=painel" faria a rede tentar um endereco que nao
    // e endereco nenhum.
    TEST_ASSERT_EQUAL_STRING("", urlDoStatus("").c_str());
}

void test_status_nao_repete_o_pedido(void) {
    // Chamada duas vezes sobre a mesma base — um refactor futuro que a chame no
    // laco em vez de no begin nao pode acumular parametros.
    const std::string uma = urlDoStatus("http://h:8787/status");
    TEST_ASSERT_EQUAL_STRING(uma.c_str(), urlDoStatus(uma).c_str());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_irma_troca_o_ultimo_segmento);
    RUN_TEST(test_irma_descarta_a_query);
    RUN_TEST(test_irma_sem_caminho_nenhum);
    RUN_TEST(test_irma_sem_esquema);
    RUN_TEST(test_status_acrescenta_o_pedido);
    RUN_TEST(test_status_respeita_query_que_ja_existe);
    RUN_TEST(test_status_sem_endereco_nao_inventa);
    RUN_TEST(test_status_nao_repete_o_pedido);
    return UNITY_END();
}
