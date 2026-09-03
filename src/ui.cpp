#include "ui.h"
#include <math.h>
#include "term_parse.h"
#include "cp437.h"
#include "display.h"
#include "board_pins.h"
#include "view_model.h"
#include "clawd.h"
#include "layout.h"
#include "grupos.h"
#include "provedores.h"
#include "DejaVuSans7pt7b.h"
#include <cstdio>
#include <cstring>

namespace {

const uint16_t BG      = RGB565(12, 14, 18);
const uint16_t CARD    = RGB565(26, 30, 38);
const uint16_t FG      = RGB565(235, 238, 242);
const uint16_t MUTED   = RGB565(130, 140, 155);
const uint16_t TRACK   = RGB565(48, 54, 66);
// UM degrau acima do CARD, e so um. E o fundo de cada sessao na lista em pe,
// onde a linha virou um objeto proprio (ver drawSessoesRetrato). Mais claro que
// isto e a linha comeca a competir com o card que a contem; menos, e o degrau
// some na tela e o cartao deixa de existir.
const uint16_t SUBCARD = RGB565(34, 39, 50);
const uint16_t C_GREEN = RGB565(64, 200, 120);
const uint16_t C_YELL  = RGB565(230, 190, 70);
const uint16_t C_RED   = RGB565(235, 85, 85);
// Quanto da cor do NIVEL entra na folga do ritmo, em porcento sobre o trilho —
// ver drawBar. Nao e cor fixa: ela segue o nivel, entao a barra inteira
// continua falando numa cor so.
const int RITMO_FOLGA = 34;

// A fresta de trilho entre o gasto e o excesso. Dois pixels nas duas
// orientacoes: em 8 px de altura ela ja e um quarto da barra, e mais do que
// isso faria a barra parecer partida em duas.
//
// E a UNICA linha vertical que a barra desenha, e ela so existe quando o gasto
// passou do ritmo. Enquanto ele esta dentro, quem marca o ritmo e a ponta
// arredondada da folga.
const int FRESTA_W = 2;

// ---- A paleta do herdr, para o estado das sessoes ----
// Sao as cores do proprio herdr (ui_theme.h do cliente dele), e nao as nossas:
// quem olha a placa e o terminal ao lado ve a MESMA cor para o mesmo estado, e
// nao precisa traduzir duas paletas. Vale so para as bolinhas de estado — as
// barras e os percentuais continuam na paleta do painel, que fala de outra
// coisa (quanto sobrou), e nao de em que pe esta o agente.
const uint16_t H_WORKING = RGB565(201, 162,  74);   // ambar  #c9a24a
const uint16_t H_DONE    = RGB565( 95, 158, 168);   // teal   #5f9ea8
const uint16_t H_IDLE    = RGB565(125, 169, 125);   // verde  #7da97d
const uint16_t H_BLOCKED = RGB565(192,  90,  85);   // vermelho #c05a55

// A ORIGEM PC2, violeta #9a7fd0: a unica familia de matiz ainda livre — nao
// colide com os niveis, com os estados do herdr nem com o laranja da marca.
// Agente local nao ganha marca nenhuma; so o forasteiro se anuncia.
const uint16_t ROXO_PC2 = RGB565(154, 127, 208);

// Quando o dado esta velho, tudo perde a cor. Uma variavel de modulo evita
// arrastar um parametro "stale" por todas as funcoes de desenho.
bool g_stale = false;

// Ha duas fontes configuradas? Sem isso, nenhum chip de maquina e desenhado.
bool g_duasFontes = false;
// POR QUE o dado esta velho, em ate 11 caracteres — quem escolhe o texto e
// lib/metrics/falha.h, a partir do codigo e da duracao da ultima tentativa.
//
// Era um booleano ("o stale e por queda de radio?") e virou o motivo inteiro
// quando ficou claro que "sem contato" cobria tres situacoes com tres acoes
// diferentes: o processo da API morto, a maquina fora, a API respondendo erro.
const char *g_motivo = "SEM CONTATO";

// ---- O realce de turno concluido ----
//
// A linha da sessao que terminou e ninguem viu ainda ganha uma faixa fraca com
// um traco solido na margem. Ele nao pisca, nao esmaece e nao tem relogio: sai
// no instante em que a bolinha vira verde, e nao um segundo antes nem depois.
//
// ISTO JA FOI UM SUBSISTEMA. A primeira versao guardava uma lista de sessoes
// avisadas com o instante de cada uma e apagava o realce em seis degraus ao
// longo de 30 s — detecao de transicao, fila, expiracao, um tick proprio no
// laco principal. Tudo isso morreu numa linha, porque a API ja publica
// exatamente a pergunta que o realce faz: `done` significa "o turno acabou e
// ninguem viu", e vira falso quando alguem ve.
//
// O que sobrou e melhor e nao so menor. Um realce por tempo mente nos dois
// sentidos: some enquanto voce ainda nao viu, e fica aceso depois que voce ja
// viu. Este acompanha o mesmo estado que pinta a bolinha, entao os dois nunca
// podem discordar.
//
// O teal e o mesmo `H_DONE` da bolinha — o realce nao inventa cor nova, so
// amplia a que aquela linha ja tem.
const int AVISO_TRACO_W = 3;
// A intensidade e FIXA: o meio do caminho da versao que esmaecia, que foi onde
// ela ficou boa na placa. Numeros sobre 48 e sobre 6.
const int AVISO_FUNDO_NUM = 4, AVISO_FUNDO_DEN = 48;   // ~8% de teal
const int AVISO_TRACO_NUM = 4, AVISO_TRACO_DEN = 6;    // ~67% de teal

// Interpola duas cores RGB565 componente a componente. `num/den` = 0 devolve
// `a`, 1 devolve `b`.
uint16_t misturar(uint16_t a, uint16_t b, int num, int den) {
    if (den <= 0) return a;
    const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    const int r  = ar + (br - ar) * num / den;
    const int g  = ag + (bg - ag) * num / den;
    const int bl = ab + (bb - ab) * num / den;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

// A marca da linha. `y` e o topo da faixa e `h` a altura.
//
// SEM CANTOS PROPRIOS, e isso e o conserto de um erro concreto: a primeira
// versao usava `fillRoundRect` de raio 6 com ~65% de teal, e dentro de um card
// que ja e um retangulo arredondado ela lia como uma segunda moldura mal
// encaixada — alem de tirar contraste do texto branco por cima. Daqui saem as
// duas regras: margem a margem, como linha de tabela, e fraca o bastante para
// nao disputar com o texto.
//
// A faixa diz QUAL linha; o traco e o que ainda se ve de longe, porque 8% de
// teal sobre o card e quase o card.
// `base` e a cor sobre a qual a marca mistura. Por padrao e o CARD, que e o
// fundo de quase toda linha; a tela em pe passa o SUBCARD, porque la a linha
// tem um fundo proprio e misturar contra o CARD deixaria a marca mais ESCURA
// que a linha que ela deveria realcar.
// `raio` maior que zero = a linha e um CARTAO de canto redondo, e o realce tem
// que ter o mesmo canto. Com `fillRect` dentro de um cartao arredondado a marca
// vaza pelos quatro cantos, e o cartao perde a forma justamente no momento em
// que ele quer chamar atencao — que e o oposto do que a marca serve para fazer.
//
// Com raio, o traco de margem tambem sai: naquela coluna mora a faixa de
// estado, que ja e a marca da borda daquele cartao. Ele so existe na forma
// reta, onde nao ha faixa nenhuma.
void drawAvisoLinha(Arduino_Canvas *g, bool marcado,
                    int x, int y, int w, int h, uint16_t base = CARD,
                    int raio = 0) {
    if (!marcado || g_stale) return;
    const uint16_t fundo = misturar(base, H_DONE, AVISO_FUNDO_NUM,
                                    AVISO_FUNDO_DEN);
    if (raio > 0) {
        g->fillRoundRect(x, y, w, h, raio, fundo);
        return;
    }
    g->fillRect(x, y, w, h, fundo);
    g->fillRect(x, y, AVISO_TRACO_W, h,
                misturar(base, H_DONE, AVISO_TRACO_NUM, AVISO_TRACO_DEN));
}

uint16_t colorOf(Level l) {
    if (g_stale)            return MUTED;
    if (l == Level::Red)    return C_RED;
    if (l == Level::Yellow) return C_YELL;
    return C_GREEN;
}

uint16_t fgColor() { return g_stale ? MUTED : FG; }

// ---- Logo do Claude ----
// O mesmo desenho do CLI:
//     ▐▛███▜▌
//    ▝▜█████▛▘
//      ▘▘ ▝▝
// Aqueles sao blocos Unicode (U+2580..U+259F) e as fontes embutidas do
// Arduino_GFX so tem ASCII, entao imprimi-los sairia lixo. Cada bloco e uma
// celula dividida em QUADRANTES (▐ = metade direita, ▛ = tudo menos o inferior
// direito, ▝ = so o superior direito), o que transforma as 3 linhas de texto
// em um bitmap 17x5 desenhado com retangulos — fiel e independente de fonte.
const int      LOGO_W = 17;
const int      LOGO_H = 5;
const uint16_t LARANJA = RGB565(217, 119, 87);   // laranja da marca, #D97757

// A linha de um agente que ACABOU de aparecer. Mesma forma do aviso de turno
// concluido — faixa fraca com o traco na margem —, em LARANJA: a cor ja
// significa "atividade nova" no resto do painel (o logo, o trabalhando).
//
// Vale por alguns segundos e some sozinha. Nao ha o que responder aqui: e so
// para o olho encontrar quem chegou sem ler a lista inteira.
const int NOVO_FUNDO_NUM = 5, NOVO_FUNDO_DEN = 48;
const int NOVO_TRACO_NUM = 4, NOVO_TRACO_DEN = 6;

// `raio` como em drawAvisoLinha: a marca acompanha o canto do cartao.
void drawNovoLinha(Arduino_Canvas *g, bool novo, int x, int y, int w, int h,
                   uint16_t base = CARD, int raio = 0) {
    if (!novo || g_stale) return;
    const uint16_t fundo = misturar(base, LARANJA, NOVO_FUNDO_NUM,
                                    NOVO_FUNDO_DEN);
    if (raio > 0) {
        g->fillRoundRect(x, y, w, h, raio, fundo);
        return;
    }
    g->fillRect(x, y, w, h, fundo);
    g->fillRect(x, y, AVISO_TRACO_W, h,
                misturar(base, LARANJA, NOVO_TRACO_NUM, NOVO_TRACO_DEN));
}

const char *LOGO[LOGO_H] = {
    "00011111111111100",
    "00011011111101100",
    "01111111111111111",
    "00011111111111100",
    "00001010000101000",
};

// Cada quadrante e desenhado com o DOBRO da altura da largura. No terminal uma
// celula de caractere e ~1:2, entao seus quadrantes tambem sao 1:2 — desenhar
// quadrados deixa o logo 2x mais largo do que deveria (achatado).
// A tag que um agente mostra: a que a maquina dele publicou. Vazia quando ha
// uma fonte so (nao ha o que distinguir) ou contra uma API que nao publica tag.
const std::string &tagDe(const std::string &tag) {
    static const std::string NADA;
    return g_duasFontes ? tag : NADA;
}

// Uma cor por maquina: verde a de casa, violeta a de fora. Duas cores cheias em
// vez de cinza-e-cor porque as duas tags sao dado, e nao legenda — a leitura
// util aqui e "de qual das duas", e nao "esta e a estranha".
uint16_t corDaTag(int origem) { return origem ? ROXO_PC2 : C_GREEN; }

// O selo VIA: o cabecalho desta resposta veio da segunda maquina porque o
// master nao respondeu a volta. Texto escuro sobre violeta para nao competir
// com os numeros — e um aviso de procedencia, nao um alarme.
//
// O nome sai do proprio payload que esta na tela: e a maquina que respondeu
// que diz como se chama.
int drawSeloVia(Arduino_Canvas *g, int x, int y, const std::string &tag) {
    const std::string txt = tag.empty() ? "VIA RESERVA" : "VIA " + tag;
    const int w = (int)txt.size() * 6 + 10;
    g->fillRoundRect(x, y, w, 16, 4, ROXO_PC2);
    g->setTextColor(BG);
    g->setTextSize(1);
    g->setCursor(x + 5, y + 4);
    g->print(txt.c_str());
    return w;
}

int logoW(int sx) { return LOGO_W * sx; }
int logoH(int sx) { return LOGO_H * sx * 2; }

void drawLogo(Arduino_Canvas *g, int x, int y, int sx, uint16_t color) {
    const int sy = sx * 2;
    for (int r = 0; r < LOGO_H; r++)
        for (int c = 0; c < LOGO_W; c++)
            if (LOGO[r][c] == '1')
                g->fillRect(x + c * sx, y + r * sy, sx, sy, color);
}

// Temperatura com o sinal de grau. A fonte embutida do Arduino_GFX so tem
// ASCII, entao nao existe caractere de grau para imprimir — trocar de fonte so
// por isso custaria flash e mudaria de lugar todo o resto da interface. Um anel
// de 2 px de raio faz o papel e custa uma chamada. Devolve a largura ocupada.
int drawTemp(Arduino_Canvas *g, int x, int y, int temp, uint16_t cor, int sz = 2) {
    char n[8];
    snprintf(n, sizeof(n), "%d", temp);
    const int cw = 6 * sz;                    // largura de um caractere no corpo sz
    const int nw = (int)strlen(n) * cw;

    g->setTextColor(cor);
    g->setTextSize(sz);
    g->setCursor(x, y);
    g->print(n);
    g->drawCircle(x + nw + sz * 3 / 2, y + 2 * sz, sz, cor);   // grau proporcional
    g->setCursor(x + nw + sz * 9 / 2, y);
    g->print("C");
    return nw + sz * 9 / 2 + cw;
}



void drawFooter(Arduino_Canvas *g, const Status &s, int page, int staleSeconds) {
    // A turma no lugar do logo: tres bichos animados, so o da esquerda falando
    // de estado (ver clawd.h). Todos pisam na MESMA linha, e nao centrados: os
    // quadros tem alturas diferentes e centrar faria cada um flutuar na sua.
    //
    // O logo antigo carregava a cor da pior metrica; essa informacao migrou
    // para o cabecalho, com numero e tudo. Sem o cartao, cai no logo de sempre
    // — laranja, ja que o papel de indicador nao e mais dele.
    const int chao = SCREEN_H - 8;

    // Fora das paginas de bicho grande: nelas o caranguejo ja ocupa a tela
    // inteira e o trio seria o mesmo desenho de novo. Na do nivel o rodape
    // ainda e usado — pela barra de XP, que precisa da largura toda.
    //
    // A TELA NOVA TEM a fileira, no mesmo canto inferior esquerdo das outras.
    // Ela saiu do MIOLO — que era onde ela morava em pe, e onde deitado ela
    // custaria caro e roubaria o espaco dos aneis —, nao do painel.
    if (page != 3 && page != 4) {
        const int fs = 3;
        if (!clawd::crewW() || !clawd::drawCrewInto(g, 14, chao))
            drawLogo(g, 14, chao - logoH(fs) - 6, fs, g_stale ? MUTED : LARANJA);
    }

    for (int i = 0; i < ui::PAGES; i++) {
        const int cx = SCREEN_W - 40 - (ui::PAGES - 1 - i) * 18;
        if (i == page) g->fillCircle(cx, SCREEN_H - 26, 5, fgColor());
        else           g->drawCircle(cx, SCREEN_H - 26, 5, MUTED);
    }

    // Idade do dado, a esquerda das bolinhas de pagina. Morava no cabecalho e
    // saiu de la para o relogio caber: ali era a informacao menos consultada
    // ocupando o canto mais visivel da tela. Com dado velho ela vira o aviso —
    // mesmo lugar, em amarelo, sem faixa extra.
    //
    // O texto sai de lib/metrics/view_model.h: ele era escrito aqui e outra vez,
    // palavra por palavra, na versao em pe. `false` desliga o ramo do master
    // fora — deitado o rodape divide a largura com a fileira de bichos, e o
    // cabecalho ja mostra o selo VIA.
    std::string txt = textoVetustez(s, staleSeconds, g_motivo, false);

    const int dotEsq = SCREEN_W - 40 - (ui::PAGES - 1) * 18 - 5;

    // A fileira do rodape cresceu para 219 px quando o alert e o sweeping
    // entraram nela, e vai ate x=233. O texto e alinhado a direita, entao um
    // atraso longo o empurra para ca — "SEM CONTATO HA 3600h" com o prefixo do
    // motor comeca em x=225 e escreveria por cima do primeiro bicho.
    //
    // Encolher o texto, e nao mover a fileira: a fileira precisa comecar em
    // x=14 para caber na faixa do flush de prefixo (ver display::flushPrefix).
    // So o aviso de contato perdido chega a esse tamanho; a idade normal ("12s")
    // nao tem como encostar em nada.
    const int trioFim = (page != 3 && page != 4 && clawd::crewW())
                            ? 14 + clawd::crewW() : 14;
    if (staleSeconds > 0 && dotEsq - 12 - (int)txt.size() * 6 < trioFim + 8)
        txt = textoVetustezCurto(s, staleSeconds);
    // O AVISO DE CARTAO AUSENTE SAIU DAQUI, e a razao dele e que sumiu.
    //
    // Ele existia porque sem cartao o painel funcionava pela metade: a config
    // vinha da NVS, o Wi-Fi associava e os numeros apareciam, mas todo sprite
    // falhava e a tela ficava com bichos que nao trocam. O aviso dava nome a
    // esse defeito, que antes so a serial sabia.
    //
    // Os sprites agora moram na flash (src/assets.h). Com o cartao fora ou
    // travado em 0x107, o painel desenha COMPLETO — a fileira, o cabecalho, o
    // clima, tudo. Um aviso vermelho permanente para uma condicao que nao
    // degrada mais nada e alarme falso, e alarme falso ensina a ignorar o
    // rodape, que e onde mora o aviso que importa: o contato perdido.
    //
    // O fato continua registrado onde ele ainda serve para diagnosticar: a
    // serial diz no boot se o cartao montou.
    // Amarelo tambem quando o motor e o de reserva: os dois motores nao tem a
    // mesma confiabilidade, e mostrar estado deduzido com a cara de estado lido
    // seria a mesma mentira que o aviso de contato perdido existe para evitar.
    g->setTextColor((staleSeconds > 0 || s.hooksEngine) ? C_YELL : MUTED);
    g->setTextSize(1);
    g->setCursor(dotEsq - 12 - (int)txt.size() * 6, SCREEN_H - 30);
    g->print(txt.c_str());

    // O CARIMBO DOS LIMITES (`textoSincronia`) NAO APARECE AQUI, so no rodape
    // em pe. Deitado a ponta esquerda e da fileira de bichos, entao ele so
    // caberia espremido entre ela e a vetustez — e some justamente quando o
    // aviso de contato perdido cresce, ou seja, na hora em que uma linha some
    // sem explicacao. Um dado que aparece e desaparece conforme o vizinho e
    // pior do que um dado que mora num lugar so.
    //
    // As duas faixas de limite continuam esmaecendo aqui (ver Metric::memoria):
    // o que nao existe deitado e o carimbo, nao o aviso.
}

// As duas janelas de limite, em segundos. Sao elas que transformam o prazo que
// falta no quanto ja passou — a API manda quanto FALTA, e nao quanto correu.
const int JANELA_5H = 5 * 3600;
const int JANELA_7D = 7 * 24 * 3600;

// Quanto da janela ja correu, em porcento, ou -1 quando nao da para saber.
//
// `resetsIn` maior que a janela nao e erro de conta: a API arredonda para cima e
// pode devolver 18010 s numa janela de 18000. Serrar em 0..100 aqui evita que o
// desenho tenha que desconfiar do numero.
int ritmoPct(const Metric &m, int janelaSeg) {
    if (!m.known || janelaSeg <= 0 || m.resetsIn <= 0) return -1;
    const long correu = (long)janelaSeg - m.resetsIn;
    if (correu <= 0) return 0;
    if (correu >= janelaSeg) return 100;
    return (int)(correu * 100 / janelaSeg);
}

// A barra com o RITMO por tras: onde o gasto estaria se ele acompanhasse o
// relogio da janela.
//
// TUDO sai da mesma cor — a do nivel —, so que em intensidades diferentes sobre
// o trilho. A barra e a cor cheia; a folga ate a marca e a mesma cor bem
// apagada; a marca fica entre as duas. Ler tres tons de uma familia so e mais
// facil do que ler tres cores diferentes, e a barra ja muda de cor sozinha:
// uma cor fixa para a folga brigaria com o verde, o amarelo e o vermelho em
// momentos diferentes.
//
// Duas formas, porque as duas situacoes nao merecem o mesmo peso:
//
//   dentro do ritmo -> a folga aparece em tom apagado, e a ponta dela e a
//       marca. E credito que o relogio ja liberou: da para ver, sem gritar.
//   passou do ritmo -> o excesso vira faixa cheia, separada do gasto por uma
//       fresta de trilho. Ai sim ha o que ver, e a area diz de quanto foi sem
//       precisar de numero.
//
// Passar do ritmo nao e erro: quem trabalha em bloco queima 40% da janela de 5h
// na primeira hora. Por isso o excesso pinta so o proprio trecho, sem piscar e
// sem recolorir a barra inteira.
void drawBar(Arduino_Canvas *g, int x, int y, int w, int h, const Metric &m,
             int janelaSeg = 0) {
    g->fillRoundRect(x, y, w, h, h / 2, TRACK);
    if (!m.known) return;

    // Preenchimento menor que a altura nao tem como virar pilula — o
    // fillRoundRect quebra — e sumia. Numa barra de 12 px isso escondia tudo
    // ate 4%, que e exatamente o comeco de janela em que mais se olha para
    // ela. O piso arredonda para cima: 3% aparece como um ponto, e um ponto e
    // informacao.
    auto piso = [&](int v) { return (v > 0 && v < h) ? h : v; };
    const int fw = piso(barWidth(m.pct, w));
    const int rp = ritmoPct(m, janelaSeg);
    const int fr = rp < 0 ? -1 : piso(barWidth(rp, w));
    // LEMBRANCA APAGA A COR DO NIVEL, como o contato perdido ja fazia em
    // `colorOf`. Um numero de uma hora atras com a barra vermelha de sempre
    // grita uma urgencia que ninguem conferiu desde entao; cinza diz a mesma
    // coisa que o resto do bloco: isto vale, mas nao e de agora.
    const uint16_t cor = m.memoria ? MUTED : colorOf(m.level);

    // Sem ritmo conhecido, a barra de sempre e mais nada.
    if (fr < 0) {
        if (fw > 0) g->fillRoundRect(x, y, fw, h, h / 2, cor);
        return;
    }

    if (fw > fr) {
        // PASSOU DO RITMO. O gasto continua na cor do NIVEL — a cor da barra
        // pertence a API (verde/amarelo/vermelho do limite) e nada aqui pode
        // sobrescreve-la. A primeira versao pintava o excesso num vermelho
        // apagado proprio, e a barra ficava vermelha com 3% de uso: alarme de
        // cor para um estado que o nivel dizia ser verde.
        //
        // Quem diz "passou" e so a FRESTA: dois pixels de trilho cravados na
        // posicao do ritmo. O que fica a direita dela e o excesso, legivel
        // pela area, sem cor nova.
        if (fw > 0) g->fillRoundRect(x, y, fw, h, h / 2, cor);

        // Ela termina no ritmo em vez de comecar ali: comecando, comeria o
        // excesso e o ritmo pareceria dois porcento adiante do que e. E vive
        // na parte RETA da barra, porque nos h/2 px de cada ponta o trilho
        // curva e um retangulo ali sairia do desenho.
        const int minX = h / 2;
        const int maxX = w - h / 2 - FRESTA_W;
        if (maxX >= minX) {
            int fx = fr - FRESTA_W;
            if (fx < minX) fx = minX;
            if (fx > maxX) fx = maxX;
            g->fillRect(x + fx, y, FRESTA_W, h, TRACK);
        }
        return;
    }

    // DENTRO DO RITMO. A folga inteira primeiro e o gasto por cima. Nada mais:
    // a PONTA da folga ja e a marca do ritmo, e ela e arredondada como todo
    // resto da barra.
    //
    // Aqui houve um risco vertical, e ele saiu. Uma linha reta encostada numa
    // ponta redonda nao le como marca: le como barra malfeita, e a
    // desproporcao aparecia justamente no caso comum, que e este.
    if (fr > 0) g->fillRoundRect(x, y, fr, h, h / 2, misturar(TRACK, cor, RITMO_FOLGA, 100));
    if (fw > 0) g->fillRoundRect(x, y, fw, h, h / 2, cor);
}

// Escreve em fonte tamanho 2 dentro de `w`, quebrando no ULTIMO espaco que
// ainda cabe. Existe por causa da data: "01/08/2026 (Sabado)" mede 228 px em
// fonte 2 e o card tem 191 uteis, entao ou quebra ou encolhe — e encolher era
// justamente o problema. Sem espaco aproveitavel corta seco, que nunca acontece
// com os dois formatos de hoje mas nao pode virar escrita fora do card.
void printWrapped2(Arduino_Canvas *g, int x, int y, int w, const std::string &txt) {
    const int CH2 = 12;                    // largura do caractere em tamanho 2
    const int cabe = w / CH2;
    if (cabe <= 0 || txt.empty()) return;

    const int corte = breakAt(txt.c_str(), cabe);
    g->setTextSize(2);
    g->setCursor(x, y);
    g->print(txt.substr(0, corte).c_str());
    if (corte >= (int)txt.size()) return;

    // O espaco da quebra nao comeca a segunda linha. Duas linhas e o teto: o
    // card nao tem altura para uma terceira, e nenhum dos dois formatos chega
    // perto disso.
    size_t resto = (size_t)corte;
    if (txt[resto] == ' ') resto++;
    g->setCursor(x, y + 18);
    g->print(txt.substr(resto, cabe).c_str());
}

// Card de metrica com titulo, percentual grande, barra e tempo de reset.
//
// `janelaSeg` e a duracao da janela daquele limite, e so serve para a barra
// saber desenhar o ritmo. Zero desliga o ritmo — e o que vale para qualquer
// metrica que nao seja uma janela com prazo, como o contexto.
void drawCard(Arduino_Canvas *g, int x, int y, int w, int h,
              const char *title, const Metric &m, const char *resetsLabel,
              int janelaSeg = 0) {
    g->fillRoundRect(x, y, w, h, 10, CARD);

    g->setTextColor(MUTED);
    g->setTextSize(1);
    g->setCursor(x + 14, y + 14);
    g->print(title);

    // O TAMANHO SAI DA LARGURA, e nao de uma constante.
    //
    // Com dois cards (219 px, 191 uteis) "100%" em fonte 5 mede 120 e cabe.
    // Com tres (141 px, 113 uteis) nao cabe, e cai para fonte 4, que mede 96.
    // Decidir aqui, e nao em quem chama, e o que faz o painel voltar exatamente
    // a tela antiga quando a API nao manda a quebra por modelo — sem encolher
    // nada a toa.
    // Medido contra "100%" (4 caracteres) e nao contra o valor de agora: pelo
    // texto corrente o card trocaria de fonte ao passar de 99% para 100%, e um
    // numero que muda de tamanho sozinho parece defeito.
    const int util = w - 28;
    const std::string pct = pctText(m);
    const int tamPct = (4 * 6 * 5 <= util) ? 5 : 4;
    // Lembranca sai esmaecida, como todo dado velho aqui. Ver Metric::memoria.
    g->setTextColor(m.known && !m.memoria ? fgColor() : MUTED);
    g->setTextSize(tamPct);
    g->setCursor(x + 14, y + 34);
    g->print(pct.c_str());

    drawBar(g, x + 14, y + 86, w - 28, 12, m, janelaSeg);

    g->setTextColor(MUTED);
    g->setTextSize(1);
    g->setCursor(x + 14, y + 104);
    g->print(resetsLabel);
    // Tamanho 3 contra o 2 do momento absoluto logo abaixo: o prazo e a
    // resposta principal do card e precisa continuar mandando na hierarquia.
    // Com os dois no mesmo tamanho eles empatavam, e o card ficava sem foco.
    // "1d06h" em fonte 3 mede 90 px; o card comporta 191.
    g->setTextColor(m.known && !m.memoria ? fgColor() : MUTED);
    g->setTextSize(3);
    g->setCursor(x + 14, y + 116);
    g->print(m.known && !m.resets.empty() ? m.resets.c_str() : "-");

    // O MESMO instante em relogio ("7:20pm") ou em data ("01/08/2026
    // (Sabado)"). O prazo de cima responde quanto falta; este responde quando —
    // que e a pergunta de quem esta decidindo se ainda comeca mais uma coisa
    // hoje, e que obrigava a fazer a conta de cabeca.
    //
    // Mesmo tamanho do prazo, e nao menor: a primeira versao usou fonte 1 e o
    // dado ficou ilegivel a distancia de onde o painel fica. O card cresceu
    // para caber — havia 60 px vazios abaixo dele, ate a turma do rodape.
    // Apagado, isso sim: e o mesmo dado do numero grande, por outro angulo.
    //
    // Se a API for antiga o campo vem vazio e a linha simplesmente nao existe.
    // Mesma regra de largura. "01/08/2026" sao 10 caracteres, e em fonte 2 eles
    // pedem 120 px — cabe nos 191 de dois cards, nao cabe nos 113 de tres.
    // Abaixo disso a data inteira entra numa linha so em fonte 1 (18 por linha).
    //
    // Este e o elemento que mais perde com os tres cards, e perde de proposito:
    // responde a MESMA pergunta que o prazo logo acima, por outro angulo. O
    // prazo continua em fonte 3 porque e a resposta principal do card.
    if (m.known && !m.at.empty() && m.at != "-") {
        g->setTextColor(MUTED);
        if (util >= 10 * 12) {
            printWrapped2(g, x + 14, y + 146, util, m.at);
        } else {
            g->setTextSize(1);
            g->setCursor(x + 14, y + 146);
            g->print(m.at.c_str());
        }
    }
}

// A bolinha de estado de um agente.
//
// UMA funcao para as duas telas que a desenham — o menu da pagina de contexto e
// o card de sessoes da primeira. O significado da cor e a coisa desta interface
// que menos pode divergir entre dois lugares: duas copias envelheceriam
// diferente e a mesma bolinha passaria a querer dizer coisas distintas conforme
// a pagina.
//
//   BLOQUEADO    vermelho   precisa de voce
//   TRABALHANDO  laranja    esta rodando agora
//   OCIOSO       verde      turno encerrado, nada pendente
//   DESCONHECIDO vazado     sem hooks: nao da para afirmar
//
// Verde aqui NAO e o verde de "contexto abaixo de 50%" — e o mesmo tom, mas
// responde outra pergunta. Como a bolinha so fala de estado, nao ha
// ambiguidade: nesta coluna verde quer dizer "tranquilo".
//
// Antes ela misturava dois eixos: bloqueado e trabalhando falavam de ESTADO, e
// o resto caia na cor do CONTEXTO. Um agente ocioso em 60% ficava amarelo, e
// nada na tela avisava que aquele amarelo tinha mudado de assunto.
//
// Vermelho FIXO e nao pulsante: pulsar custava um redesenho completo a cada
// 140 ms (~45% de CPU) para animar 10 px, porque a bolinha fica fora da faixa
// que o flush de prefixo alcanca. Quem puxa o olho e o selo do rodape, que
// anima o alert por ~9 ms o quadro.
// A cor de um estado, sem desenhar nada. Existe porque a tela em pe passou a
// dizer o estado com uma FAIXA e nao com uma bolinha, e as duas formas nao
// podem divergir de cor — a bolinha da tela deitada e a faixa da em pe sao a
// mesma informacao.
uint16_t corDoEstado(AgentState st, bool done) {
    if (g_stale) return MUTED;
    switch (st) {
        case AgentState::Blocked: return H_BLOCKED;
        case AgentState::Working: return H_WORKING;
        case AgentState::Idle:    return done ? H_DONE : H_IDLE;
        default:                  return TRACK;
    }
}

void drawEstadoDot(Arduino_Canvas *g, int cx, int cy, AgentState st, bool done) {
    switch (st) {
        case AgentState::Blocked:
            g->fillCircle(cx, cy, 5, g_stale ? MUTED : H_BLOCKED);
            break;
        case AgentState::Working:
            g->fillCircle(cx, cy, 4, g_stale ? MUTED : H_WORKING);
            break;
        case AgentState::Idle:
            // Teal CHEIO quando o turno terminou e ninguem viu; verde VAZADO
            // quando ja viu. A ordem e a do herdr: `done` e conferido antes de
            // `idle`.
            //
            // A forma tambem e a dele, e nao so a cor: la o ocioso ja visto e um
            // anel, e o preenchimento fica para o que ainda pede atencao. Faz
            // sentido sozinho — cheio pesa mais que vazado, e "ja vi" e o
            // estado que menos precisa puxar o olho desta coluna.
            if (done) g->fillCircle(cx, cy, 4, g_stale ? MUTED : H_DONE);
            else      g->drawCircle(cx, cy, 4, g_stale ? MUTED : H_IDLE);
            break;
        default:
            // Vazado: a forma diz "nao sei" melhor do que mais um tom de cinza
            // cheio, que passaria por ocioso.
            g->drawCircle(cx, cy, 4, TRACK);
            break;
    }
}

// ---- Menu de repos (lateral direita da pagina de contexto) ----
const int MENU_X   = 330;
const int MENU_W   = SCREEN_W - MENU_X - 14;   // 136
const int MENU_Y0  = 54;
const int MENU_ROW = 30;
const int MENU_MAX = 7;                        // cabe entre o cabecalho e o rodape

int menuVisible(int total) { return total < MENU_MAX ? total : MENU_MAX; }

void drawMenu(Arduino_Canvas *g, const Status &s, int selected) {
    const int n = menuVisible((int)s.agents.size());
    for (int i = 0; i < n; i++) {
        const Agent &a = s.agents[i];
        const int y = MENU_Y0 + i * MENU_ROW;

        if (i == selected) g->fillRoundRect(MENU_X, y, MENU_W, MENU_ROW - 4, 6, CARD);

        // Bolinha com a cor daquele agente: da para ver o estado de todos sem
        // precisar selecionar um por um.
        //
        // A bolinha responde UMA pergunta so: em que estado esta este agente.
        //
        //   BLOQUEADO    vermelho pulsante   precisa de voce
        //   TRABALHANDO  laranja da marca    esta rodando agora
        //   OCIOSO       verde               turno encerrado, nada pendente
        //   DESCONHECIDO cinza vazado        sem hooks: nao da para afirmar
        //
        // Verde aqui NAO e o verde de "contexto abaixo de 50%" — e o mesmo tom,
        // mas responde outra pergunta. Como a bolinha agora so fala de estado,
        // nao ha ambiguidade: nesta coluna verde quer dizer "tranquilo".
        //
        // Antes ela misturava dois eixos: bloqueado e trabalhando falavam de
        // ESTADO, e o resto caia na cor do CONTEXTO. Um agente ocioso em 60%
        // ficava amarelo, e nada na tela avisava que aquele amarelo tinha
        // mudado de assunto. O contexto ja aparece em numero grande, em barra e
        // no cabecalho — nao precisava desta bolinha tambem.
        const int cy = y + (MENU_ROW - 4) / 2;
        const int cx = MENU_X + 12;
        drawEstadoDot(g, cx, cy, a.state, a.done);

        g->setTextColor(i == selected ? fgColor() : MUTED);
        g->setTextSize(2);
        g->setCursor(MENU_X + 24, y + 5);
        // 9 caracteres e o que cabe na largura do menu com fonte tamanho 2.
        g->print(a.repo.substr(0, 9).c_str());
    }

    // Nao esconde silenciosamente: diz quantos ficaram de fora.
    const int total = (int)s.agents.size();
    if (total > n) {
        g->setTextColor(MUTED);
        g->setTextSize(1);
        g->setCursor(MENU_X + 24, MENU_Y0 + n * MENU_ROW + 4);
        g->printf("+%d", total - n);
    }
}

// ---- Botao de limpeza de contexto (pagina 1) ----
// Fica a direita do percentual grande, no unico retangulo vazio desta pagina:
// o numero ocupa x=20..128 e o menu comeca em 330.
//
// Este e o primeiro lugar do painel que AGE em vez de so mostrar, e por isso
// tem dois toques. O primeiro arma, o segundo dispara. Uma compactacao nao e
// destrutiva, mas custa tempo e interrompe o turno — e a tela vive na parede,
// onde uma manga de camisa passa por cima dela sem querer.
// Quem conta o tempo do armado e o laco principal (ARMADO_MS em main.cpp): o
// desenho so recebe o resultado.
//
// x=176 e nao 152: o percentual grande e fonte 6, e em "100%" ele mede 144 px a
// partir de x=20 — ou seja, termina em 164. Com o botao em 152 os dois se
// encostavam exatamente no dia em que a janela de contexto enchesse.
// A COLUNA DE BOTOES desta pagina, encostada na margem do menu. Ela comeca em
// x=176 porque o percentual em corpo 6 ocupa ate ~x=128 e a barra vai ate
// y=128 — este retangulo e o que sobra.
//
// Em cima fica VER TERMINAL e embaixo COMPACTAR, e nao o contrario: o de cima
// e o que existe SEMPRE, e o de baixo so acima de 50% de contexto. Com o raro
// em cima, o comum mudaria de altura conforme o contexto subisse — um botao
// que anda sozinho e um botao que se erra.
const int BTN_X = 176;
const int TB_Y = 52,  TB_H = 40;      // VER TERMINAL
const int BTN_Y = 96, BTN_H = 30;     // COMPACTAR
const int BTN_W = 144;

// O botao so existe quando ha o que compactar.
//
// Abaixo de 50% a compactacao custa mais do que devolve: ela interrompe o turno
// e joga fora o detalhe da conversa para recuperar espaco que ainda sobra. Um
// botao sempre aceso convidaria a apertar exatamente quando nao adianta.
//
// O corte e a faixa AMARELA, e nao um numero solto: e a mesma fronteira que ja
// pinta o percentual desta pagina, entao o botao aparece no instante em que o
// numero deixa de ser verde. Duas regras diferentes para a mesma pergunta
// seriam duas coisas para manter — e uma para explicar.
//
// Enquanto a limpeza acontece ele fica de pe de qualquer jeito: e ali que a
// tela diz que ela esta acontecendo, e o contexto ja caiu abaixo do corte
// justamente por causa dela.
bool botaoVisivel(const Status &s, int selected) {
    if (s.cleaning) return true;
    if (s.agents.empty()) return false;
    const Agent &a = s.agents[selected];
    return a.hasContext && a.level != Level::Green;
}

void drawCleanButton(Arduino_Canvas *g, const Status &s, bool armado) {
    const bool limpando = s.cleaning;
    // Armado em amarelo, limpando em laranja, parado em cinza: as tres cores
    // que o resto do painel ja usa para "atencao", "acontecendo" e "inerte".
    const uint16_t cor = limpando ? LARANJA : (armado ? C_YELL : TRACK);
    const char *txt = limpando ? "LIMPANDO" : (armado ? "CONFIRMA?" : "COMPACTAR");

    g->fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 8, CARD);
    g->drawRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 8, cor);
    // Armado ganha um segundo contorno: em pe, a um metro da tela, a diferenca
    // de cor sozinha nao se ve.
    if (armado && !limpando)
        g->drawRoundRect(BTN_X + 2, BTN_Y + 2, BTN_W - 4, BTN_H - 4, 6, cor);

    g->setTextColor(limpando || armado ? cor : MUTED);
    g->setTextSize(2);
    const int larg = (int)strlen(txt) * 12;
    g->setCursor(BTN_X + (BTN_W - larg) / 2, BTN_Y + (BTN_H - 16) / 2);
    g->print(txt);
}

// ---- O bloqueio no lugar do contexto ----
// Quando o agente selecionado parou esperando aprovacao, a pergunta dele toma a
// coluna esquerda desta pagina — o percentual, a barra, repo, branch e modelo
// saem. Nao e falta de espaco: e que nenhum deles responde a unica pergunta que
// importa nesse instante, que e "o que ele quer de mim". Respondido, o bloco de
// sempre volta sozinho, porque o agente deixa de estar bloqueado.
const int BQ_X   = 20;
const int BQ_W   = MENU_X - 40;
const int BQ_COLS = BQ_W / 6;              // caracteres por linha, tamanho 1
// A pergunta comeca ABAIXO da faixa do botao de terminal (y=52..92): ele existe
// nas duas montagens desta pagina e no mesmo lugar, entao e o texto que se
// acomoda a ele, e nao o contrario.
const int BQ_TEXTO_Y = 96;
// Topo FIXO dos botoes, e nao "logo abaixo da pergunta". Uma pergunta de uma
// linha deixa um vao aqui, e isso e de proposito: com o topo variavel, o botao
// mudaria de lugar quando a pergunta mudasse de tamanho — e o dedo ja estaria a
// caminho do lugar antigo.
const int BQ_TOPO = 134;
const int BQ_FIM  = 255;                   // acima do rodape
const int BQ_GAP  = 6;
const int BQ_ALT_MAX = 44;
const int BQ_ALT_MIN = 20;

// Geometria de UM botao de opcao. Esta funcao e usada pelo desenho E pelo
// toque: duas contas separadas divergem com o tempo, e divergir aqui significa
// tocar num botao e responder outro.
bool bloqueioBotao(int i, int total, int &y, int &h) {
    if (total <= 0 || i < 0 || i >= total) return false;
    int alt = (BQ_FIM - BQ_TOPO + BQ_GAP) / total - BQ_GAP;
    // Teto para que duas opcoes nao virem dois tarjoes, e piso para que cinco
    // nao virem cinco tiras intocaveis.
    if (alt > BQ_ALT_MAX) alt = BQ_ALT_MAX;
    if (alt < BQ_ALT_MIN) return false;
    y = BQ_TOPO + i * (alt + BQ_GAP);
    h = alt;
    return true;
}

// Escreve `txt` quebrando em `cols` caracteres, ate `maxLinhas`. Devolve
// quantas linhas saiu.
int textoQuebrado(Arduino_Canvas *g, int x, int y, int passo, int cols,
                  const char *txt, int maxLinhas) {
    int linhas = 0;
    const char *p = txt;
    while (*p && linhas < maxLinhas) {
        const int n = breakAt(p, cols);
        if (n <= 0) break;
        g->setCursor(x, y + linhas * passo);
        // Controle vira espaco na SAIDA: escrever '\n' aqui moveria o cursor por
        // conta propria e a linha seguinte sairia deslocada, por cima do que
        // ja esta na tela.
        for (int i = 0; i < n; i++)
            g->write(eControleDeCursor(p[i]) ? ' ' : p[i]);
        linhas++;
        p += n;
        while (*p == ' ') p++;
    }
    return linhas;
}

// Declarado aqui porque a definicao mora abaixo, junto do card de sessoes: o
// chip de ORIGEM e o mesmo desenho nos dois lugares, e duplica-lo para evitar
// esta linha seria pior.
int drawChip(Arduino_Canvas *g, int x, int y, const std::string &txt,
             uint16_t corTxt, int xLimite);

void drawBloqueio(Arduino_Canvas *g, const Status &s, int opcaoArmada) {
    const Bloqueio &b = s.bloqueio;

    // A linha de titulo vira a instrucao quando ha opcao armada. Ela e o unico
    // lugar desta pagina que sobra livre a essa altura — abaixo esta a pergunta
    // e a direita, o botao de terminal. E o aviso importa mais que o rotulo:
    // quem armou ja sabe que precisa dele.
    g->setTextColor(opcaoArmada ? C_YELL : C_RED);
    g->setTextSize(1);
    g->setCursor(BQ_X, 58);
    g->print(opcaoArmada ? "CONFIRMAR? TOQUE DE NOVO" : "PRECISA DE VOCE");

    // De qual maquina e o agente que precisa de voce. So quando nao ha opcao
    // armada: o aviso de confirmacao ja tomou a linha.
    if (!opcaoArmada && !tagDe(b.tag).empty())
        drawChip(g, BQ_X + 15 * 6 + 8, 55, tagDe(b.tag), corDaTag(b.origem),
                 BQ_X + BQ_W);

    // A pergunta em tres linhas no maximo. O servidor ja corta em 200 bytes;
    // aqui o corte e de espaco vertical, e o que sobrar fica de fora.
    g->setTextColor(fgColor());
    g->setTextSize(1);
    if (b.pergunta.empty()) {
        g->setTextColor(MUTED);
        g->setCursor(BQ_X, BQ_TEXTO_Y);
        // A API nao conseguiu ler a tela. Dizer isso e melhor do que uma area
        // vazia que parece defeito do painel.
        g->print("(nao deu para ler a tela dele)");
    } else {
        textoQuebrado(g, BQ_X, BQ_TEXTO_Y, 11, BQ_COLS, b.pergunta.c_str(), 3);
    }

    if (b.opcoes.empty()) {
        // Bloqueado, mas o que esta na tela nao e um seletor navegavel. Sem
        // botao: um Enter numa tela que nao esperava Enter responde uma
        // pergunta que ninguem fez.
        g->setTextColor(MUTED);
        g->setTextSize(1);
        g->setCursor(BQ_X, BQ_TOPO + 8);
        g->print("a tela dele nao tem opcoes numeradas.");
        g->setCursor(BQ_X, BQ_TOPO + 22);
        g->print("abra o TERMINAL para ver o que ele pede.");
        return;
    }

    const int total = (int)b.opcoes.size();
    for (int i = 0; i < total; i++) {
        int y = 0, h = 0;
        if (!bloqueioBotao(i, total, y, h)) break;
        const Opcao &o = b.opcoes[i];
        const bool armada = (opcaoArmada == o.n);

        g->fillRoundRect(BQ_X, y, BQ_W, h, 8, CARD);
        g->drawRoundRect(BQ_X, y, BQ_W, h, 8, armada ? C_YELL : TRACK);
        // Armada ganha um segundo contorno, como o botao de limpeza: em pe, a
        // um metro da tela, a diferenca de cor sozinha nao se ve.
        if (armada) g->drawRoundRect(BQ_X + 2, y + 2, BQ_W - 4, h - 4, 6, C_YELL);

        // O numero da opcao a esquerda, o rotulo ao lado. O numero e o mesmo
        // que esta na tela do agente: quem estiver olhando os dois ve o par.
        g->setTextColor(armada ? C_YELL : LARANJA);
        g->setTextSize(2);
        g->setCursor(BQ_X + 8, y + (h - 16) / 2);
        g->printf("%d", o.n);

        const int tx = BQ_X + 28;
        const int cols = (BQ_X + BQ_W - 8 - tx) / 6;
        // Duas linhas de rotulo cabem nos botoes altos; nos baixos, uma so.
        const int maxL = h >= 30 ? 2 : 1;
        g->setTextColor(armada ? C_YELL : fgColor());
        g->setTextSize(1);
        const int usadas = textoQuebrado(g, tx, y + (h - (maxL * 10 - 2)) / 2 + 1,
                                         10, cols, o.rotulo.c_str(), maxL);
        // A opcao que abre campo de texto nao resolve o bloqueio, so troca de
        // pergunta. Dizer isso antes do toque evita a decepcao depois dele.
        if (o.texto && usadas < maxL) {
            g->setTextColor(MUTED);
            g->setCursor(tx, y + (h - (maxL * 10 - 2)) / 2 + 1 + usadas * 10);
            g->print("(abre campo de texto)");
        }
    }

}

// ---- Botao de entrar no modo terminal ----
// No TOPO da coluna de botoes, o lugar mais alcancavel desta pagina — e o mesmo
// retangulo nas duas montagens dela, com o bloco de contexto e com a pergunta
// do bloqueio. Ele nao anda: em modo bloqueio o COMPACTAR nao e desenhado, e a
// pergunta e que comeca abaixo desta faixa.

// So existe quando ha para onde apontar. Sem pane do herdr nao ha tela para
// ler, e um botao que abre uma tela vazia e pior do que nenhum botao.
bool terminalVisivel(const Status &s, int selected) {
    if (selected < 0 || selected >= (int)s.agents.size()) return false;
    return !s.agents[selected].paneId.empty();
}

void drawTerminalButton(Arduino_Canvas *g) {
    g->fillRoundRect(BTN_X, TB_Y, BTN_W, TB_H, 8, CARD);
    g->drawRoundRect(BTN_X, TB_Y, BTN_W, TB_H, 8, TRACK);
    g->setTextColor(MUTED);
    g->setTextSize(2);
    const char *txt = "TERMINAL";
    const int larg = (int)strlen(txt) * 12;
    g->setCursor(BTN_X + (BTN_W - larg) / 2, TB_Y + (TB_H - 16) / 2);
    g->print(txt);
}

void drawRow(Arduino_Canvas *g, int y, const char *label, const char *value) {
    g->setTextColor(MUTED);
    g->setTextSize(1);
    g->setCursor(20, y);
    g->print(label);
    g->setTextColor(fgColor());
    g->setTextSize(2);
    g->setCursor(96, y - 4);
    // Trunca para nao invadir o menu da direita: "Opus 5 (1M context)" sozinho
    // ja encosta nele.
    const int maxCh = (MENU_X - 96 - 8) / 12;
    std::string v = (value && value[0]) ? value : "-";
    if ((int)v.size() > maxCh) v = v.substr(0, maxCh - 1) + "~";
    g->print(v.c_str());
}


// Terceiro card da primeira tela: quem esta aberto agora, e em que estado.
//
// Ele tomou o lugar do card de custo do dia. A troca e de utilidade por
// utilidade: o custo ja e um numero que so se olha de vez em quando, e vive
// tambem no nivel do Clawd da quarta tela; ja "quem esta rodando e quem parou"
// e a pergunta que faz alguem olhar o painel de longe. Repetir a coluna da
// segunda tela aqui nao e redundancia — e o que torna a PRIMEIRA tela
// suficiente para a olhada rapida, sem precisar deslizar.
//
// Sem selecao aqui, ao contrario do menu da outra pagina: esta tela nao tem
// agente selecionado, e destacar um deles insinuaria um estado que nao existe.
// Esforco em duas letras, para caber num chip.
std::string effortCurto(const std::string &e) {
    if (e == "XHigh")  return "XH";
    if (e == "High")   return "Hi";
    if (e == "Medium") return "Md";
    if (e == "Low")    return "Lo";
    if (e == "Max")    return "Mx";
    return e;
}

// Um chip: retangulo arredondado com texto em corpo 1. Devolve a largura
// ocupada, ou 0 quando nao cabe ate `xLimite` — encadeie somando o retorno.
int drawChip(Arduino_Canvas *g, int x, int y, const std::string &txt,
             uint16_t corTxt, int xLimite) {
    if (txt.empty()) return 0;
    const int cw = (int)txt.size() * 6 + 8;
    if (x + cw > xLimite) return 0;
    g->fillRoundRect(x, y, cw, 14, 4, TRACK);
    g->setTextColor(corTxt);
    g->setTextSize(1);
    g->setCursor(x + 4, y + 3);
    g->print(txt.c_str());
    return cw;
}

// O mesmo chip, mas CORTANDO o texto em vez de desistir dele.
//
// Existe por causa do modelo dos agentes que nao sao o Claude. "Claude Opus
// 4.6" pede 98 px e sobram 74 depois do chip da maquina: com `drawChip` o chip
// inteiro sumia, e a linha do Codex e a do Antigravity apareciam sem modelo
// nenhum — que foi exatamente o defeito visto na placa. Um nome cortado ainda
// diz qual modelo e; a ausencia dele nao diz nada.
//
// So o modelo usa esta variante. A tag da maquina e o esforco sao curtos e
// fixos, e cortar um deles seria perder o dado inteiro.
int drawChipCortando(Arduino_Canvas *g, int x, int y, const std::string &txt,
                     uint16_t corTxt, int xLimite) {
    if (txt.empty()) return 0;
    const int cabe = (xLimite - x - 8) / 6;
    if (cabe < 3) return 0;              // menos que tres letras nao informa
    if ((int)txt.size() <= cabe)
        return drawChip(g, x, y, txt, corTxt, xLimite);
    return drawChip(g, x, y, txt.substr(0, cabe), corTxt, xLimite);
}

// Os planos do Status no formato que a lib grupos entende. Duas structs
// separadas de proposito: `lib/grupos` nao pode depender do formato do payload,
// senao a mudanca de um campo do servidor arrastaria a lib de agrupamento
// junto.
std::vector<grupos::Plano> planosDe(const Status &s) {
    std::vector<grupos::Plano> p;
    p.reserve(s.planos.size());
    for (const PlanoConta &c : s.planos)
        p.push_back({c.agente, c.rotulo});
    return p;
}

// O plano da conta ao lado do rotulo do card, no caso de um provedor so.
// `xTexto` e onde ele comeca; `xLim`, onde ele nao pode passar.
//
// TRUNCA em vez de invadir a borda: o cabecalho do card deitado tem 115 px
// uteis, e "SESSOES 4" ja come 62 deles — "Enterprise" com o "- " na frente
// pede 72 e sairia pela direita do card. Cortado ainda se le; derramado por
// cima da moldura, nao.
void drawPlanoUnico(Arduino_Canvas *g, int xTexto, int y, int xLim,
                    const std::string &plano) {
    const int cabe = (xLim - xTexto) / 6;
    if (cabe < 4) return;                  // nem "- Ma" cabe: melhor nada
    std::string txt = "- " + plano;
    if ((int)txt.size() > cabe) txt.resize(cabe);
    g->setTextSize(1);
    g->setTextColor(MUTED);
    g->setCursor(xTexto, y);
    g->print(txt.c_str());
}

// O cabecalho de um grupo: rotulo a esquerda, plano a direita, filete ligando
// os dois. Corpo 1 para nao competir com os nomes de repo, que sao quem manda
// no card. `w` e a largura util da linha, a partir de `x`.
//
// O plano ausente vira "-" e nao string vazia: a lacuna diz "nao se sabe", e um
// espaco em branco leria como se o grupo nao tivesse plano nenhum.
// A altura da faixa de cabecalho de grupo. Constante, e nao numero solto,
// porque ela e lida em TRES lugares: o desenho da faixa, a centragem do icone
// dentro dela, e a conta de quantas linhas cabem no card (nas duas telas). Um
// deles fora de sincronia com os outros e cabecalho sobrepondo sessao.
// 16 e nao 14: os icones passaram a vir dos SVGs oficiais, e o knot da OpenAI
// so se le a partir de 12 px de altura (abaixo disso as seis alcas se fundem
// numa mancha). Com 12 px de arte, 14 de faixa deixavam 1 px de folga de cada
// lado — o icone encostava nas duas bordas e a faixa lia como aperto. Os 2 px
// a mais custam 6 px de tela com tres grupos.
const int H_CAB = 16;

// Mistura a cor da marca com o fundo da faixa na proporcao do alfa. Os dois
// lados sao decompostos do MESMO jeito, do RGB565 de volta para 8 bits, para
// que alfa 0 devolva exatamente TRACK — decompor so um deles deixava franja de
// um degrau em volta de cada icone.
// `fundo` e a cor sobre a qual o icone assenta. Por padrao e o TRACK da faixa
// de cabecalho; a lista em pe passa o SUBCARD, porque la o icone mora dentro do
// cartao da sessao — misturar contra o fundo errado devolve exatamente a franja
// de um degrau que este cuidado existe para evitar.
uint16_t misturaNaFaixa(uint16_t cor, uint8_t a, uint16_t fundo = TRACK) {
    const int cr = ((cor   >> 11) & 0x1F) * 255 / 31;
    const int cg = ((cor   >>  5) & 0x3F) * 255 / 63;
    const int cb = ( cor          & 0x1F) * 255 / 31;
    const int fr = ((fundo >> 11) & 0x1F) * 255 / 31;
    const int fg = ((fundo >>  5) & 0x3F) * 255 / 63;
    const int fb = ( fundo        & 0x1F) * 255 / 31;
    return RGB565((cr * a + fr * (255 - a)) / 255,
                  (cg * a + fg * (255 - a)) / 255,
                  (cb * a + fb * (255 - a)) / 255);
}

// O icone de um fornecedor, na cor da marca, com o meio-tom da mascara.
void drawIconeProvedor(Arduino_Canvas *g, int x, int y,
                       const provedores::Icone &ic, uint16_t cor,
                       uint16_t fundo = TRACK) {
    for (int r = 0; r < ic.h; r++)
        for (int c = 0; c < ic.w; c++) {
            // Os arredondamentos da reducao deixam lixo de 1 ou 2 no fundo
            // vazio. Pintar isso nao muda o pixel (o resultado quantiza de
            // volta para TRACK) e so custa chamada; o corte tambem garante que
            // o retangulo do icone nao pise no fundo da faixa.
            const uint8_t a = ic.alfa[r * ic.w + c];
            if (a >= 8) g->drawPixel(x + c, y + r, misturaNaFaixa(cor, a, fundo));
        }
}

// O cabecalho de um grupo: uma FAIXA de fundo com o icone do fornecedor na
// esquerda e o plano da conta na direita.
//
// A faixa substituiu um filete que atravessava a tela entre o rotulo e o
// plano. O traco tinha que competir por largura com o nome do grupo e com a
// contagem, e o resultado eram tres coisas disputando 12 px de altura. Um
// fundo um degrau acima do card separa sem disputar nada.
//
// O icone substituiu o nome escrito: "CLAUDE" custava 36 px de largura, e o
// icone faz o mesmo trabalho em 12 a 16. A contagem do grupo saiu junto —
// quantas sessoes sao se ve contando as linhas logo abaixo, que estao na tela.
//
// `w` e a largura util da linha, a partir de `x`.
void drawCabecalhoGrupo(Arduino_Canvas *g, int x, int y, int w,
                        const grupos::Linha &l) {
    // A faixa sangra 4 px para cada lado do texto do card: ela precisa ler
    // como uma barra que atravessa o bloco, e nao como mais uma linha indentada
    // igual as sessoes.
    //
    // A altura da faixa e `H_CAB`, e o porque de ela ser 16 esta la em cima:
    // e a arte mais alta (12 px) mais uma folga que se ve dos dois lados.
    g->fillRect(x - 4, y - 2, w + 8, H_CAB, TRACK);

    const provedores::Icone ic = provedores::iconeDe(l.cli);
    const uint16_t cor = provedores::corDe(l.cli);

    // O icone centrado na faixa pela ALTURA DELE, e nao por um numero fixo: as
    // artes tem alturas diferentes (o Claude e 5, as outras 12, porque cada
    // logotipo tem a proporcao que tem), e centrar todas pelo mesmo valor
    // cortava a mais alta contra a borda de cima.
    int cx = x;
    if (ic.w) {
        drawIconeProvedor(g, cx, y - 2 + (H_CAB - ic.h) / 2, ic, cor);
        cx += ic.w + 5;
    }

    // O nome do fornecedor por extenso, ao lado do icone. Ele voltou depois de
    // ter saido: o icone sozinho identifica quem ja sabe o que procurar, e um
    // logotipo de 16x10 nao le como "Anthropic" para mais ninguem. Na cor da
    // marca, como o icone — os dois sao a mesma etiqueta.
    //
    // Na DejaVu, e nao no corpo 1 da grade: com a fonte embutida o nome saia
    // com metade da altura do icone ao lado dele, e lia como legenda em vez de
    // etiqueta. E a mesma fonte dos nomes de repo logo abaixo, entao a faixa
    // deixa de ter um corpo so dela. A LINHA NAO MUDOU: `H_CAB` continua 16, e
    // a fonte cabe porque sua caixa inteira (`d` no topo, `g` embaixo) mede 14.
    const std::string nome = provedores::nomeDe(l.cli);
    const int wPlano = (int)l.plano.size() * 6;
    const int xPlano = x + w - wPlano;
    if (!nome.empty()) {
        g->setFont(&DejaVuSans7pt7b);
        g->setTextSize(1);
        g->setTextColor(cor);

        // O nome cede se colidir com o plano: o plano e a informacao que este
        // cabecalho existe para trazer, e o nome ja tem o icone o apoiando. O
        // corte e por MEDIDA e nao por contagem de letras — a DejaVu e
        // proporcional, e dividir por uma largura fixa cortaria cedo demais.
        std::string txt = nome;
        int16_t tx1, ty1; uint16_t tw, th;
        while (!txt.empty()) {
            g->getTextBounds(txt.c_str(), 0, 0, &tx1, &ty1, &tw, &th);
            if ((int)tw <= xPlano - 8 - cx) break;
            txt.pop_back();
        }
        if (!txt.empty()) {
            // `y + 10` e a BASELINE, nao o topo: com a fonte da grade o cursor
            // marcava o canto de cima, e repetir o `y + 1` daqui jogaria o
            // nome para fora da faixa por baixo. Daqui a caixa sobe 11 px (ate
            // `y - 1`, um dentro da borda) e desce 3 (ate `y + 13`, a ultima
            // linha da faixa) — o rabo do `g` de "Antigravity" e o unico que
            // chega la.
            g->setCursor(cx, y + 10);
            g->print(txt.c_str());
        }
        g->setFont();
    }

    // O plano na direita, em corpo 1. Sobre a faixa ele usa a cor de frente e
    // nao a apagada: aqui ele e o dado, e o resto da linha e etiqueta.
    if (!l.plano.empty() && xPlano > cx) {
        g->setTextSize(1);
        g->setTextColor(fgColor());
        g->setCursor(xPlano, y + 1);
        g->print(l.plano.c_str());
    }
}

void drawCardSessoes(Arduino_Canvas *g, int x, int y, int w, int h,
                     const Status &s) {
    g->fillRoundRect(x, y, w, h, 10, CARD);

    g->setTextColor(MUTED);
    g->setTextSize(1);
    g->setCursor(x + 14, y + 14);
    g->print("SESSOES");

    // O total logo ao lado do rotulo, e nao junto dele: o rotulo e apagado
    // porque e legenda, e o numero e DADO — a mesma separacao que os outros
    // dois cards fazem entre o titulo e o percentual.
    //
    // Ele responde o que a lista abaixo nao responde sozinha quando nao cabe
    // todo mundo. O "+N" do rodape do card diz quantos ficaram de fora; este
    // diz quantos existem, que e a pergunta que se faz primeiro.
    char total[8];
    snprintf(total, sizeof(total), "%d", (int)s.agents.size());
    g->setTextColor(s.agents.empty() ? MUTED : fgColor());
    g->setCursor(x + 14 + 7 * 6 + 8, y + 14);      // depois de "SESSOES"
    g->print(total);

    if (s.agents.empty()) {
        g->setTextColor(MUTED);
        g->setTextSize(2);
        g->setCursor(x + 14, y + 40);
        g->print("-");
        return;
    }

    // Duas linhas por sessao: o repo em cima, modelo e esforco em chips embaixo.
    // 38 px cabe quatro sessoes inteiras nos 156 px uteis do card; o total ao
    // lado do rotulo ja diz quantas existem, e o "+N" quantas ficaram de fora.
    const int passo = 38;
    const int hCab  = H_CAB;
    // UMA vez por desenho, e nunca dentro do laco: cada chamada aloca o vetor
    // de linhas inteiro, e o card e redesenhado a cada poll.
    const grupos::Lista lista = grupos::montarLista(s.agents, planosDe(s));

    // Com UM provedor so nao ha cabecalho, e o plano vai para o lado do rotulo
    // do card: o caso mais comum e o mais apertado, e gastar 12 px para dizer
    // "CLAUDE" numa tela onde tudo e Claude seria pagar caro por nada.
    if (!lista.planoUnico.empty())
        drawPlanoUnico(g, x + 14 + 7 * 6 + 8 + (int)strlen(total) * 6 + 8,
                       y + 14, x + w - 12, lista.planoUnico);

    int fora = 0;
    const int n = grupos::cabemLinhas(lista.linhas, h - 34, hCab, passo, fora);
    const int xLim = x + w - 12;

    int ly = y + 34;
    for (int i = 0; i < n; i++) {
        const grupos::Linha &linha = lista.linhas[i];

        if (linha.cabecalho) {
            drawCabecalhoGrupo(g, x + 14, ly, w - 26, linha);
            ly += hCab;
            continue;
        }

        const Agent &a = s.agents[linha.agente];

        // A marca de turno concluido, atras da linha inteira.
        drawNovoLinha(g, a.novo, x + 2, ly - 5, w - 4, passo - 2);
        drawNovoLinha(g, a.novo, x + 2, ly - 5, w - 4, passo - 2);
        drawAvisoLinha(g, a.done, x + 2, ly - 5, w - 4, passo - 2);

        drawEstadoDot(g, x + 14, ly + 8, a.state, a.done);

        // Linha 1: o repo, ainda em corpo 2 para ler de longe.
        g->setTextColor(fgColor());
        g->setTextSize(2);
        g->setCursor(x + 26, ly + 2);
        g->print(a.repo.substr(0, 9).c_str());

        // Linha 2: chips. O chip do AGENTE saiu: o cabecalho do grupo ja diz de
        // que CLI a sessao e, e a largura que ele ocupava passa para o nome do
        // modelo, que era o primeiro a ser cortado na cascata.
        int cx = x + 26;
        {
            // A TAG da maquina, primeiro chip da linha: e o dado que responde
            // "onde isto esta rodando", e vem antes do que o agente e.
            const int cwo = drawChip(g, cx, ly + 19, tagDe(a.tag),
                                     corDaTag(a.origem), xLim);
            if (cwo) cx += cwo + 4;
        }
        int cw = drawChipCortando(g, cx, ly + 19, a.model, fgColor(), xLim);
        if (cw) cx += cw + 4;
        drawChip(g, cx, ly + 19, effortCurto(a.effort), C_YELL, xLim);

        // Mini-barra de contexto embaixo dos chips: cor do nivel, preenchida
        // pelo context%. Fina (3 px) para nao roubar a linha da quarta sessao —
        // a cor e o quanto ela enche ja dizem o contexto de longe.
        const int bx = x + 26, bw = xLim - bx, by = ly + 33;
        g->drawFastHLine(bx, by, bw, TRACK);
        if (a.hasContext && a.contextPct > 0) {
            int fill = bw * a.contextPct / 100;
            if (fill < 2) fill = 2;
            g->fillRect(bx, by - 1, fill, 3, colorOf(a.level));
        }
        ly += passo;
    }

    // Nao coube todo mundo: dizer QUANTOS ficaram de fora e melhor do que uma
    // lista que termina sem aviso e parece completa. `fora` conta AGENTES, e
    // nunca cabecalhos — "+1" tem que significar uma sessao que voce nao esta
    // vendo.
    if (fora > 0) {
        g->setTextColor(MUTED);
        g->setTextSize(1);
        g->setCursor(x + 14, ly + 2);
        g->printf("+%d", fora);
    }
}

// ---- A pergunta do herdr em TELA CHEIA, no lugar dos 3 cards (P0) ----
// A mesma resposta da P1, mas aqui sem selecao: a P0 nao tem agente escolhido,
// entao mostra o bloqueio CORRENTE (s.bloqueio, um por vez, ja resolvido pela
// API). Ocupa a faixa dos cards (54..244); cabecalho e rodape ficam.
// FUNCOES e nao constantes: em pe o miolo tem outra forma, e a pergunta e a
// unica coisa desta pagina que ja era paramétrica o bastante para acompanhar a
// rotacao sozinha. Deitado ela sobra 158 px de altura para os botoes; em pe,
// 292 — com tres opcoes o botao vai de 47 para 92 px.
const int P0Q_X       = 20;
const int P0Q_GAP     = 8;
// Sem teto: com poucas opcoes os botoes devem crescer e tomar o miolo inteiro,
// que e o que torna o alvo grande o bastante para nao errar o toque.
const int P0Q_ALT_MIN = 22;

int p0qW()    { return display::telaW() - 40; }
int p0qCols() { return p0qW() / 6; }               // caracteres em corpo 1
// A pergunta e o titulo desta tela, em corpo 1: ela CONTEXTUALIZA, e quem
// precisa de tamanho e o botao, que e onde o dedo age. Duas linhas no maximo,
// coladas no topo do miolo para sobrar tudo o que der para os botoes.
//
// Em pe o miolo comeca abaixo da turma (que la mora no topo, ver drawRetrato) e
// termina acima da linha de idade do dado.
int p0qTitY() { return display::retrato() ? 112 : 54; }
// 114 deitado, e nao 78: a pergunta passou a ser escrita em corpo 2 e pede tres
// linhas de 19 px em vez de duas de 11. Os 36 px vem dos botoes, que os tem de
// sobra desde que quatro opcoes deixaram de ser quatro faixas e viraram duas
// colunas — a celula cai de 75 para 57 px e continua o dobro do que era antes.
int p0qTopo() { return display::retrato() ? 176 : 114; }
int p0qFim()  { return display::retrato() ? 438 : 236; }

// Em pe a pergunta usa a fonte PROPORCIONAL, e nao a grade embutida.
//
// A grade so tem multiplos inteiros: corpo 1 sao 8 px e corpo 2 sao 16. Na
// placa, o 1 ficava miudo ao lado de botoes de 60 px — dava para acertar o
// toque de longe e nao para ler o que se estava aprovando — e o 2 ficava um
// pouco grande, comendo altura dos botoes. Nao existe corpo intermediario.
//
// A DejaVu 7pt do projeto tem ~10 px de altura e cai exatamente no meio. Ela ja
// estava na flash (a linha do dia da pagina do Clawd usa a mesma), entao isto
// nao custa espaco — custa medir a quebra, porque proporcional nao tem largura
// fixa de 6 px por caractere. Ver textoQuebradoProp.
const int P0Q_TIT_BASE  = 11;    // do topo pedido ate a linha de base
const int P0Q_TIT_PASSO = 15;
const int P0Q_TIT_MAX   = 3;

// Deitado a pergunta usa a GRADE em corpo 2 — o mesmo corpo dos rotulos dos
// botoes logo abaixo. Em corpo 1 ela media 8 px de altura e nao se lia a um
// metro do painel, que e a distancia de onde ele fica.
//
// O dobro de corpo custa metade das colunas: 36 por linha em vez de 73. A
// terceira linha devolve o que o corpo tirou — 108 caracteres contra os 146 de
// antes — e a API ja corta a pergunta em 200 bytes, entao o que passa disso
// nunca chegou inteiro de qualquer jeito.
const int P0Q_TIT_PASSO_D = 19;   // 16 px de altura mais 3 de respiro
const int P0Q_TIT_MAX_D   = 3;

// Escreve quebrando por largura MEDIDA, e nao por contagem de caracteres.
//
// A fonte tem que estar setada antes: e ela que `getTextBounds` mede. `y` e a
// LINHA DE BASE da primeira linha, e nao o topo — com GFXfont o cursor e o
// baseline, ao contrario da grade embutida.
int textoQuebradoProp(Arduino_Canvas *g, int x, int y, int passo, int larg,
                      const std::string &txt0, int maxLinhas) {
    // Sem caractere de controle. Quem desenha conta as linhas e poe o cursor
    // onde quer, mas `print` trata '\n' sozinho e pula linha por conta
    // propria — furando o limite e escrevendo por cima do que vem abaixo. O
    // parse ja limpa (ver umaLinha em status.cpp); isto e a segunda tranca,
    // porque a funcao e publica e o proximo texto pode nao vir de la.
    std::string txt;
    txt.reserve(txt0.size());
    for (char c : txt0) txt += eControleDeCursor(c) ? ' ' : c;

    int linhas = 0;
    size_t p = 0;
    while (p < txt.size() && linhas < maxLinhas) {
        size_t corte = txt.size() - p;
        while (corte > 0) {
            int16_t x1, y1; uint16_t w, h;
            g->getTextBounds(txt.substr(p, corte).c_str(), 0, 0, &x1, &y1, &w, &h);
            if ((int)w <= larg) break;
            // Recua ate o espaco anterior. Sem espaco aproveitavel corta seco,
            // um caractere por vez: uma palavra partida e ruim, escrever para
            // fora da area e pior.
            const size_t esp = txt.rfind(' ', p + corte - 1);
            corte = (esp != std::string::npos && esp > p) ? esp - p : corte - 1;
        }
        if (corte == 0) break;
        g->setCursor(x, y + linhas * passo);
        g->print(txt.substr(p, corte).c_str());
        linhas++;
        p += corte;
        while (p < txt.size() && txt[p] == ' ') p++;
    }
    return linhas;
}

// Geometria de UM botao — usada pelo desenho E pelo toque, para nao divergirem.
//
// Deitado e com quatro opcoes ou mais, a grade vira DUAS COLUNAS: o botao dobra
// de altura sem tirar a fileira de bichos do rodape (ver layout::gradeColunas).
// A aritmetica mora em lib/layout porque e o que da para conferir no PC — aqui
// fica so a area util, que depende da rotacao.
bool perguntaP0Botao(int i, int total, Alvo &out) {
    Grade g;
    g.x      = P0Q_X;
    g.larg   = p0qW();
    g.topo   = p0qTopo();
    g.fim    = p0qFim();
    g.gap    = P0Q_GAP;
    g.altMin = P0Q_ALT_MIN;
    g.cols   = gradeColunas(total, !display::retrato());
    return gradeCelula(g, i, total, out);
}

void drawPerguntaP0(Arduino_Canvas *g, const Status &s, int opcaoArmada) {
    const Bloqueio &b = s.bloqueio;

    // A PERGUNTA e o titulo, em corpo 2 e no topo do miolo. Duas linhas no
    // maximo: a terceira empurraria os botoes, e sao eles que precisam do
    // espaco. Armada, ela vira o aviso de confirmacao — mesmo lugar, em
    // amarelo, sem faixa extra.
    // O titulo desta tela. Uma so decisao de cor e texto, dois desenhos: em pe
    // com a fonte proporcional, deitado com a grade de sempre.
    const bool emPe = display::retrato();
    std::string titulo;
    uint16_t    corTit;
    if (opcaoArmada) {
        corTit = C_YELL;
        titulo = emPe ? "TOQUE DE NOVO PARA CONFIRMAR"
                      : "TOQUE DE NOVO NA MESMA OPCAO PARA CONFIRMAR";
    } else if (b.pergunta.empty()) {
        corTit = MUTED;
        titulo = "(nao deu para ler a tela dele)";
    } else {
        corTit = fgColor();
        titulo = b.pergunta;
    }

    g->setTextColor(corTit);
    g->setTextSize(1);
    if (emPe) {
        g->setFont(&DejaVuSans7pt7b);
        textoQuebradoProp(g, P0Q_X, p0qTitY() + P0Q_TIT_BASE, P0Q_TIT_PASSO,
                          p0qW(), titulo, P0Q_TIT_MAX);
        // OBRIGATORIO: a fonte e do canvas inteiro, e os botoes sao desenhados
        // logo abaixo. Sem voltar para a embutida, a aritmetica de 6 px por
        // caractere que mede os rotulos passaria a mentir.
        g->setFont();
    } else {
        // OBRIGATORIO voltar ao corpo 1 depois: os botoes medem o rotulo por
        // conta propria e o `setTextSize` e do canvas inteiro.
        g->setTextSize(2);
        textoQuebrado(g, P0Q_X, p0qTitY(), P0Q_TIT_PASSO_D, p0qW() / 12,
                      titulo.c_str(), P0Q_TIT_MAX_D);
        g->setTextSize(1);
    }

    // Quem travou, no rodape do miolo e alinhado A DIREITA: e contexto, nao
    // decisao — encostado na margem ele informa sem disputar com os botoes.
    for (const Agent &a : s.agents) {
        if (a.id == b.agentId) {
            std::string quem = a.repo;
            if (!a.agent.empty())  quem += " " + a.agent;
            if (!a.model.empty())  quem += " " + a.model;
            if (!a.effort.empty()) quem += " " + effortCurto(a.effort);
            const int qx = P0Q_X + p0qW() - (int)quem.size() * 6;
            // De qual maquina e quem travou, colado na assinatura dele. Vazio
            // com uma fonte so — la nao ha duvida de origem para desfazer.
            const std::string &tag = tagDe(b.tag);
            if (!tag.empty())
                drawChip(g, qx - (int)tag.size() * 6 - 8 - 6, p0qFim() + 1,
                         tag, corDaTag(b.origem), qx);
            g->setTextColor(MUTED);
            g->setTextSize(1);
            g->setCursor(qx, p0qFim() + 4);
            g->print(quem.c_str());
            break;
        }
    }

    if (b.opcoes.empty()) {
        g->setTextColor(MUTED);
        g->setTextSize(1);
        // QUEBRADO por medida, e nao em linhas fixas. Escritas soltas, as duas
        // frases do modo em pe passavam da margem direita: 54 caracteres a 6 px
        // dao 324, e a tela em pe tem 320. O painel ignora recorte, entao o que
        // passa nao some — reaparece do outro lado, por cima da linha seguinte.
        //
        // Em pe nao HA aba 2: mandar para la seria mandar para um lugar que nao
        // existe, e a saida e girar a tela pelo bicho do cabecalho. Tudo em
        // ASCII, que e o que as fontes do painel cobrem.
        textoQuebrado(g, P0Q_X, p0qTopo() + 8, 12, p0qCols(),
                      display::retrato()
                          ? "a tela dele nao tem opcoes numeradas. toque duas "
                            "vezes no bicho do topo para deitar a tela e abrir "
                            "o TERMINAL na aba 2."
                          : "a tela dele nao tem opcoes numeradas - abra o "
                            "TERMINAL na aba 2.",
                      4);
        return;
    }

    // Botoes SEM teto de altura: com duas opcoes eles viram dois tarjoes de
    // ~60 px, que e o alvo que nao se erra em pe. Deitado, a partir de quatro
    // opcoes eles se repartem em duas colunas (ver perguntaP0Botao).
    const int total = (int)b.opcoes.size();
    const int qw = p0qW();
    for (int i = 0; i < total; i++) {
        Alvo a;
        if (!perguntaP0Botao(i, total, a)) break;
        const int y = a.y, h = a.h;
        const Opcao &o = b.opcoes[i];
        const bool armada = (opcaoArmada == o.n);

        g->fillRoundRect(a.x, y, a.w, h, 10, CARD);
        g->drawRoundRect(a.x, y, a.w, h, 10, armada ? C_YELL : TRACK);
        if (armada) g->drawRoundRect(a.x + 2, y + 2, a.w - 4, h - 4, 8, C_YELL);

        // Numero e rotulo crescem com a altura do botao: com poucas opcoes o
        // botao vira um tarjao e o texto acompanha, em vez de ficar uma legenda
        // perdida no meio de um retangulo enorme.
        const int nsz = h >= 60 ? 4 : (h >= 40 ? 3 : 2);
        g->setTextColor(armada ? C_YELL : LARANJA);
        g->setTextSize(nsz);
        g->setCursor(a.x + 16, y + (h - nsz * 8) / 2);
        g->printf("%d", o.n);

        const int tx  = a.x + 16 + nsz * 6 + 16;
        // TETO DE CORPO 2 EM PE. La o botao chega a 130 px com duas opcoes, e o
        // corpo 3 que essa altura liberava dava 15 caracteres por linha — os
        // rotulos vinham cortados no meio da palavra dentro de um botao com
        // metade dele vazio. O que o botao grande compra e alvo para o dedo, e
        // nao letra grande: quem precisa ser lido e o texto, e ele cabe inteiro
        // em corpo 2.
        //
        // O corpo 3 exige ALTURA E LARGURA INTEIRA. Numa das duas colunas o
        // botao tem 75 px de altura e 216 de largura: a altura sozinha
        // liberaria o corpo 3, e ele daria 8 caracteres por linha — corta o
        // rotulo mais curto do AskUserQuestion. Quem manda na letra e a coluna
        // mais estreita das duas, e nao a mais alta.
        const int lsz = display::retrato() ? (h >= 34 ? 2 : 1)
                                           : (h >= 60 && a.w >= qw ? 3
                                                                   : (h >= 34 ? 2 : 1));
        const int cols = (a.x + a.w - 12 - tx) / (6 * lsz);
        // Rotulo longo usa quantas linhas couberem, ate tres. Antes o teto era
        // dois, e um rotulo de tres linhas era cortado num botao que tinha
        // altura de sobra para ele.
        const int passo = 8 * lsz + 3;
        int maxL = (h - 8) / passo;
        if (maxL > 3) maxL = 3;
        if (maxL < 1) maxL = 1;
        g->setTextColor(armada ? C_YELL : fgColor());
        g->setTextSize(lsz);
        // Centra o bloco de texto (1 ou 2 linhas) na altura do botao.
        const int th = maxL * passo - (passo - 8 * lsz);
        textoQuebrado(g, tx, y + (h - th) / 2, passo, cols, o.rotulo.c_str(), maxL);
    }
}

void drawPageLimits(Arduino_Canvas *g, const Status &s, int opcaoArmada) {
    // Um agente travado numa pergunta? Ela toma o lugar dos 3 cards, para que
    // responder nao dependa de trocar de aba e selecionar o agente certo.
    if (s.bloqueio.known) {
        drawPerguntaP0(g, s, opcaoArmada);
        return;
    }

    // TRES cards sempre: o terceiro deixou de depender da quebra por modelo da
    // API e passou a mostrar as sessoes, que a placa sempre tem.
    const int n = 3;
    const int cardW = (SCREEN_W - 14 * (n + 1)) / n;

    // 190 de altura: o card vai de 54 a 244, e a turma do rodape comeca em 270
    // (chao 312 menos os 42 do selo). Sobram 26 px de folga, e nada mais mora
    // nessa faixa nesta pagina.
    drawCard(g, 14,                  54, cardW, 190, "5 HORAS", s.session,
             "reseta em", JANELA_5H);
    drawCard(g, 14 * 2 + cardW,      54, cardW, 190, "SEMANA",  s.week,
             "reseta em", JANELA_7D);
    drawCardSessoes(g, 14 * 3 + cardW * 2, 54, cardW, 190, s);
}

// O bloqueio publicado pela API e DESTE agente? A pagina mostra o contexto do
// agente selecionado, entao ela mostra a pergunta do agente selecionado — nao a
// de outro que por acaso tambem parou.
bool bloqueioDe(const Status &s, const Agent &a) {
    return s.bloqueio.known && !s.bloqueio.agentId.empty()
           && s.bloqueio.agentId == a.id;
}

void drawPageContext(Arduino_Canvas *g, const Status &s, int selected,
                     bool botaoArmado, int opcaoArmada) {
    if (s.agents.empty()) {
        g->setTextColor(MUTED);
        g->setTextSize(2);
        g->setCursor(20, 140);
        g->print("nenhum agente ativo");
        return;
    }

    const Agent &a = s.agents[selected];

    // A pergunta toma o lugar do bloco de contexto. O menu da direita fica: e
    // por ele que se troca de agente, inclusive para sair de um bloqueado.
    if (bloqueioDe(s, a)) {
        drawBloqueio(g, s, opcaoArmada);
        // O botao de terminal fica DE PE aqui de proposito: ler a tela do
        // agente e mais util quando ele esta travado do que quando esta
        // tranquilo — e frequentemente e o unico jeito de entender a pergunta.
        if (terminalVisivel(s, selected)) drawTerminalButton(g);
        drawMenu(g, s, selected);
        return;
    }

    g->setTextColor(MUTED);
    g->setTextSize(1);
    g->setCursor(20, 58);
    char rotCtx[32];
    snprintf(rotCtx, sizeof(rotCtx), "CONTEXT  (%d de %d)", selected + 1,
             (int)s.agents.size());
    g->print(rotCtx);

    // A TAG da maquina do agente selecionado, ao lado do rotulo — o mesmo chip
    // que o card de sessoes da primeira tela mostra por linha. O limite e a
    // coluna de botoes (BTN_X): nao cabendo, ele some em vez de encostar nela.
    drawChip(g, 20 + (int)strlen(rotCtx) * 6 + 8, 55, tagDe(a.tag),
             corDaTag(a.origem), BTN_X - 6);

    // O percentual do AGENTE selecionado, nao o agregado do topo.
    Metric m;
    m.pct   = a.contextPct;
    m.level = a.level;
    m.known = a.hasContext;

    g->setTextColor(a.hasContext ? fgColor() : MUTED);
    g->setTextSize(6);
    g->setCursor(20, 74);
    g->print(pctText(m).c_str());

    drawBar(g, 20, 128, MENU_X - 40, 14, m);
    if (botaoVisivel(s, selected)) drawCleanButton(g, s, botaoArmado);

    drawRow(g, 168, "repo",   a.repo.c_str());
    drawRow(g, 200, "branch", a.branch.c_str());

    // Modelo e esforco na mesma linha: sao a mesma pergunta ("o que esta
    // pensando aqui, e com quanta forca"). Cabem juntos porque a API encurta
    // "Opus 5 (1M context)" para "Opus 5 (1M)" — "Opus 5 (1M) High" da 16
    // caracteres, e a linha comporta 18.
    std::string modelo = a.model;
    if (!a.effort.empty()) modelo += " " + a.effort;
    drawRow(g, 232, "modelo", modelo.c_str());

    // Linha de estado. Cada caso diz exatamente o que se sabe — e o Unknown
    // admite que nao sabe, em vez de chutar "provavelmente bloqueado" como a
    // versao anterior fazia.
    const char *txt = nullptr;
    uint16_t    cor = MUTED;
    switch (a.state) {
        case AgentState::Blocked:
            txt = "BLOQUEADO - esperando voce"; cor = C_RED;  break;
        case AgentState::Idle:
            txt = "turno encerrado";            cor = MUTED;  break;
        case AgentState::Working:
            txt = "trabalhando";                cor = LARANJA; break;
        case AgentState::Unknown:
            // Sem hooks nesta sessao: so da para relatar o silencio.
            if (a.stale) { txt = "sem publicar"; cor = C_YELL; }
            break;
    }
    if (txt) {
        g->setTextColor(cor);
        g->setTextSize(1);
        // Acima de 261, onde comeca o selo do rodape.
        g->setCursor(20, 248);
        if (a.state == AgentState::Unknown) g->printf("%s %s", txt, formatAge(a.age).c_str());
        else                                g->print(txt);
    }

    if (terminalVisivel(s, selected)) drawTerminalButton(g);
    drawMenu(g, s, selected);
}

// Rotulo pequeno e apagado com o valor logo abaixo. `x` e a borda esquerda, ou
// a DIREITA quando `direita` — assim a coluna da direita fica colada na margem
// sem quem chama precisar medir o texto.
void rotuloValor(Arduino_Canvas *g, int x, int y, const char *rotulo,
                 const char *valor, int tam, bool direita) {
    const int lw = (int)strlen(rotulo) * 6;
    const int vw = (int)strlen(valor) * 6 * tam;

    g->setTextColor(MUTED);
    g->setTextSize(1);
    g->setCursor(direita ? x - lw : x, y);
    g->print(rotulo);

    g->setTextColor(fgColor());
    g->setTextSize(tam);
    g->setCursor(direita ? x - vw : x, y + 12);
    g->print(valor);
}

// ---- Pagina do Clawd ----
// Sem moldura: o sprite e desenhado sobre o fundo normal da tela. A primeira
// versao tinha um retangulo arredondado atras dele, para casar com a cor contra
// a qual os sprites foram compostos — e o resultado foi o contrario do
// esperado. O quadro do sprite reserva espaco para o balao de alerta e para os
// "Z" do sono, entao a moldura ficava enorme e vazia, com o bicho encolhido num
// canto. Sem ela, o espaco vazio simplesmente nao existe.
void drawPageClawd(Arduino_Canvas *g, const Status &s) {
    if (!clawd::ready()) {
        g->setTextColor(MUTED);
        g->setTextSize(2);
        g->setCursor(20, 130);
        g->print("SEM O CLAWD");
        g->setTextSize(1);
        g->setCursor(20, 160);
        g->print(clawd::erro()[0] ? clawd::erro() : "cartao sem /clawd");
        return;
    }

    // Em pe o bicho pisa mais baixo (ha altura sobrando) e o teto e a linha do
    // cabecalho do retrato. As colunas, que deitado LADEIAM o bicho, descem
    // para baixo dele: em 320 px de largura nao existe "lado".
    const bool emPe = display::retrato();
    const int  W    = display::telaW();
    if (emPe) clawd::drawIntoEm(g, W / 2, 300, 52);
    else      clawd::drawInto(g);

    const AgentState st   = overallState(s);
    const std::string qual = overallAgent(s);

    const char *txt = "sem sessao";
    uint16_t    cor = MUTED;
    switch (st) {
        case AgentState::Blocked: txt = "BLOQUEADO";   cor = C_RED;   break;
        case AgentState::Working: txt = "trabalhando"; cor = LARANJA; break;
        case AgentState::Idle: txt = "em espera";   cor = MUTED;   break;
        case AgentState::Unknown: if (!s.agents.empty()) txt = "estado desconhecido";
                                  break;
    }

    // Abaixo da linha do chao (238) e do quadro mais alto, que desce ate 246.
    // O sprite pinta o proprio fundo: se o texto ficasse dentro do retangulo
    // dele, seria apagado no quadro seguinte.
    g->setTextColor(g_stale ? MUTED : cor);
    g->setTextSize(2);
    const int larg = (int)strlen(txt) * 12;
    g->setCursor((W - larg) / 2, emPe ? 308 : 252);
    g->print(txt);

    if (!qual.empty()) {
        g->setTextColor(MUTED);
        g->setTextSize(1);
        const int lw = (int)qual.size() * 6;
        g->setCursor((W - lw) / 2, emPe ? 332 : 272);
        g->print(qual.c_str());
    }

    // Os cumulativos DO MESMO agente cujo nome esta escrito acima, em duas
    // colunas ladeando o bicho, um pouco acima do meio da tela.
    //
    // Margens FIXAS, e nao calculadas a partir do retangulo do sprite. A
    // moldura varia demais — conducting ocupa 152 px de largura e sweeping 414
    // dos 480 — mas o CORPO fica sempre centrado e estreito (o do sweeping tem
    // 168 px), e a moldura larga e quase toda transparente. Entao as colunas
    // caem sobre o vazio da moldura, e nao sobre o caranguejo.
    //
    // Nada e escondido quando o dado envelhece: `fgColor()` esmaece junto com o
    // resto da tela, que e a regra do painel inteiro — dado velho continua
    // util, so precisa parecer velho.
    const int qi = overallIndex(s);
    if (qi < 0) return;
    const Agent &q = s.agents[qi];

    char buf[32];
    const int TOPO = emPe ? 352 : 118;

    // ---- Esquerda: o que este turno consumiu ----
    if (q.hasCost) {
        snprintf(buf, sizeof(buf), "US$ %.2f", q.costUsd);
        rotuloValor(g, 14, TOPO, "CUSTO", buf, 2, false);
    }
    if (q.hasLines) {
        snprintf(buf, sizeof(buf), "+%d/-%d", q.linesAdded, q.linesRemoved);
        rotuloValor(g, 14, TOPO + 42, "LINHAS", buf, 1, false);
    }

    // ---- Direita: o que ele gastou de relogio ----
    if (q.hasApiMs)
        rotuloValor(g, W - 14, TOPO, "API",
                    formatApiTime(q.apiMs).c_str(), 2, true);
    if (q.hasContext) {
        snprintf(buf, sizeof(buf), "%d%%", q.contextPct);
        rotuloValor(g, W - 14, TOPO + 42, "CONTEXTO", buf, 1, true);
    }

    // ---- A linha do DIA, no rodape da pagina ----
    // As colunas acima falam da SESSAO, cumulativa desde que ela abriu. Esta
    // linha fala do DIA, somando todas as sessoes que ja fecharam turnos — vem
    // do livro-caixa e nao dos contadores. Sao perguntas diferentes, entao
    // moram em lugares diferentes.
    //
    // Aqui embaixo e livre nesta pagina: a turma do rodape nao e desenhada na
    // pagina do Clawd, e a idade do dado fica alinhada a direita, a partir de
    // x=369 no caso comum.
    if (!s.works.known || !s.works.trabalhos) return;

    // Fonte proporcional, e so aqui. A embutida do Arduino_GFX e uma grade de
    // 5x7 que serve para numero e rotulo curto; esta linha e uma frase, e frase
    // em fonte de grade fica dura de ler a distancia de onde o painel esta.
    //
    // Com fonte GFX o cursor Y e a LINHA DE BASE. Em 312 a tinta ocupa de 302
    // (topo do 'A') a 315 (pe do 'g'): abaixo das bolinhas de pagina, que
    // terminam em 299, e da idade do dado, e com folga ate a borda.
    g->setFont(&DejaVuSans7pt7b);
    g->setTextSize(1);

    // A frase em PEDACOS, alternando o que e palavra e o que e numero da API.
    // Palavra em cinza, valor na cor do texto: o olho encontra os quatro
    // numeros de uma vez, sem ler a frase inteira. Sao a mesma dupla de cores
    // que os cards da primeira tela ja usam entre rotulo e valor.
    const int MAX_PARTES = 8;
    const char *parte[MAX_PARTES];
    bool  valor[MAX_PARTES];
    int   n = 0;

    char turnos[16], tempo[16], espera[24], custo[24];
    snprintf(turnos, sizeof(turnos), "%d", s.works.trabalhos);
    snprintf(tempo, sizeof(tempo), "%s", formatDuration(s.works.seconds).c_str());
    snprintf(espera, sizeof(espera), "%s", formatDuration(s.works.blocked).c_str());
    snprintf(custo, sizeof(custo), "%.2f", s.works.costUsd);

    parte[n] = "HOJE ";  valor[n++] = false;
    parte[n] = turnos;   valor[n++] = true;
    parte[n] = s.works.trabalhos == 1 ? " turno  " : " turnos  ";
    valor[n++] = false;
    parte[n] = tempo;    valor[n++] = true;
    const int SEM_EXTRAS = n;

    // O tempo esperando por voce so entra quando existe: um dia sem bloqueio
    // nenhum nao precisa de um "0s" para dizer isso.
    if (s.works.blocked > 0) {
        parte[n] = "  esperando ";  valor[n++] = false;
        parte[n] = espera;          valor[n++] = true;
    }
    const int SEM_CUSTO = n;
    if (s.works.hasCost && s.works.costUsd > 0) {
        parte[n] = "  US$ ";  valor[n++] = false;
        parte[n] = custo;     valor[n++] = true;
    }

    // Largura em fonte proporcional depende do texto, entao ela e MEDIDA. Do
    // mais para o menos informativo: o custo cai fora primeiro, depois o tempo
    // esperando. Escolher uma frase curta o bastante para o pior caso deixaria
    // o caso comum pobre — mesma regra do bloco de tempo no cabecalho.
    // Medida na frase INTEIRA e nao pedaco a pedaco: getTextBounds devolve a
    // caixa da tinta, entao o espaco no fim de "HOJE " nao entraria na conta e
    // a soma sairia menor do que o que sera desenhado.
    //
    // Com a fonte menor o pior caso mede 364 px contra os 452 uteis, entao esta
    // escada praticamente nao dispara mais. Ela fica porque a largura depende
    // do TEXTO, e um dia com numeros maiores do que os ja vistos volta a
    // precisar dela.
    const int UTIL = W - 28;
    const int cortes[] = {n, SEM_CUSTO, SEM_EXTRAS};
    int usar = SEM_EXTRAS;
    for (int c = 0; c < 3; c++) {
        char frase[96];
        frase[0] = '\0';
        for (int i = 0; i < cortes[c]; i++)
            strncat(frase, parte[i], sizeof(frase) - strlen(frase) - 1);
        int16_t x1, y1; uint16_t w, h;
        g->getTextBounds(frase, 0, 0, &x1, &y1, &w, &h);
        if ((int)w <= UTIL) { usar = cortes[c]; break; }
    }

    g->setCursor(14, emPe ? 452 : 312);
    for (int i = 0; i < usar; i++) {
        g->setTextColor(valor[i] ? fgColor() : MUTED);
        g->print(parte[i]);      // o cursor anda sozinho: nao ha o que medir
    }
    // OBRIGATORIO: a fonte e do canvas inteiro, e o rodape e desenhado depois
    // desta funcao. Sem voltar para a embutida, todo o resto da tela herdaria
    // esta fonte e a aritmetica de largura de 6 px por caractere passaria a
    // mentir em todo lugar.
    g->setFont();
}

// ---- Pagina de calibracao (TEMPORARIA) ----
// Tres marcas em posicoes conhecidas. Cada uma e desenhada DUAS vezes: um
// contorno pelo canvas (caminho que sabemos correto, porque toda a interface
// aparece no lugar) e um bloco solido pelo caminho do sprite. Se os dois
// caminhos concordam, os blocos caem dentro dos contornos.

// ====================================================================
//  MODO EM PE (retrato, 320x480)
// ====================================================================
//
// So a PRIMEIRA pagina existe aqui — nao ha troca de tela em pe, entao nao ha
// bolinhas de pagina, contexto por agente, Clawd grande nem nivel. A saida e o
// bicho do cabecalho: dois toques nele deitam a tela de volta.
//
// A ORDEM VERTICAL NAO E ESTETICA. A faixa barata do flush de prefixo sao as N
// primeiras linhas do PAINEL, e de pe elas viram as N primeiras linhas da TELA
// (ver display::setRetrato). Por isso o bicho do cabecalho e a turma — as duas
// coisas animadas do painel — moram nos primeiros 106 px: um envio de prefixo
// cobre os dois por ~11 ms. Deixar a turma no rodape, como ela fica deitada,
// custaria um flush inteiro (~48 ms) por quadro e ela teria que congelar.
//
// Quem mover a turma daqui para baixo mata a animacao sem perceber, porque na
// tela nada denuncia: ela continua desenhando, so fica cara.
const int R_HDR_BASE   = 44;    // base do bicho e da hora
const int R_HDR_LINHA  = 48;    // divisoria do cabecalho
const int R_TURMA_CHAO = 100;   // a turma pisa aqui
const int R_TOPO_FIM   = 106;   // fim da faixa barata (e a segunda divisoria)
const int R_MARG       = 14;
const int R_CARD_W     = PANEL_W - R_MARG * 2;    // 292
const int R_LIM_Y      = 114;
// OS DOIS LIMITES LADO A LADO, e nao empilhados.
//
// Empilhados eles custavam 146 px — 30% da tela em pe — para dizer dois
// numeros, e a lista de sessoes, que e o que muda, ficava com um vao vazio
// embaixo. Em coluna o par cabe em 98 px e devolve 48 para a lista.
//
// O que a coluna PERDE e largura: 143 px contra 292, e o rodape da coluna leva
// dois textos (o prazo e o instante do reset) em 123 px uteis. O da SEMANA e o
// apertado dos dois — "1d09h" e "Sabado (23h)" na mesma linha.
//
// O percentual continua na DejaVu dobrada (~20 px) e NAO sobe de corpo com a
// coluna mais estreita: a regra e a de sempre, o texto que manda nesta tela sao
// os nomes das sessoes (ver a calibracao por foto de 13/08).
const int R_LIM_H      = 98;
const int R_LIM_W      = 143;
// O vao minimo entre o prazo e o instante no rodape da coluna. Abaixo
// disso os dois se leem como uma palavra so, mesmo sem se tocarem.
const int GAP_RODAPE   = 6;
const int R_LIM_GAP    = 6;      // 14 + 143 + 6 + 143 + 14 = 320
const int R_SES_Y      = R_LIM_Y + R_LIM_H + 8;                 // 220
const int R_SES_FIM    = 456;
// O passo de um cartao de sessao. Constante, e nao numero solto, porque ele e
// lido em DOIS lugares que precisam concordar: quem desenha a lista e o
// hit-test que diz em qual cartao o dedo caiu. Divergirem significa abrir o
// terminal da sessao errada, e nada na tela denunciaria.
const int R_SES_PASSO  = 44;
const int R_STATUS_Y   = 462;

// O alvo do gesto de girar: o canto do bicho, com folga larga. Vale nas duas
// orientacoes e em todas as paginas — o cabecalho e o mesmo em todas.
//
// O tamanho dele mora em lib/layout (`alvoIconeCabecalho`), com o porque das
// medidas e um teste que as confere contra uma foto da tela real.

// Cabecalho em pe: SO o bicho e a hora, na mesma linha.
//
// O nome e a temperatura saem a pedido, e o resultado e melhor do que uma
// traducao do cabecalho deitado: sem eles a hora cabe em corpo 5 (40 px de
// altura contra os 24 de la), que e o tamanho que se le do outro lado da sala.
// O nome tambem nao faz falta como marca — o bicho ja e a marca, e agora ele e
// tambem o unico controle desta tela.
void drawHeaderRetrato(Arduino_Canvas *g, const Status &s, int staleSeconds,
                       bool climaNoCanto = false) {
    // Na pagina do Clawd em pe o canto e do CLIMA — deitado ele ja mora no
    // cabecalho dessa pagina, e em pe o unico lugar dele e o slot do bicho.
    if (climaNoCanto && clawd::climaW()) {
        clawd::drawClimaInto(g, R_MARG, R_HDR_BASE - clawd::climaH());
    } else {
        const int iw = clawd::iconW(false);
        const int ih = clawd::iconH(false);
        if (iw && ih) clawd::drawIconInto(g, R_MARG, R_HDR_BASE - ih, false);
        else          drawLogo(g, R_MARG, R_HDR_BASE - logoH(2) - 4, 2, LARANJA);
    }

    if (s.clock.known) {
        // Corpo 4, depois de rodar por 5, 4, 3 e 4 de novo na placa.
        //
        // O 5 (40 px) dominava o bicho ao lado, que tem 36 e e a marca do
        // painel. O 3 conviveu bem ate a tela inteira crescer um degrau — ai o
        // relogio ficou o menor elemento do topo. O 4 (32) e o ponto em que
        // ele acompanha sem passar do bicho.
        const int sz = 4;
        const int w  = (int)s.clock.hm.size() * 6 * sz;
        g->setTextColor(fgColor());
        g->setTextSize(sz);
        // Centrado na ALTURA do cabecalho (0..48), e nao apoiado na base do
        // bicho: apoiado, sobrava todo o respiro em cima e o relogio parecia
        // escorregando para a divisoria.
        g->setCursor(PANEL_W - R_MARG - w, (R_HDR_LINHA - 8 * sz) / 2);
        g->print(s.clock.hm.c_str());
    }

    if (s.viaSlave) {
        // A esquerda do relogio, centrado na faixa: procedencia ao lado da
        // informacao que ela qualifica.
        const int rw = s.clock.known ? (int)s.clock.hm.size() * 6 * 4 + 10 : 0;
        const int sw = (s.tag.empty() ? 11 : (int)s.tag.size() + 4) * 6 + 10;
        drawSeloVia(g, PANEL_W - R_MARG - rw - sw, (R_HDR_LINHA - 16) / 2, s.tag);
    }

    g->drawFastHLine(R_MARG, R_HDR_LINHA, R_CARD_W,
                     staleSeconds > 0 ? C_YELL : TRACK);
}

// Um limite (5 HORAS / SEMANA) numa faixa larga e baixa.
//
// Mesmos campos do card deitado — titulo, percentual, barra, prazo e o instante
// absoluto —, so que deitados em vez de empilhados: em pe o card tem 292 px de
// largura, mais do que o dobro dos 141 de la, e o que falta e altura. O
// percentual cai de corpo 4 para 3 e e o unico preco desta forma.
// A HORA do reset, em 24h, a partir do relogio de agora e do prazo em segundos.
// Vazia quando falta um dos dois.
//
// A soma tem DUAS fontes de ruido de um minuto cada: o relogio da placa perde
// os segundos do minuto corrente, e o prazo chega envelhecido pela latencia do
// poll. Arredondar so ao minuto deixava o resultado oscilando entre 22:59 e
// 23:00 conforme o instante do fetch. Os resets caem em hora redonda, entao o
// total arredonda ao multiplo de CINCO minutos mais proximo — o ruido morre e
// um reset quebrado ainda apareceria certo.
std::string horaDoReset(const Metric &m, const Clock *rel) {
    int hh = -1, mm = 0;
    if (!rel || !rel->known || m.resetsIn <= 0) return "";
    if (sscanf(rel->hm.c_str(), "%d:%d", &hh, &mm) != 2) return "";

    long tot = (long)hh * 60 + mm + (m.resetsIn + 30) / 60;
    tot = ((tot + 2) / 5) * 5 % (24 * 60);
    char buf[16];
    if (tot % 60) snprintf(buf, sizeof(buf), "%02ld:%02ldh", tot / 60, tot % 60);
    else          snprintf(buf, sizeof(buf), "%ldh", tot / 60);
    return buf;
}

void drawColunaLimite(Arduino_Canvas *g, int x, int y, int w, int h,
                      const char *titulo, const Metric &m, int janelaSeg = 0,
                      const Clock *rel = nullptr) {
    g->fillRoundRect(x, y, w, h, 8, CARD);

    // A coluna inteira fala em DejaVu, em dois tamanhos: titulo e rodape na
    // natural (~10 px) e o percentual dobrado (~20). E o meio-termo que a grade
    // nao tem — o corpo 2 sumia ao lado da barra, e o corpo 3 brigava com os
    // nomes das sessoes, que sao o texto mais importante da tela.
    //
    // Empilhado, e nao deitado como na faixa larga: o titulo abre a coluna, o
    // percentual e a linha grande logo abaixo, a barra separa, e o rodape leva
    // o prazo e o instante em cada ponta.
    g->setFont(&DejaVuSans7pt7b);
    g->setTextSize(1);
    g->setTextColor(MUTED);
    g->setCursor(x + 8, y + 22);
    g->print(titulo);

    const std::string pct = pctText(m);
    // Lembranca sai ESMAECIDA, como o dado velho do resto do painel: o numero
    // continua valendo, mas nao pode se apresentar como leitura de agora.
    g->setTextColor(m.known && !m.memoria ? fgColor() : MUTED);
    g->setTextSize(2);
    int16_t x1, y1; uint16_t tw, th;
    g->getTextBounds(pct.c_str(), 0, 0, &x1, &y1, &tw, &th);
    g->setCursor(x + w - 8 - (int)tw, y + 46);
    g->print(pct.c_str());
    g->setTextSize(1);

    g->setFont();
    drawBar(g, x + 8, y + 56, w - 16, 8, m, janelaSeg);
    g->setFont(&DejaVuSans7pt7b);

    // Prazo e instante na MESMA linha, um em cada ponta — SO os valores. A
    // linha respira 14 px abaixo da barra, e nao 7: colada, ela se lia como
    // legenda da barra em vez de rodape do cartao, e os dois numeros do reset
    // nao pertencem a barra — pertencem a janela inteira. O
    // "reseta em" saiu: com um prazo em cada coluna e o instante ao lado, o
    // rotulo nao desambiguava nada, so gastava a largura, que aqui e o que
    // falta. A fonte e proporcional, entao a ponta direita alinha por medida.
    g->setTextColor(MUTED);
    const std::string prazo = m.known && !m.resets.empty() ? m.resets : "-";
    uint16_t pw, ph;
    g->getTextBounds(prazo.c_str(), 0, 0, &x1, &y1, &pw, &ph);
    g->setCursor(x + 8, y + 86);
    g->print(prazo.c_str());

    if (m.known && !m.at.empty() && m.at != "-") {
        // Da data por extenso, so o DIA DA SEMANA: "15/08/2026 (Sabado)" vira
        // "Sabado". O prazo ao lado ja diz quando em dias e horas, e para
        // decidir se da para esperar o que importa e "cai no sabado" — a data
        // completa mora na tela deitada. O instante sem parenteses ("6:00am")
        // passa reto.
        std::string at = m.at;
        const size_t abre = at.find('(');
        if (abre != std::string::npos) {
            const size_t fecha = at.find(')', abre);
            if (fecha != std::string::npos && fecha > abre + 1)
                at = at.substr(abre + 1, fecha - abre - 1);

            // A API nao manda relogio para a semana — so data. A hora sai da
            // conta com o relogio da placa.
            const std::string hora = horaDoReset(m, rel);
            if (!hora.empty()) at += " (" + hora + ")";
        }
        // CASCATA DE CORTE, e nao um texto fixo: em 123 px uteis a SEMANA
        // estourou na placa — "1d09h" e "Sabado (23h)" mediram 135 juntos e o
        // prazo entrou por cima do dia. A SESSAO ("1h01m" e "2:40pm", 81 px)
        // nunca chega perto, entao encurtar os dois seria pagar um preco que so
        // uma das colunas deve.
        //
        // A ordem sacrifica o que menos custa: primeiro o dia por extenso vira
        // as tres letras de sempre (Sab, Dom, Seg...), que ninguem le
        // diferente; so depois cai a hora entre parenteses, que e informacao
        // de verdade. A medida e a REAL (getTextBounds), porque a DejaVu e
        // proporcional e contar caractere aqui mentiria.
        //
        // A MARGEM DA COLUNA E 8 E NAO 12 POR CAUSA DESTA LINHA. Somando o
        // xAdvance da DejaVuSans7pt7b para as 28.224 combinacoes possiveis de
        // prazo (0d00h..6d23h) e instante em hora redonda (Seg..Dom, 0h..23h):
        // com margem 12 a hora cai em 2.352 delas (8%), e com 8 nao cai em
        // nenhuma — a folga minima e de 1 px, no par "0d00h" com "Dom (10h)".
        // Reset em hora quebrada ("Dom (23:45h)") nunca cabe e sempre degrada
        // para so o dia; e o caso raro, e o dia e o que se le mesmo.
        const int xFim  = x + w - 8;
        const int xPraz = x + 8 + (int)pw;    // onde o prazo termina
        g->getTextBounds(at.c_str(), 0, 0, &x1, &y1, &tw, &th);
        if (xFim - (int)tw < xPraz + GAP_RODAPE && at.size() > 3) {
            const size_t esp = at.find(' ');
            const std::string resto = esp == std::string::npos
                                          ? std::string()
                                          : at.substr(esp);
            at = at.substr(0, 3) + resto;
            g->getTextBounds(at.c_str(), 0, 0, &x1, &y1, &tw, &th);
        }
        if (xFim - (int)tw < xPraz + GAP_RODAPE) {
            const size_t esp = at.find(' ');
            if (esp != std::string::npos) at = at.substr(0, esp);
            g->getTextBounds(at.c_str(), 0, 0, &x1, &y1, &tw, &th);
        }
        g->setCursor(xFim - (int)tw, y + 86);
        g->print(at.c_str());
    }
    g->setFont();
}

// As sessoes em LINHA UNICA — e e por isso que em pe cabem sete em vez de
// quatro.
//
// Deitado a sessao precisa de duas linhas porque o card tem 141 px: o repo em
// corpo 2 sozinho ja come 108 deles. Em pe o card tem 292, entao repo, chips e
// a barra de contexto cabem lado a lado e o passo cai de 38 para 26 px.
//
// A cascata de corte e a mesma de la: cada chip so entra se couber ate o limite,
// e o `xLimite` para aqui antes da barra, que e o elemento que nunca sai.
// A FAIXA DE ESTADO na borda esquerda de um cartao arredondado.
//
// Um `fillRect` reto aqui era o defeito visivel: o cartao curva no canto e a
// faixa nao, entao ela sobrava para fora em cima e embaixo e o cartao inteiro
// lia como quadrado — justo do lado onde mora a unica cor forte da linha.
//
// A borda esquerda de um retangulo de canto redondo, na linha `i`, esta a
// `r - sqrt(r^2 - dy^2)` pixels de x, onde `dy` e a distancia daquela linha ao
// centro do arco. Fora dos arcos o deslocamento e zero e a faixa e reta, que e
// a maior parte da altura.
void drawFaixaEstado(Arduino_Canvas *g, int x, int y, int w, int h, int r,
                     uint16_t cor) {
    for (int i = 0; i < h; i++) {
        int dy = -1;
        if (i < r)          dy = r - 1 - i;
        else if (i >= h - r) dy = i - (h - r);
        int dx = 0;
        if (dy >= 0) {
            const float dentro = (float)(r * r - dy * dy);
            dx = r - (int)(dentro > 0 ? sqrtf(dentro) + 0.5f : 0);
            if (dx < 0) dx = 0;
        }
        g->drawFastHLine(x + dx, y + i, w, cor);
    }
}

// Um contador de linhas em no maximo CINCO caracteres, sinal incluso.
//
// MEDIDO, e nao estimado: a fileira de baixo do cartao e toda em grade (6 px
// por caractere), e o pior caso real — chip "Opus 5 (1M)" mais "XHigh" — termina
// em x=192 com o percentual do contexto comendo a ponta direita a partir de 264.
// Sobram 72 px para os dois numeros. Sem abreviar, uma sessao de cinco digitos
// de cada lado ("+12345 -1234") pede 76 e o `drawChip` do esforco simplesmente
// nao desenha — o effort sumiria em silencio nas sessoes mais produtivas, que
// sao justamente as que se quer olhar.
//
// Com o k, o teto e "+999k" / "-999k": 30 px cada, 64 com o vao, e sobra folga.
void contadorCurto(char *buf, size_t cap, char sinal, int v) {
    if (v < 0) v = 0;
    if (v < 1000)        snprintf(buf, cap, "%c%d", sinal, v);
    else if (v < 10000)  snprintf(buf, cap, "%c%d.%dk", sinal, v / 1000,
                                  (v % 1000) / 100);
    else                 snprintf(buf, cap, "%c%dk", sinal, v / 1000);
}

// SEM MOLDURA EXTERNA, e essa e a diferenca desta versao.
//
// Desde que cada sessao virou um cartao proprio, o cartao que os continha nao
// fazia trabalho nenhum: desenhava uma borda em volta de objetos que ja tinham
// borda, e a parte dele que sobrava era o vao vazio embaixo da lista — ~110 px
// de moldura com nada dentro, que lia como erro. Sem ele o vazio e fundo, e
// fundo vazio nao parece erro; de quebra os cartoes vao de margem a margem e
// ganham 12 px de largura.
//
// SEM rotulo e SEM total, como antes: o que a lista e ja e obvio, e quantas
// existem esta no "+N" do rodape somado ao que esta na tela.
void drawSessoesRetrato(Arduino_Canvas *g, int x, int y, int w, int h,
                        const Status &s) {
    if (s.agents.empty()) {
        g->setTextColor(MUTED);
        g->setTextSize(2);
        g->setCursor(x + 12, y + 20);
        g->print("-");
        return;
    }

    // 44 e nao 26: as colunas de limite devolveram 46 px a este card, e a linha
    // passou a ser um OBJETO em vez de uma faixa. Em 26 px o nome, os chips e a
    // ponta direita disputavam a mesma faixa horizontal de 292 px e o nome
    // perdia — "kubernetes-ind..." era o resultado tipico. Em dois andares o
    // nome tem 180 px so para ele e o esforco passa a caber sempre.
    const int passo = R_SES_PASSO;

    // SEM CABECALHO DE GRUPO nesta tela. O agrupamento por CLI existia para
    // dizer de que ferramenta cada sessao e, e custava 16 px por grupo mais a
    // quebra visual entre eles — com dois provedores, 32 px, quase uma sessao
    // inteira. O icone do fornecedor no comeco da segunda linha diz a mesma
    // coisa por sessao, em 12 px que ja estavam vazios, e a lista volta a ser
    // uma lista: cinco cartoes iguais, sem degraus.
    //
    // O PLANO DA CONTA saiu junto — ele morava no cabecalho e nao tem por que
    // se repetir em toda linha. Ele continua na tela deitada, que mantem os
    // grupos.
    //
    // A ordem e a da API, com UMA excecao: quem esta bloqueado sobe para o topo.
    // A lista corta no que cabe e o resto vira "+N"; se a sessao que sobrar for
    // a bloqueada, o painel esconde a unica linha que pede acao — e responder
    // pergunta de agente parado e a razao de este painel existir. Ver
    // `grupos::ordemComBloqueadosNoTopo`, que e estavel: nada mais muda de
    // lugar, e no caso comum (nenhuma bloqueada) a lista fica exatamente como
    // a API mandou.
    const int y0 = y + 8;

    const std::vector<int> ordem = grupos::ordemComBloqueadosNoTopo(s.agents);
    const int cabem = (h - (y0 - y)) / passo;
    const int n     = (int)ordem.size() < cabem ? (int)ordem.size() : cabem;
    const int fora  = (int)ordem.size() - n;

    // A geometria do sub-cartao. `SC_X` e a margem dele dentro do card, e a
    // faixa de estado nasce nessa mesma coluna — a faixa E a borda esquerda do
    // cartao, e nao um enfeite encostado nela.
    const int SC_X    = x;                     // sem moldura: margem a margem
    const int SC_W    = w;
    const int SC_H    = passo - 4;             // 4 px de respiro entre cartoes
    // 8, o MESMO raio dos cards de SESSAO e SEMANA logo acima. Era 6, e o
    // degrau entre os dois aparecia: dois arredondamentos diferentes na mesma
    // tela leem como descuido, nao como hierarquia.
    const int SC_R    = 8;
    const int FAIXA_W = 3;
    const int TXT_X   = SC_X + 12;             // depois da faixa, com folga
    const int DIR     = SC_X + SC_W - 8;       // a ponta direita util
    // O trilho do contexto abre na MESMA coluna do conteudo, e nao 4 px antes.
    // Na direita ele ja terminava alinhado com o percentual; na esquerda comecava
    // colado na faixa de estado, e a barra parecia sair de dentro dela em vez de
    // ser a base do cartao. Agora as duas pontas batem com o que esta acima.
    const int TRILHO_X = TXT_X;

    int ly = y0;
    for (int i = 0; i < n; i++) {
        const Agent &a = s.agents[ordem[i]];

        // O CARTAO DA SESSAO. Um degrau acima do card que o contem — ver
        // SUBCARD. Ele resolve o que a linha de dois andares deixava em aberto:
        // sem fundo proprio, o segundo andar de uma sessao se lia como o
        // primeiro da seguinte.
        g->fillRoundRect(SC_X, ly, SC_W, SC_H, SC_R, SUBCARD);

        // As marcas de turno novo e concluido misturam contra o SUBCARD, que e
        // o fundo real desta linha agora.
        drawNovoLinha(g, a.novo, SC_X, ly, SC_W, SC_H, SUBCARD, SC_R);
        drawAvisoLinha(g, a.done, SC_X, ly, SC_W, SC_H, SUBCARD, SC_R);

        // A FAIXA DE ESTADO substitui a bolinha. Sao 3x36 px contra os 10 de
        // diametro do ponto: a diferenca entre ambar e verde a dois metros
        // deixa de ser chute, e este painel vive a essa distancia. A cor e
        // exatamente a da bolinha da tela deitada (ver corDoEstado).
        drawFaixaEstado(g, SC_X, ly, FAIXA_W, SC_H, SC_R,
                        corDoEstado(a.state, a.done));

        // ANDAR DE CIMA: o nome, e o turno na ponta.
        //
        // A DejaVu da flash tem UM corpo (~10 px) e o Arduino_GFX so escala por
        // inteiro, entao 13 px nao existe — o degrau seguinte seria 20, que
        // dominaria o cartao. O que esta forma compra para o nome nao e corpo,
        // e VAO: 180 px contra os 106 da faixa antiga, e o corte por medida
        // passa a caber quase tudo.
        g->setFont(&DejaVuSans7pt7b);
        g->setTextSize(1);
        g->setTextColor(fgColor());
        const int limNome = DIR - 40 - TXT_X;   // 40 px reservados ao turno
        std::string nome = a.repo;
        int16_t nx1, ny1; uint16_t nw, nh;
        while (!nome.empty()) {
            g->getTextBounds(nome.c_str(), 0, 0, &nx1, &ny1, &nw, &nh);
            if ((int)nw <= limNome) break;
            nome.pop_back();
        }
        g->setCursor(TXT_X, ly + 15);
        g->print(nome.c_str());
        g->setFont();

        // O turno cronometrado, alinhado a direita do cartao. Mesmo contador do
        // terminal do Claude Code: o valor chega PRONTO do laco principal
        // (last.agents[].turnoS), extrapolado entre polls e protegido contra
        // andar para tras — ver turnoMonotonico.
        //
        // Terminado o turno o numero NAO some: para onde parou e fica em cinza,
        // dizendo quanto durou a ultima execucao. Cor viva e o que esta
        // acontecendo, cinza e o que ja aconteceu.
        if (a.turnoS >= 0) {
            const std::string t = formatTurno(a.turnoS);
            const uint16_t cor = a.state == AgentState::Working ? H_WORKING
                               : a.state == AgentState::Blocked ? H_BLOCKED
                                                                : MUTED;
            g->setTextColor(g_stale ? MUTED : cor);
            g->setTextSize(1);
            g->setCursor(DIR - (int)t.size() * 6, ly + 6);
            g->print(t.c_str());
        }

        // ANDAR DE BAIXO: o icone da CLI, os chips, e o contexto na ponta.
        // O QUE A SESSAO PRODUZIU, entre os chips e o percentual do contexto.
        //
        // Tres sessoes abertas no mesmo modelo e no mesmo estado se desenhavam
        // identicas, e duas delas tinham escrito ~2.400 linhas enquanto a
        // terceira nao tinha escrito nenhuma. `state` diz o que a sessao esta
        // fazendo AGORA; isto diz o que ela ja fez, e e a distincao que faltava
        // numa tela de cinco.
        //
        // Verde e vermelho nao sao convencao nova: e a de todo diff. Sao
        // contadores CUMULATIVOS da sessao (nao do dia), e o Codex nao os
        // publica — `hasLines` falso deixa o vao vazio, como o contexto ausente
        // ja deixa o "-" no lugar do numero.
        char prod[12] = "", prodMenos[12] = "";
        int prodW = 0, prodMenosW = 0;
        if (a.hasLines) {
            contadorCurto(prod, sizeof(prod), '+', a.linesAdded);
            contadorCurto(prodMenos, sizeof(prodMenos), '-', a.linesRemoved);
            prodW      = (int)strlen(prod) * 6;
            prodMenosW = (int)strlen(prodMenos) * 6;
        }
        // O percentual do contexto ocupa a ponta direita desta fileira; o
        // trilho dele mora na base do cartao (ver abaixo). Os chips param antes
        // da producao, que por sua vez para antes do numero.
        const int xProd = DIR - 26 - 8;
        const int xLim  = prodW ? xProd - prodW - prodMenosW - 10 : xProd;
        int cx = TXT_X;

        // O ICONE DO FORNECEDOR abre a linha — e o que sobrou do cabecalho de
        // grupo. Ele mistura contra o SUBCARD e nao contra o TRACK da faixa:
        // com o fundo errado, o meio-tom da mascara deixa uma franja de um
        // degrau em volta de cada icone (ver misturaNaFaixa).
        //
        // Identificador sem arte propria cai na lhama do Ollama (ver
        // provedores::iconeDe) — nao ha texto de reserva aqui, porque o nome da
        // CLI escrito custaria os 36 px que o icone resolve em 12.
        {
            const provedores::Icone ic = provedores::iconeDe(a.agent);
            if (ic.w) {
                drawIconeProvedor(g, cx, ly + 19 + (14 - ic.h) / 2, ic,
                                  g_stale ? MUTED : provedores::corDe(a.agent),
                                  SUBCARD);
                cx += ic.w + 8;
            }
        }

        // CHIPS, e nao texto separado por pontos. A alternativa foi medida
        // somando o xAdvance da DejaVu contra a grade: "WIN . Opus 5 (1M) .
        // XHigh" sai em 189 px em texto e 146 px em chip. A grade gasta 6 px
        // fixos por caractere, a proporcional gasta mais, e cada separador
        // custa 17 px — mais que os 8 px de borda de um chip inteiro. O texto
        // solto parecia mais limpo e era mais largo.
        {
            const int cwo = drawChip(g, cx, ly + 20, tagDe(a.tag),
                                     corDaTag(a.origem), xLim);
            if (cwo) cx += cwo + 4;
        }
        const int cw = drawChipCortando(g, cx, ly + 20, a.model, fgColor(), xLim);
        if (cw) cx += cw + 4;
        // O esforco POR EXTENSO enquanto couber. A abreviacao (XH, Mx, Hi)
        // existe para a linha de 26 px da tela deitada, onde o chip inteiro nao
        // entra; aqui "XHigh" e uma palavra que se le, enquanto "XH" precisa
        // ser decifrada. O drawChip devolve 0 quando nao cabe, entao a queda
        // para a forma curta e o proprio retorno.
        if (!drawChip(g, cx, ly + 19, a.effort, C_YELL, xLim))
            drawChip(g, cx, ly + 20, effortCurto(a.effort), C_YELL, xLim);

        // O CONTEXTO ATRAVESSA O CARTAO. A barrinha de 44 px na ponta direita
        // existia porque a barra precisa de posicao FIXA para as sessoes serem
        // comparaveis entre si — e essa posicao fixa deixava ~40 px de vazio
        // entre ela e os chips. Na base do cartao ela e as duas coisas: fixa
        // (mesma origem e mesma largura em toda linha, entao ainda se comparam)
        // e larga, e o vazio some porque nao ha mais o que ficar entre elas.
        //
        // De 44 para 252 px de resolucao. Antes, 8% e 34% eram dois tracinhos
        // de comprimento parecido; agora as linhas alinhadas leem como um
        // grafico de barras deitado, e a que esta enchendo o contexto acende
        // sozinha — a cor continua sendo a do nivel que a API manda.
        //
        // 3 px de altura e o trilho bem apagado, de proposito: a faixa de
        // estado ja e a cor forte do cartao, e duas cores fortes por cartao
        // seria uma lista listrada.
        // PILULA, como as barras dos cards de limite: `fillRoundRect` de raio
        // metade da altura nas duas pontas, e nao uma linha reta. O piso e o
        // mesmo de `drawBar` — preenchimento menor que a altura nao vira pilula
        // e sumiria, entao 1% aparece como um ponto, e ponto e informacao.
        // 2 px. E o trilho DO CONTEXTO, e nao as barras de SESSAO e SEMANA la em
        // cima, que continuam em 8 — ali a barra e o assunto do card, aqui ela e
        // o rodape de um cartao de 40 px que ja carrega nome e chips.
        //
        // A conta da altura: nome ate ~17, chips de 19 a 33, trilho em 35 e 36.
        // Sobram 3 px ate a base. Foi de 4 para 3 e de 3 para 2 na placa — em 4
        // o trilho encostava no pe do cartao e lia como a borda dele; em 3 ainda
        // pesava mais que a linha de texto acima.
        const int trilhoW = DIR - TRILHO_X;
        const int TR_H    = 2;
        const int TR_Y    = ly + 35;
        g->fillRoundRect(TRILHO_X, TR_Y, trilhoW, TR_H, TR_H / 2, TRACK);
        if (a.hasContext && a.contextPct > 0) {
            int fill = trilhoW * a.contextPct / 100;
            if (fill < TR_H) fill = TR_H;
            g->fillRoundRect(TRILHO_X, TR_Y, fill, TR_H, TR_H / 2,
                             colorOf(a.level));
        }
        if (prodW) {
            g->setTextSize(1);
            g->setTextColor(g_stale ? MUTED : C_GREEN);
            g->setCursor(xProd - prodMenosW - 4 - prodW, ly + 22);
            g->print(prod);
            g->setTextColor(g_stale ? MUTED : C_RED);
            g->setCursor(xProd - prodMenosW, ly + 22);
            g->print(prodMenos);
        }

        {
            // O numero sobe para a fileira dos chips: la ele nao disputa altura
            // com nada, e o trilho fica inteiro.
            char buf[8];
            if (a.hasContext) snprintf(buf, sizeof(buf), "%d%%", a.contextPct);
            else              snprintf(buf, sizeof(buf), "-");
            g->setTextColor(a.hasContext ? MUTED : TRACK);
            g->setTextSize(1);
            g->setCursor(DIR - (int)strlen(buf) * 6, ly + 22);
            g->print(buf);
        }

        ly += passo;
    }

    // `fora` conta sessoes que nao couberam — e agora ele conta certo por
    // construcao, porque nao ha mais cabecalho nenhum na lista para ser somado
    // por engano.
    if (fora > 0) {
        g->setTextColor(MUTED);
        g->setTextSize(1);
        g->setCursor(x + 12, ly + 3);
        g->printf("+%d", fora);
    }
}

// A turma e a linha de idade do dado. Nao ha bolinhas de pagina: em pe existe
// uma pagina so, e um indicador de quatro com uma acesa seria mentira.
void drawTurmaRetrato(Arduino_Canvas *g) {
    // A turma se espalha pelos 292 px do card, em quatro fatias iguais, e nao
    // na fileira compacta de larguras variaveis. Medido na placa antes: os vaos
    // entre os quatro eram 47, 45 e 27 px — o ultimo colava no penultimo e
    // sobravam 39 px vazios na direita contra 18 na esquerda. Cada slot mede o
    // maior sprite que ele pode mostrar, entao o espaco visual entre dois
    // bichos era "vao fixo + sobra de um + sobra do outro", que nao e constante.
    //
    // Em pe isto e de graca: a faixa da turma mora dentro do prefixo de LINHAS
    // (ver drawRetrato), e alargar na horizontal nao muda quantas linhas o flush
    // envia. Deitado seria caro, e por isso la a fileira continua compacta.
    if (!clawd::crewW(R_CARD_W) ||
        !clawd::drawCrewInto(g, R_MARG, R_TURMA_CHAO, R_CARD_W))
        drawLogo(g, R_MARG, R_TURMA_CHAO - logoH(3) - 4, 3,
                 g_stale ? MUTED : LARANJA);
    g->drawFastHLine(R_MARG, R_TOPO_FIM, R_CARD_W, TRACK);
}

void drawStatusRetrato(Arduino_Canvas *g, const Status &s, int staleSeconds) {
    // O mesmo texto do rodape deitado, e agora literalmente o mesmo codigo (ver
    // lib/metrics/view_model.h). `true` liga o ramo do master fora, que so
    // existe aqui: em pe ha largura para ele, e o rodape nao divide espaco com a
    // fileira de bichos.
    std::string txt = textoVetustez(s, staleSeconds, g_motivo, true);

    // O aviso de cartao ausente saiu daqui tambem. Ver o comentario longo no
    // rodape deitado: com os sprites na flash, cartao fora deixou de degradar a
    // tela, e o aviso virou alarme falso.
    g->setTextColor((staleSeconds > 0 || s.hooksEngine || s.viaSlave)
                        ? C_YELL : MUTED);
    g->setTextSize(1);
    g->setCursor(PANEL_W - R_MARG - (int)txt.size() * 6, R_STATUS_Y);
    g->print(txt.c_str());

    // Na ponta OPOSTA, quando os limites sao lembranca: de um lado quando eles
    // foram lidos, do outro quando o payload chegou. Ver `textoSincronia`.
    //
    // As bolinhas de pagina ficam no meio e nao entram nesta conta: a margem
    // esquerda vai ate a primeira delas com folga de sobra para dez caracteres.
    const std::string sync = textoSincronia(s);
    if (!sync.empty()) {
        g->setTextColor(MUTED);
        g->setCursor(R_MARG, R_STATUS_Y);
        g->print(sync.c_str());
    }
}

// A tela do Token: um limite estourou, e abaixo do cabecalho fica so ele e os
// dois prazos. O cabecalho continua o de sempre — e o unico pedaco que segue
// recebendo o mundo. A turma NAO aparece: a tela e do Token.
// A tela do RESET: uma janela de limite acabou de liberar. Mesmo molde da do
// Token — cabecalho vivo, o bicho tomando o resto — e de proposito: sao os dois
// extremos da mesma noticia, e reconhecer a forma vale mais que variar.
// O teto da caixa do bicho. Constante, e nao numero solto, porque ela e lida
// em dois lugares: o desenho completo daqui e o quadro de animacao do
// `redrawReset`, que desenha exatamente a mesma area. A ALTURA da caixa vem do
// arquivo — cada personagem do rodizio tem a sua.
//
// 54 e nao 78: cada quadro envia um prefixo que vai do topo ao PE do bicho, e
// as 24 linhas de respiro que sobravam acima dele custavam 2,4 ms por quadro —
// pagos 14 vezes por segundo para nao mostrar nada. Ele encosta na divisoria do
// cabecalho e o recado ganha o respiro embaixo, que e onde ele estava faltando.
const int R_RESET_Y = 54;

void drawTelaReset(Arduino_Canvas *g, const Status &s, int staleSeconds,
                   const char *qual) {
    g->fillScreen(BG);
    drawHeaderRetrato(g, s, staleSeconds);

    const int tw = clawd::resetW();
    clawd::drawResetInto(g, (PANEL_W - tw) / 2, R_RESET_Y);

    // O recado em duas linhas, centradas: o que aconteceu e o convite.
    //
    // `qual` chega em caixa alta ("SESSAO", "SEMANA") porque e assim que as
    // faixas de limite o escrevem; aqui a linha e uma frase, entao so a
    // primeira letra fica. Sem acento: as duas fontes do painel cobrem ASCII e
    // nada mais (ver DejaVuSans7pt7b.h, 0x20..0x7E).
    char buf[40];
    snprintf(buf, sizeof(buf), "%s reiniciada", qual);
    for (char *p = buf + 1; *p && *p != ' '; p++) *p = (char)tolower(*p);
    g->setTextSize(2);
    g->setTextColor(fgColor());
    g->setCursor((PANEL_W - (int)strlen(buf) * 12) / 2, PANEL_H - 74);
    g->print(buf);

    g->setFont(&DejaVuSans7pt7b);
    g->setTextSize(1);
    g->setTextColor(MUTED);
    const char *sub = "Vamos gastar tokens";
    int16_t x1, y1; uint16_t sw, sh;
    g->getTextBounds(sub, 0, 0, &x1, &y1, &sw, &sh);
    g->setCursor((PANEL_W - (int)sw) / 2, PANEL_H - 44);
    g->print(sub);
    g->setFont();
}

// O molde das duas telas de bicho em tela cheia: cabecalho vivo, o bicho
// ocupando o resto e os dois prazos miudos no rodape.
//
// Ele nasceu como o corpo da tela do Token e virou molde quando a tela de
// servidor fora apareceu. As duas sao a MESMA tela com outro recado na camisa,
// e reconhecer a forma vale mais do que variar: quem ja aprendeu que o duplo
// toque devolve o painel nao precisa aprender de novo.
// O chao do bicho: onde comeca o recado da tela de servidor fora. Serve de
// limite para centrar quem e menor que a area — sem isso o Clawd dormindo
// (169 px) ficava colado no cabecalho, com 139 px de vazio ate o texto.
const int R_BICHO_TETO = 64;
const int R_BICHO_CHAO = PANEL_H - 108;

void drawTelaBicho(Arduino_Canvas *g, const Status &s, int staleSeconds,
                   int larg, int alt,
                   bool (*desenhar)(Arduino_Canvas *, int, int)) {
    g->fillScreen(BG);
    drawHeaderRetrato(g, s, staleSeconds);

    // Centrado na faixa, com piso no teto: o Token tem 336 px de altura e nao
    // cabe nela, entao para ele a conta da negativo e vale o 64 de sempre.
    const int folga = (R_BICHO_CHAO - R_BICHO_TETO - alt) / 2;
    desenhar(g, (PANEL_W - larg) / 2, R_BICHO_TETO + (folga > 0 ? folga : 0));

    // Os dois prazos no rodape, MIUDOS de proposito: a tela e do bicho, e os
    // numeros so precisam estar la para quem procurar. Rotulo no corpo 1 da
    // grade, valor na DejaVu natural.
    const char *sess = s.session.known && !s.session.resets.empty()
                           ? s.session.resets.c_str() : "-";
    const char *sem  = s.week.known && !s.week.resets.empty()
                           ? s.week.resets.c_str() : "-";
    g->setTextSize(1);
    g->setTextColor(MUTED);
    g->setCursor(52, PANEL_H - 28);
    g->print("SESSAO");
    g->setCursor(180, PANEL_H - 28);
    g->print("SEMANA");
    g->setFont(&DejaVuSans7pt7b);
    g->setTextColor(fgColor());
    g->setCursor(52 + 6 * 6 + 8, PANEL_H - 20);
    g->print(sess);
    g->setCursor(180 + 6 * 6 + 8, PANEL_H - 20);
    g->print(sem);
    g->setFont();
}

void drawTelaToken(Arduino_Canvas *g, const Status &s, int staleSeconds) {
    drawTelaBicho(g, s, staleSeconds, clawd::tokenW(), clawd::tokenH(),
                  clawd::drawTokenInto);
}

// A tela de SERVIDOR FORA. Os prazos do rodape continuam la e continuam certos:
// eles nao vem mais do payload, sao contados pela placa a partir do ultimo valor
// bom (ver lib/metrics/relogio.h). O cabecalho ja diz ha quanto tempo o dado e
// velho, entao a tela nao repete o numero — quem quer a idade tem ela em cima.
void drawTelaOffline(Arduino_Canvas *g, const Status &s, int staleSeconds) {
    drawTelaBicho(g, s, staleSeconds, clawd::offlineW(), clawd::offlineH(),
                  clawd::drawOfflineInto);

    // O recado, que ate aqui vinha PINTADO na camisa do bicho (ver
    // tools/build_token_offline.py). O sprite novo e o Clawd DORMINDO, e quem
    // dorme nao veste cartaz: a frase sai da arte e vira texto, abaixo dele.
    //
    // Corpo 3 na grade de 6x8: "CLAUDE OFFLINE" mede 14*18 = 252 px e sobra
    // margem de 34 px de cada lado. Vale para esta frase — uma maior nao cabe,
    // e por isso a largura entra na conta em vez de um X fixo.
    const char *frase = "CLAUDE OFFLINE";
    g->setTextSize(3);
    g->setTextColor(fgColor());
    g->setCursor((PANEL_W - (int)strlen(frase) * 18) / 2, PANEL_H - 108);
    g->print(frase);

    // O bicho e o mesmo nos dois motivos de dormir; o que muda e a linha
    // miuda. Sem ela, "sem contato com o servidor" e "nenhuma sessao ativa"
    // virariam a mesma tela — e sao problemas diferentes, um de rede e outro
    // de ninguem estar trabalhando.
    g->setFont(&DejaVuSans7pt7b);
    g->setTextSize(1);
    g->setTextColor(MUTED);
    const char *sub = semSessao(s) ? "nenhuma sessao ativa"
                                   : "sem contato com o servidor";
    int16_t x1, y1; uint16_t sw, sh;
    g->getTextBounds(sub, 0, 0, &x1, &y1, &sw, &sh);
    g->setCursor((PANEL_W - (int)sw) / 2, PANEL_H - 68);
    g->print(sub);
    g->setFont();
}

// As bolinhas de pagina, agora que em pe ha para onde ir. Centradas na linha
// da idade do dado, que continua na direita.
void drawBolinhasRetrato(Arduino_Canvas *g, int page) {
    for (int i = 0; i < ui::PAGES; i++) {
        const int cx = PANEL_W / 2 - (ui::PAGES - 1) * 10 + i * 20;
        if (i == page) g->fillCircle(cx, R_STATUS_Y + 4, 4, fgColor());
        else           g->drawCircle(cx, R_STATUS_Y + 4, 3, TRACK);
    }
}

// ---- Pagina de contexto, em pe ----
// O que deitado e coluna lateral vira lista embaixo: o menu de agentes fala a
// mesma lingua do card AGENTS da primeira tela, e o toque seleciona. Os botoes
// de TERMINAL e LIMPAR, que deitado vivem em cantos diferentes, ficam lado a
// lado — mesma fileira, mesmo peso.
// Os botoes viraram PILULAS na linha do estado: ali havia 200 px vazios, e
// deixa-los no fim do bloco do agente faz a leitura virar "quem e, como esta, o
// que fazer". A altura de 18 px cabe na linha de texto sem empurrar nada.
//
// A TURMA MORA NO TOPO, no lugar exato da tela inicial (R_TURMA_CHAO), e nao
// entre os cards como ficou na primeira versao desta pagina.
//
// Foi uma troca de layout por CADENCIA. Entre os cards ela caia fora da faixa
// barata do flush, que em pe e o topo (ver drawRetrato): cada quadro pedia um
// flush inteiro (~48 ms), e a trava que segurava esse custo a deixava em ~3
// quadros por segundo contra os 10 da P0. Na tela isso nao leu como economia,
// leu como bicho travado ao lado dos que se mexem.
//
// O preco foi vertical: o conteudo desce 62 px para caber embaixo da faixa do
// topo, e o card do agente devolve 12 px nos respiros internos — sem isso a
// lista perderia a quarta linha.
// A ordem das pilulas e por ESTABILIDADE, nao por importancia: o TERMINAL fica
// colado na margem direita porque ele e o que sempre existe; o LIMPAR aparece e
// some conforme o contexto cruza os 50%, e some para a ESQUERDA, sem arrastar o
// vizinho. Ao contrario, o TERMINAL dancaria de lugar a cada compactacao.
const int R1_PIL_Y  = 286, R1_PIL_H = 18;
const int R1_PIL_TERM = 58, R1_PIL_LIMPAR = 60;
// Dois cards, como na tela inicial: um do agente (ate as pilulas) e outro da
// lista. Os dois comecam abaixo de R_TOPO_FIM, que e da turma.
const int R1_C1_Y = 114, R1_C1_H = 198;        // 114..312
const int R1_PAD  = 12;
const int R1_C2_Y = 322, R1_C2_H = 130;        // 322..452
// O respiro entre o titulo e a primeira linha: 26 px contra os 18 do card de
// sessoes da tela inicial, porque aqui as linhas sao mais altas.
const int R1_LST_Y0 = R1_C2_Y + 26, R1_LST_PASSO = 26, R1_LST_MAX = 4;

// Corta o texto na largura util da grade (6 px por caractere no corpo 1),
// marcando o corte com "~" — o mesmo sinal que drawRow usa na paisagem.
std::string cortar(const std::string &txt, int largura, int tam) {
    const int cabe = largura / (6 * tam);
    if (cabe <= 0) return "";
    if ((int)txt.size() <= cabe) return txt;
    return txt.substr(0, cabe - 1) + "~";
}

void drawPageContextRetrato(Arduino_Canvas *g, const Status &s, int selected,
                            bool botaoArmado, int opcaoArmada) {
    if (s.agents.empty()) {
        g->setTextColor(MUTED);
        g->setTextSize(2);
        g->setCursor(20, 200);
        g->print("nenhum agente ativo");
        return;
    }
    const Agent &a = s.agents[selected];

    g->fillRoundRect(R_MARG, R1_C1_Y, R_CARD_W, R1_C1_H, 10, CARD);

    g->setTextColor(MUTED);
    g->setTextSize(1);
    g->setCursor(R_MARG + R1_PAD, R1_C1_Y + 10);
    char rotCtx[32];
    snprintf(rotCtx, sizeof(rotCtx), "CONTEXT  (%d de %d)", selected + 1,
             (int)s.agents.size());
    g->print(rotCtx);

    // A TAG da maquina do agente selecionado, logo depois do rotulo. Esta
    // pagina fala de UM agente, e sem ela a unica tela que dizia de qual PC ele
    // vinha era a inicial — o mesmo chip, com a mesma cor por maquina.
    //
    // O limite e o rotulo do reset da sessao, que mora na outra ponta desta
    // linha: nao cabendo, o chip some em vez de escrever por cima dele.
    // y+7 e nao y+10: o texto do chip e desenhado 3 px abaixo da borda dele,
    // entao a linha de base bate com a do rotulo ao lado.
    drawChip(g, R_MARG + R1_PAD + (int)strlen(rotCtx) * 6 + 8, R1_C1_Y + 7,
             tagDe(a.tag), corDaTag(a.origem),
             PANEL_W - R_MARG - R1_PAD - 16 * 6 - 8);

    Metric m;
    m.pct   = a.contextPct;
    m.level = a.level;
    m.known = a.hasContext;

    // Centrado no vao entre o rotulo (que acaba 8 px abaixo do topo do card) e
    // a barra: 40 px de texto no corpo 5, sobrando 10 de cada lado. Encostado
    // no rotulo ele deixava todo o respiro embaixo.
    g->setTextColor(a.hasContext ? fgColor() : MUTED);
    g->setTextSize(5);
    g->setCursor(R_MARG + R1_PAD, R1_C1_Y + 26);
    g->print(pctText(m).c_str());

    // ---- O reset da SESSAO, no vazio a direita do numero grande ----
    // Sao 200 px que o percentual nao usa. O dado e de outra natureza — o
    // numero grande e o contexto do AGENTE, este e o limite da CONTA — e por
    // isso ele mora numa coluna propria em vez de virar mais uma linha de
    // rotulo/valor: ficaria lido como atributo do agente.
    //
    // A cor segue o nivel da sessao, a mesma das barras da tela inicial: o
    // mesmo dado nao pode falar duas linguas em telas diferentes.
    if (s.session.known && !s.session.resets.empty()) {
        const char *rot = "SESSAO RESETA EM";
        g->setTextColor(MUTED);
        g->setTextSize(1);
        g->setCursor(PANEL_W - R_MARG - R1_PAD - (int)strlen(rot) * 6,
                     R1_C1_Y + 10);
        g->print(rot);

        g->setFont(&DejaVuSans7pt7b);
        int16_t rx1, ry1; uint16_t rw, rh;
        g->getTextBounds(s.session.resets.c_str(), 0, 0, &rx1, &ry1, &rw, &rh);
        g->setTextColor(colorOf(s.session.level));
        g->setCursor(PANEL_W - R_MARG - R1_PAD - (int)rw, R1_C1_Y + 40);
        g->print(s.session.resets.c_str());
        g->setFont();

        const std::string hora = horaDoReset(s.session, &s.clock);
        if (!hora.empty()) {
            const std::string as = "as " + hora;
            g->setTextColor(MUTED);
            g->setTextSize(1);
            g->setCursor(PANEL_W - R_MARG - R1_PAD - (int)as.size() * 6,
                         R1_C1_Y + 50);
            g->print(as.c_str());
        }
    }

    drawBar(g, R_MARG + R1_PAD, R1_C1_Y + 76, R_CARD_W - R1_PAD * 2, 12, m);

    // Repo e branch na mesma linha, um em cada ponta; modelo na linha de baixo
    // com a largura toda — "Opus 5 (1M) XH" nao cabe dividindo com ninguem.
    // Repo e branch dividem a linha, e os dois podem ser longos
    // ("kubernetes-indexacao-sosdocs" na esquerda, "master" na direita). Cada
    // um leva METADE da largura util, com o corte por medida — sem isso os
    // dois escreviam por cima um do outro no meio da linha.
    //
    // Os tres blocos de rotulo/valor (28 px cada) andam de 38 em 38 e nao de 42
    // como antes: sao os 12 px que a subida da turma cobrou do card. O respiro
    // que sobra entre eles e de 10 px, e nenhum bloco encosta no vizinho.
    const int meiaCol = (R_CARD_W - R1_PAD * 2 - 8) / 2;
    rotuloValor(g, R_MARG + R1_PAD, R1_C1_Y + 96, "REPO",
                cortar(a.repo.empty() ? "-" : a.repo, meiaCol, 2).c_str(),
                2, false);
    rotuloValor(g, PANEL_W - R_MARG - R1_PAD, R1_C1_Y + 96, "BRANCH",
                cortar(a.branch.empty() ? "-" : a.branch, meiaCol, 2).c_str(),
                2, true);
    std::string modelo = a.model.empty() ? "-" : a.model;
    if (!a.effort.empty()) modelo += " " + a.effort;
    rotuloValor(g, R_MARG + R1_PAD, R1_C1_Y + 134, "MODELO",
                cortar(modelo, R_CARD_W - R1_PAD * 2, 2).c_str(), 2, false);

    const char *txt = nullptr;
    uint16_t    cor = MUTED;
    switch (a.state) {
        case AgentState::Blocked: txt = "BLOQUEADO - esperando voce"; cor = C_RED; break;
        case AgentState::Idle:    txt = "turno encerrado"; cor = MUTED;   break;
        case AgentState::Working: txt = "trabalhando";     cor = LARANJA; break;
        case AgentState::Unknown:
            if (a.stale) { txt = "sem publicar"; cor = C_YELL; }
            break;
    }
    if (txt) {
        g->setTextColor(cor);
        g->setTextSize(1);
        g->setCursor(R_MARG + R1_PAD, R1_PIL_Y + 4);
        if (a.state == AgentState::Unknown)
            g->printf("%s %s", txt, formatAge(a.age).c_str());
        else
            g->print(txt);
    }

    // As pilulas, na ponta direita da linha do estado. Cada uma so quando
    // existe — regra dos botoes deitados: controle invisivel nao responde ao
    // toque, e a area sensivel vale o que o desenho vale (ver os hit-tests).
    if (terminalVisivel(s, selected)) {
        const int px = PANEL_W - R_MARG - R1_PAD - R1_PIL_TERM;
        g->drawRoundRect(px, R1_PIL_Y, R1_PIL_TERM, R1_PIL_H, R1_PIL_H / 2, TRACK);
        g->setTextColor(MUTED);
        g->setTextSize(1);
        g->setCursor(px + (R1_PIL_TERM - 4 * 6) / 2, R1_PIL_Y + 5);
        g->print("TERM");
    }
    if (botaoVisivel(s, selected)) {
        const int px = PANEL_W - R_MARG - R1_PAD - R1_PIL_TERM - 6 - R1_PIL_LIMPAR;
        const uint16_t cor2 = botaoArmado ? C_YELL : TRACK;
        g->drawRoundRect(px, R1_PIL_Y, R1_PIL_LIMPAR, R1_PIL_H, R1_PIL_H / 2, cor2);
        const char *rot = botaoArmado ? "CONFIRMA?" : "LIMPAR";
        const int lw = (int)strlen(rot) * 6;
        g->setTextColor(botaoArmado ? C_YELL : MUTED);
        g->setTextSize(1);
        g->setCursor(px + (R1_PIL_LIMPAR - lw) / 2, R1_PIL_Y + 5);
        g->print(rot);
    }

    // A lista, na lingua do card AGENTS da primeira tela. O toque seleciona.
    // A turma nao e desenhada aqui: quem a poe no topo e drawStatus, com a
    // mesma chamada da tela inicial.
    g->fillRoundRect(R_MARG, R1_C2_Y, R_CARD_W, R1_C2_H, 10, CARD);
    g->setTextColor(MUTED);
    g->setTextSize(1);
    g->setCursor(R_MARG + R1_PAD, R1_C2_Y + 10);
    g->print("AGENTS");

    const int n = (int)s.agents.size() < R1_LST_MAX ? (int)s.agents.size()
                                                    : R1_LST_MAX;
    for (int i = 0; i < n; i++) {
        const Agent &ag = s.agents[i];
        const int ly = R1_LST_Y0 + i * R1_LST_PASSO;
        // Dentro de um card, destacar com a cor do card nao destaca nada: a
        // faixa do selecionado e um tom ACIMA dele.
        drawNovoLinha(g, ag.novo, R_MARG + 4, ly - 2, R_CARD_W - 8,
                      R1_LST_PASSO - 2);
        drawNovoLinha(g, ag.novo, R_MARG + 4, ly - 2, R_CARD_W - 8,
                      R1_LST_PASSO - 2);
        if (i == selected)
            g->fillRoundRect(R_MARG + 4, ly - 2, R_CARD_W - 8,
                             R1_LST_PASSO - 2, 6, misturar(CARD, FG, 5, 48));
        // A linha fala a mesma lingua do card AGENTS da tela inicial: bolinha,
        // nome, chip e a barrinha de contexto. Sem o chip de ESFORCO — aqui o
        // agente selecionado ja mostra modelo e esforco no bloco de cima, e
        // repetir os dois na lista so gastaria a largura que a barra quer.
        //
        // O percentual fica no FIM da linha, depois da barra: e o numero exato
        // do que a barra desenha por cima, e ler os dois na mesma ordem em que
        // aparecem na tela inicial evita ter que reaprender a linha.
        const int pctW = 4 * 6;                    // "100%"
        const int barW = 34;
        const int pctX = PANEL_W - R_MARG - R1_PAD - pctW;
        const int barX = pctX - 6 - barW;

        drawEstadoDot(g, R_MARG + 20, ly + 9, ag.state, ag.done);

        // Nome cortado por MEDIDA ate a coluna do chip: proporcional nao tem
        // largura fixa, e contar caracteres deixaria uns curtos e outros
        // invadindo o vizinho.
        const int chipX = R_MARG + 128;
        g->setFont(&DejaVuSans7pt7b);
        g->setTextSize(1);
        g->setTextColor(fgColor());
        std::string nome = ag.repo;
        int16_t nx1, ny1; uint16_t nw, nh;
        while (!nome.empty()) {
            g->getTextBounds(nome.c_str(), 0, 0, &nx1, &ny1, &nw, &nh);
            if ((int)nw <= chipX - (R_MARG + 32) - 6) break;
            nome.pop_back();
        }
        g->setCursor(R_MARG + 32, ly + 14);
        g->print(nome.c_str());
        g->setFont();

        // A TAG da maquina abre a coluna de chips, como na lista da tela
        // inicial: a mesma linha, na mesma ordem, com a mesma cor por maquina.
        // Em cascata — nao cabendo os dois, o modelo e que cai, porque de qual
        // PC vem a sessao e o que esta lista nao diz de outro jeito.
        int cx = chipX;
        const int cwo = drawChip(g, cx, ly + 5, tagDe(ag.tag),
                                 corDaTag(ag.origem), barX - 6);
        if (cwo) cx += cwo + 4;
        drawChip(g, cx, ly + 5, ag.model, fgColor(), barX - 6);

        // A barrinha existe sempre; o preenchimento, so com contexto conhecido.
        g->drawFastHLine(barX, ly + 12, barW, TRACK);
        if (ag.hasContext && ag.contextPct > 0) {
            int fill = barW * ag.contextPct / 100;
            if (fill < 2) fill = 2;
            g->drawFastHLine(barX, ly + 12, fill,
                             g_stale ? MUTED : colorOf(ag.level));
        }

        if (ag.hasContext) {
            char pb[8];
            snprintf(pb, sizeof(pb), "%d%%", ag.contextPct);
            g->setTextSize(1);
            g->setTextColor(MUTED);
            g->setCursor(pctX + pctW - (int)strlen(pb) * 6, ly + 5);
            g->print(pb);
        }
    }

    (void)opcaoArmada;
}

// ====================================================================
//  A TELA NOVA (pagina 0 em pe)
// ====================================================================
//
// A primeira tela do painel, escolhida em prancheta (ver o canvas "Tela 1 do
// Claudinho"): o nome DIGITANDO no lugar do bicho do cabecalho, a fileira com
// o bicho no CENTRO, os limites em ANEIS e a mesma lista de sessoes da
// principal. A principal continua inteira na pagina 1 — as duas convivem.
//
// A geometria REUSA as constantes da principal de proposito: topo em
// R_TOPO_FIM, aneis na faixa R_LIM_Y..R_LIM_Y+R_LIM_H e lista em R_SES_Y.
// E isso que faz os hit-tests (cartao de sessao, atalhos de percentual,
// turma) valerem nas duas paginas sem geometria dobrada.

// ---- O nome digitando ----
// Corpo 3 da grade (18 px por letra), o degrau proporcional abaixo da hora em
// corpo 4 — "CLAUDINHO" mede 162 px e a hora 120, cabendo nos 292 uteis.
const char NOME_PAINEL[]   = "CLAUDINHO";
const int  NOME_SZ         = 3;
const int  NOME_LEN        = 9;
// Uma letra a cada 320 ms; nome completo, o cursor pisca por ~2,5 s e o ciclo
// recomeca. Todos os quadros saem pelo prefixo barato (~11 ms), entao a
// animacao custa o mesmo que a turma ja paga.
const uint32_t NOME_LETRA_MS  = 320;
const uint32_t NOME_CURSOR_MS = 400;
const int      NOME_PISCADAS  = 6;      // 6 meias-fases = ~2,4 s de pausa

int      g_nomeLetras  = 0;     // quantas letras ja estao na tela
int      g_nomeFase    = 0;     // piscadas do cursor na pausa
bool     g_nomeCursor  = true;
uint32_t g_nomeMs      = 0;

// `xLimite` e onde comeca o que vier a direita (a hora). O CURSOR RESPEITA ESSE
// limite e o nome nao precisa: "CLAUDINHO" em corpo 3 mede 162 px e cabe nos
// 164 que sobram, mas o cursor da celula seguinte pediria mais 12 e escrevia
// por cima do primeiro digito do relogio.
//
// Isto so apareceu na FOTO da placa. A conta dizia que cabia porque media o
// nome, e o cursor e um decimo elemento que nenhuma das duas larguras previa —
// e a colisao de 4 px nao aparece em nenhum teste, porque o painel nao recorta:
// ele simplesmente desenha um por cima do outro.
//
// Cortar o cursor em vez de encolher o nome e a escolha certa: o corpo do nome
// e proporcional ao da hora de proposito, e o cursor e enfeite da digitacao. Na
// pratica ele pisca ate a oitava letra e some na nona, o que le como "terminou
// de escrever".
void drawNomeCabecalho(Arduino_Canvas *g, int xLimite) {
    // Centrado na faixa do cabecalho como a hora (ver drawHeaderRetrato).
    const int y = (R_HDR_LINHA - 8 * NOME_SZ) / 2;
    g->setTextColor(LARANJA);
    g->setTextSize(NOME_SZ);
    g->setCursor(R_MARG, y);
    for (int i = 0; i < g_nomeLetras; i++) g->print(NOME_PAINEL[i]);

    // O cursor e um bloco cheio na celula seguinte, como o de terminal.
    if (g_nomeCursor) {
        const int cx = R_MARG + g_nomeLetras * 6 * NOME_SZ + 2;
        const int cw = 4 * NOME_SZ;
        if (cx + cw <= xLimite)
            g->fillRect(cx, y, cw, 8 * NOME_SZ, LARANJA);
    }
}

// Onde o cabecalho da tela nova deixa de ser do nome. E o inicio da hora menos
// um respiro; sem relogio conhecido, a margem direita.
int limiteDoNome(const Status &s) {
    if (!s.clock.known) return PANEL_W - R_MARG;
    return PANEL_W - R_MARG - (int)s.clock.hm.size() * 6 * 4 - 6;
}

// ---- Os aneis de limite ----
// A mesma faixa das colunas da principal (R_LIM_Y, altura R_LIM_H): e isso que
// mantem os atalhos de toque (alvoPctSessao/alvoPctSemana) valendo aqui.
// 38 e nao 40, e o preco veio de MEDIDA e nao de gosto: a coluna tem 143 px e o
// texto ao lado precisa de 61 para o pior prazo real ("12h05m", 59 px somando o
// xAdvance da DejaVu). Com o anel em 80 sobravam 55, e o instante saia cortado
// no meio — "12:09a" apareceu na foto da placa. Quatro pixels de anel compram a
// linha inteira de volta.
const int ANEL_R1 = 38;                 // raio externo: diametro 76
const int ANEL_R2 = 29;                 // 9 px de espessura

// O centro dos aneis sai do VAO REAL — da divisoria do topo (R_TOPO_FIM) ao
// PRIMEIRO CARTAO da lista, e o cartao e nao o `R_SES_Y`, porque o que o olho
// mede e a borda que ele ve. NAO se herda o meio da faixa de limites da tela
// principal (`R_LIM_Y + R_LIM_H / 2`): aquela faixa foi dimensionada para
// colunas de card, mais altas do que um anel.
//
// E A BARRA DO FABLE ENTRA NA CONTA. Ela e fina e apagada, e a primeira versao
// a tratou como enfeite que nao pesa — com a barra ainda invisivel (a API viva
// nao publicava o campo), centrar so o anel era de fato o certo. Assim que ela
// acendeu, virou conteudo embaixo dos aneis, e o centro sem ela empurrava o par
// contra a lista. O bloco que se centra e anel MAIS barra.
const int ANEL_TETO  = R_TOPO_FIM;                  // 106
const int ANEL_CHAO  = R_SES_Y + 8;                 // 228, o topo do 1o cartao
const int FABLE_ALT  = 24;                          // a linha do Fable, com o vao
const int ANEL_CY    = (ANEL_TETO + ANEL_CHAO - FABLE_ALT) / 2;   // 155

// Um trecho do anel em "graus de percentual": 0..100 vira 0..360 a partir do
// topo, em sentido horario. O fillArc do Arduino_GFX conta graus do leste,
// entao o topo e 270.
void arcoPct(Arduino_Canvas *g, int cx, float dePct, float atePct,
             uint16_t cor) {
    if (atePct <= dePct) return;
    g->fillArc(cx, ANEL_CY, ANEL_R1, ANEL_R2,
               270.0f + dePct * 3.6f, 270.0f + atePct * 3.6f, cor);
}

// A largura real de um texto na DejaVu. Proporcional: contar caractere aqui
// mentiria, e foi contando que a coluna estourou.
// CUIDADO, ELA MEXE NA FONTE DO CANVAS: mede na DejaVu e devolve a EMBUTIDA,
// que e o estado padrao — nao o que estava antes, porque `gfxFont` e protegido
// e nao ha como ler a fonte corrente para restaura-la. Chamar isto no meio de
// um bloco que ja esta na DejaVu derruba o texto seguinte para a grade (ver
// drawAnelDeitado, onde isso custou o alinhamento de uma linha inteira).
int larguraDejaVu(Arduino_Canvas *g, const std::string &t) {
    if (t.empty()) return 0;
    int16_t x1, y1; uint16_t w, h;
    g->setFont(&DejaVuSans7pt7b);
    g->setTextSize(1);
    g->getTextBounds(t.c_str(), 0, 0, &x1, &y1, &w, &h);
    g->setFont();
    return (int)w;
}

// O instante do reset, no formato que cabe em `maxW`. Vazio quando nao ha o que
// dizer.
//
// SEMPRE EM 24H, inclusive na sessao, e isso e conserto e nao gosto: a API
// manda "12:09am" e o relogio do cabecalho, dois centimetros acima, diz "21:47".
// Duas convencoes de hora na mesma tela e uma conta que quem olha tem que fazer
// de cabeca. Em 24h a mesma informacao cai de 64 para 50 px, que e o que faz ela
// caber ao lado do anel.
//
// A cascata e a mesma do rodape da tela principal, e sacrifica na mesma ordem:
// primeiro o dia por extenso vira as tres letras de sempre, depois cai a HORA,
// que e informacao de verdade mas e a segunda pergunta — "cai no sabado" decide
// mais do que "as 23h".
std::string instanteCurto(Arduino_Canvas *g, const Metric &m, const Clock *rel,
                          int maxW) {
    if (!m.known || m.at.empty() || m.at == "-") return "";

    const std::string hora = horaDoReset(m, rel);
    const size_t abre = m.at.find('(');
    if (abre == std::string::npos) {
        // Janela de HORAS: a API manda relogio ("12:09am"). A conta da placa
        // devolve o mesmo instante em 24h; sem relogio na placa, o texto da API
        // e melhor do que nada.
        return hora.empty() ? m.at : hora;
    }

    // Janela de DIAS: a API manda data com o dia por extenso.
    std::string dia = m.at;
    const size_t fecha = dia.find(')', abre);
    if (fecha != std::string::npos && fecha > abre + 1)
        dia = dia.substr(abre + 1, fecha - abre - 1);
    if (dia.size() > 3) dia = dia.substr(0, 3);

    if (!hora.empty()) {
        const std::string completo = dia + " " + hora;
        if (larguraDejaVu(g, completo) <= maxW) return completo;
    }
    return dia;
}

// UM limite, dentro da coluna `colX`..`colX+colW`: o anel a esquerda e os dois
// numeros a direita, o CONJUNTO centrado na coluna.
//
// SEM ROTULO. "SESSAO" e "SEMANA" sairam a pedido, e a coluna precisava dos
// dois: o percentual mora no miolo do anel e o rotulo repetia, em corpo 1, o
// que a posicao ja dizia — a esquerda e a janela curta, a direita e a semana.
// O que ele custava era largura, e largura era exatamente o que faltava.
//
// O conjunto e CENTRADO e nao encostado na margem: com o anel colado na
// esquerda, o texto sobrava para dentro da coluna vizinha e o instante da
// sessao encostava no anel da semana (visto na foto da placa, nao na conta).
void drawAnelLimite(Arduino_Canvas *g, int colX, int colW, const Metric &m,
                    int janelaSeg = 0, const Clock *rel = nullptr) {
    const int GAP    = 6;
    const int maxTxt = colW - ANEL_R1 * 2 - GAP;      // 61 px

    // Os dois textos primeiro: e a largura deles que decide onde o anel comeca.
    // O instante ja nasce cortado na medida — a cascata dele escolhe QUE parte
    // sai, o que e sempre melhor do que perder a ultima letra.
    const std::string prazo = m.known && !m.resets.empty() ? m.resets : "-";
    const std::string inst  = instanteCurto(g, m, rel, maxTxt);

    const int pw = larguraDejaVu(g, prazo);
    const int iw = larguraDejaVu(g, inst);
    int txtW = pw > iw ? pw : iw;
    if (txtW > maxTxt) txtW = maxTxt;

    const int total = ANEL_R1 * 2 + GAP + txtW;
    const int x0    = colX + (colW - total) / 2;
    const int cx    = x0 + ANEL_R1;
    const int txtX  = x0 + ANEL_R1 * 2 + GAP;
    // O trilho inteiro primeiro; os trechos coloridos por cima. E o mesmo
    // desenho da barra reta (ver drawBar), dobrado em circulo: gasto na cor do
    // nivel, folga ate o ritmo na cor apagada, fresta de trilho quando o gasto
    // passou do ritmo.
    g->fillArc(cx, ANEL_CY, ANEL_R1, ANEL_R2, 0, 360, TRACK);

    if (m.known) {
        const uint16_t cor = colorOf(m.level);
        const int rp = ritmoPct(m, janelaSeg);
        const float pct = (float)(m.pct < 0 ? 0 : (m.pct > 100 ? 100 : m.pct));
        if (rp < 0) {
            arcoPct(g, cx, 0, pct, cor);
        } else if (pct > (float)rp) {
            // PASSOU DO RITMO: a fresta de ~2 graus de trilho cravada na
            // posicao do ritmo, como a da barra reta — quem diz "passou" e so
            // ela, sem cor nova.
            arcoPct(g, cx, 0, pct, cor);
            const float fr = (float)rp;
            g->fillArc(cx, ANEL_CY, ANEL_R1, ANEL_R2,
                       270.0f + fr * 3.6f - 2.0f, 270.0f + fr * 3.6f, TRACK);
        } else {
            arcoPct(g, cx, 0, (float)rp, misturar(TRACK, cor, RITMO_FOLGA, 100));
            arcoPct(g, cx, 0, pct, cor);
        }
    }

    // O percentual no MIOLO, corpo 2 da grade na cor do nivel — e o numero que
    // esta tela inteira existe para dizer de longe.
    const std::string pct = pctText(m);
    g->setTextColor(!m.known || m.memoria
                        ? MUTED : (g_stale ? MUTED : colorOf(m.level)));
    g->setTextSize(2);
    g->setCursor(cx - (int)pct.size() * 6, ANEL_CY - 8);
    g->print(pct.c_str());

    // Os dois numeros ao LADO do anel, centrados na altura dele: o prazo em
    // cima, na cor do texto, e o instante embaixo, apagado. Sem rotulo — a
    // posicao ja diz qual janela e, e o percentual mora no miolo do anel.
    //
    // O corte e por MEDIDA contra `txtW`, e nao por contagem: a DejaVu e
    // proporcional, entao contar caractere deixaria uns curtos e outros
    // invadindo o anel vizinho — que foi exatamente o defeito da foto.
    auto cortar = [&](std::string t) {
        int16_t x1, y1; uint16_t tw, th;
        while (!t.empty()) {
            g->getTextBounds(t.c_str(), 0, 0, &x1, &y1, &tw, &th);
            if ((int)tw <= txtW) break;
            t.pop_back();
        }
        return t;
    };

    g->setFont(&DejaVuSans7pt7b);
    g->setTextSize(1);
    g->setTextColor(m.known && !m.memoria ? fgColor() : MUTED);
    g->setCursor(txtX, ANEL_CY - 2);
    g->print(cortar(prazo).c_str());

    if (!inst.empty()) {
        g->setTextColor(MUTED);
        g->setCursor(txtX, ANEL_CY + 16);
        g->print(cortar(inst).c_str());
    }
    g->setFont();
}

// ---- A barra do FABLE ----
// Discreta DE PROPOSITO: cinza, sem cor de nivel, na fresta entre os aneis e a
// lista. E consumo informativo, nao limite que pede acao — foi o pedido.
// So aparece quando a API publica o campo; contra uma API antiga a fresta fica
// vazia e nada na tela denuncia que falta algo.
// Abaixo do anel, e e ela que fecha o bloco centrado (ver ANEL_CY). O rotulo
// tem 8 px de altura e a barra de 3 fica centrada nessa mesma linha, entao o
// bloco vai de FABLE_Y-1 a FABLE_Y+7.
//
// 16 px de vao e nao 8: colada no anel ela lia como legenda dele, e nao como
// uma terceira medida. O respiro sai do proprio bloco (FABLE_ALT sobe junto),
// entao o conjunto continua centrado e o anel apenas sobe os mesmos 4 px.
const int FABLE_Y = ANEL_CY + ANEL_R1 + 16;     // 209

// A linha inteira mede 204 px e fica CENTRADA, em vez de ir de margem a margem
// como as barras dos limites. Sao 30% a menos de largura, 15% recolhidos de
// cada lado: de margem a margem ela tinha o mesmo peso horizontal dos aneis
// logo acima, e competia com eles — o Fable e informacao de canto de olho, e o
// tamanho tem que dizer isso antes de o texto dizer.
const int FABLE_W    = 204;
const int FABLE_ROT  = 5 * 6;      // "FABLE" na grade de 6 px
const int FABLE_NUM  = 4 * 6;      // reservado para "100%", o pior caso
const int FABLE_GAP  = 8;

void drawFableRetrato(Arduino_Canvas *g, const Status &s) {
    if (!s.fableKnown) return;

    // A COR e a do nivel, como nos aneis e nas barras dos limites: verde ate
    // 50%, amarela ate 80, vermelha acima. Ela vem resolvida do servidor (ver
    // `colors.fable`), entao a placa nao tem limiar proprio para discordar.
    //
    // O ROTULO fica apagado de proposito, e so a barra e o numero pegam cor: o
    // que muda aqui e a medida, nao o nome dela — e a mesma divisao de papeis
    // dos cartoes de limite.
    const uint16_t cor = colorOf(s.fableLevel);

    const int x0 = (PANEL_W - FABLE_W) / 2;
    g->setTextColor(MUTED);
    g->setTextSize(1);
    g->setCursor(x0, FABLE_Y - 1);
    g->print("FABLE");

    // O numero e alinhado a DIREITA dentro de uma coluna de largura fixa: assim
    // a barra nao muda de tamanho quando o percentual passa de uma casa para
    // duas, e as duas leituras seguidas ficam comparaveis.
    char pb[8];
    snprintf(pb, sizeof(pb), "%d%%", s.fablePct);
    const int numX = x0 + FABLE_W - FABLE_NUM;
    g->setTextColor(cor);
    g->setCursor(numX + FABLE_NUM - (int)strlen(pb) * 6, FABLE_Y - 1);
    g->print(pb);

    const int bx = x0 + FABLE_ROT + FABLE_GAP;
    const int bw = numX - FABLE_GAP - bx;
    g->fillRoundRect(bx, FABLE_Y + 1, bw, 3, 1, CARD);
    int fill = bw * (s.fablePct < 0 ? 0 : (s.fablePct > 100 ? 100 : s.fablePct)) / 100;
    if (fill > 0 && fill < 3) fill = 3;
    if (fill > 0) g->fillRoundRect(bx, FABLE_Y + 1, fill, 3, 1, cor);
}

void drawTurmaNova(Arduino_Canvas *g) {
    if (!clawd::crewW(R_CARD_W) ||
        !clawd::drawCrewComCentroInto(g, R_MARG, R_TURMA_CHAO, R_CARD_W))
        drawLogo(g, R_MARG, R_TURMA_CHAO - logoH(3) - 4, 3,
                 g_stale ? MUTED : LARANJA);
    g->drawFastHLine(R_MARG, R_TOPO_FIM, R_CARD_W, TRACK);
}

void drawHeaderNova(Arduino_Canvas *g, const Status &s, int staleSeconds) {
    drawNomeCabecalho(g, limiteDoNome(s));

    // A hora e o selo VIA sao os mesmos do cabecalho da principal — e a mesma
    // informacao no mesmo lugar, so o canto esquerdo mudou de dono.
    if (s.clock.known) {
        const int sz = 4;
        const int w  = (int)s.clock.hm.size() * 6 * sz;
        g->setTextColor(fgColor());
        g->setTextSize(sz);
        g->setCursor(PANEL_W - R_MARG - w, (R_HDR_LINHA - 8 * sz) / 2);
        g->print(s.clock.hm.c_str());
    }
    if (s.viaSlave) {
        const int rw = s.clock.known ? (int)s.clock.hm.size() * 6 * 4 + 10 : 0;
        const int sw = (s.tag.empty() ? 11 : (int)s.tag.size() + 4) * 6 + 10;
        drawSeloVia(g, PANEL_W - R_MARG - rw - sw, (R_HDR_LINHA - 16) / 2, s.tag);
    }
    g->drawFastHLine(R_MARG, R_HDR_LINHA, R_CARD_W,
                     staleSeconds > 0 ? C_YELL : TRACK);
}

void drawRetratoNova(Arduino_Canvas *g, const Status &s, int staleSeconds,
                     int opcaoArmada) {
    g->fillScreen(BG);
    drawHeaderNova(g, s, staleSeconds);
    drawTurmaNova(g);

    if (!s.online && s.agents.empty() && !s.doCache) {
        g->setTextColor(MUTED);
        g->setTextSize(2);
        g->setCursor(20, 200);
        g->print("CLAUDIO OFFLINE");
        g->setTextSize(1);
        g->setCursor(20, 226);
        g->print("nenhuma sessao ativa");
    } else if (s.bloqueio.known) {
        drawPerguntaP0(g, s, opcaoArmada);
    } else {
        // As colunas sao as mesmas da tela principal (143 px com vao de 6),
        // entao os atalhos de toque continuam batendo. Cada uma centra o
        // proprio conjunto anel+numeros.
        drawAnelLimite(g, R_MARG, R_LIM_W, s.session, JANELA_5H, &s.clock);
        drawAnelLimite(g, R_MARG + R_LIM_W + R_LIM_GAP, R_LIM_W,
                       s.week, JANELA_7D, &s.clock);
        drawFableRetrato(g, s);
        drawSessoesRetrato(g, R_MARG, R_SES_Y, R_CARD_W,
                           R_SES_FIM - R_SES_Y, s);
    }

    drawBolinhasRetrato(g, 0);
    drawStatusRetrato(g, s, staleSeconds);
}

void drawRetrato(Arduino_Canvas *g, const Status &s, int staleSeconds,
                 int opcaoArmada, bool telaToken, const char *telaReset,
                 bool telaOffline) {
    if (telaReset && clawd::resetW()) {
        drawTelaReset(g, s, staleSeconds, telaReset);
        return;
    }
    // Antes da do Token: com o servidor fora, o limite estourado que ela
    // anunciaria e uma leitura velha, e "o outro lado calou" e a resposta a
    // pergunta que quem olha esta fazendo — por que nada muda.
    if (telaOffline && clawd::offlineW()) {
        drawTelaOffline(g, s, staleSeconds);
        return;
    }
    if (telaToken && clawd::tokenW()) {
        drawTelaToken(g, s, staleSeconds);
        return;
    }
    // O Clawd dorme tambem quando o servidor RESPONDE e nao ha sessao nenhuma.
    // E o motivo mais comum dos dois, e ate aqui ele caia no aviso de texto la
    // embaixo — a tela do bicho existia e nunca aparecia no caso que o usuario
    // testa. `offlineW()` porque sem o sprite em cena o texto ainda e o recado.
    if (semSessao(s) && clawd::offlineW()) {
        drawTelaOffline(g, s, staleSeconds);
        return;
    }

    g->fillScreen(BG);
    drawHeaderRetrato(g, s, staleSeconds);
    drawTurmaRetrato(g);

    // `!doCache` pela mesma razao da tela deitada: um retrato do cartao nao pode
    // afirmar que nao ha sessao ativa — ele nem guarda a lista. Aqui ele cede o
    // lugar para as duas faixas de limite, que sao exatamente o que o cache
    // trouxe.
    if (!s.online && s.agents.empty() && !s.doCache) {
        g->setTextColor(MUTED);
        g->setTextSize(2);
        g->setCursor(20, 200);
        g->print("CLAUDIO OFFLINE");
        g->setTextSize(1);
        g->setCursor(20, 226);
        g->print("nenhuma sessao ativa");
    } else if (s.bloqueio.known) {
        // Mesma pergunta em tela cheia da pagina deitada, e literalmente a mesma
        // funcao: a geometria dela ja sai de display::telaW/telaH.
        drawPerguntaP0(g, s, opcaoArmada);
    } else {
        drawColunaLimite(g, R_MARG, R_LIM_Y, R_LIM_W, R_LIM_H,
                         "SESSAO", s.session, JANELA_5H);
        drawColunaLimite(g, R_MARG + R_LIM_W + R_LIM_GAP, R_LIM_Y,
                         R_LIM_W, R_LIM_H, "SEMANA", s.week, JANELA_7D,
                         &s.clock);
        drawSessoesRetrato(g, R_MARG, R_SES_Y, R_CARD_W,
                           R_SES_FIM - R_SES_Y, s);
    }

    // Pagina 1 agora: a tela nova tomou a bolinha da frente.
    drawBolinhasRetrato(g, 1);
    drawStatusRetrato(g, s, staleSeconds);
}
}  // namespace

namespace ui {

void marcarMotivo(const char *v) { if (v) g_motivo = v; }

void duasFontes(bool v) { g_duasFontes = v; }

bool limiteEstourado(const Status &s) {
    return (s.session.known && s.session.pct >= 100) ||
           (s.week.known && s.week.pct >= 100);
}

// Resolve o ID guardado para uma posicao na lista atual. Se aquele agente
// morreu (ou nunca existiu), cai no primeiro — nunca em indice invalido.
static int indexOfId(const Status &s, const std::string &id) {
    for (size_t i = 0; i < s.agents.size(); i++)
        if (s.agents[i].id == id) return (int)i;
    return 0;
}

// "2026-06-30" -> "30-06-2026". Rearranjo de string e nada mais.
//
// Feito AQUI e nao na API, ao contrario das outras datas do painel: aquelas
// nascem de um carimbo epoch e precisam de fuso, que a placa nao tem. Esta ja
// chega como data pronta, entao virar o dia para a frente e trabalho de
// apresentacao — e o campo continua sendo dado no JSON, nao texto de tela.
//
// Formato inesperado passa direto, sem tentar adivinhar: mostrar a data como a
// API mandou e melhor do que embaralhar pedacos de uma string que nao e o que
// se esperava.
std::string dataDiaPrimeiro(const std::string &iso) {
    if (iso.size() != 10 || iso[4] != '-' || iso[7] != '-') return iso;
    return iso.substr(8, 2) + "-" + iso.substr(5, 2) + "-" + iso.substr(0, 4);
}

// A quarta pagina: o nivel do Clawd.
//
// Os tres numeros que a alimentam vem de lugares diferentes de proposito.
// Turnos e custo sao da API, que ja tem o livro-caixa inteiro e sobrevive a
// cartao morto; as horas de convivio sao do cartao, porque so a placa sabe que
// esteve ligada. Ver nivel.h.
void drawPageNivel(Arduino_Canvas *g, const Status &s, const nivel::Estado &e,
                   float xpDoDia) {
    if (!s.vitalicio.known) {
        // API anterior a este recurso. Dizer o que falta e melhor do que
        // mostrar nivel 1, que seria afirmar sobre uma historia nao contada.
        g->setTextColor(MUTED);
        g->setTextSize(2);
        g->setCursor(14, 140);
        g->print("SEM HISTORICO");
        g->setTextSize(1);
        g->setCursor(14, 168);
        g->print("a API nao publica o bloco vitalicio");
        return;
    }

    const float horas = e.contatoS / 3600.0f;
    int n = nivel::calcular(s.vitalicio.turnos, s.vitalicio.costUsd, horas);
    // Nunca envelhece para tras: se a API perder historia, o bicho fica onde
    // chegou. Um nivel que regride seria pior do que um numero otimista.
    if (n < e.nivelMax) n = e.nivelMax;

    const bool emPe = display::retrato();
    const int  W    = display::telaW();

    char buf[48];

    // ---- O numero, no alto, onde o bicho ficava ----
    // Corpo 3 e nao 2: com o sprite fora, "NIVEL 66" e o unico elemento grande
    // da pagina, e no corpo antigo ele boiava no vazio que o bicho deixou.
    snprintf(buf, sizeof(buf), "NIVEL %d", n);
    g->setTextColor(g_stale ? MUTED : LARANJA);
    g->setTextSize(3);
    g->setCursor((W - (int)strlen(buf) * 18) / 2, emPe ? 150 : 96);
    g->print(buf);

    // Uma linha so, com o periodo e o que falta. Sao as duas coisas que dao
    // contexto ao numero de cima — desde quando conta, e quanto ainda falta —
    // e uma linha basta: ela e legenda do titulo, e nao um bloco de texto.
    {
        char per[24] = "";
        char fal[32] = "";
        if (!s.vitalicio.desde.empty())
            snprintf(per, sizeof(per), "desde %s",
                     dataDiaPrimeiro(s.vitalicio.desde).c_str());
        if (n < 99)   // no 99 nao ha proximo, e "faltam 0 XP" seria ruido
            snprintf(fal, sizeof(fal), "faltam %d XP para o %d",
                     (int)(nivel::xpDoNivel(n + 1) -
                           nivel::xp(s.vitalicio.turnos, s.vitalicio.costUsd,
                                     horas)),
                     n + 1);
        if (per[0] || fal[0]) {
            snprintf(buf, sizeof(buf), "%s%s%s",
                     per, (per[0] && fal[0]) ? "  -  " : "", fal);
            g->setTextColor(MUTED);
            g->setTextSize(1);
            g->setCursor((W - (int)strlen(buf) * 6) / 2, emPe ? 192 : 138);
            g->print(buf);
        }
    }

    // ---- As colunas ----
    // As colunas sobem: sem o bicho, o terco do meio da pagina esta livre.
    const int TOPO = emPe ? 240 : 180;

    snprintf(buf, sizeof(buf), "%ld", s.vitalicio.turnos);
    rotuloValor(g, 14, TOPO, "TURNOS", buf, 2, false);

    snprintf(buf, sizeof(buf), "%.0fh", horas);
    rotuloValor(g, 14, TOPO + 42, "CONVIVIO", buf, 1, false);

    snprintf(buf, sizeof(buf), "%.0f", s.vitalicio.costUsd);
    rotuloValor(g, W - 14, TOPO, "US$", buf, 2, true);

    const float xpAgora = nivel::xp(s.vitalicio.turnos, s.vitalicio.costUsd, horas);

    // O XP DE HOJE, e nao o total. O total ja esta dito de duas formas nesta
    // tela — pelo proprio nivel e pela barra de progresso —, enquanto o ganho do
    // dia e o unico numero daqui que muda enquanto se olha.
    //
    // Com "+" na frente para deixar claro que e ganho, e nao acumulado. Zerado
    // no comeco do dia (ou antes da primeira marca) mostra "+0", que e uma
    // resposta legitima: hoje ainda nao rendeu nada.
    snprintf(buf, sizeof(buf), "+%d", (int)xpDoDia);
    rotuloValor(g, W - 14, TOPO + 42, "XP HOJE", buf, 1, true);

    // ---- A barra de XP no rodape, que nesta pagina esta livre ----
    // O trio de bichos nao e desenhado aqui (ver drawFooter), entao ela tem a
    // largura toda em vez de disputar espaco com eles.
    //
    // Em y=278 de proposito: a fileira do rodape comeca em 289 (as bolinhas de
    // pagina tem centro em SCREEN_H-26 e raio 5, e a idade do dado imprime em
    // SCREEN_H-30). Descer a barra ate 292, como na primeira versao, a fazia
    // passar POR BAIXO das bolinhas e do aviso de contato.
    const float xpDe  = nivel::xpDoNivel(n);
    const float xpAte = nivel::xpDoNivel(n + 1);
    const int   barW  = W - 28;
    const int   barY  = emPe ? 430 : 278;
    float frac = 1.0f;
    if (n < 99 && xpAte > xpDe) {
        frac = (xpAgora - xpDe) / (xpAte - xpDe);
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
    }
    const int barH = emPe ? 8 : 6;
    g->fillRect(14, barY, barW, barH, TRACK);
    g->fillRect(14, barY, (int)(barW * frac), barH, g_stale ? MUTED : LARANJA);
}

// ====================================================================
//  A TELA NOVA DEITADA (pagina 0 em paisagem)
// ====================================================================
//
// A irma da tela nova em pe, e o que muda entre as duas e o que a orientacao
// permite. SEM A FILEIRA DE BICHOS: deitado a faixa barata do flush e de
// COLUNAS, entao um bicho no meio da tela custaria o quadro inteiro (~64 ms)
// em vez dos ~9 do prefixo — e o espaco deles vira tamanho de anel, que era o
// pedido. O que sobra do topo cabe a temperatura, que em pe nao tinha largura.
//
// Os cartoes de sessao viram uma FILEIRA de cinco em vez de uma pilha: deitado
// ha 480 px de largura e 320 de altura, o inverso do retrato, e empilhar
// deixaria uma coluna estreita com sobra dos dois lados.
const int L_MARG    = 14;
const int L_HDR_H   = 42;
// 70 e o maior raio que cabe, e ele nao serve: cada metade tem 226 px e o
// grupo (anel + vao + texto) mediria 232 — o anel sairia 3 px por fora da
// margem, dos DOIS lados. 70 com texto de 72 fecharia em 222, mas ai o
// instante da semana ("Sab 23h", 58 px) fica sem folga nenhuma. 70/76 fecha
// exatamente em 226, com folga ZERO entre os dois grupos, que na tela lê como
// aneis se tocando.
const int L_ANEL_R1 = 68;                  // diametro 136, contra os 128 de antes
const int L_ANEL_R2 = 52;                  // 16 px de espessura
// O VERTICAL INTEIRO, de cima para baixo, e ele e apertado de proposito:
//
//   0..42     cabecalho (nome, temperatura, hora)
//   50..186   os dois aneis (CY 118, raio 68)
//   195..203  a linha do Fable
//   210..252  a fileira de cartoes
//   267..312  a turma, no canto inferior esquerdo
//
// A turma pisa em SCREEN_H-8 e mede ~45 px de altura no tema South Park, entao
// os cartoes tem que terminar antes de 267 — e e por isso que eles perderam
// altura quando os bichos voltaram para o rodape.
const int L_ANEL_CY = 118;
const int L_FABLE_Y = 196;
const int L_CARD_Y  = 210;
// 42 e nao 52: os aneis cresceram e a turma voltou ao rodape, e o espaco saiu
// daqui nas duas vezes. O cartao deitado tem dois andares (nome em cima,
// provedor e turno embaixo) mais o trilho na base, e 42 e onde os tres ainda
// cabem sem se encostar.
const int L_CARD_H  = 42;
// O TETO de cartoes na fileira, e nao a divisao dela: a largura sai da
// quantidade REAL de sessoes (ver drawDeitadaNova), entao com tres na tela cada
// uma leva um terco da linha. Este numero so decide onde comeca o "+N".
const int L_CARD_MAX = 6;
// A coluna de texto ao lado do anel. 76 px comportam o pior prazo real
// ("12h05m", 59 px) e o instante em 24h com o dia ("Sab 23h", 58 px); acima
// disso a cascata do `instanteCurto` corta a hora e sobra o dia.
const int L_TXT_W   = 76;
const int L_ANEL_GAP = 10;

void drawAnelDeitado(Arduino_Canvas *g, int cx, int txtX, const char *titulo,
                     const Metric &m, int janelaSeg, const Clock *rel) {
    g->fillArc(cx, L_ANEL_CY, L_ANEL_R1, L_ANEL_R2, 0, 360, TRACK);

    if (m.known) {
        const uint16_t cor = colorOf(m.level);
        const int rp = ritmoPct(m, janelaSeg);
        const float pct = (float)(m.pct < 0 ? 0 : (m.pct > 100 ? 100 : m.pct));
        auto arco = [&](float de, float ate, uint16_t c) {
            if (ate > de)
                g->fillArc(cx, L_ANEL_CY, L_ANEL_R1, L_ANEL_R2,
                           270.0f + de * 3.6f, 270.0f + ate * 3.6f, c);
        };
        if (rp < 0) {
            arco(0, pct, cor);
        } else if (pct > (float)rp) {
            arco(0, pct, cor);
            g->fillArc(cx, L_ANEL_CY, L_ANEL_R1, L_ANEL_R2,
                       270.0f + rp * 3.6f - 2.0f, 270.0f + rp * 3.6f, TRACK);
        } else {
            arco(0, (float)rp, misturar(TRACK, cor, RITMO_FOLGA, 100));
            arco(0, pct, cor);
        }
    }

    // O INSTANTE E ESCOLHIDO ANTES DE A FONTE MUDAR, e a ordem e o conserto:
    // a cascata dele mede com `larguraDejaVu`, que devolve o canvas a fonte
    // EMBUTIDA no fim. Chamada no meio do bloco da DejaVu, ela derrubava a
    // fonte no instante seguinte — e so na SEMANA, que e o unico ramo que
    // chega a medir. Na tela, o "Sab 23h" saia em corpo de grade e 10 px mais
    // baixo que o "12:20h" do anel vizinho, porque na embutida o cursor e o
    // TOPO e na DejaVu e a BASELINE. E o mesmo cuidado que drawAnelLimite ja
    // tomava sem dizer por que.
    const std::string inst = instanteCurto(g, m, rel, L_TXT_W);

    // TUDO CENTRADO NA LINHA DO ANEL, e as alturas sao as UTEIS: a fonte
    // embutida deixa a ultima linha da celula vazia (corpo 4 pinta 28 px, e nao
    // 32) e a DejaVu e posicionada pela baseline, com 11 px de glifo acima
    // dela. Escritas a mao, essas duas diferencas punham o miolo 2 px abaixo do
    // centro e o par lateral 1 acima — pouco em cada item, visivel no conjunto.
    const int PCT_H  = 7 * 4;      // corpo 4: 7 linhas de glifo, 4 px cada
    const int ROT_H  = 7;          // corpo 1
    const int DEJA_H = 11;         // glifo da DejaVu acima da baseline
    const int VAO    = 9;
    const int mioloY = L_ANEL_CY - (PCT_H + VAO + ROT_H) / 2;
    const int latY   = L_ANEL_CY - (DEJA_H + VAO + DEJA_H) / 2 + DEJA_H;

    // O percentual e o ROTULO moram os dois no miolo: deitado o anel tem 98 px
    // de vao interno, e o rotulo ali dentro dispensa a linha que em pe tinha de
    // ficar ao lado. O corpo 4 e o degrau que se le do outro lado da sala, que
    // e a distancia deste painel deitado.
    const std::string pct = pctText(m);
    g->setTextColor(!m.known || m.memoria
                        ? MUTED : (g_stale ? MUTED : colorOf(m.level)));
    g->setTextSize(4);
    g->setCursor(cx - (int)pct.size() * 12, mioloY);
    g->print(pct.c_str());
    g->setTextSize(1);
    g->setTextColor(MUTED);
    g->setCursor(cx - (int)strlen(titulo) * 3, mioloY + PCT_H + VAO);
    g->print(titulo);

    // Prazo e instante ao lado, empilhados e centrados na altura do anel. As
    // duas linhas nascem na MESMA altura nos dois aneis (L_ANEL_CY e comum),
    // entao "3h15m" e "1d13h" ficam na mesma linha da tela, e os instantes
    // logo abaixo — ler os dois limites vira uma comparacao horizontal.
    g->setFont(&DejaVuSans7pt7b);
    g->setTextSize(1);
    g->setTextColor(m.known && !m.memoria ? fgColor() : MUTED);
    g->setCursor(txtX, latY);
    g->print(m.known && !m.resets.empty() ? m.resets.c_str() : "-");

    if (!inst.empty()) {
        g->setTextColor(MUTED);
        g->setCursor(txtX, latY + VAO + DEJA_H);
        g->print(inst.c_str());
    }
    g->setFont();
}

// Um cartao de sessao na fileira. Mesma linguagem do cartao da tela em pe —
// faixa de estado na borda, nome, icone da CLI, turno e o contexto como trilho
// na base — so que mais curto: deitado a fileira tem cinco em 452 px, entao
// cada um leva ~86 e os chips nao cabem. O que sai sao eles; o que fica e o
// que se le de longe.
void drawCartaoDeitado(Arduino_Canvas *g, int x, int y, int w, const Agent &a) {
    g->fillRoundRect(x, y, w, L_CARD_H, 8, SUBCARD);
    drawNovoLinha(g, a.novo, x, y, w, L_CARD_H, SUBCARD, 8);
    drawAvisoLinha(g, a.done, x, y, w, L_CARD_H, SUBCARD, 8);
    drawFaixaEstado(g, x, y, 3, L_CARD_H, 8, corDoEstado(a.state, a.done));

    const int tx  = x + 10;
    const int dir = x + w - 8;

    // O nome, cortado por medida ate a borda: a DejaVu e proporcional e contar
    // caractere deixaria uns curtos e outros passando do cartao.
    g->setFont(&DejaVuSans7pt7b);
    g->setTextSize(1);
    g->setTextColor(fgColor());
    std::string nome = a.repo;
    int16_t x1, y1; uint16_t nw, nh;
    while (!nome.empty()) {
        g->getTextBounds(nome.c_str(), 0, 0, &x1, &y1, &nw, &nh);
        if ((int)nw <= dir - tx) break;
        nome.pop_back();
    }
    g->setCursor(tx, y + 17);
    g->print(nome.c_str());
    g->setFont();

    // O contexto EM CIMA DO TRILHO QUE ELE EXPLICA, e nao na linha do nome.
    //
    // Ali ele era escrito por cima: o nome e cortado pela largura INTEIRA do
    // cartao (e tem que ser — e ele o dado da linha), entao um repo comprido
    // chegava embaixo do numero. Nesta linha o vizinho e o turno, que e curto e
    // pode ceder, e o numero passa a ficar logo acima da barra que ele mede.
    char pb[8];
    if (a.hasContext) snprintf(pb, sizeof(pb), "%d%%", a.contextPct);
    else              snprintf(pb, sizeof(pb), "-");
    const int pctX = dir - (int)strlen(pb) * 6;
    g->setTextColor(a.hasContext ? MUTED : TRACK);
    g->setTextSize(1);
    g->setCursor(pctX, y + 26);
    g->print(pb);

    // O icone do fornecedor e o turno na mesma linha: quem esta rodando e ha
    // quanto tempo, que e a leitura desta fileira.
    int cx = tx;
    const provedores::Icone ic = provedores::iconeDe(a.agent);
    if (ic.w) {
        drawIconeProvedor(g, cx, y + 24 + (12 - ic.h) / 2, ic,
                          g_stale ? MUTED : provedores::corDe(a.agent), SUBCARD);
        cx += ic.w + 5;
    }
    if (a.turnoS >= 0) {
        const std::string t = formatTurno(a.turnoS);
        const uint16_t cor = a.state == AgentState::Working ? H_WORKING
                           : a.state == AgentState::Blocked ? H_BLOCKED
                                                            : MUTED;
        // O turno CEDE ao contexto quando o cartao aperta: e o dado mais barato
        // dos dois nesta linha, e sumir e melhor do que escrever por cima — que
        // e exatamente o defeito que trouxe o numero para ca.
        if (cx + (int)t.size() * 6 <= pctX - 4) {
            g->setTextColor(g_stale ? MUTED : cor);
            g->setTextSize(1);
            g->setCursor(cx, y + 26);
            g->print(t.c_str());
        }
    }

    const int tw = dir - tx;
    g->fillRoundRect(tx, y + L_CARD_H - 8, tw, 2, 1, TRACK);
    if (a.hasContext && a.contextPct > 0) {
        int fill = tw * a.contextPct / 100;
        if (fill < 2) fill = 2;
        g->fillRoundRect(tx, y + L_CARD_H - 8, fill, 2, 1, colorOf(a.level));
    }
}

// O CABECALHO DE TODAS AS TELAS DEITADAS: o nome digitando a esquerda, a
// temperatura e a hora a direita.
//
// Ele nasceu na tela nova e virou o cabecalho de todas a pedido — e a troca
// vale por si: o bicho que morava no canto esquerdo era enfeite ocupando o
// lugar onde a marca deveria estar, e a marca ja tinha que ser escrita ao lado
// dele, gastando duas vezes a mesma largura. Sem ele, o nome comeca na margem
// e sobra espaco para a hora crescer de corpo 3 para 4.
//
// O CANTO CONTINUA SENDO O BOTAO DE GIRAR (ver alvoIconeCabecalho): o alvo
// cobre o nome inteiro, entao o gesto nao mudou de lugar — mudou de desenho.
void drawHeaderDeitado(Arduino_Canvas *g, const Status &s, int staleSeconds) {
    // O limite do nome e onde comeca o que vem da direita. Medido, e nao fixo:
    // com temperatura ele e mais apertado que sem.
    int esq = SCREEN_W - L_MARG;
    if (s.clock.known) esq -= (int)s.clock.hm.size() * 6 * 4;
    if (s.weather.known) esq -= 52;
    drawNomeCabecalho(g, esq - 8);

    if (s.clock.known) {
        const int w = (int)s.clock.hm.size() * 6 * 4;
        g->setTextColor(fgColor());
        g->setTextSize(4);
        g->setCursor(SCREEN_W - L_MARG - w, 6);
        g->print(s.clock.hm.c_str());
        if (s.weather.known)
            drawTemp(g, SCREEN_W - L_MARG - w - 52, 12, s.weather.temp, MUTED, 2);
    } else if (s.weather.known) {
        drawTemp(g, SCREEN_W - L_MARG - 52, 12, s.weather.temp, MUTED, 2);
    }

    // O selo VIA vai ABAIXO da linha, encostado na margem direita: no
    // cabecalho antigo ele disputava a faixa com o titulo e o clima e sumia em
    // silencio quando nao cabia. Aqui ele tem lugar proprio.
    if (s.viaSlave) {
        const int sw = (s.tag.empty() ? 11 : (int)s.tag.size() + 4) * 6 + 10;
        drawSeloVia(g, SCREEN_W - L_MARG - sw, L_HDR_H + 4, s.tag);
    }

    g->drawFastHLine(L_MARG, L_HDR_H, SCREEN_W - L_MARG * 2,
                     staleSeconds > 0 ? C_YELL : TRACK);
}

void drawDeitadaNova(Arduino_Canvas *g, const Status &s, int staleSeconds,
                     int opcaoArmada) {
    g->fillScreen(BG);
    drawHeaderDeitado(g, s, staleSeconds);

    if (s.bloqueio.known) {
        drawPerguntaP0(g, s, opcaoArmada);
        drawFooter(g, s, 0, staleSeconds);
        return;
    }

    // ---- Os dois aneis, cada grupo centrado na SUA metade ----
    // Grupo = anel + vao + coluna de texto. Centrar cada um na metade que lhe
    // cabe distribui a linha inteira sem que nenhum encoste na borda — que era
    // o que acontecia com o anel colado na margem e o texto correndo para
    // dentro da metade vizinha.
    const int metade = (SCREEN_W - L_MARG * 2) / 2;
    const int grupo  = L_ANEL_R1 * 2 + L_ANEL_GAP + L_TXT_W;
    const int folga  = (metade - grupo) / 2;
    const int esq    = L_MARG + folga;
    const int dir    = L_MARG + metade + folga;
    drawAnelDeitado(g, esq + L_ANEL_R1, esq + L_ANEL_R1 * 2 + L_ANEL_GAP,
                    "SESSAO", s.session, JANELA_5H, &s.clock);
    drawAnelDeitado(g, dir + L_ANEL_R1, dir + L_ANEL_R1 * 2 + L_ANEL_GAP,
                    "SEMANA", s.week, JANELA_7D, &s.clock);

    // ---- A linha do Fable, centrada ----
    if (s.fableKnown) {
        const uint16_t cor = colorOf(s.fableLevel);
        const int lw = 300;
        const int x0 = (SCREEN_W - lw) / 2;
        g->setTextColor(MUTED);
        g->setTextSize(1);
        g->setCursor(x0, L_FABLE_Y - 1);
        g->print("FABLE");

        char pb[8];
        snprintf(pb, sizeof(pb), "%d%%", s.fablePct);
        const int numX = x0 + lw - 4 * 6;
        g->setTextColor(cor);
        g->setCursor(numX + 4 * 6 - (int)strlen(pb) * 6, L_FABLE_Y - 1);
        g->print(pb);

        const int bx = x0 + 5 * 6 + 8;
        const int bw = numX - 8 - bx;
        g->fillRoundRect(bx, L_FABLE_Y + 1, bw, 3, 1, CARD);
        int fill = bw * (s.fablePct < 0 ? 0 : (s.fablePct > 100 ? 100 : s.fablePct)) / 100;
        if (fill > 0 && fill < 3) fill = 3;
        if (fill > 0) g->fillRoundRect(bx, L_FABLE_Y + 1, fill, 3, 1, cor);
    }

    // ---- A fileira de sessoes ----
    // Bloqueado primeiro, como na lista em pe: a fileira corta no que cabe, e
    // se a sessao que sobrasse fosse a bloqueada o painel esconderia a unica
    // linha que pede acao.
    // A FILEIRA OCUPA A LINHA INTEIRA, dividida pela quantidade REAL de
    // sessoes: com tres na tela cada uma leva um terco, e nao um quinto com
    // dois vaos mortos na direita. O ultimo cartao termina na margem por
    // construcao — a sobra da divisao inteira vai para ele, em vez de virar um
    // degrau visivel no fim da linha.
    const std::vector<int> ordem = grupos::ordemComBloqueadosNoTopo(s.agents);
    const int n = (int)ordem.size() < L_CARD_MAX ? (int)ordem.size() : L_CARD_MAX;
    const int faixa = SCREEN_W - L_MARG * 2;
    const int gap = 6;
    for (int i = 0; i < n; i++) {
        const int x0 = L_MARG + faixa * i / n;
        const int x1 = L_MARG + faixa * (i + 1) / n;
        drawCartaoDeitado(g, x0, L_CARD_Y, x1 - x0 - (i < n - 1 ? gap : 0),
                          s.agents[ordem[i]]);
    }

    if (n == 0) {
        g->setTextColor(MUTED);
        g->setTextSize(1);
        g->setCursor(L_MARG, L_CARD_Y + 20);
        g->print("nenhuma sessao ativa");
    }

    // O rodape e o de sempre, MENOS os bichos: `drawFooter` os desenha, e aqui
    // eles nao existem. As bolinhas e a idade do dado vem dele.
    drawFooter(g, s, 0, staleSeconds);

    // As que nao couberam, na ponta direita do ULTIMO cartao: o rodape voltou a
    // ser da turma, e escrever ali passaria por cima dos bichos.
    if ((int)ordem.size() > n) {
        char buf[12];
        snprintf(buf, sizeof(buf), "+%d", (int)ordem.size() - n);
        g->setTextColor(MUTED);
        g->setTextSize(1);
        g->setCursor(SCREEN_W - L_MARG - (int)strlen(buf) * 6,
                     L_CARD_Y + L_CARD_H + 4);
        g->print(buf);
    }
}

void drawStatus(const Status &s, int page, const std::string &selectedId,
                int staleSeconds, bool botaoArmado,
                const nivel::Estado &nivelNoCartao, float xpDoDia,
                int opcaoArmada, bool telaToken, const char *telaReset,
                bool telaOffline) {
    g_stale = staleSeconds > 0;
    Arduino_Canvas *g = display::canvas();

    // EM PE e outra tela, e nao a mesma tela estreita: cabecalho, ordem
    // vertical e formato dos cards sao proprios. Sai daqui antes de tudo para
    // que nenhuma geometria da paisagem (que e escrita em constantes de
    // SCREEN_W) escape para la.
    if (display::retrato()) {
        // Em pe as paginas sao CINCO: 0 = tela nova, 1 = principal,
        // 2 = contexto, 3 = Clawd, 4 = nivel (ver ui::PAGES).
        if (telaToken || telaReset || telaOffline) {
            // As telas de bicho tomam o painel em qualquer pagina — o caminho
            // da principal ja sabe desenha-las e sai antes de tudo.
            drawRetrato(g, s, staleSeconds, opcaoArmada, telaToken, telaReset,
                        telaOffline);
        } else if (page == 0) {
            drawRetratoNova(g, s, staleSeconds, opcaoArmada);
        } else if (page == 1) {
            drawRetrato(g, s, staleSeconds, opcaoArmada, false, nullptr,
                        false);
        } else {
            // A limpeza e incondicional de novo. A quarta pagina pintava a
            // tela inteira com o fundo animado do nivel e pulava o
            // `fillScreen`; esse fundo saiu — era arte por faixa de dezena
            // (20 arquivos), e a particao de flash so cabe o que o painel usa
            // em toda tela. Ver src/assets.h.
            g->fillScreen(BG);
            drawHeaderRetrato(g, s, staleSeconds, page == 3);
            // Bloqueio em qualquer pagina em pe vira a pergunta em tela
            // cheia: em pe o painel e de relance, e a pergunta e o unico
            // evento que pede acao.
            if (s.bloqueio.known) {
                // A pergunta toma a tela, mas a turma FICA — e ela vai para o
                // topo, no lugar da primeira tela, e nao onde a pagina de
                // contexto a poe: dali para baixo e tudo da pergunta. Sem
                // isto, abrir um bloqueio fora da P0 fazia os quatro sumirem.
                drawTurmaRetrato(g);
                drawPerguntaP0(g, s, opcaoArmada);
            } else if (page == 2) {
                // A turma no MESMO lugar da tela inicial, e pela mesma razao:
                // so no topo a faixa barata do flush a alcanca, e so assim ela
                // anima na cadencia dos outros bichos (ver R1_C1_Y).
                drawTurmaRetrato(g);
                drawPageContextRetrato(g, s, indexOfId(s, selectedId),
                                       botaoArmado, opcaoArmada);
            } else if (page == 3) {
                drawPageClawd(g, s);
            } else {
                drawPageNivel(g, s, nivelNoCartao, xpDoDia);
            }
            drawBolinhasRetrato(g, page);
            drawStatusRetrato(g, s, staleSeconds);
        }
        display::flush();
        return;
    }

    // A limpeza e incondicional de novo. A quarta pagina pintava a tela inteira
    // com o fundo animado do nivel e pulava o `fillScreen` para nao gastar os
    // 15,7 ms medidos escrevendo pixel que o fundo cobria no instante seguinte;
    // esse fundo saiu — era arte por faixa de dezena (20 arquivos), e a
    // particao de flash so cabe o que o painel usa em toda tela. Ver
    // src/assets.h.
    // A TELA NOVA DEITADA tem cabecalho proprio (o nome digitando no lugar do
    // mago e do titulo), entao ela sai antes do `drawHeader` comum.
    if (page == 0) {
        drawDeitadaNova(g, s, staleSeconds, opcaoArmada);
        display::flush();
        return;
    }

    g->fillScreen(BG);
    // O MESMO cabecalho da tela nova em TODAS as paginas deitadas: nome
    // digitando, temperatura e hora. O antigo (mago no canto, titulo ao lado,
    // hora em corpo 3) saiu inteiro — ver drawHeaderDeitado.
    drawHeaderDeitado(g, s, staleSeconds);

    // Tela de OFFLINE so quando nao ha agente NENHUM. Com `online: false` mas
    // agentes na lista, eles estao apenas ociosos (bloqueados) — esconde-los
    // seria repetir o bug que essa marcacao veio corrigir.
    // `!doCache` porque este aviso e uma AFIRMACAO sobre o presente — "nao ha
    // sessao ativa" —, e um retrato restaurado do cartao nao tem como afirmar
    // isso: ele nem guarda lista de agentes. Sem esta guarda, o cache carregava
    // e a tela o escondia, trocando os limites que ele acabou de trazer por uma
    // frase sobre um presente que ninguem observou.
    //
    // E ele so come a PRIMEIRA pagina. As de contexto, do Clawd e do nivel nao
    // dependem de sessao nenhuma, e engoli-las junto era ir alem do que o aviso
    // sabe.
    if (!s.online && s.agents.empty() && !s.doCache && page == 1) {
        g->setTextColor(MUTED);
        g->setTextSize(3);
        g->setCursor(20, 140);
        g->print("CLAUDIO OFFLINE");
        g->setTextSize(1);
        g->setCursor(20, 180);
        g->print("nenhuma sessao ativa");
    } else if (page == 1) {
        drawPageLimits(g, s, opcaoArmada);
    } else if (page == 2) {
        drawPageContext(g, s, indexOfId(s, selectedId), botaoArmado, opcaoArmada);
    } else if (page == 3) {
        drawPageClawd(g, s);
    } else {
        drawPageNivel(g, s, nivelNoCartao, xpDoDia);
    }

    drawFooter(g, s, page, staleSeconds);
    display::flush();
}

bool tickNome(uint32_t nowMs) {
    // O primeiro tick so arma o relogio: sem isto a primeira letra nasceria
    // com atraso zero e o inicio do ciclo dependeria de quando o boot caiu.
    if (!g_nomeMs) { g_nomeMs = nowMs ? nowMs : 1; return false; }

    if (g_nomeLetras < NOME_LEN) {
        if (nowMs - g_nomeMs < NOME_LETRA_MS) return false;
        g_nomeMs = nowMs;
        g_nomeLetras++;
        g_nomeCursor = true;
        g_nomeFase   = 0;
        return true;
    }

    // Nome completo: o cursor pisca por NOME_PISCADAS meias-fases e o ciclo
    // recomeca do zero — e a animacao 1 da prancheta, "escreve e pisca".
    if (nowMs - g_nomeMs < NOME_CURSOR_MS) return false;
    g_nomeMs = nowMs;
    if (++g_nomeFase >= NOME_PISCADAS) {
        g_nomeLetras = 0;
        g_nomeFase   = 0;
        g_nomeCursor = true;
    } else {
        g_nomeCursor = !g_nomeCursor;
    }
    return true;
}

void redrawTopoNova(const Status &s, int staleSeconds) {
    g_stale = staleSeconds > 0;
    Arduino_Canvas *g = display::canvas();

    // A faixa do nome e limpa INTEIRA (nome + celula do cursor): o quadro novo
    // pode ter menos letras que o anterior, e o blit nao apaga nada sozinho.
    // A limpeza para no MESMO limite do desenho — passar dele apagaria o
    // primeiro digito da hora, que este redesenho nao repinta.
    const int y   = (R_HDR_LINHA - 8 * NOME_SZ) / 2;
    const int lim = limiteDoNome(s);
    if (lim > R_MARG)
        g->fillRect(R_MARG, y, lim - R_MARG, 8 * NOME_SZ, BG);
    drawNomeCabecalho(g, lim);

    // DEITADO nao ha fileira: a tela nova em paisagem nao tem bichos, e o
    // prefixo aqui e de COLUNAS (ver display::flushPrefix). Enviar ate o fim do
    // nome custa ~18 ms — mais que os ~11 do retrato, e ainda muito abaixo dos
    // ~64 de um quadro inteiro, que era o que a digitacao estava pagando por
    // nao ter caminho proprio deste lado.
    if (!display::retrato()) {
        display::flushPrefix(lim + 4);
        return;
    }

    // A mesma faixa da turma do redrawBadge, com a caixa do CENTRO na conta —
    // o bicho do cabecalho pode ser mais baixo ou mais alto que o trio.
    const int cw = clawd::crewW(R_CARD_W);
    const int ch = clawd::crewComCentroH();
    if (cw && ch) {
        g->fillRect(R_MARG, R_TURMA_CHAO - ch, cw, ch, BG);
        clawd::drawCrewComCentroInto(g, R_MARG, R_TURMA_CHAO, R_CARD_W);
    }
    display::flushPrefix(R_TOPO_FIM);
}

void redrawBadge(const Status &s, int staleSeconds, bool semTurma) {
    // O Status nao e lido aqui: a animacao ja foi escolhida por clawd::select
    // quando o estado chegou. O parametro fica pela simetria com drawStatus, e
    // porque o esmaecimento depende de staleSeconds.
    (void)s;
    g_stale = staleSeconds > 0;

    Arduino_Canvas *g = display::canvas();

    // EM PE a faixa barata e o TOPO, e o bicho do cabecalho e a turma ja foram
    // postos la de proposito (ver drawRetrato). Entao o prefixo aqui e fixo:
    // vai do topo ate a divisoria da turma, e cobre os dois de uma vez.
    if (display::retrato()) {
        const int iw = clawd::iconW(false);
        const int ih = clawd::iconH(false);
        if (iw && ih) {
            g->fillRect(R_MARG, R_HDR_BASE - ih, iw, ih, BG);
            clawd::drawIconInto(g, R_MARG, R_HDR_BASE - ih, false);
        }
        // `semTurma` e a tela do Token (e a do Reset): ali a faixa do topo
        // pertence a cabeca do bicho. Nas paginas normais em pe a turma mora
        // sempre no mesmo lugar, entao nao ha caso por pagina.
        if (!semTurma) {
            // A MESMA faixa do desenho completo (ver drawTurmaRetrato): limpar
            // a fileira compacta e redesenhar a espalhada deixaria resto do
            // quadro anterior na direita.
            const int cw = clawd::crewW(R_CARD_W);
            const int ch = clawd::crewH();
            if (cw && ch) {
                g->fillRect(R_MARG, R_TURMA_CHAO - ch, cw, ch, BG);
                clawd::drawCrewInto(g, R_MARG, R_TURMA_CHAO, R_CARD_W);
            }
        }
        display::flushPrefix(semTurma ? R_HDR_LINHA + 2 : R_TOPO_FIM);
        return;
    }

    int colunas = 0;

    // DEITADO O CANTO DO CABECALHO NAO E MAIS DO BICHO. Ele era desenhado aqui
    // em (14, 40) junto com a turma, porque os dois moravam na mesma tira do
    // prefixo e um envio so levava ambos. Com o cabecalho novo (nome digitando
    // a partir da margem, ver drawHeaderDeitado) esse canto passou a ser do
    // NOME — e este redesenho continuava pintando o bicho por cima dele, e
    // ainda limpava a caixa antes, comendo as primeiras letras. O desenho
    // completo nao o mostrava mais; so este caminho barato o ressuscitava, e
    // por isso ele so aparecia nas paginas que animam.
    //
    // O bicho segue vivo EM PE (ramo acima) e na tela de reset, onde o canto
    // ainda e dele.

    const int chao = SCREEN_H - 8;

    const int w = clawd::crewW();
    const int h = clawd::crewH();
    if (w && h) {
        // Limpa a faixa INTEIRA antes de blitar. O blit pula os pixels
        // transparentes, entao sem limpar os quadros se acumulam uns sobre os
        // outros. E a caixa maxima da fileira, e nao a do quadro atual, para
        // que trocar de animacao nao deixe resto da anterior.
        g->fillRect(14, chao - h, w, h, BG);
        clawd::drawCrewInto(g, 14, chao);
        const int c = prefixColumns(14, w, 6, SCREEN_W);
        if (c > colunas) colunas = c;
    }

    display::flushPrefix(colunas);
}

// Um quadro da danca do bicho da vez, e nada mais.
//
// O irmao do `redrawBadge`, pela mesma razao e com a mesma conta. A medida saiu
// da danca antiga do Cartman, de 24 quadros a 70 ms, em que o desenho completo
// nao cabia no intervalo: 15,7 ms de `fillScreen`, o cabecalho e os dois textos
// repintados a toa, e 48 ms de flush de tela inteira. A animacao andava um
// quadro por volta do laco, a ~11 quadros por segundo e com o atraso variando
// conforme o resto da volta — e jitter, na tela, se le como travamento.
//
// Aqui so a caixa do bicho e repintada, e o envio para no pe dele: 347 a 354
// linhas em vez de 480. Os dois textos de baixo nao entram no prefixo porque sao
// ESTATICOS — eles ficam no painel desde o desenho completo da abertura, que e
// quem tem de acontecer antes deste caminho valer.
void redrawReset() {
    const int w = clawd::resetW(), h = clawd::resetH();
    if (!w || !h) return;

    Arduino_Canvas *g = display::canvas();
    const int x = (PANEL_W - w) / 2;

    // O bicho do cabecalho vem JUNTO, e nao pelo `redrawBadge`: ele mora dentro
    // do prefixo que este envio ja manda, entao aqui ele custa o decode e mais
    // nada. Pelo outro caminho custaria um envio proprio de 50 linhas.
    const int iw = clawd::iconW(false), ih = clawd::iconH(false);
    if (iw && ih) {
        g->fillRect(R_MARG, R_HDR_BASE - ih, iw, ih, BG);
        clawd::drawIconInto(g, R_MARG, R_HDR_BASE - ih, false);
    }
    // Opaco, e por isso sem limpar a caixa antes: o decode ja entrega o vazio
    // pintado de BG (ver clawd::drawResetOpacoInto).
    clawd::drawResetOpacoInto(g, x, R_RESET_Y, BG);
    display::flushPrefix(R_RESET_Y + h);
}

int agentIndexAt(const Status &s, int x, int y) {
    // Em pe o menu lateral nao existe: a lista mora embaixo, e com a pergunta
    // em tela cheia ela nem foi desenhada.
    if (display::retrato()) {
        if (s.bloqueio.known) return -1;
        const int n = (int)s.agents.size() < R1_LST_MAX ? (int)s.agents.size()
                                                        : R1_LST_MAX;
        const int i = (y - (R1_LST_Y0 - 2)) / R1_LST_PASSO;
        if (y < R1_LST_Y0 - 2 || i >= n) return -1;
        return i;
    }
    if (x < MENU_X) return -1;
    const int i = (y - MENU_Y0) / MENU_ROW;
    if (y < MENU_Y0 || i >= menuVisible((int)s.agents.size())) return -1;
    return i;
}

int cartaoSessaoRetratoAt(const Status &s, int x, int y) {
    if (!display::retrato() || s.bloqueio.known || s.agents.empty()) return -1;
    // Fora das margens do card nao ha cartao — o desenho vai de R_MARG a
    // R_MARG + R_CARD_W, e o alvo acompanha o desenho.
    if (x < R_MARG || x >= R_MARG + R_CARD_W) return -1;

    // Quantos couberam de fato: o que virou "+N" nao esta na tela e nao pode
    // responder ao dedo. A conta e a mesma de drawSessoesRetrato.
    const int cabem = (R_SES_FIM - R_SES_Y - 8) / R_SES_PASSO;
    const int n = (int)s.agents.size() < cabem ? (int)s.agents.size() : cabem;
    return cartaoSessaoAt(y, n);
}

bool cleanButtonAt(const Status &s, const std::string &selectedId, int x, int y) {
    // A area sensivel vale o que o desenho vale. Sem esta pergunta, o retangulo
    // continuaria armando o botao depois de uma compactacao que o fez sumir da
    // tela — um controle invisivel respondendo ao toque.
    if (!botaoVisivel(s, indexOfId(s, selectedId))) return false;
    // Com o bloqueio na tela o botao de limpeza nem foi desenhado. Sem esta
    // guarda o retangulo dele continuaria armado por baixo dos botoes de opcao.
    const int i = indexOfId(s, selectedId);
    if (i >= 0 && i < (int)s.agents.size() && bloqueioDe(s, s.agents[i]))
        return false;
    if (display::retrato()) {
        if (s.bloqueio.known) return false;
        // Alvo com folga vertical: a pilula tem 18 px de altura, e dedo em
        // painel de parede nao acerta 18 px. A folga nao encosta na lista, que
        // comeca 24 px abaixo.
        const int px = PANEL_W - R_MARG - R1_PAD - R1_PIL_TERM - 6 - R1_PIL_LIMPAR;
        return x >= px && x < px + R1_PIL_LIMPAR &&
               y >= R1_PIL_Y - 8 && y < R1_PIL_Y + R1_PIL_H + 8;
    }
    return x >= BTN_X && x < BTN_X + BTN_W && y >= BTN_Y && y < BTN_Y + BTN_H;
}

bool terminalButtonAt(const Status &s, const std::string &selectedId, int x, int y) {
    if (!terminalVisivel(s, indexOfId(s, selectedId))) return false;
    if (display::retrato()) {
        if (s.bloqueio.known) return false;
        const int px = PANEL_W - R_MARG - R1_PAD - R1_PIL_TERM;
        return x >= px && x < px + R1_PIL_TERM &&
               y >= R1_PIL_Y - 8 && y < R1_PIL_Y + R1_PIL_H + 8;
    }
    return x >= BTN_X && x < BTN_X + BTN_W && y >= TB_Y && y < TB_Y + TB_H;
}

std::string paneDoSelecionado(const Status &s, const std::string &selectedId) {
    const int i = indexOfId(s, selectedId);
    if (i < 0 || i >= (int)s.agents.size()) return "";
    return s.agents[i].paneId;
}

int opcaoAt(const Status &s, const std::string &selectedId, int x, int y) {
    const int i = indexOfId(s, selectedId);
    if (i < 0 || i >= (int)s.agents.size()) return 0;
    if (!bloqueioDe(s, s.agents[i])) return 0;
    if (x < BQ_X || x >= BQ_X + BQ_W) return 0;

    const int total = (int)s.bloqueio.opcoes.size();
    for (int k = 0; k < total; k++) {
        int by = 0, bh = 0;
        if (!bloqueioBotao(k, total, by, bh)) break;
        if (y >= by && y < by + bh) return s.bloqueio.opcoes[k].n;
    }
    return 0;
}

// O bicho do canto superior esquerdo — o botao de girar a tela.
//
// O retangulo e o mesmo nas duas orientacoes porque o bicho mora no mesmo canto
// nas duas, e e MAIOR que o desenho (36x36) de proposito: em pe este gesto e a
// unica saida da tela, ja que la nao ha troca de pagina. Um controle sem
// alternativa nao pode exigir pontaria.
//
// Nao ha guarda de pagina: quem chama e que decide quando perguntar, e o
// cabecalho e o mesmo em todas as paginas.
bool iconeCabecalhoAt(int x, int y) {
    return dentro(alvoIconeCabecalho(), x, y);
}

// A fileira de bichos — o botao de trocar o ELENCO.
//
// O retangulo sai da largura que a propria turma reportou, e nao de um numero
// fixo: cada tema tem a sua, e o alvo tem que seguir o desenho. Deitado ela
// pisa no rodape; em pe, no topo — as duas posicoes vem das mesmas constantes
// que desenham.
//
// A folga de 6 px em volta e o mesmo motivo do botao de girar: bicho de 40 px
// nao e alvo de dedo. Ela nao encosta no icone do cabecalho porque em pe a
// turma comeca 54 px abaixo dele.
bool turmaAt(int x, int y, int page) {
    const int w = clawd::crewW(), h = clawd::crewH();
    if (w <= 0 || h <= 0) return false;

    // Nas paginas de bicho grande a fileira nao e desenhada, e um toque ali
    // pertence ao que estiver no lugar dela. Em pe elas sao a 3 e a 4 — a
    // tela nova empurrou tudo uma casa.
    const bool emPe = display::retrato();
    if (emPe ? (page == 3 || page == 4) : (page == 2 || page == 3))
        return false;

    // Em pe a fileira mora no topo em TODA pagina que a mostra — a inicial e a
    // de contexto usam as mesmas duas constantes, entao o alvo tambem e um so.
    const int bx   = emPe ? R_MARG : 14;
    const int chao = emPe ? R_TURMA_CHAO : SCREEN_H - 8;
    const int folga = 6;
    return x >= bx - folga && x < bx + w + folga &&
           y >= chao - h - folga && y < chao + folga;
}

// O toque caiu no percentual da SEMANA (em pe)? E o atalho para ABRIR a tela
// do Token sem esperar o estouro — para conferir os prazos, ou so ver o bicho.
//
// O alvo e a metade direita da faixa da semana: o numero e o unico conteudo
// daquele canto, e um alvo do tamanho exato do texto exigiria pontaria que um
// atalho nao pode exigir.
// Os retangulos moram em lib/layout, conferidos contra uma foto da tela real
// (ver test/test_alvos). O que fica aqui e a pergunta que so o firmware sabe
// responder: estamos em pe?
bool pctSemanaAt(int x, int y) {
    return display::retrato() && dentro(alvoPctSemana(), x, y);
}

// O gemeo do de cima, na faixa da SESSAO: ele ensaia a tela de RESET. Os dois
// atalhos vivem no mesmo canto de faixas vizinhas de proposito — quem aprendeu
// um acha o outro.
bool pctSessaoAt(int x, int y) {
    return display::retrato() && dentro(alvoPctSessao(), x, y);
}

// A metade ESQUERDA da faixa da SEMANA — onde mora o rotulo. E o terceiro
// atalho de ensaio: ele mata o Kenny por alguns segundos.
//
// A morte de verdade depende de uma sessao ficar 30 s sem publicar, e esperar
// meia janela para conferir um sprite nao e teste, e paciencia. Os tres ensaios
// ficam nas duas faixas: percentuais a direita, rotulos a esquerda.
bool rotuloSemanaAt(int x, int y) {
    return display::retrato() && dentro(alvoRotuloSemana(), x, y);
}

// Versao da P0: sem selecao, mede pela geometria da pergunta em tela cheia.
int perguntaP0At(const Status &s, int x, int y) {
    if (!s.bloqueio.known || s.bloqueio.opcoes.empty()) return 0;
    const int total = (int)s.bloqueio.opcoes.size();
    for (int k = 0; k < total; k++) {
        Alvo a;
        // Os QUATRO lados, e nao so o `y`: com duas colunas, filtrar o x pela
        // faixa inteira e decidir pela altura faria o toque na coluna da
        // direita responder a opcao da esquerda.
        if (!perguntaP0Botao(k, total, a)) break;
        if (dentro(a, x, y)) return s.bloqueio.opcoes[k].n;
    }
    return 0;
}

// ---- Modo terminal ----
// A tela do agente, em tela cheia. E a unica coisa neste painel com mais
// conteudo do que cabe, e por isso a unica que rola.
//
// Sem cor: o texto vem da API em ASCII puro, ja traduzido (ver server/texto.py).
// Ler SGR exigiria um parser e um grid de atributos por celula, e o que se quer
// aqui e ler o que o agente escreveu — nao reproduzir o terminal.
const int TERM_BAR_H = 24;                      // barra de cima, com o botao
const int TERM_X     = 4;
const int TERM_Y     = TERM_BAR_H + 4;
// 6x8 px por celula na fonte base. Estes dois numeros VIAJAM ate a API, que
// quebra as linhas por eles — ver net::terminalAbrir.
const int TERM_CW    = 6;
const int TERM_CH    = 8;
// A barra de rolagem, no rodape. Ela existe porque em pe o terminal nao tem
// outro jeito de andar: deitado o swipe vertical rola, e em pe o mesmo gesto
// disputa com a troca de pagina. Quatro alvos de 76 px, que e o tamanho do
// alvo do bicho do cabecalho — a medida que ja foi calibrada para o dedo.
const int TERM_BARRA_H = 34;

// Botao de sair, no canto superior ESQUERDO. Ali porque e o canto que nao
// compete com nada: o texto comeca abaixo dele, e sair e a unica acao desta
// tela que nao pode falhar em ser encontrada.
const int SAIR_X = 4, SAIR_Y = 2, SAIR_W = 62, SAIR_H = 20;

// A GRADE, medida contra a tela QUE ESTA NA FRENTE.
//
// Estas duas linhas usavam SCREEN_W/SCREEN_H, que sao `#define` da paisagem e
// NAO giram (include/board_pins.h). O terminal nasceu antes do modo em pe e
// ficou pedindo 78 colunas para uma tela de 320 px: o `write` do Arduino_GFX
// faz wrap sozinho ao passar da borda, entao a partir do 53o caractere cada
// linha continuava EM CIMA da seguinte. Era isso que fazia a tela ilegivel, com
// duas camadas de texto no mesmo lugar.
//
// Os dois numeros VIAJAM ate a API, que quebra as linhas por eles (ver
// net::terminalAbrir e server/sgr.py) — errar aqui erra dos dois lados.
int termCols() { return (display::telaW() - TERM_X * 2) / TERM_CW; }
int termRows() { return (display::telaH() - TERM_Y - TERM_BARRA_H) / TERM_CH; }

// A PALETA que o parser resolve. Sao as 16 cores do terminal em RGB888, mais o
// par de frente e fundo padrao — o `term_parse` aplica bold, dim e reverse
// sobre elas e devolve a cor final de cada run, entao o laco de desenho nao
// precisa saber o que 38;5;9 significa.
//
// Os valores acompanham a paleta do painel (BG, FG e as cores do herdr): uma
// tela de terminal com cor propria dentro de um painel que tem a sua brigaria
// com tudo o resto, e o que se quer aqui e ler o que o agente escreveu.
const term_palette_t TERM_PAL = {
    {
        0x0c0e12, 0xeb5555, 0x40c878, 0xe6be46,
        0x6f9fd8, 0x9a7fd0, 0x5f9ea8, 0xebeef2,
        0x303642, 0xff7b7b, 0x6fe89c, 0xffd76b,
        0x93bdf0, 0xb9a1e0, 0x87c4cd, 0xffffff,
    },
    0xebeef2,   // default_fg — o FG do painel
    0x0c0e12,   // default_bg — o BG do painel
};

// RGB888 (o que o parser devolve) para RGB565 (o que o canvas quer).
uint16_t de888(uint32_t c) {
    return RGB565((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

// O grid vive na PSRAM e SO enquanto a tela de terminal esta aberta. Sao ~24 KB
// com os tetos desta tela (ver lib/termparse/term_parse.h) — muito para a RAM
// interna, que ja carrega o framebuffer, e desperdicio para ficar residente
// numa tela que quase nunca esta aberta.
term_grid_t *g_grid = nullptr;

bool gridPronto() {
    if (g_grid) return true;
    g_grid = (term_grid_t *)ps_malloc(sizeof(term_grid_t));
    if (!g_grid) Serial.println("terminal: sem PSRAM para o grid — tela sem cor");
    return g_grid != nullptr;
}

void gridSoltar() {
    if (!g_grid) return;
    free(g_grid);
    g_grid = nullptr;
}

// A barra de rolagem do rodape. Quatro alvos iguais, e o indice de cada um e o
// que `terminalBotaoAt` devolve.
const char *TERM_BOTOES[] = {"TOPO", "^", "v", "FIM"};
const int   TERM_N_BOT    = 4;

void terminalBotao(Arduino_Canvas *g, int i, int &bx, int &bw) {
    const int W = display::telaW();
    const int marg = 4, gap = 4;
    bw = (W - marg * 2 - gap * (TERM_N_BOT - 1)) / TERM_N_BOT;
    bx = marg + i * (bw + gap);
}

void drawTerminal(const std::vector<std::string> &linhas, const char *titulo,
                  bool travado) {
    Arduino_Canvas *g = display::canvas();
    const int W = display::telaW(), H = display::telaH();
    g->fillScreen(BG);

    g->fillRoundRect(SAIR_X, SAIR_Y, SAIR_W, SAIR_H, 5, CARD);
    g->drawRoundRect(SAIR_X, SAIR_Y, SAIR_W, SAIR_H, 5, LARANJA);
    g->setTextColor(LARANJA);
    g->setTextSize(1);
    g->setCursor(SAIR_X + 9, SAIR_Y + 7);
    g->print("< SAIR");

    if (titulo && titulo[0]) {
        g->setTextColor(FG);
        g->setTextSize(1);
        g->setCursor(SAIR_X + SAIR_W + 10, SAIR_Y + 7);
        g->print(titulo);
    }

    // Sem a trava, o texto chega na largura do host e a tela vira uma sopa de
    // linhas quebradas. Dizer o motivo e o que separa "esta quebrado" de "falta
    // uma peca do outro lado" — e a segunda e acionavel.
    const int rows = termRows();
    if (!linhas.empty() && !travado) {
        const char *aviso = "SEM AJUSTE DE LARGURA";
        g->setTextColor(C_YELL);
        g->setTextSize(1);
        g->setCursor(W - 8 - (int)strlen(aviso) * 6, SAIR_Y + 7);
        g->print(aviso);
    }

    g->drawFastHLine(0, TERM_BAR_H, W, TRACK);

    if (linhas.empty()) {
        g->setTextColor(MUTED);
        g->setTextSize(1);
        g->setCursor(TERM_X + 4, TERM_Y + 12);
        g->print("lendo a tela do agente...");
        display::flush();
        return;
    }

    // O TEXTO, COM COR. Cada linha chega da API ja em ASCII, ja cortada em
    // `termCols()` colunas e com o SGR reaberto no comeco (ver server/sgr.py),
    // entao ela pode ser parseada sozinha — o que importa aqui, porque a janela
    // rolada nao traz as linhas de cima.
    //
    // `setTextWrap(false)` e a segunda metade do conserto do estouro: com a
    // grade certa nada deveria passar da borda, mas um caractere a mais fazia o
    // Arduino_GFX pular de linha sozinho e escrever POR CIMA da seguinte. Sem o
    // wrap, o que nao cabe simplesmente nao aparece — que e o comportamento
    // certo para uma tela que ja foi cortada do outro lado.
    g->setTextSize(1);
    g->setTextWrap(false);
    const int cols = termCols();
    const bool comCor = gridPronto();

    for (size_t i = 0; i < linhas.size() && (int)i < rows; i++) {
        const std::string &l = linhas[i];
        if (l.empty()) continue;
        const int ly = TERM_Y + (int)i * TERM_CH;

        if (!comCor) {
            // Sem PSRAM para o grid: escreve cru, pulando os escapes. Feio, mas
            // legivel — e melhor que uma tela preta.
            int c = 0;
            for (size_t k = 0; k < l.size() && c < cols; k++) {
                if (l[k] == 0x1B) {                 // pula ESC [ ... letra
                    while (++k < l.size() && !isalpha((unsigned char)l[k])) {}
                    continue;
                }
                g->setTextColor(FG);
                g->setCursor(TERM_X + c * TERM_CW, ly);
                int gasto = 0;
                const uint8_t b = cp437::proximo(l.c_str() + k, l.size() - k,
                                                 gasto);
                if (!b) break;
                g->write((char)b);
                k += gasto - 1;          // o `k++` do laco fecha a conta
                c++;
            }
            continue;
        }

        if (term_parse(l.c_str(), g_grid, &TERM_PAL) < 1) continue;
        const term_line_t &tl = g_grid->lines[0];
        int col = 0;
        for (int r = 0; r < tl.run_count && col < cols; r++) {
            const term_run_t &run = g_grid->runs[tl.run_start + r];
            const char *txt = &g_grid->text[run.text_off];
            const int x = TERM_X + col * TERM_CW;
            int n = run.cols;
            if (col + n > cols) n = cols - col;

            // O fundo vem ANTES do texto, e so quando ele difere do padrao: o
            // parser marca isso na flag para o laco nao pintar a tela inteira
            // de retangulos que ninguem ve.
            if (run.flags & TERM_F_HAS_BG)
                g->fillRect(x, ly, n * TERM_CW, TERM_CH, de888(run.bg));

            g->setTextColor(de888(run.fg));
            g->setCursor(x, ly);
            // UMA COLUNA POR CARACTERE, e nao por byte: o desenho de caixa
            // chega em UTF-8 de tres bytes e a fonte o tem em UM (ver
            // lib/cp437). Escrever byte a byte aqui desenharia tres glifos de
            // lixo e empurraria o resto da linha — que e exatamente o que a
            // reducao a `-|+` existia para evitar, e agora nao precisa mais.
            {
                const size_t bytes = strlen(txt);
                size_t k = 0;
                for (int c = 0; c < n && k < bytes; c++) {
                    int gasto = 0;
                    const uint8_t b = cp437::proximo(txt + k, bytes - k, gasto);
                    if (!b) break;
                    g->write((char)b);
                    k += gasto;
                }
            }

            // Sublinhado e riscado a fonte da grade nao tem: viram um filete,
            // que e o que a mesma fonte faria se tivesse o glifo.
            if (run.flags & TERM_F_UNDERLINE)
                g->drawFastHLine(x, ly + TERM_CH - 1, n * TERM_CW, de888(run.fg));
            if (run.flags & TERM_F_STRIKE)
                g->drawFastHLine(x, ly + TERM_CH / 2, n * TERM_CW, de888(run.fg));
            col += n;
        }
    }
    g->setTextWrap(true);

    // A BARRA DE ROLAGEM. Em pe ela e o unico jeito de andar no texto: deitado
    // o swipe vertical rola, e em pe o mesmo gesto disputa com a troca de
    // pagina — a lista da primeira tela ja ensinou que quem perde essa disputa
    // fica sem alvo nenhum.
    g->drawFastHLine(0, H - TERM_BARRA_H, W, TRACK);
    g->fillRect(0, H - TERM_BARRA_H + 1, W, TERM_BARRA_H - 1, CARD);
    for (int i = 0; i < TERM_N_BOT; i++) {
        int bx = 0, bw = 0;
        terminalBotao(g, i, bx, bw);
        const int by = H - TERM_BARRA_H + 5, bh = TERM_BARRA_H - 10;
        g->fillRoundRect(bx, by, bw, bh, 5, BG);
        g->drawRoundRect(bx, by, bw, bh, 5, TRACK);
        const char *rot = TERM_BOTOES[i];
        g->setTextColor(FG);
        g->setTextSize(1);
        g->setCursor(bx + (bw - (int)strlen(rot) * 6) / 2, by + (bh - 8) / 2);
        g->print(rot);
    }
    display::flush();
}

// Qual botao da barra esta em (x,y). -1 fora dela.
int terminalBotaoAt(int x, int y) {
    const int H = display::telaH();
    if (y < H - TERM_BARRA_H) return -1;
    for (int i = 0; i < TERM_N_BOT; i++) {
        int bx = 0, bw = 0;
        terminalBotao(display::canvas(), i, bx, bw);
        if (x >= bx && x < bx + bw) return i;
    }
    return -1;
}

bool terminalSairAt(int x, int y) {
    // Alvo maior que o desenho: o botao mede 62x20 e um dedo cobre bem mais do
    // que isso. Sair e a acao que nao pode exigir pontaria.
    return x < SAIR_X + SAIR_W + 20 && y < SAIR_Y + SAIR_H + 12;
}

void drawMessage(const char *title, const char *detail) {
    g_stale = false;
    Arduino_Canvas *g = display::canvas();
    g->fillScreen(BG);
    g->setTextColor(FG);
    g->setTextSize(3);
    g->setCursor(20, 130);
    g->print(title);
    if (detail && detail[0]) {
        g->setTextColor(MUTED);
        g->setTextSize(1);
        g->setCursor(20, 170);
        g->print(detail);
    }
    display::flush();
}

}
