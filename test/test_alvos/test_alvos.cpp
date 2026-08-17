#include <unity.h>
#include "layout.h"

// Os alvos de toque conferidos contra uma FOTO da tela real.
//
// A placa mandou o quadro pelo `POST /tela` em 17/08/2026, em pe, 320x480 — o
// mesmo protocolo do `tools/tela.py`. As coordenadas abaixo foram MEDIDAS nessa
// imagem, sobre os elementos desenhados, e sao a unica coisa neste arquivo que
// nao veio do codigo.
//
// E isso que o teste prova: que o retangulo que RESPONDE ao dedo cobre o
// desenho que CONVIDA o dedo. Um alvo errado nao da erro — ele so nao responde,
// ou responde no lugar do vizinho, e a unica forma de descobrir e tocando.
//
// O que a foto mostrava, e onde:
//
//   fogo do nivel (canto superior esquerdo)   x 8..48    y 5..45
//   turma do South Park                       x 14..200  y 55..100
//   faixa SESSAO, rotulo a esquerda           x 22..75   y 128..145
//   faixa SESSAO, "6%" a direita              x 265..300 y 128..148
//   faixa SEMANA, rotulo a esquerda           x 22..80   y 200..218
//   faixa SEMANA, "9%" a direita              x 278..300 y 200..222

void setUp(void) {}
void tearDown(void) {}

// O bicho do cabecalho e a unica saida da tela em pe — se ele nao responder, nao
// ha como voltar para a paisagem sem reiniciar a placa.
void test_o_icone_do_cabecalho_cobre_o_fogo_desenhado(void) {
    const Alvo a = alvoIconeCabecalho();
    TEST_ASSERT_TRUE(dentro(a, 8, 5));      // canto do desenho
    TEST_ASSERT_TRUE(dentro(a, 48, 45));    // canto oposto
    TEST_ASSERT_TRUE(dentro(a, 28, 25));    // meio, onde o dedo cai

    // E ele nao invade a turma, que comeca em y=55 e mora logo abaixo.
    TEST_ASSERT_FALSE(dentro(a, 28, 55));
    TEST_ASSERT_FALSE(dentro(a, 28, 80));
}

void test_o_alvo_da_sessao_cobre_o_percentual_desenhado(void) {
    const Alvo a = alvoPctSessao();
    TEST_ASSERT_TRUE(dentro(a, 265, 128));
    TEST_ASSERT_TRUE(dentro(a, 300, 148));
    TEST_ASSERT_TRUE(dentro(a, 282, 138));   // meio do "6%"
}

void test_o_alvo_da_semana_cobre_o_percentual_desenhado(void) {
    const Alvo a = alvoPctSemana();
    TEST_ASSERT_TRUE(dentro(a, 278, 200));
    TEST_ASSERT_TRUE(dentro(a, 300, 222));
    TEST_ASSERT_TRUE(dentro(a, 289, 210));   // meio do "9%"
}

void test_o_rotulo_da_semana_cobre_a_palavra_desenhada(void) {
    const Alvo a = alvoRotuloSemana();
    TEST_ASSERT_TRUE(dentro(a, 22, 200));
    TEST_ASSERT_TRUE(dentro(a, 80, 218));
}

// A divisao da faixa em duas metades e o que separa dois gestos diferentes:
// ensaiar a morte do Kenny (esquerda) e abrir a tela do Token (direita). Se as
// metades se sobrepusessem, um deles nunca aconteceria.
void test_as_duas_metades_da_faixa_nao_se_pisam(void) {
    const Alvo esq = alvoRotuloSemana();
    const Alvo dir = alvoPctSemana();

    // Encostam sem sobrepor: onde uma acaba, a outra comeca.
    TEST_ASSERT_EQUAL_INT(esq.x + esq.w, dir.x);

    // E nenhum ponto pertence as duas.
    for (int x = esq.x; x < dir.x + dir.w; x += 7)
        TEST_ASSERT_FALSE(dentro(esq, x, 205) && dentro(dir, x, 205));
}

// As duas faixas sao janelas diferentes, e um toque na de 5h nao pode abrir a
// tela da de 7 dias.
void test_as_duas_faixas_nao_se_pisam(void) {
    const Alvo sessao = alvoPctSessao();
    const Alvo semana = alvoPctSemana();

    TEST_ASSERT_TRUE(sessao.y + sessao.h <= semana.y);
    TEST_ASSERT_FALSE(dentro(sessao, 289, 210));   // o "9%" nao e da sessao
    TEST_ASSERT_FALSE(dentro(semana, 282, 138));   // nem o "6%" e da semana
}

// Os alvos ficam DENTRO da tela. Um retangulo que passa da borda responde a
// toques que o painel nem reporta, e some do lado de fora sem ninguem notar.
void test_nenhum_alvo_escapa_da_tela(void) {
    const Alvo todos[] = {alvoIconeCabecalho(), alvoPctSessao(),
                          alvoRotuloSemana(), alvoPctSemana()};
    for (const Alvo &a : todos) {
        TEST_ASSERT_TRUE(a.x >= 0 && a.y >= 0);
        TEST_ASSERT_TRUE(a.w > 0 && a.h > 0);
        TEST_ASSERT_TRUE(a.x + a.w <= 320);
        TEST_ASSERT_TRUE(a.y + a.h <= 480);
    }
}

// A margem do card e simetrica: o alvo da direita tem que terminar onde o card
// termina, senao o percentual desenhado na borda fica fora do que responde.
void test_o_alvo_da_direita_termina_no_fim_do_card(void) {
    const Alvo a = alvoPctSemana();
    TEST_ASSERT_EQUAL_INT(320 - 14, a.x + a.w);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_o_icone_do_cabecalho_cobre_o_fogo_desenhado);
    RUN_TEST(test_o_alvo_da_sessao_cobre_o_percentual_desenhado);
    RUN_TEST(test_o_alvo_da_semana_cobre_o_percentual_desenhado);
    RUN_TEST(test_o_rotulo_da_semana_cobre_a_palavra_desenhada);
    RUN_TEST(test_as_duas_metades_da_faixa_nao_se_pisam);
    RUN_TEST(test_as_duas_faixas_nao_se_pisam);
    RUN_TEST(test_nenhum_alvo_escapa_da_tela);
    RUN_TEST(test_o_alvo_da_direita_termina_no_fim_do_card);
    return UNITY_END();
}
