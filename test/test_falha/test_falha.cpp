#include <unity.h>
#include <cstring>
#include <initializer_list>
#include "falha.h"

void setUp(void) {}
void tearDown(void) {}

// O radio manda em tudo: sem ele o codigo da ultima tentativa descreve um mundo
// que nao existe mais. Ate um 500 fresco perde para o Wi-Fi caido.
void test_sem_wifi_ganha_de_qualquer_codigo(void) {
    TEST_ASSERT_EQUAL_STRING("SEM WIFI", motivoDaFalha(500, 10, false));
    TEST_ASSERT_EQUAL_STRING("SEM WIFI", motivoDaFalha(HTTP_RECUSADA, 3000, false));
    TEST_ASSERT_EQUAL_STRING("SEM WIFI", motivoDaFalha(0, 0, false));
}

// O `-1` do Arduino cobre dois mundos e so o TEMPO os separa: um RST de porta
// fechada volta em milissegundos.
void test_recusa_rapida_acusa_a_api(void) {
    TEST_ASSERT_EQUAL_STRING("API FORA", motivoDaFalha(HTTP_RECUSADA, 2, true));
    TEST_ASSERT_EQUAL_STRING("API FORA",
                             motivoDaFalha(HTTP_RECUSADA, LIMIAR_RESPOSTA_MS - 1, true));
}

// E o que NAO se pode concluir da espera longa. Medido na placa: com a API
// derrubada no Windows, o firewall descarta o SYN em silencio e a tentativa
// consome os 3 s inteiros — a maquina esta de pe e parece morta. Chamar isso de
// "PC FORA" mandaria procurar a maquina errada.
void test_espera_longa_nao_acusa_ninguem(void) {
    TEST_ASSERT_EQUAL_STRING("SEM CONTATO",
                             motivoDaFalha(HTTP_RECUSADA, LIMIAR_RESPOSTA_MS, true));
    TEST_ASSERT_EQUAL_STRING("SEM CONTATO", motivoDaFalha(HTTP_RECUSADA, 3000, true));
}

// Conexao perdida no meio segue a mesma regra: o que importa e se alguem estava
// do outro lado, e nao em que ponto a requisicao morreu.
void test_conexao_perdida_usa_a_mesma_regra(void) {
    TEST_ASSERT_EQUAL_STRING("API FORA", motivoDaFalha(HTTP_PERDIDA, 30, true));
    TEST_ASSERT_EQUAL_STRING("SEM CONTATO", motivoDaFalha(HTTP_PERDIDA, 3000, true));
}

// Conectou e a resposta nao veio: a API existe e esta presa. E o unico caso em
// que esperar mais pode adiantar.
void test_leitura_lenta_e_api_travada(void) {
    TEST_ASSERT_EQUAL_STRING("API TRAVADA",
                             motivoDaFalha(HTTP_LEITURA_LENTA, 6000, true));
}

// A unica familia em que olhar o servidor resolve — nas outras nao ha servidor
// para olhar.
void test_erro_http_aponta_para_a_api(void) {
    TEST_ASSERT_EQUAL_STRING("ERRO NA API", motivoDaFalha(500, 12, true));
    TEST_ASSERT_EQUAL_STRING("ERRO NA API", motivoDaFalha(503, 12, true));
    TEST_ASSERT_EQUAL_STRING("ERRO NA API", motivoDaFalha(404, 12, true));
}

void test_url_recusada_antes_do_socket(void) {
    TEST_ASSERT_EQUAL_STRING("URL RUIM", motivoDaFalha(HTTP_URL_RUIM, 0, true));
}

// Codigo que o firmware nao sabe ler: o texto de sempre, que e honesto — ha algo
// errado e o painel nao sabe dizer o que.
void test_desconhecido_cai_no_texto_de_sempre(void) {
    TEST_ASSERT_EQUAL_STRING("SEM CONTATO", motivoDaFalha(0, 0, true));
    TEST_ASSERT_EQUAL_STRING("SEM CONTATO", motivoDaFalha(200, 40, true));
    TEST_ASSERT_EQUAL_STRING("SEM CONTATO", motivoDaFalha(-99, 40, true));
}

// Onze caracteres e a largura que o rodape tem. Um rotulo maior empurra o aviso
// para a esquerda ate encostar na turma — e isso so apareceria na placa.
void test_nenhum_motivo_passa_de_onze_caracteres(void) {
    const int codigos[] = {500, 404, HTTP_LEITURA_LENTA, HTTP_URL_RUIM,
                           HTTP_RECUSADA, HTTP_PERDIDA, 0, 200, -99};
    for (int c : codigos) {
        for (int d : {0, 100, 3000}) {
            for (bool w : {true, false}) {
                const size_t n = std::strlen(motivoDaFalha(c, d, w));
                TEST_ASSERT_TRUE(n > 0 && n <= 11);
            }
        }
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_sem_wifi_ganha_de_qualquer_codigo);
    RUN_TEST(test_recusa_rapida_acusa_a_api);
    RUN_TEST(test_espera_longa_nao_acusa_ninguem);
    RUN_TEST(test_conexao_perdida_usa_a_mesma_regra);
    RUN_TEST(test_leitura_lenta_e_api_travada);
    RUN_TEST(test_erro_http_aponta_para_a_api);
    RUN_TEST(test_url_recusada_antes_do_socket);
    RUN_TEST(test_desconhecido_cai_no_texto_de_sempre);
    RUN_TEST(test_nenhum_motivo_passa_de_onze_caracteres);
    return UNITY_END();
}
