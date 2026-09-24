#include <unity.h>
#include "ajustes.h"

using namespace ajustes;

void setUp(void) {}
void tearDown(void) {}

// Um dia qualquer de 2026, a meia-noite local. As horas somam em cima.
static const long DIA = 1790000000L - (1790000000L % 86400L);
static long as(int h, int m) { return DIA + h * 3600L + m * 60L; }

// ---- A janela da noite ----

// A janela atravessa a meia-noite: as duas pontas sao noite, o miolo do dia nao.
void test_noite_atravessa_a_meia_noite(void) {
    TEST_ASSERT_TRUE(horaDaNoite(as(23, 0)));
    TEST_ASSERT_TRUE(horaDaNoite(as(23, 59)));
    TEST_ASSERT_TRUE(horaDaNoite(as(0, 0)));
    TEST_ASSERT_TRUE(horaDaNoite(as(3, 40)));
    TEST_ASSERT_TRUE(horaDaNoite(as(6, 59)));
}

// 23:00 entra, 07:00 ja e dia.
void test_bordas_da_noite(void) {
    TEST_ASSERT_FALSE(horaDaNoite(as(22, 59)));
    TEST_ASSERT_FALSE(horaDaNoite(as(7, 0)));
    TEST_ASSERT_FALSE(horaDaNoite(as(12, 0)));
}

// Sem relogio (placa sem SNTP e sem API) nao ha noite: apagar pela hora de 1970
// seria a tela escura no meio da tarde.
void test_sem_hora_nao_e_noite(void) {
    TEST_ASSERT_FALSE(horaDaNoite(0));
    TEST_ASSERT_FALSE(horaDaNoite(3600L));   // 01:00 de 1970
}

void test_tela_de_noite_pede_ajuste_hora_sono_e_sem_sessao(void) {
    TEST_ASSERT_TRUE(telaDeNoite(true, as(1, 0), false, true));
    TEST_ASSERT_FALSE(telaDeNoite(false, as(1, 0), false, true));   // ajuste desligado
    TEST_ASSERT_FALSE(telaDeNoite(true, as(1, 0), true, true));     // acordada
    TEST_ASSERT_FALSE(telaDeNoite(true, as(15, 0), false, true));   // de dia
    TEST_ASSERT_FALSE(telaDeNoite(true, as(1, 0), false, false));   // sessao aberta
}

// ---- Prazos ----

void test_prazo_zero_e_sem_prazo(void) {
    TEST_ASSERT_FALSE(noPrazo(0, 1000));
    TEST_ASSERT_EQUAL(0, minutosAte(0, 1000));
}

void test_prazo_vence_no_instante(void) {
    TEST_ASSERT_TRUE(noPrazo(30000, 29999));
    TEST_ASSERT_FALSE(noPrazo(30000, 30000));
    TEST_ASSERT_FALSE(noPrazo(30000, 40000));
}

// O millis() vira aos 49 dias. Um prazo armado antes da virada e vencido depois
// dela tem que continuar valendo o que valia.
void test_prazo_atravessa_a_virada_do_millis(void) {
    const uint32_t antes = 0xFFFFF000UL;
    const uint32_t ate   = antes + 30000UL;       // ja virou
    TEST_ASSERT_TRUE(noPrazo(ate, antes));
    TEST_ASSERT_TRUE(noPrazo(ate, antes + 29999UL));
    TEST_ASSERT_FALSE(noPrazo(ate, antes + 30000UL));
}

// "falta 1m" ate o ultimo segundo, e a hora cheia comeca em 60.
void test_minutos_arredondam_para_cima(void) {
    TEST_ASSERT_EQUAL(60, minutosAte(3600000UL, 0));
    TEST_ASSERT_EQUAL(60, minutosAte(3600000UL, 1));
    TEST_ASSERT_EQUAL(59, minutosAte(3600000UL, 60000));
    TEST_ASSERT_EQUAL(1, minutosAte(3600000UL, 3599999UL));
    TEST_ASSERT_EQUAL(0, minutosAte(3600000UL, 3600000UL));
}

// ---- Brilho ----

void test_degraus_sobem_e_terminam_no_maximo(void) {
    for (int d = 1; d < DEGRAUS; d++)
        TEST_ASSERT_TRUE(brilhoDoDegrau(d) > brilhoDoDegrau(d - 1));
    TEST_ASSERT_EQUAL(255, brilhoDoDegrau(DEGRAUS - 1));
    TEST_ASSERT_TRUE(BRILHO_NOITE < brilhoDoDegrau(0));
}

void test_degrau_fora_da_escala_cai_na_ponta(void) {
    TEST_ASSERT_EQUAL(brilhoDoDegrau(0), brilhoDoDegrau(-1));
    TEST_ASSERT_EQUAL(brilhoDoDegrau(DEGRAUS - 1), brilhoDoDegrau(DEGRAUS));
}

// Ida e volta: cada degrau le de volta como ele mesmo.
void test_degrau_do_proprio_brilho(void) {
    for (int d = 0; d < DEGRAUS; d++)
        TEST_ASSERT_EQUAL(d, degrauDoBrilho(brilhoDoDegrau(d)));
}

// O 200 do config.json de fabrica acende o degrau mais perto dele.
void test_brilho_do_config_acha_o_degrau_mais_perto(void) {
    TEST_ASSERT_EQUAL(4, degrauDoBrilho(200));   // 170 esta a 30, 255 a 55
    TEST_ASSERT_EQUAL(0, degrauDoBrilho(0));
    TEST_ASSERT_EQUAL(DEGRAUS - 1, degrauDoBrilho(250));
}

// ---- Textos ----

void test_sinal_fraco_abaixo_de_80(void) {
    TEST_ASSERT_EQUAL_STRING("-62 dBm", textoDoSinal(true, -62).c_str());
    TEST_ASSERT_EQUAL_STRING("-80 dBm", textoDoSinal(true, -80).c_str());
    TEST_ASSERT_EQUAL_STRING("-84 dBm fraco", textoDoSinal(true, -84).c_str());
    TEST_ASSERT_EQUAL_STRING("sem Wi-Fi", textoDoSinal(false, 0).c_str());
    TEST_ASSERT_FALSE(sinalFraco(true, -80));
    TEST_ASSERT_TRUE(sinalFraco(true, -81));
    TEST_ASSERT_TRUE(sinalFraco(false, 0));
}

// ---- Geometria ----

// O centro de cada peca devolve ela mesma, nas duas orientacoes: o toque vale
// o que o desenho vale.
void test_centro_de_cada_peca_e_ela_mesma(void) {
    for (bool retrato : {false, true}) {
        for (int d = 0; d < DEGRAUS; d++) {
            const Alvo q = quadradoDoDegrau(d, retrato);
            TEST_ASSERT_EQUAL(d, noPonto(q.x + q.w / 2, q.y + q.h / 2, retrato));
        }
        for (int id = SOM; id < SOM + BOTOES; id++) {
            const Alvo b = botao(id, retrato);
            TEST_ASSERT_EQUAL(id, noPonto(b.x + b.w / 2, b.y + b.h / 2, retrato));
        }
    }
}

// Tudo dentro do painel e dentro da tela, e nenhum botao pisa no outro.
void test_pecas_cabem_e_nao_se_sobrepoem(void) {
    for (bool retrato : {false, true}) {
        const int W = retrato ? 320 : 480, H = retrato ? 480 : 320;
        const Alvo p = painel(retrato);
        TEST_ASSERT_TRUE(p.x >= 0 && p.x + p.w <= W && p.y + p.h <= H);
        for (int i = SOM; i < SOM + BOTOES; i++) {
            const Alvo a = botao(i, retrato);
            TEST_ASSERT_TRUE(a.x >= p.x && a.x + a.w <= p.x + p.w);
            TEST_ASSERT_TRUE(a.y >= p.y && a.y + a.h <= p.y + p.h);
            for (int j = i + 1; j < SOM + BOTOES; j++) {
                const Alvo b = botao(j, retrato);
                const bool longe = a.x + a.w <= b.x || b.x + b.w <= a.x ||
                                   a.y + a.h <= b.y || b.y + b.h <= a.y;
                TEST_ASSERT_TRUE(longe);
            }
        }
        const Alvo ult = quadradoDoDegrau(DEGRAUS - 1, retrato);
        TEST_ASSERT_TRUE(ult.x + ult.w <= p.x + p.w);
    }
}

// O dedo um pouco abaixo do quadrado de brilho ainda acerta: 22 px sao pouco.
void test_degrau_aceita_dedo_um_pouco_fora(void) {
    const Alvo q = quadradoDoDegrau(2, false);
    TEST_ASSERT_EQUAL(2, noPonto(q.x + q.w / 2, q.y + q.h + 6, false));
    TEST_ASSERT_EQUAL(2, noPonto(q.x + q.w / 2, q.y - 6, false));
}

// Fora do painel fecha (-2); dentro dele, entre as pecas, nao faz nada (-1).
void test_fora_fecha_e_vao_nao_faz_nada(void) {
    TEST_ASSERT_EQUAL(-2, noPonto(240, 300, false));
    TEST_ASSERT_EQUAL(-2, noPonto(160, 450, true));
    TEST_ASSERT_EQUAL(-1, noPonto(30, 10, false));     // o rotulo AJUSTES
    TEST_ASSERT_EQUAL(-1, noPonto(30, 10, true));
}

// Deitado a maquete F1 manda: SOM no canto de cima a esquerda, Wi-Fi embaixo a
// direita. Em pe, duas colunas: GIRAR desce para a segunda linha.
void test_ordem_dos_botoes(void) {
    TEST_ASSERT_EQUAL(SOM, noPonto(60, 100, false));
    TEST_ASSERT_EQUAL(GIRAR, noPonto(400, 100, false));
    TEST_ASSERT_EQUAL(WIFI, noPonto(400, 170, false));
    TEST_ASSERT_EQUAL(GIRAR, noPonto(60, 200, true));
    TEST_ASSERT_EQUAL(WIFI, noPonto(260, 270, true));
}

// O defeito que a revisao achou: um prazo vencido e esquecido volta a parecer
// futuro depois de 2^31 ms. Zerado a cada volta, ele fica morto.
void test_prazo_vencido_fica_morto(void) {
    const uint32_t ate = 1000UL + 3600000UL;          // silencio de 1 h
    TEST_ASSERT_EQUAL_UINT32(ate, prazoVivo(ate, 2000));
    TEST_ASSERT_EQUAL_UINT32(0, prazoVivo(ate, ate));
    TEST_ASSERT_EQUAL_UINT32(0, prazoVivo(0, 5000));
    // Sem zerar, 25 dias depois o prazo velho "voltaria":
    const uint32_t depois = ate + 0x80000001UL;
    TEST_ASSERT_TRUE(noPrazo(ate, depois));
    // ...e e por isso que o laco zera antes: com 0 ele nunca volta.
    TEST_ASSERT_FALSE(noPrazo(prazoVivo(ate, ate + 1), depois));
}

void test_o_aviso_do_poll(void) {
    TEST_ASSERT_TRUE(Aviso::Nenhum == avisoDoPoll(0, 0, true, false));
    TEST_ASSERT_TRUE(Aviso::Pronto == avisoDoPoll(2, 0, true, false));
    // A espera ganha do pronto no mesmo poll.
    TEST_ASSERT_TRUE(Aviso::Espera == avisoDoPoll(1, 1, true, false));
    // Som desligado ou em silencio: nada.
    TEST_ASSERT_TRUE(Aviso::Nenhum == avisoDoPoll(1, 1, false, false));
    TEST_ASSERT_TRUE(Aviso::Nenhum == avisoDoPoll(1, 1, true, true));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_noite_atravessa_a_meia_noite);
    RUN_TEST(test_bordas_da_noite);
    RUN_TEST(test_sem_hora_nao_e_noite);
    RUN_TEST(test_tela_de_noite_pede_ajuste_hora_sono_e_sem_sessao);
    RUN_TEST(test_prazo_zero_e_sem_prazo);
    RUN_TEST(test_prazo_vence_no_instante);
    RUN_TEST(test_prazo_atravessa_a_virada_do_millis);
    RUN_TEST(test_minutos_arredondam_para_cima);
    RUN_TEST(test_degraus_sobem_e_terminam_no_maximo);
    RUN_TEST(test_degrau_fora_da_escala_cai_na_ponta);
    RUN_TEST(test_degrau_do_proprio_brilho);
    RUN_TEST(test_brilho_do_config_acha_o_degrau_mais_perto);
    RUN_TEST(test_sinal_fraco_abaixo_de_80);
    RUN_TEST(test_centro_de_cada_peca_e_ela_mesma);
    RUN_TEST(test_pecas_cabem_e_nao_se_sobrepoem);
    RUN_TEST(test_degrau_aceita_dedo_um_pouco_fora);
    RUN_TEST(test_fora_fecha_e_vao_nao_faz_nada);
    RUN_TEST(test_ordem_dos_botoes);
    RUN_TEST(test_prazo_vencido_fica_morto);
    RUN_TEST(test_o_aviso_do_poll);
    return UNITY_END();
}
