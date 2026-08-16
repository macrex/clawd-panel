#include <unity.h>
#include "app_config.h"

void setUp(void) {}
void tearDown(void) {}

static const char *COMPLETO = R"({
  "wifi": { "ssid": "MINHA_REDE", "password": "segredo" },
  "api":  { "url": "http://192.168.0.10:8787/status" },
  "poll_ms": 2000,
  "brightness": 200
})";

void test_config_completo_e_valido(void) {
    AppConfig c = parseConfig(COMPLETO);
    TEST_ASSERT_TRUE(c.valid);
    TEST_ASSERT_EQUAL_STRING("MINHA_REDE", c.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("segredo", c.password.c_str());
    TEST_ASSERT_EQUAL_STRING("http://192.168.0.10:8787/status", c.url.c_str());
    TEST_ASSERT_EQUAL_UINT32(2000, c.pollMs);
    TEST_ASSERT_EQUAL_UINT8(200, c.brightness);
}

void test_campos_opcionais_usam_default(void) {
    AppConfig c = parseConfig(R"({"wifi":{"ssid":"a","password":"b"},"api":{"url":"http://x"}})");
    TEST_ASSERT_TRUE(c.valid);
    TEST_ASSERT_EQUAL_UINT32(2000, c.pollMs);
    TEST_ASSERT_EQUAL_UINT8(200, c.brightness);
}

void test_ssid_ausente_invalida_e_explica(void) {
    AppConfig c = parseConfig(R"({"wifi":{"password":"b"},"api":{"url":"http://x"}})");
    TEST_ASSERT_FALSE(c.valid);
    TEST_ASSERT_EQUAL_STRING("wifi.ssid ausente", c.error.c_str());
}

void test_url_ausente_invalida_e_explica(void) {
    AppConfig c = parseConfig(R"({"wifi":{"ssid":"a","password":"b"}})");
    TEST_ASSERT_FALSE(c.valid);
    TEST_ASSERT_EQUAL_STRING("api.url ausente", c.error.c_str());
}

void test_json_quebrado_invalida_e_explica(void) {
    AppConfig c = parseConfig("{{{");
    TEST_ASSERT_FALSE(c.valid);
    TEST_ASSERT_EQUAL_STRING("JSON invalido", c.error.c_str());
}

void test_senha_vazia_e_aceita(void) {
    // Rede aberta e um caso legitimo; senha vazia nao pode invalidar.
    AppConfig c = parseConfig(R"({"wifi":{"ssid":"a","password":""},"api":{"url":"http://x"}})");
    TEST_ASSERT_TRUE(c.valid);
}

void test_url2_ausente_deixa_uma_fonte_so(void) {
    // Sem o campo novo o comportamento e o de hoje: uma fonte so.
    AppConfig c = parseConfig(R"({"wifi":{"ssid":"a","password":"b"},"api":{"url":"http://x"}})");
    TEST_ASSERT_TRUE(c.valid);
    TEST_ASSERT_EQUAL_STRING("", c.url2.c_str());
}

void test_url2_lida(void) {
    AppConfig c = parseConfig(R"({"wifi":{"ssid":"a","password":"b"},
        "api":{"url":"http://x","url2":"http://192.168.0.23:8787/status"}})");
    TEST_ASSERT_TRUE(c.valid);
    TEST_ASSERT_EQUAL_STRING("http://192.168.0.23:8787/status", c.url2.c_str());
}

void test_fuso_e_ntp_tem_padrao_util(void) {
    // Um cartao gravado antes destes campos existirem tem que ligar com a hora
    // certa mesmo assim: o padrao e a mesa em que este painel mora.
    AppConfig c = parseConfig(R"({"wifi":{"ssid":"a","password":"b"},"api":{"url":"http://x"}})");
    TEST_ASSERT_TRUE(c.valid);
    TEST_ASSERT_EQUAL_STRING("<-03>3", c.tz.c_str());
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", c.ntp.c_str());
}

void test_fuso_e_ntp_lidos(void) {
    AppConfig c = parseConfig(R"({"wifi":{"ssid":"a","password":"b"},
        "api":{"url":"http://x"},"tz":"EST5EDT,M3.2.0,M11.1.0","ntp":"192.168.0.1"})");
    TEST_ASSERT_TRUE(c.valid);
    TEST_ASSERT_EQUAL_STRING("EST5EDT,M3.2.0,M11.1.0", c.tz.c_str());
    TEST_ASSERT_EQUAL_STRING("192.168.0.1", c.ntp.c_str());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_config_completo_e_valido);
    RUN_TEST(test_campos_opcionais_usam_default);
    RUN_TEST(test_ssid_ausente_invalida_e_explica);
    RUN_TEST(test_url_ausente_invalida_e_explica);
    RUN_TEST(test_json_quebrado_invalida_e_explica);
    RUN_TEST(test_senha_vazia_e_aceita);
    RUN_TEST(test_url2_ausente_deixa_uma_fonte_so);
    RUN_TEST(test_url2_lida);
    RUN_TEST(test_fuso_e_ntp_tem_padrao_util);
    RUN_TEST(test_fuso_e_ntp_lidos);
    return UNITY_END();
}
