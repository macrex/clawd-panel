#include <unity.h>
#include <stdlib.h>
#include "toque.h"

// O maior salto entre duas amostras vizinhas que uma senoide de `hz` com
// amplitude PICO consegue dar. Qualquer degrau acima disso e um estalo: nota
// cortada no meio da onda, ou comecando sem ataque.
static int saltoMaximo(uint16_t hz) {
    return (int)(toque::PICO * 6.2831853f * hz / toque::TAXA * 1.1f) + 2;
}

static uint16_t maiorHz(const toque::Melodia &m) {
    uint16_t hz = 0;
    for (int k = 0; k < m.n; k++) if (m.notas[k].hz > hz) hz = m.notas[k].hz;
    return hz;
}

// Aviso longo vira sirene na mesa de trabalho: os dois cabem em ~400 ms.
void test_os_dois_cabem_em_400_ms(void) {
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(toque::TAXA * 400 / 1000, toque::duracao(toque::PRONTO));
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(toque::TAXA * 400 / 1000, toque::duracao(toque::ESPERA));
    TEST_ASSERT_GREATER_THAN_UINT32(0, toque::duracao(toque::PRONTO));
    TEST_ASSERT_GREATER_THAN_UINT32(0, toque::duracao(toque::ESPERA));
}

// O volume e o PICO, e nenhuma amostra passa dele. Uma conta em float que
// estourasse o int16 daria a volta para o outro sinal: um estalo no maximo.
static void confereVolume(const toque::Melodia &m) {
    const uint32_t n = toque::duracao(m);
    for (uint32_t i = 0; i < n; i++) {
        TEST_ASSERT_LESS_OR_EQUAL_INT(toque::PICO, abs(toque::amostra(m, i)));
    }
}
void test_nenhuma_amostra_passa_do_pico(void) {
    TEST_ASSERT_LESS_OR_EQUAL_INT(32767, toque::PICO);
    confereVolume(toque::PRONTO);
    confereVolume(toque::ESPERA);
}

// Sem estalo: o envelope leva cada nota de zero a zero, entao nenhum par de
// amostras vizinhas salta mais do que a propria onda salta.
static void confereSemDegrau(const toque::Melodia &m) {
    const int lim = saltoMaximo(maiorHz(m));
    const uint32_t n = toque::duracao(m);
    int anterior = 0;   // o silencio antes do aviso
    for (uint32_t i = 0; i <= n; i++) {   // <= n: inclui a volta ao silencio
        const int s = toque::amostra(m, i);
        TEST_ASSERT_LESS_OR_EQUAL_INT(lim, abs(s - anterior));
        anterior = s;
    }
}
void test_sem_degrau_do_inicio_ao_fim(void) {
    confereSemDegrau(toque::PRONTO);
    confereSemDegrau(toque::ESPERA);
}

// Fora do aviso e silencio: a tarefa de som escreve a cauda de zeros pedindo
// amostras alem do fim, e ela nao pode tocar lixo ali.
void test_depois_do_fim_e_silencio(void) {
    const uint32_t n = toque::duracao(toque::PRONTO);
    TEST_ASSERT_EQUAL_INT16(0, toque::amostra(toque::PRONTO, n));
    TEST_ASSERT_EQUAL_INT16(0, toque::amostra(toque::PRONTO, n + 12345));
}

// A nota sai na frequencia pedida: uma senoide de f Hz cruza o zero 2f vezes
// por segundo. Uma TAXA errada na conta da fase mudaria o tom inteiro.
void test_a_primeira_nota_sai_no_tom_certo(void) {
    const toque::Nota &a = toque::PRONTO.notas[0];
    const uint32_t len = toque::TAXA * a.ms / 1000;
    int cruzou = 0;
    int anterior = toque::amostra(toque::PRONTO, 1);
    for (uint32_t i = 2; i < len - 1; i++) {
        const int s = toque::amostra(toque::PRONTO, i);
        if ((anterior < 0) != (s < 0)) cruzou++;
        anterior = s;
    }
    const float esperado = 2.0f * a.hz * a.ms / 1000.0f;
    TEST_ASSERT_FLOAT_WITHIN(3.0f, esperado, (float)cruzou);
}

// O que faz os dois avisos serem reconhecidos de ouvido, sem olhar a tela:
// Pronto sobe; Espera repete a mesma nota, mais grave que qualquer uma do Pronto.
void test_pronto_sobe(void) {
    uint16_t anterior = 0;
    for (int k = 0; k < toque::PRONTO.n; k++) {
        const uint16_t hz = toque::PRONTO.notas[k].hz;
        if (hz == 0) continue;   // pausa
        TEST_ASSERT_GREATER_THAN_UINT16(anterior, hz);
        anterior = hz;
    }
}

void test_espera_repete_uma_nota_mais_grave(void) {
    uint16_t graveDoPronto = 0xFFFF;
    for (int k = 0; k < toque::PRONTO.n; k++) {
        const uint16_t hz = toque::PRONTO.notas[k].hz;
        if (hz && hz < graveDoPronto) graveDoPronto = hz;
    }
    uint16_t nota = 0;
    int tocadas = 0;
    for (int k = 0; k < toque::ESPERA.n; k++) {
        const uint16_t hz = toque::ESPERA.notas[k].hz;
        if (hz == 0) continue;
        if (nota) TEST_ASSERT_EQUAL_UINT16(nota, hz);
        nota = hz;
        tocadas++;
    }
    TEST_ASSERT_EQUAL_INT(2, tocadas);
    TEST_ASSERT_LESS_THAN_UINT16(graveDoPronto, nota);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_os_dois_cabem_em_400_ms);
    RUN_TEST(test_nenhuma_amostra_passa_do_pico);
    RUN_TEST(test_sem_degrau_do_inicio_ao_fim);
    RUN_TEST(test_depois_do_fim_e_silencio);
    RUN_TEST(test_a_primeira_nota_sai_no_tom_certo);
    RUN_TEST(test_pronto_sobe);
    RUN_TEST(test_espera_repete_uma_nota_mais_grave);
    return UNITY_END();
}
