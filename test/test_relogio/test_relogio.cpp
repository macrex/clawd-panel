#include <unity.h>
#include <string>
#include "relogio.h"

void setUp(void) {}
void tearDown(void) {}

// 2026-08-15, um sabado. Epoch ja em hora LOCAL: quem aplica o fuso e quem
// chama, porque fuso e configuracao e nao conta.
static const long SABADO_00H00 = 1786752000L;
static const long SABADO_10H59 = 1786791540L;

// ---- O prazo que a placa passa a contar sozinha ----
//
// Antes ela so repetia a string pronta da API (`week_resets_dh`), e isso tinha
// dois defeitos que apareciam juntos: a string CONGELA quando o servidor cala, e
// ela e formatada em dias e horas, entao qualquer coisa abaixo de uma hora vira
// "0d00h". Faltando 39 minutos para a virada, o painel dizia zero dias e zero
// horas — que se le como "ja passou" e nao como "falta pouco".

void test_prazo_dias_bate_com_a_api(void) {
    // Acima de um dia a forma continua a mesma que a API mandava, para a tela
    // nao mudar de cara sem motivo.
    TEST_ASSERT_EQUAL_STRING("2d23h", prazoTexto(259199).c_str());
    TEST_ASSERT_EQUAL_STRING("3d06h", prazoTexto(3 * 86400 + 6 * 3600).c_str());
    TEST_ASSERT_EQUAL_STRING("1d00h", prazoTexto(86400).c_str());
}

void test_prazo_horas(void) {
    TEST_ASSERT_EQUAL_STRING("4h54m", prazoTexto(17699).c_str());
    TEST_ASSERT_EQUAL_STRING("1h00m", prazoTexto(3600).c_str());
    // 23h59m e o ultimo antes de virar dia: aqui a forma ainda e a de horas.
    TEST_ASSERT_EQUAL_STRING("23h59m", prazoTexto(86399).c_str());
}

void test_prazo_minutos_e_o_que_faltava(void) {
    // ESTE e o caso que dava "0d00h" na tela da semana.
    TEST_ASSERT_EQUAL_STRING("39m", prazoTexto(39 * 60).c_str());
    TEST_ASSERT_EQUAL_STRING("59m", prazoTexto(3599).c_str());
    TEST_ASSERT_EQUAL_STRING("1m", prazoTexto(60).c_str());
}

void test_prazo_segundos_nao_arredonda_para_zero(void) {
    // Falta menos de um minuto: dizer "0m" seria dizer que acabou. O painel
    // prefere admitir que e menos do que a resolucao dele.
    TEST_ASSERT_EQUAL_STRING("<1m", prazoTexto(59).c_str());
    TEST_ASSERT_EQUAL_STRING("<1m", prazoTexto(1).c_str());
}

void test_prazo_vencido(void) {
    // Zero ou negativo NAO e "0m": a janela ja virou e a placa ainda nao viu o
    // prazo novo, porque quem carimba a janela seguinte e a API.
    TEST_ASSERT_EQUAL_STRING("agora", prazoTexto(0).c_str());
    TEST_ASSERT_EQUAL_STRING("agora", prazoTexto(-500).c_str());
}

// ---- O prazo envelhece com o dado ----

void test_restante_desconta_a_idade(void) {
    Metric m;
    m.known = true;
    m.resetsIn = 3600;
    // Dado fresco: o prazo e o que a API disse.
    TEST_ASSERT_EQUAL_INT(3600, prazoRestante(m, 0));
    // Dez minutos sem contato: dez minutos a menos no relogio.
    TEST_ASSERT_EQUAL_INT(3000, prazoRestante(m, 600));
    // Passou da hora enquanto o servidor estava fora. Nao vira negativo: quem
    // le so precisa saber que venceu.
    TEST_ASSERT_EQUAL_INT(0, prazoRestante(m, 7200));
}

void test_restante_sem_prazo_continua_sem_prazo(void) {
    Metric m;
    m.known = true;
    m.resetsIn = 0;      // a API nao carimbou a janela nova ainda
    TEST_ASSERT_EQUAL_INT(0, prazoRestante(m, 600));

    Metric desconhecida;
    desconhecida.known = false;
    desconhecida.resetsIn = 3600;
    TEST_ASSERT_EQUAL_INT(0, prazoRestante(desconhecida, 0));
}

void test_restante_ignora_idade_negativa(void) {
    // Idade negativa nao existe no mundo, mas existe em aritmetica de relogio
    // com duas fontes de tempo. Ela nao pode ESTICAR o prazo.
    Metric m;
    m.known = true;
    m.resetsIn = 3600;
    TEST_ASSERT_EQUAL_INT(3600, prazoRestante(m, -900));
}

void test_texto_da_tela_separa_os_dois_zeros(void) {
    // prazoRestante devolve zero para "venceu" e para "nunca houve prazo", e a
    // tela precisa dizer coisas diferentes nos dois casos.
    Metric venceu;
    venceu.known = true;
    venceu.resetsIn = 600;
    TEST_ASSERT_EQUAL_STRING("agora", prazoDaTela(venceu, 3600).c_str());

    Metric sem_carimbo;
    sem_carimbo.known = true;
    sem_carimbo.resetsIn = 0;
    TEST_ASSERT_EQUAL_STRING("-", prazoDaTela(sem_carimbo, 0).c_str());

    Metric desconhecida;
    desconhecida.known = false;
    TEST_ASSERT_EQUAL_STRING("-", prazoDaTela(desconhecida, 0).c_str());

    // E o caso que motivou tudo: a semana com 39 minutos de vida, lida 5
    // minutos depois do ultimo contato.
    Metric semana;
    semana.known = true;
    semana.resetsIn = 39 * 60;
    TEST_ASSERT_EQUAL_STRING("34m", prazoDaTela(semana, 300).c_str());
}

// ---- O relogio da propria placa ----
//
// A placa passou a ter hora sem depender do servidor (SNTP no boot). O que mora
// aqui e so a conversao de epoch LOCAL para os tres textos do cabecalho.

void test_relogio_meia_noite_e_meio_dia(void) {
    // Meia-noite e 00, e nao 24.
    Clock meia = relogioDe(SABADO_00H00);
    TEST_ASSERT_TRUE(meia.known);
    TEST_ASSERT_EQUAL_STRING("00:00", meia.hm.c_str());
    TEST_ASSERT_EQUAL_STRING("15/08", meia.date.c_str());

    Clock meio = relogioDe(SABADO_00H00 + 12 * 3600);
    TEST_ASSERT_EQUAL_STRING("12:00", meio.hm.c_str());
}

void test_relogio_dia_e_semana(void) {
    Clock c = relogioDe(SABADO_10H59);
    TEST_ASSERT_TRUE(c.known);
    TEST_ASSERT_EQUAL_STRING("10:59", c.hm.c_str());
    TEST_ASSERT_EQUAL_STRING("15/08", c.date.c_str());
    // Sem acento: as fontes do painel so cobrem ASCII.
    TEST_ASSERT_EQUAL_STRING("SAB", c.weekday.c_str());
}

void test_relogio_domingo_fecha_a_semana(void) {
    // A tabela de dias e indexada por um calculo, e o domingo e a ponta dela:
    // um erro de um dia no indice aparece aqui antes de aparecer na tela.
    Clock c = relogioDe(SABADO_00H00 + 86400);
    TEST_ASSERT_EQUAL_STRING("DOM", c.weekday.c_str());
    TEST_ASSERT_EQUAL_STRING("16/08", c.date.c_str());
}

void test_relogio_sem_sincronia_se_cala(void) {
    // Antes da primeira sincronia a placa liga em 1970. Um relogio que diz
    // 01/01 com cara de certeza e pior do que um que admite nao saber — e o
    // cabecalho ja sabe esconder a linha quando `known` e falso.
    TEST_ASSERT_FALSE(relogioDe(0).known);
    TEST_ASSERT_FALSE(relogioDe(3600).known);
}

// ---- O instante da virada, que nao envelhecia ----

void test_at_vale_enquanto_a_janela_nao_virou(void) {
    Metric m;
    m.known    = true;
    m.resetsIn = 2 * 3600;
    m.at       = "7:20pm";
    TEST_ASSERT_EQUAL_STRING("7:20pm", atDaTela(m, 0).c_str());
    // Meia hora de dado velho ainda deixa uma hora e meia de janela.
    TEST_ASSERT_EQUAL_STRING("7:20pm", atDaTela(m, 1800).c_str());
}

void test_at_some_quando_a_janela_vence(void) {
    Metric m;
    m.known    = true;
    m.resetsIn = 600;
    m.at       = "7:20pm";
    TEST_ASSERT_EQUAL_STRING("", atDaTela(m, 600).c_str());
    TEST_ASSERT_EQUAL_STRING("", atDaTela(m, 99999).c_str());
}

void test_at_do_retrato_velho_nao_afirma_horario(void) {
    // O caso que motivou tudo: `lerCache` recalcula `resetsIn` para zero num
    // retrato lido depois da virada, mas o texto do instante vem intacto do
    // cartao. Sem isto a tela diz "vira as 6:00am" sobre uma janela de ontem.
    Metric doCartao;
    doCartao.known    = true;
    doCartao.resetsIn = 0;
    doCartao.at       = "01/08/2026 (Sabado)";
    TEST_ASSERT_EQUAL_STRING("", atDaTela(doCartao, 0).c_str());
}

void test_at_ausente_ou_travessao_continua_vazio(void) {
    Metric semCampo;
    semCampo.known    = true;
    semCampo.resetsIn = 3600;
    TEST_ASSERT_EQUAL_STRING("", atDaTela(semCampo, 0).c_str());

    // O travessao e o "nao sei" da propria API, e nao um instante.
    Metric travessao = semCampo;
    travessao.at = "-";
    TEST_ASSERT_EQUAL_STRING("", atDaTela(travessao, 0).c_str());

    Metric desconhecida;
    desconhecida.known    = false;
    desconhecida.resetsIn = 3600;
    desconhecida.at       = "7:20pm";
    TEST_ASSERT_EQUAL_STRING("", atDaTela(desconhecida, 0).c_str());
}

// ---- O piso que a NVS guarda entre um boot e o proximo ----

void test_epoch_abaixo_do_minimo_e_recusado(void) {
    // Uma placa sem sincronia liga em 1970, e uma API antiga manda zero.
    TEST_ASSERT_FALSE(epochAceitavel(0, 0));
    TEST_ASSERT_FALSE(epochAceitavel(3600, 0));
    TEST_ASSERT_FALSE(epochAceitavel(EPOCH_MINIMO - 1, 0));
    TEST_ASSERT_TRUE(epochAceitavel(EPOCH_MINIMO, 0));
}

void test_sem_piso_qualquer_hora_plausivel_serve(void) {
    // Primeiro boot da placa, NVS limpa: nao ha com o que comparar.
    TEST_ASSERT_TRUE(epochAceitavel(SABADO_10H59, 0));
    TEST_ASSERT_TRUE(epochAceitavel(SABADO_10H59, -1));
}

void test_o_tempo_nao_anda_para_tras(void) {
    // O servidor com o relogio desacertado, o payload corrompido, o SNTP
    // respondido por um cache mentiroso: todos chegam como um epoch plausivel e
    // ANTERIOR ao ultimo instante que esta placa ja viu.
    TEST_ASSERT_FALSE(epochAceitavel(SABADO_00H00, SABADO_10H59));
    TEST_ASSERT_TRUE(epochAceitavel(SABADO_10H59, SABADO_00H00));
    // O mesmo instante passa: a placa que reinicia em segundos ve o proprio piso.
    TEST_ASSERT_TRUE(epochAceitavel(SABADO_10H59, SABADO_10H59));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_prazo_dias_bate_com_a_api);
    RUN_TEST(test_prazo_horas);
    RUN_TEST(test_prazo_minutos_e_o_que_faltava);
    RUN_TEST(test_prazo_segundos_nao_arredonda_para_zero);
    RUN_TEST(test_prazo_vencido);
    RUN_TEST(test_restante_desconta_a_idade);
    RUN_TEST(test_restante_sem_prazo_continua_sem_prazo);
    RUN_TEST(test_restante_ignora_idade_negativa);
    RUN_TEST(test_texto_da_tela_separa_os_dois_zeros);
    RUN_TEST(test_relogio_meia_noite_e_meio_dia);
    RUN_TEST(test_relogio_dia_e_semana);
    RUN_TEST(test_relogio_domingo_fecha_a_semana);
    RUN_TEST(test_relogio_sem_sincronia_se_cala);
    RUN_TEST(test_at_vale_enquanto_a_janela_nao_virou);
    RUN_TEST(test_at_some_quando_a_janela_vence);
    RUN_TEST(test_at_do_retrato_velho_nao_afirma_horario);
    RUN_TEST(test_at_ausente_ou_travessao_continua_vazio);
    RUN_TEST(test_epoch_abaixo_do_minimo_e_recusado);
    RUN_TEST(test_sem_piso_qualquer_hora_plausivel_serve);
    RUN_TEST(test_o_tempo_nao_anda_para_tras);
    return UNITY_END();
}
