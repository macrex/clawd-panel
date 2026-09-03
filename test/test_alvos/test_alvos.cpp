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
// AS MEDIDAS FORAM REFEITAS em 21/08/2026, sobre uma foto nova: os limites
// deixaram de ser duas faixas empilhadas de 70 px e viraram duas COLUNAS de
// 143x98 lado a lado, e as coordenadas antigas apontavam para o vazio abaixo
// delas. O teste seguiu a tela — nao o contrario.
//
// O que a foto de 21/08 mostrava, e onde:
//
//   fogo do nivel (canto superior esquerdo)   x 8..48    y 5..45
//   turma do South Park                       x 34..283  y 52..100
//   coluna SESSAO, rotulo a esquerda          x 23..77   y 126..135
//   coluna SESSAO, "20%" a direita            x 93..150  y 140..159
//   coluna SEMANA, rotulo a esquerda          x 172..229 y 126..135
//   coluna SEMANA, "81%" a direita            x 242..299 y 140..159
//   coluna SESSAO, barra                      x 22..148  y 170..177
//   coluna SESSAO, prazo (rodape)             x 23..51   y 190..199

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

// Na tela nova aquele canto e o NOME, que vai de x=14 a x=176 em corpo 3. O
// alvo cobre a palavra INTEIRA: o giro e a unica saida do modo em pe, e um
// controle sem saida alternativa nao pode responder so na primeira metade.
void test_o_alvo_do_cabecalho_cobre_o_nome_inteiro(void) {
    const Alvo a = alvoIconeCabecalho();
    TEST_ASSERT_TRUE(dentro(a, 14, 20));     // primeira letra
    TEST_ASSERT_TRUE(dentro(a, 176, 20));    // ultima letra
    TEST_ASSERT_TRUE(dentro(a, 95, 20));     // meio da palavra

    // E para antes da HORA, que comeca em 186 e nao e alvo de nada.
    TEST_ASSERT_FALSE(dentro(a, 186, 20));
}

void test_o_alvo_da_sessao_cobre_o_percentual_desenhado(void) {
    const Alvo a = alvoPctSessao();
    TEST_ASSERT_TRUE(dentro(a, 93, 140));    // canto do "20%"
    TEST_ASSERT_TRUE(dentro(a, 150, 159));   // canto oposto
    TEST_ASSERT_TRUE(dentro(a, 121, 149));   // meio, onde o dedo cai
}

void test_o_alvo_da_semana_cobre_o_percentual_desenhado(void) {
    const Alvo a = alvoPctSemana();
    TEST_ASSERT_TRUE(dentro(a, 242, 140));
    TEST_ASSERT_TRUE(dentro(a, 299, 159));
    TEST_ASSERT_TRUE(dentro(a, 270, 149));   // meio do "81%"
}

void test_o_rotulo_da_semana_cobre_a_palavra_desenhada(void) {
    const Alvo a = alvoRotuloSemana();
    TEST_ASSERT_TRUE(dentro(a, 172, 126));
    TEST_ASSERT_TRUE(dentro(a, 229, 135));
    TEST_ASSERT_TRUE(dentro(a, 200, 130));   // meio de "SEMANA"
}

// A divisao da COLUNA em duas metades e o que separa dois gestos diferentes:
// ensaiar a morte do Kenny (esquerda) e abrir a tela do Token (direita). Se as
// metades se sobrepusessem, um deles nunca aconteceria.
void test_as_duas_metades_da_coluna_nao_se_pisam(void) {
    const Alvo esq = alvoRotuloSemana();
    const Alvo dir = alvoPctSemana();

    // Encostam sem sobrepor: onde uma acaba, a outra comeca.
    TEST_ASSERT_EQUAL_INT(esq.x + esq.w, dir.x);

    // E nenhum ponto pertence as duas.
    for (int x = esq.x; x < dir.x + dir.w; x += 7)
        TEST_ASSERT_FALSE(dentro(esq, x, 150) && dentro(dir, x, 150));
}

// As duas janelas agora sao VIZINHAS DE LADO, e nao mais empilhadas: elas
// dividem a mesma altura e o que as separa e o x. Um toque na coluna de 5h nao
// pode abrir a tela da de 7 dias.
void test_as_duas_colunas_nao_se_pisam(void) {
    const Alvo sessao = alvoPctSessao();
    const Alvo semana = alvoPctSemana();

    TEST_ASSERT_TRUE(sessao.x + sessao.w <= semana.x);
    TEST_ASSERT_FALSE(dentro(sessao, 270, 149));   // o "81%" nao e da sessao
    TEST_ASSERT_FALSE(dentro(semana, 121, 149));   // nem o "20%" e da semana

    // O vao de 6 px entre as colunas nao pertence a ninguem: um dedo ali nao
    // pode disparar o ensaio errado.
    const Alvo rotulo = alvoRotuloSemana();
    TEST_ASSERT_FALSE(dentro(sessao, 159, 149));
    TEST_ASSERT_FALSE(dentro(rotulo, 159, 149));
}

// A coluna da SESSAO tem a metade esquerda LIVRE — o rotulo dela nunca teve
// ensaio. Um alvo ali roubaria o toque de quem erra a mira no percentual.
void test_o_rotulo_da_sessao_nao_dispara_nada(void) {
    const Alvo sessao = alvoPctSessao();
    TEST_ASSERT_FALSE(dentro(sessao, 23, 130));    // canto do "SESSAO"
    TEST_ASSERT_FALSE(dentro(sessao, 77, 130));    // fim da palavra
    TEST_ASSERT_FALSE(dentro(alvoRotuloSemana(), 50, 130));
    TEST_ASSERT_FALSE(dentro(alvoPctSemana(), 50, 130));
}

// A altura util cresceu de 40 para 98 px: a coluna inteira responde, do titulo
// ao rodape. Era 40 porque duas faixas empilhadas dividiam a vertical.
void test_o_alvo_cobre_a_coluna_inteira_em_altura(void) {
    const Alvo a = alvoPctSemana();
    TEST_ASSERT_EQUAL_INT(114, a.y);
    TEST_ASSERT_EQUAL_INT(98, a.h);
    TEST_ASSERT_TRUE(dentro(a, 270, 177));   // a altura da barra
    TEST_ASSERT_TRUE(dentro(a, 270, 199));   // a altura do rodape
    TEST_ASSERT_FALSE(dentro(a, 270, 212));  // e para onde a coluna acaba
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


// ---- Os cartoes da lista de sessoes ----
// Geometria em pe: primeiro cartao em y=228, passo 44, cartao de 40 px. O vao
// de 4 px entre dois cartoes nao pertence a nenhum dos dois.

void test_cada_cartao_responde_na_sua_faixa(void) {
    TEST_ASSERT_EQUAL_INT(0, cartaoSessaoAt(228, 5));   // topo do primeiro
    TEST_ASSERT_EQUAL_INT(0, cartaoSessaoAt(267, 5));   // ultima linha dele
    TEST_ASSERT_EQUAL_INT(1, cartaoSessaoAt(272, 5));   // topo do segundo
    TEST_ASSERT_EQUAL_INT(4, cartaoSessaoAt(404, 5));   // o quinto
    TEST_ASSERT_EQUAL_INT(4, cartaoSessaoAt(443, 5));
}

void test_o_vao_entre_cartoes_nao_responde(void) {
    // 268..271 sao os 4 px de respiro depois do primeiro cartao. Um dedo ali
    // nao pode abrir o terminal da sessao de baixo por meio pixel.
    for (int y = 268; y <= 271; y++)
        TEST_ASSERT_EQUAL_INT(-1, cartaoSessaoAt(y, 5));
}

void test_fora_da_lista_nao_responde(void) {
    TEST_ASSERT_EQUAL_INT(-1, cartaoSessaoAt(227, 5));   // acima do primeiro
    TEST_ASSERT_EQUAL_INT(-1, cartaoSessaoAt(114, 5));   // na coluna de limite
    TEST_ASSERT_EQUAL_INT(-1, cartaoSessaoAt(462, 5));   // no rodape
}

// O que virou "+N" nao esta na tela e nao pode responder ao dedo: com tres
// sessoes desenhadas, o quarto e o quinto lugares sao fundo.
void test_o_que_nao_coube_nao_tem_alvo(void) {
    TEST_ASSERT_EQUAL_INT(2, cartaoSessaoAt(316, 3));    // o terceiro existe
    TEST_ASSERT_EQUAL_INT(-1, cartaoSessaoAt(360, 3));   // o quarto nao
    TEST_ASSERT_EQUAL_INT(-1, cartaoSessaoAt(404, 3));
    TEST_ASSERT_EQUAL_INT(-1, cartaoSessaoAt(228, 0));   // lista vazia
    TEST_ASSERT_EQUAL_INT(-1, cartaoSessaoAt(228, -2));
}

// ---- Os dois aneis da primeira tela DEITADA ----
// Medidas do desenho (drawDeitadaNova, com L_MARG 14, L_ANEL_R1 68, vao 10 e
// coluna de texto de 76): cada grupo centrado na sua metade de 226 px poe os
// centros em x=84 e x=310, na linha y=118. O rotulo da janela ("SESSAO",
// "SEMANA") fica no miolo, 15 px abaixo do centro.
void test_os_aneis_deitados_cobrem_o_rotulo_do_miolo(void) {
    const Alvo ses = alvoAnelSessaoDeitado();
    const Alvo sem = alvoAnelSemanaDeitado();

    TEST_ASSERT_TRUE(dentro(ses, 84, 133));      // onde "SESSAO" esta escrito
    TEST_ASSERT_TRUE(dentro(sem, 310, 133));     // e "SEMANA"
    TEST_ASSERT_TRUE(dentro(ses, 84, 118));      // o percentual, logo acima
    TEST_ASSERT_TRUE(dentro(sem, 310, 118));
}

// Um anel nao responde pelo outro: sao duas telas diferentes que eles abrem.
void test_os_dois_aneis_deitados_nao_se_pisam(void) {
    TEST_ASSERT_FALSE(dentro(alvoAnelSessaoDeitado(), 310, 133));
    TEST_ASSERT_FALSE(dentro(alvoAnelSemanaDeitado(), 84, 133));
    // E o vao entre eles nao pertence a ninguem.
    TEST_ASSERT_FALSE(dentro(alvoAnelSessaoDeitado(), 200, 118));
    TEST_ASSERT_FALSE(dentro(alvoAnelSemanaDeitado(), 200, 118));
}

// Eles ficam INTEIROS abaixo do cabecalho (linha em y=42) e dentro da tela
// deitada: um alvo que subisse ali roubaria o toque do botao de girar, que e a
// unica saida da orientacao.
void test_os_aneis_deitados_nao_invadem_o_cabecalho_nem_a_borda(void) {
    const Alvo a[2] = {alvoAnelSessaoDeitado(), alvoAnelSemanaDeitado()};
    for (int i = 0; i < 2; i++) {
        TEST_ASSERT_TRUE(a[i].y > 46);                  // abaixo do alvo do giro
        TEST_ASSERT_TRUE(a[i].x >= 0);
        TEST_ASSERT_TRUE(a[i].x + a[i].w <= 480);       // dentro da tela deitada
        TEST_ASSERT_TRUE(a[i].y + a[i].h <= 320);
    }
}

// A escala dos bichos deitados, contra a caixa REAL da tela (240x250) e os
// tamanhos REAIS dos dois sprites, lidos dos arquivos: o Token e 224x336 e o
// Clawd dormindo e 192x169.
void test_a_escala_enche_a_caixa_sem_estourar(void) {
    // O Token e limitado pela ALTURA: 336 -> 250, e a largura acompanha.
    const Escala t = escalaParaCaber(224, 336, 240, 250);
    TEST_ASSERT_EQUAL_INT(250, 336 * t.num / t.den);
    TEST_ASSERT_EQUAL_INT(166, 224 * t.num / t.den);
    TEST_ASSERT_TRUE(224 * t.num / t.den <= 240);

    // O Clawd dormindo e o contrario: sobra altura, e quem aperta e a LARGURA.
    // Ele CRESCE — por divisor inteiro ficaria parado nos 169 px nativos.
    const Escala o = escalaParaCaber(192, 169, 240, 250);
    TEST_ASSERT_EQUAL_INT(240, 192 * o.num / o.den);
    TEST_ASSERT_EQUAL_INT(211, 169 * o.num / o.den);
    TEST_ASSERT_TRUE(169 * o.num / o.den <= 250);
}

// Cabe exatamente = 1:1, e caixa degenerada nao vira divisao por zero nem
// escala zero — sem escala calculada, o tamanho nativo e a resposta certa.
void test_escala_nos_extremos(void) {
    const Escala igual = escalaParaCaber(240, 250, 240, 250);
    TEST_ASSERT_EQUAL_INT(240, 240 * igual.num / igual.den);
    TEST_ASSERT_EQUAL_INT(250, 250 * igual.num / igual.den);

    const Escala d[4] = {escalaParaCaber(0, 100, 240, 250),
                         escalaParaCaber(100, 0, 240, 250),
                         escalaParaCaber(100, 100, 0, 250),
                         escalaParaCaber(100, 100, 240, -3)};
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT(1, d[i].num);
        TEST_ASSERT_EQUAL_INT(1, d[i].den);
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_os_aneis_deitados_cobrem_o_rotulo_do_miolo);
    RUN_TEST(test_os_dois_aneis_deitados_nao_se_pisam);
    RUN_TEST(test_os_aneis_deitados_nao_invadem_o_cabecalho_nem_a_borda);
    RUN_TEST(test_a_escala_enche_a_caixa_sem_estourar);
    RUN_TEST(test_escala_nos_extremos);
    RUN_TEST(test_o_icone_do_cabecalho_cobre_o_fogo_desenhado);
    RUN_TEST(test_o_alvo_do_cabecalho_cobre_o_nome_inteiro);
    RUN_TEST(test_o_alvo_da_sessao_cobre_o_percentual_desenhado);
    RUN_TEST(test_o_alvo_da_semana_cobre_o_percentual_desenhado);
    RUN_TEST(test_o_rotulo_da_semana_cobre_a_palavra_desenhada);
    RUN_TEST(test_as_duas_metades_da_coluna_nao_se_pisam);
    RUN_TEST(test_as_duas_colunas_nao_se_pisam);
    RUN_TEST(test_o_rotulo_da_sessao_nao_dispara_nada);
    RUN_TEST(test_o_alvo_cobre_a_coluna_inteira_em_altura);
    RUN_TEST(test_nenhum_alvo_escapa_da_tela);
    RUN_TEST(test_o_alvo_da_direita_termina_no_fim_do_card);
    RUN_TEST(test_cada_cartao_responde_na_sua_faixa);
    RUN_TEST(test_o_vao_entre_cartoes_nao_responde);
    RUN_TEST(test_fora_da_lista_nao_responde);
    RUN_TEST(test_o_que_nao_coube_nao_tem_alvo);
    return UNITY_END();
}
