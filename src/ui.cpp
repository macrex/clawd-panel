#include "ui.h"
#include "display.h"
#include "board_pins.h"
#include "view_model.h"
#include "clawd.h"
#include "layout.h"
#include "DejaVuSans7pt7b.h"
#include <cstdio>
#include <cstring>

namespace {

const uint16_t BG      = RGB565(12, 14, 18);
const uint16_t CARD    = RGB565(26, 30, 38);
const uint16_t FG      = RGB565(235, 238, 242);
const uint16_t MUTED   = RGB565(130, 140, 155);
const uint16_t TRACK   = RGB565(48, 54, 66);
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
// O stale e por queda de RADIO? Muda o texto do rodape: "sem contato" se
// resolve esperando, "sem wifi" nao.
bool g_semWifi = false;

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
void drawAvisoLinha(Arduino_Canvas *g, bool marcado,
                    int x, int y, int w, int h) {
    if (!marcado || g_stale) return;
    g->fillRect(x, y, w, h, misturar(CARD, H_DONE, AVISO_FUNDO_NUM, AVISO_FUNDO_DEN));
    g->fillRect(x, y, AVISO_TRACO_W, h,
                misturar(CARD, H_DONE, AVISO_TRACO_NUM, AVISO_TRACO_DEN));
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

void drawNovoLinha(Arduino_Canvas *g, bool novo, int x, int y, int w, int h) {
    if (!novo || g_stale) return;
    g->fillRect(x, y, w, h, misturar(CARD, LARANJA, NOVO_FUNDO_NUM, NOVO_FUNDO_DEN));
    g->fillRect(x, y, AVISO_TRACO_W, h,
                misturar(CARD, LARANJA, NOVO_TRACO_NUM, NOVO_TRACO_DEN));
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

// Quantos caracteres cabem na linha de baixo do bloco de tempo. Serve de
// criterio, e nao de corte: a maxima e minima do dia so entram quando a
// descricao e curta o bastante para as tres coisas caberem juntas.
const int TEMPO_MAX_CH = 16;

// O instante em que o limite vira, pronto para a tela. Vazio quando nao se sabe.
//
// Sai o ANO da data semanal: a API manda "15/08/2026 (Sabado)" e o ano nao
// informa nada num limite que vira em no maximo sete dias. Sao 30 px que valem
// mais como espaco para o bicho do clima.
//
// `curto` tira tambem o dia da semana. A data sozinha ja responde QUANDO; o
// nome do dia e conforto, e e o primeiro a sair quando o cabecalho aperta.
//
// Cai na contagem regressiva quando a API nao manda o instante — uma versao
// anterior dela so tinha "1h44m". Dado antigo vale mais do que linha vazia.
std::string quandoVira(const Metric &m, bool curto) {
    if (!m.known) return "";
    std::string q = (!m.at.empty() && m.at != "-") ? m.at : m.resets;

    const size_t b1 = q.find('/');
    if (b1 != std::string::npos) {
        const size_t b2 = q.find('/', b1 + 1);
        if (b2 != std::string::npos && q.size() >= b2 + 5) q.erase(b2, 5);
    }
    if (curto) {
        const size_t par = q.find(" (");
        if (par != std::string::npos) q.erase(par);
    }

    // "3:00pm" vira "3:00PM": nesta fonte de 6 px o par de minusculas some ao
    // lado do numero em corpo dobrado.
    const size_t n = q.size();
    if (n >= 2 && (q[n - 1] == 'm' || q[n - 1] == 'M')
               && (q[n - 2] == 'a' || q[n - 2] == 'p')) {
        q[n - 2] = (char)toupper((unsigned char)q[n - 2]);
        q[n - 1] = 'M';
    }
    return q;
}

void drawHeader(Arduino_Canvas *g, const Status &s, int staleSeconds,
                bool paginaDoNivel) {
    const int hs     = 2;
    const int marcaY = 14;      // logo da marca e os limites da conta
    // Corpo 3 (24 px de altura), na mesma proporcao da hora e da temperatura,
    // centrado na faixa de 42 px do cabecalho.
    const int tituloY = 9;

    // O MAGO fica aqui, na borda esquerda, e nao e escolha estetica: a faixa do
    // flush de prefixo e uma tira de altura inteira (x=0..92), entao este canto
    // e o rodape sao enviados JUNTOS. Aqui ele anima de graca; ao lado do
    // relogio custaria ~26% de CPU. Ver display::flushPrefix.
    //
    // Sem o cartao (ou sem o wizard nele) cai no logo da marca, para o
    // cabecalho nunca comecar com um buraco.
    const int iw = clawd::iconW(paginaDoNivel);
    if (iw) clawd::drawIconInto(g, 14, 40 - clawd::iconH(paginaDoNivel),
                                paginaDoNivel);
    else    drawLogo(g, 14, marcaY + (8 * 2 - logoH(hs)) / 2, hs, LARANJA);

    const int tituloX = 14 + (iw ? iw : logoW(hs)) + 12;
    // O titulo e o corpo dele saem daqui, e a largura e MEDIDA a partir dos dois
    // (ver `limFim`, abaixo). Escrever a medida a mao ja custou: ela dizia
    // `7 * 12` — sete letras a doze pixels — para uma palavra de nove letras
    // desenhada em corpo 3, que mede 18 px por letra. Eram 84 px contra 162
    // reais, e os 78 de diferenca liberavam o clima e o selo VIA a escreverem
    // por cima do proprio titulo.
    const char *TITULO   = "CLAUDINHO";
    const int   TITULO_SZ = 3;
    // Laranja da marca, e nao a cor do texto: o titulo e marca, nao dado. Nao
    // esmaece com o resto quando o contato cai, pelo mesmo motivo do logo.
    g->setTextColor(LARANJA);
    g->setTextSize(TITULO_SZ);
    g->setCursor(tituloX, tituloY);
    g->print(TITULO);

    // ---- Direita: relogio e tempo, MEDIDOS antes de qualquer coisa ----
    // A ordem inverteu, e por um motivo concreto. Antes os limites da conta
    // tinham largura fixa e o bloco do tempo encolhia para caber ao lado deles;
    // agora a data da virada e que tem duas formas, e ela precisa saber quanto
    // espaco existe para escolher uma. Medir a direita primeiro e o que permite
    // a linha da esquerda encolher em vez de atropelar o resto.
    //
    // O bicho do clima e o ULTIMO da fila deste cabecalho e o primeiro a sumir
    // em silencio quando algo cresce — ja custou uma investigacao inteira. Aqui
    // ele entra na conta antes de o texto ser escolhido.
    // Direita: HORA e TEMPERATURA, ambas grandes (corpo 3). A data ("TER 11/08")
    // e a previsao em texto ("estrelado 20-29") sairam a pedido; a hora e a
    // temperatura cresceram para ocupar o espaco, centradas na faixa de 42 px.
    const int GRANDE = 3;                    // corpo 3 = 24 px de altura
    const int GY     = 9;                    // (42 - 24) / 2, centra na vertical
    int esq = SCREEN_W - 14;
    int hx  = 0;
    if (s.clock.known) {
        hx  = esq - (int)s.clock.hm.size() * 6 * GRANDE;
        esq = hx;
    }

    // Porcentagens de sessao/semana REMOVIDAS do cabecalho a pedido; o resumo
    // permanente saiu daqui e segue na pagina dedicada (card "5 HORAS").
    // Onde o titulo ACABA, mais um respiro. E o piso de tudo o que vem da
    // direita: o clima e o selo VIA so desenham quando o passam por 8 px.
    // A largura sai da string e do corpo dela, e nao de dois numeros escritos a
    // mao que a proxima troca de nome deixaria mentindo.
    const int limFim = tituloX + (int)strlen(TITULO) * 6 * TITULO_SZ + 20;

    if (s.clock.known) {
        g->setTextColor(fgColor());
        g->setTextSize(GRANDE);
        g->setCursor(hx, GY);
        g->print(s.clock.hm.c_str());
    }

    if (s.weather.known) {
        // So a temperatura (numero + grau + "C"), tambem em corpo 3, a esquerda
        // da hora. A previsao em texto saiu. Largura calculada como drawTemp
        // devolve, para alinhar o bloco a direita antes de desenhar.
        char tn[8];
        snprintf(tn, sizeof(tn), "%d", s.weather.temp);
        const int tempW = (int)strlen(tn) * 6 * GRANDE + GRANDE * 9 / 2 + 6 * GRANDE;
        int wx = esq - 16 - tempW;
        if (wx > limFim + 8) {
            drawTemp(g, wx, GY, s.weather.temp, fgColor(), GRANDE);
            esq = wx;
        }
    }

    if (s.viaSlave) {
        const int sw = (s.tag.empty() ? 11 : (int)s.tag.size() + 4) * 6 + 10;
        const int sx = esq - 16 - sw;
        // So quando cabe: o cabecalho deitado ja disputa espaco, e o rodape do
        // retrato conta a mesma historia por extenso.
        if (sx > limFim + 8) drawSeloVia(g, sx, (42 - 16) / 2, s.tag);
    }

    // AQUI NAO VAI MAIS O LOGO. Ele ficou deste lado desde que trocou de lugar
    // com o mago, condicionado a sobrar largura — e essa condicao ficou num
    // empate perverso: os limites conhecidos ("5H 63%  14:00") ocupam ~78 px e
    // desconhecidos ("5H -") ~24, uma diferenca de 54 px. O logo precisa de
    // 34 + 12 + 8 = 54. Exatamente a folga que sobra quando os limites somem.
    //
    // O resultado era um enfeite que nunca aparecia na operacao normal e
    // materializava do nada quando a placa perdia os limites da conta — um
    // terceiro bicho laranja brotando ao lado do bicho do clima, sem causa
    // visivel. Ele custou uma investigacao para descobrir que nao era bicho.
    //
    // E ele nao informava nada: a marca ja esta escrita em laranja a poucos
    // pixels dali, no titulo. Aparecer SO no estado degradado e o contrario do
    // util — ensina o olho a associar a marca com defeito.
    //
    // `drawLogo` continua vivo nos dois lugares onde e substituto de algo que
    // faltou: o canto esquerdo sem o icone do nivel, e o rodape sem a turma.

    g->drawFastHLine(14, 42, SCREEN_W - 28, staleSeconds > 0 ? C_YELL : TRACK);
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
    if (page != 2 && page != 3) {
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
    char buf[64];
    // O motor de reserva vira PREFIXO do texto que ja mora aqui. Rotulo proprio
    // precisaria de espaco que o rodape nao tem.
    const char *mot = s.hooksEngine ? "HOOKS  " : "";
    if (staleSeconds > 0 && g_semWifi) {
        snprintf(buf, sizeof(buf), "%sSEM WIFI HA %ds", mot,
                 staleSeconds < 3600 ? staleSeconds : staleSeconds / 3600);
    } else if (staleSeconds > 0) {
        // Acima de uma hora vira horas. Nao e estetica: em segundos, um atraso
        // de dias empurra este texto para a esquerda ate encostar no trio.
        if (staleSeconds < 3600)
            snprintf(buf, sizeof(buf), "%sSEM CONTATO HA %ds", mot, staleSeconds);
        else
            snprintf(buf, sizeof(buf), "%sSEM CONTATO HA %dh", mot, staleSeconds / 3600);
    } else {
        snprintf(buf, sizeof(buf), "%s%s", mot, formatAge(s.updated_ago).c_str());
    }

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
    const int trioFim = (page != 2 && clawd::crewW()) ? 14 + clawd::crewW() : 14;
    if (staleSeconds > 0 && dotEsq - 12 - (int)strlen(buf) * 6 < trioFim + 8) {
        if (staleSeconds < 3600)
            snprintf(buf, sizeof(buf), "%sS/CONTATO %ds", mot, staleSeconds);
        else
            snprintf(buf, sizeof(buf), "%sS/CONTATO %dh", mot, staleSeconds / 3600);
    }
    // Amarelo tambem quando o motor e o de reserva: os dois motores nao tem a
    // mesma confiabilidade, e mostrar estado deduzido com a cara de estado lido
    // seria a mesma mentira que o aviso de contato perdido existe para evitar.
    g->setTextColor((staleSeconds > 0 || s.hooksEngine) ? C_YELL : MUTED);
    g->setTextSize(1);
    g->setCursor(dotEsq - 12 - (int)strlen(buf) * 6, SCREEN_H - 30);
    g->print(buf);
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
    const uint16_t cor = colorOf(m.level);

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
    g->setTextColor(m.known ? fgColor() : MUTED);
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
    g->setTextColor(m.known ? fgColor() : MUTED);
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
        for (int i = 0; i < n; i++) {
            const char c = p[i];
            g->write((c == '\n' || c == '\r' || c == '\t') ? ' ' : c);
        }
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
    // 37 px cabe quatro sessoes inteiras nos 156 px uteis do card; o total ao
    // lado do rotulo ja diz quantas existem, e o "+N" quantas ficaram de fora.
    const int passo = 38;
    const int cabem = (h - 34) / passo;
    const int n = (int)s.agents.size() < cabem ? (int)s.agents.size() : cabem;
    const int xLim = x + w - 12;

    for (int i = 0; i < n; i++) {
        const Agent &a = s.agents[i];
        const int ly = y + 34 + i * passo;

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

        // Linha 2: chips. O agente so aparece quando foge de "claude" (senao
        // seria a mesma palavra em toda linha); depois o modelo e o esforco.
        // Cada chip so entra se couber — o card e estreito e o corte e em cascata.
        int cx = x + 26;
        {
            // A TAG da maquina, primeiro chip da linha: e o dado que responde
            // "onde isto esta rodando", e vem antes do que o agente e.
            const int cwo = drawChip(g, cx, ly + 20, tagDe(a.tag),
                                     corDaTag(a.origem), xLim);
            if (cwo) cx += cwo + 4;
        }
        if (!a.agent.empty() && a.agent != "claude") {
            int cw = drawChip(g, cx, ly + 20, a.agent, LARANJA, xLim);
            if (cw) cx += cw + 4;
        }
        int cw = drawChip(g, cx, ly + 20, a.model, fgColor(), xLim);
        if (cw) cx += cw + 4;
        drawChip(g, cx, ly + 20, effortCurto(a.effort), C_YELL, xLim);

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
    }

    // Nao coube todo mundo: dizer QUANTOS ficaram de fora e melhor do que uma
    // lista que termina sem aviso e parece completa.
    if (n < (int)s.agents.size()) {
        g->setTextColor(MUTED);
        g->setTextSize(1);
        g->setCursor(x + 14, y + 34 + n * passo + 2);
        g->printf("+%d", (int)s.agents.size() - n);
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
int p0qTopo() { return display::retrato() ? 176 : 78; }
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
    for (char c : txt0) txt += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;

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
bool perguntaP0Botao(int i, int total, int &y, int &h) {
    if (total <= 0 || i < 0 || i >= total) return false;
    const int topo = p0qTopo();
    const int alt  = (p0qFim() - topo + P0Q_GAP) / total - P0Q_GAP;
    if (alt < P0Q_ALT_MIN) return false;
    y = topo + i * (alt + P0Q_GAP);
    h = alt;
    return true;
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
        textoQuebrado(g, P0Q_X, p0qTitY(), 11, p0qCols(), titulo.c_str(), 2);
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

    // Botoes empilhados, largura toda e SEM teto de altura: com duas opcoes
    // eles viram dois tarjoes de ~60 px, que e o alvo que nao se erra em pe.
    const int total = (int)b.opcoes.size();
    const int qw = p0qW();
    for (int i = 0; i < total; i++) {
        int y = 0, h = 0;
        if (!perguntaP0Botao(i, total, y, h)) break;
        const Opcao &o = b.opcoes[i];
        const bool armada = (opcaoArmada == o.n);

        g->fillRoundRect(P0Q_X, y, qw, h, 10, CARD);
        g->drawRoundRect(P0Q_X, y, qw, h, 10, armada ? C_YELL : TRACK);
        if (armada) g->drawRoundRect(P0Q_X + 2, y + 2, qw - 4, h - 4, 8, C_YELL);

        // Numero e rotulo crescem com a altura do botao: com poucas opcoes o
        // botao vira um tarjao e o texto acompanha, em vez de ficar uma legenda
        // perdida no meio de um retangulo enorme.
        const int nsz = h >= 60 ? 4 : (h >= 40 ? 3 : 2);
        g->setTextColor(armada ? C_YELL : LARANJA);
        g->setTextSize(nsz);
        g->setCursor(P0Q_X + 16, y + (h - nsz * 8) / 2);
        g->printf("%d", o.n);

        const int tx  = P0Q_X + 16 + nsz * 6 + 16;
        // TETO DE CORPO 2 EM PE. La o botao chega a 130 px com duas opcoes, e o
        // corpo 3 que essa altura liberava dava 15 caracteres por linha — os
        // rotulos vinham cortados no meio da palavra dentro de um botao com
        // metade dele vazio. O que o botao grande compra e alvo para o dedo, e
        // nao letra grande: quem precisa ser lido e o texto, e ele cabe inteiro
        // em corpo 2.
        const int lsz = display::retrato() ? (h >= 34 ? 2 : 1)
                                           : (h >= 60 ? 3 : (h >= 34 ? 2 : 1));
        const int cols = (P0Q_X + qw - 12 - tx) / (6 * lsz);
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
// 70 e nao 54: a faixa cresceu junto com os corpos dela — tudo na DejaVu
// (titulo ~10, percentual ~20, prazo ~10) e a barra em 12. O percentual em
// corpo 3 da grade foi tentado e brigava com os nomes das sessoes, que sao o
// texto mais importante da tela; a DejaVu dobrada e o degrau entre os dois. O
// preco sai do card de agents, que perde uma linha (6 em vez de 7) e continua
// sobrando para as cinco sessoes tipicas.
const int R_LIM_H      = 70;
const int R_LIM_GAP    = 6;
const int R_SES_Y      = R_LIM_Y + (R_LIM_H + R_LIM_GAP) * 2;   // 258
const int R_SES_FIM    = 456;
const int R_STATUS_Y   = 462;

// O alvo do gesto de girar: o canto do bicho, com folga larga. Vale nas duas
// orientacoes e em todas as paginas — o cabecalho e o mesmo em todas.
//
// 76 e nao 50: o bicho do cabecalho muda de LARGURA conforme o nivel e o
// sorteio do rodizio (o alvo e a altura, 36 px; a largura sai do recorte).
// Medidos na placa: 33x32, 51x30, 64x35 — um alvo do tamanho do desenho
// encolheria junto com ele. Se um sorteio trouxer um bicho mais largo que 76, o
// excesso simplesmente nao gira, e o dedo continua tendo alvo de sobra.
const int R_GIRO_W = 76;
const int R_GIRO_H = 46;

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

void drawFaixaLimite(Arduino_Canvas *g, int x, int y, int w, int h,
                     const char *titulo, const Metric &m, int janelaSeg = 0,
                     const Clock *rel = nullptr) {
    g->fillRoundRect(x, y, w, h, 8, CARD);

    // A faixa inteira fala em DejaVu, em dois tamanhos: titulo e prazo na
    // natural (~10 px) e o percentual dobrado (~20). E o meio-termo que a
    // grade nao tem — o corpo 2 sumia ao lado da barra grossa, e o corpo 3
    // brigava com os nomes das sessoes, que sao o texto mais importante da
    // tela e ficam logo abaixo.
    // Titulo e percentual CENTRADOS na altura entre o teto do card e a barra
    // (0..38): cada um com a propria altura de texto, entao as baselines
    // diferem — ~8 px de texto no titulo, ~20 no percentual dobrado.
    g->setFont(&DejaVuSans7pt7b);
    g->setTextSize(1);
    g->setTextColor(MUTED);
    g->setCursor(x + 12, y + 23);
    g->print(titulo);

    const std::string pct = pctText(m);
    g->setTextColor(m.known ? fgColor() : MUTED);
    g->setTextSize(2);
    int16_t x1, y1; uint16_t tw, th;
    g->getTextBounds(pct.c_str(), 0, 0, &x1, &y1, &tw, &th);
    g->setCursor(x + w - 12 - (int)tw, y + 29);
    g->print(pct.c_str());
    g->setTextSize(1);

    g->setFont();
    drawBar(g, x + 12, y + 38, w - 24, 8, m, janelaSeg);
    g->setFont(&DejaVuSans7pt7b);

    // Prazo e instante na MESMA linha, um em cada ponta — SO os valores. O
    // "reseta em" saiu: com um prazo em cada faixa e o instante ao lado, o
    // rotulo nao desambiguava nada, so gastava a largura. A fonte e
    // proporcional, entao a ponta direita alinha por medida e nao por
    // contagem de caracteres.
    g->setTextColor(MUTED);
    g->setCursor(x + 12, y + 63);
    g->print(m.known && !m.resets.empty() ? m.resets.c_str() : "-");

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
        g->getTextBounds(at.c_str(), 0, 0, &x1, &y1, &tw, &th);
        g->setCursor(x + w - 12 - (int)tw, y + 63);
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
void drawSessoesRetrato(Arduino_Canvas *g, int x, int y, int w, int h,
                        const Status &s) {
    g->fillRoundRect(x, y, w, h, 10, CARD);

    // Cabecalho no corpo 1 da grade, discreto como era: quem manda neste card
    // sao os nomes das sessoes, e o rotulo so precisa dizer o que a lista e.
    g->setTextColor(MUTED);
    g->setTextSize(1);
    g->setCursor(x + 12, y + 11);
    g->print("AGENTS");

    char total[8];
    snprintf(total, sizeof(total), "%d", (int)s.agents.size());
    g->setTextColor(s.agents.empty() ? MUTED : fgColor());
    g->setCursor(x + 12 + 6 * 6 + 8, y + 11);
    g->print(total);

    if (s.agents.empty()) {
        g->setTextColor(MUTED);
        g->setTextSize(2);
        g->setCursor(x + 12, y + 34);
        g->print("-");
        return;
    }

    const int passo = 26;
    const int cabem = (h - 30) / passo;
    const int n = (int)s.agents.size() < cabem ? (int)s.agents.size() : cabem;
    const int bw   = 40;                       // barra de contexto, na direita
    const int bx   = x + w - 6 - bw;
    const int xLim = bx - 6;                   // ate onde os chips podem ir

    for (int i = 0; i < n; i++) {
        const Agent &a = s.agents[i];
        const int ly = y + 30 + i * passo;

        // A marca de turno concluido, atras da linha inteira.
        drawNovoLinha(g, a.novo, x + 2, ly - 3, w - 4, passo - 2);
        drawNovoLinha(g, a.novo, x + 2, ly - 3, w - 4, passo - 2);
        drawAvisoLinha(g, a.done, x + 2, ly - 3, w - 4, passo - 2);

        drawEstadoDot(g, x + 13, ly + 11, a.state, a.done);

        // Nome na DejaVu (~10 px): o unico degrau abaixo do corpo 2 da
        // grade, que dominava a linha. Proporcional, entao cabem mais letras
        // no mesmo vao — o corte e por MEDIDA ate a coluna dos chips, e nao
        // por contagem.
        g->setFont(&DejaVuSans7pt7b);
        g->setTextSize(1);
        g->setTextColor(fgColor());
        std::string nome = a.repo.substr(0, 16);
        int16_t nx1, ny1; uint16_t nw, nh;
        while (!nome.empty()) {
            g->getTextBounds(nome.c_str(), 0, 0, &nx1, &ny1, &nw, &nh);
            if ((int)nw <= 106) break;
            nome.pop_back();
        }
        g->setCursor(x + 24, ly + 16);
        g->print(nome.c_str());
        g->setFont();

        // Coluna FIXA para os chips, e nao "logo depois do repo": com o inicio
        // variavel os chips dancariam de linha para linha conforme o tamanho do
        // nome, e a coluna deixaria de ser lida como coluna.
        int cx = x + 136;
        {
            // A TAG da maquina abre a coluna de chips, como no card deitado.
            const int cwo = drawChip(g, cx, ly + 5, tagDe(a.tag),
                                     corDaTag(a.origem), xLim);
            if (cwo) cx += cwo + 4;
        }
        if (!a.agent.empty() && a.agent != "claude") {
            const int cw = drawChip(g, cx, ly + 5, a.agent, LARANJA, xLim);
            if (cw) cx += cw + 4;
        }
        const int cw = drawChip(g, cx, ly + 5, a.model, fgColor(), xLim);
        if (cw) cx += cw + 4;
        drawChip(g, cx, ly + 5, effortCurto(a.effort), C_YELL, xLim);

        g->drawFastHLine(bx, ly + 12, bw, TRACK);
        if (a.hasContext && a.contextPct > 0) {
            int fill = bw * a.contextPct / 100;
            if (fill < 2) fill = 2;
            g->fillRect(bx, ly + 11, fill, 3, colorOf(a.level));
        }
    }

    if (n < (int)s.agents.size()) {
        g->setTextColor(MUTED);
        g->setTextSize(1);
        g->setCursor(x + 12, y + 30 + n * passo + 3);
        g->printf("+%d", (int)s.agents.size() - n);
    }
}

// A turma e a linha de idade do dado. Nao ha bolinhas de pagina: em pe existe
// uma pagina so, e um indicador de quatro com uma acesa seria mentira.
void drawTurmaRetrato(Arduino_Canvas *g) {
    if (!clawd::crewW() || !clawd::drawCrewInto(g, R_MARG, R_TURMA_CHAO))
        drawLogo(g, R_MARG, R_TURMA_CHAO - logoH(3) - 4, 3,
                 g_stale ? MUTED : LARANJA);
    g->drawFastHLine(R_MARG, R_TOPO_FIM, R_CARD_W, TRACK);
}

void drawStatusRetrato(Arduino_Canvas *g, const Status &s, int staleSeconds) {
    char buf[64];
    const char *mot = s.hooksEngine ? "HOOKS  " : "";
    if (staleSeconds > 0 && g_semWifi) {
        snprintf(buf, sizeof(buf), "%sSEM WIFI HA %ds", mot,
                 staleSeconds < 3600 ? staleSeconds : staleSeconds / 3600);
    } else if (staleSeconds > 0) {
        if (staleSeconds < 3600)
            snprintf(buf, sizeof(buf), "%sSEM CONTATO HA %ds", mot, staleSeconds);
        else
            snprintf(buf, sizeof(buf), "%sSEM CONTATO HA %dh", mot, staleSeconds / 3600);
    } else if (s.viaSlave) {
        // Em contato, mas nao com quem manda: o dado desta tela veio da
        // reserva, e ela mesma diz como se chama.
        snprintf(buf, sizeof(buf), "%sMASTER OFF HA %ds - VIA %s", mot,
                 s.masterSemContatoS,
                 s.tag.empty() ? "RESERVA" : s.tag.c_str());
    } else {
        snprintf(buf, sizeof(buf), "%s%s", mot, formatAge(s.updated_ago).c_str());
    }
    g->setTextColor((staleSeconds > 0 || s.hooksEngine || s.viaSlave) ? C_YELL : MUTED);
    g->setTextSize(1);
    g->setCursor(PANEL_W - R_MARG - (int)strlen(buf) * 6, R_STATUS_Y);
    g->print(buf);
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
void drawTelaBicho(Arduino_Canvas *g, const Status &s, int staleSeconds,
                   int larg, bool (*desenhar)(Arduino_Canvas *, int, int)) {
    g->fillScreen(BG);
    drawHeaderRetrato(g, s, staleSeconds);

    desenhar(g, (PANEL_W - larg) / 2, 64);

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
    drawTelaBicho(g, s, staleSeconds, clawd::tokenW(), clawd::drawTokenInto);
}

// A tela de SERVIDOR FORA. Os prazos do rodape continuam la e continuam certos:
// eles nao vem mais do payload, sao contados pela placa a partir do ultimo valor
// bom (ver lib/metrics/relogio.h). O cabecalho ja diz ha quanto tempo o dado e
// velho, entao a tela nao repete o numero — quem quer a idade tem ela em cima.
void drawTelaOffline(Arduino_Canvas *g, const Status &s, int staleSeconds) {
    drawTelaBicho(g, s, staleSeconds, clawd::offlineW(), clawd::drawOfflineInto);

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

    g->setFont(&DejaVuSans7pt7b);
    g->setTextSize(1);
    g->setTextColor(MUTED);
    const char *sub = "nenhuma sessao ativa";
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
        drawFaixaLimite(g, R_MARG, R_LIM_Y, R_CARD_W, R_LIM_H,
                        "SESSAO", s.session, JANELA_5H);
        drawFaixaLimite(g, R_MARG, R_LIM_Y + R_LIM_H + R_LIM_GAP,
                        R_CARD_W, R_LIM_H, "SEMANA", s.week, JANELA_7D,
                        &s.clock);
        drawSessoesRetrato(g, R_MARG, R_SES_Y, R_CARD_W,
                           R_SES_FIM - R_SES_Y, s);
    }

    drawBolinhasRetrato(g, 0);
    drawStatusRetrato(g, s, staleSeconds);
}
}  // namespace

namespace ui {

void marcarSemWifi(bool v) { g_semWifi = v; }

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

    // Quem manda e o nivel que esta CARREGADO, escolhido no laco principal.
    // Recalcular aqui daria dois numeros independentes que podem discordar — o
    // sprite mostrando um nivel e o texto escrevendo outro.
    if (clawd::nivelEmCena() > 0) n = clawd::nivelEmCena();

    // ---- O bicho, CENTRADO, como na pagina do Clawd ----
    // Em pe as colunas descem para baixo do bicho e o fundo animado NAO existe:
    // ele e arte de paisagem (480x320), e girar ou recortar mentiria a cena.
    const bool emPe = display::retrato();
    const int  W    = display::telaW();
    const int bw = clawd::nivelW();
    if (bw) {
        clawd::drawNivelInto(g, (W - bw) / 2,
                             (emPe ? 270 : 238) - clawd::nivelH());
    } else {
        // Sprite ausente DIZ o que falta. A primeira versao ficava so vazia, e
        // uma pagina vazia parece decisao de projeto em vez de arquivo faltando.
        g->setTextColor(MUTED);
        g->setTextSize(1);
        const char *err = clawd::nivelErro();
        const int lw = (int)strlen(err) * 6;
        g->setCursor((W - lw) / 2, 150);
        g->print(err);
    }

    char buf[48];

    // ---- O numero, centrado abaixo do bicho, onde a pagina 3 poe o estado ----
    snprintf(buf, sizeof(buf), "NIVEL %d", n);
    g->setTextColor(g_stale ? MUTED : LARANJA);
    g->setTextSize(2);
    g->setCursor((W - (int)strlen(buf) * 12) / 2, emPe ? 282 : 244);
    g->print(buf);

    // Uma linha so, com o periodo e o que falta. Sao as duas coisas que dao
    // contexto ao numero de cima — desde quando conta, e quanto ainda falta —
    // e o terco inferior da tela nao comporta duas linhas centradas alem do
    // titulo sem encostar na fileira do rodape.
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
            g->setCursor((W - (int)strlen(buf) * 6) / 2, emPe ? 308 : 266);
            g->print(buf);
        }
    }

    // ---- As colunas que ladeiam o bicho, no mesmo TOPO da pagina 3 ----
    // Margens fixas pela mesma razao de la: a moldura do sprite varia muito,
    // mas o CORPO fica centrado e estreito, entao as colunas caem sobre o vazio
    // transparente da moldura e nao sobre o caranguejo.
    const int TOPO = emPe ? 330 : 118;

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
        if (page == 0 || telaToken || telaReset || telaOffline) {
            // A P0 (e as telas de bicho, que so existem sobre ela) seguem o
            // caminho proprio: turma no topo, pergunta em tela cheia.
            drawRetrato(g, s, staleSeconds, opcaoArmada, telaToken, telaReset,
                        telaOffline);
        } else {
            // O fundo do nivel pinta a tela inteira; nas outras paginas (ou
            // enquanto o arquivo da orientacao ainda nao carregou) fica o
            // fundo liso.
            const bool fundoPintou = (page == 3) && clawd::drawFundoInto(g);
            if (!fundoPintou) g->fillScreen(BG);
            drawHeaderRetrato(g, s, staleSeconds, page == 2);
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
            } else if (page == 1) {
                // A turma no MESMO lugar da tela inicial, e pela mesma razao:
                // so no topo a faixa barata do flush a alcanca, e so assim ela
                // anima na cadencia dos outros bichos (ver R1_C1_Y).
                drawTurmaRetrato(g);
                drawPageContextRetrato(g, s, indexOfId(s, selectedId),
                                       botaoArmado, opcaoArmada);
            } else if (page == 2) {
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

    // O fundo da pagina do nivel vem ANTES de tudo: ele pinta a tela inteira,
    // entao desenhado depois apagaria cabecalho, conteudo e rodape.
    //
    // E quando ele pinta, a limpeza NAO acontece: `fillScreen` custa 15,7 ms
    // medidos para escrever pixel que o fundo cobre no instante seguinte.
    const bool fundoPintou = (page == 3) && clawd::drawFundoInto(g);
    if (!fundoPintou) g->fillScreen(BG);
    drawHeader(g, s, staleSeconds, page == 3);

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
    if (!s.online && s.agents.empty() && !s.doCache && page == 0) {
        g->setTextColor(MUTED);
        g->setTextSize(3);
        g->setCursor(20, 140);
        g->print("CLAUDIO OFFLINE");
        g->setTextSize(1);
        g->setCursor(20, 180);
        g->print("nenhuma sessao ativa");
    } else if (page == 0) {
        drawPageLimits(g, s, opcaoArmada);
    } else if (page == 1) {
        drawPageContext(g, s, indexOfId(s, selectedId), botaoArmado, opcaoArmada);
    } else if (page == 2) {
        drawPageClawd(g, s);
    } else {
        drawPageNivel(g, s, nivelNoCartao, xpDoDia);
    }

    drawFooter(g, s, page, staleSeconds);
    display::flush();
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
            const int cw = clawd::crewW();
            const int ch = clawd::crewH();
            if (cw && ch) {
                g->fillRect(R_MARG, R_TURMA_CHAO - ch, cw, ch, BG);
                clawd::drawCrewInto(g, R_MARG, R_TURMA_CHAO);
            }
        }
        display::flushPrefix(semTurma ? R_HDR_LINHA + 2 : R_TOPO_FIM);
        return;
    }

    int colunas = 0;

    // Os QUATRO sprites animados moram na mesma tira: o mago no topo e a turma
    // no pe. A tira do prefixo tem altura inteira, entao um envio so leva
    // todos — o mago nao custa nada alem do que o rodape ja custava.
    // `redrawBadge` so e chamado nas paginas baratas: as caras pagam
    // redesenho inteiro. Entao aqui nunca e a pagina do nivel.
    const int iw = clawd::iconW(false);
    const int ih = clawd::iconH(false);
    if (iw && ih) {
        g->fillRect(14, 40 - ih, iw, ih, BG);
        clawd::drawIconInto(g, 14, 40 - ih, false);
        colunas = prefixColumns(14, iw, 6, SCREEN_W);
    }

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
    return x >= 0 && x < R_GIRO_W && y >= 0 && y < R_GIRO_H;
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
    // pertence ao que estiver no lugar dela.
    const bool emPe = display::retrato();
    // Nas paginas de bicho grande a fileira nao existe em nenhuma orientacao.
    if (page == 2 || page == 3) return false;

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
bool pctSemanaAt(int x, int y) {
    if (!display::retrato()) return false;
    const int y0 = R_LIM_Y + R_LIM_H + R_LIM_GAP;      // teto da faixa SEMANA
    return x >= R_MARG + R_CARD_W / 2 && x < R_MARG + R_CARD_W &&
           y >= y0 && y < y0 + 40;
}

// O gemeo do de cima, na faixa da SESSAO: ele ensaia a tela de RESET. Os dois
// atalhos vivem no mesmo canto de faixas vizinhas de proposito — quem aprendeu
// um acha o outro.
bool pctSessaoAt(int x, int y) {
    if (!display::retrato()) return false;
    return x >= R_MARG + R_CARD_W / 2 && x < R_MARG + R_CARD_W &&
           y >= R_LIM_Y && y < R_LIM_Y + 40;
}

// A metade ESQUERDA da faixa da SEMANA — onde mora o rotulo. E o terceiro
// atalho de ensaio: ele mata o Kenny por alguns segundos.
//
// A morte de verdade depende de uma sessao ficar 30 s sem publicar, e esperar
// meia janela para conferir um sprite nao e teste, e paciencia. Os tres ensaios
// ficam nas duas faixas: percentuais a direita, rotulos a esquerda.
bool rotuloSemanaAt(int x, int y) {
    if (!display::retrato()) return false;
    const int y0 = R_LIM_Y + R_LIM_H + R_LIM_GAP;
    return x >= R_MARG && x < R_MARG + R_CARD_W / 2 &&
           y >= y0 && y < y0 + 40;
}

// Versao da P0: sem selecao, mede pela geometria da pergunta em tela cheia.
int perguntaP0At(const Status &s, int x, int y) {
    if (!s.bloqueio.known || s.bloqueio.opcoes.empty()) return 0;
    if (x < P0Q_X || x >= P0Q_X + p0qW()) return 0;
    const int total = (int)s.bloqueio.opcoes.size();
    for (int k = 0; k < total; k++) {
        int by = 0, bh = 0;
        if (!perguntaP0Botao(k, total, by, bh)) break;
        if (y >= by && y < by + bh) return s.bloqueio.opcoes[k].n;
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

// Botao de sair, no canto superior ESQUERDO. Ali porque e o canto que nao
// compete com nada: o texto comeca abaixo dele, e sair e a unica acao desta
// tela que nao pode falhar em ser encontrada.
const int SAIR_X = 4, SAIR_Y = 2, SAIR_W = 62, SAIR_H = 20;

int termCols() { return (SCREEN_W - TERM_X * 2) / TERM_CW; }
int termRows() { return (SCREEN_H - TERM_Y) / TERM_CH; }

void drawTerminal(const std::vector<std::string> &linhas, const char *titulo,
                  bool travado) {
    Arduino_Canvas *g = display::canvas();
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
        g->setCursor(SCREEN_W - 8 - (int)strlen(aviso) * 6, SAIR_Y + 7);
        g->print(aviso);
    }

    g->drawFastHLine(0, TERM_BAR_H, SCREEN_W, TRACK);

    if (linhas.empty()) {
        g->setTextColor(MUTED);
        g->setTextSize(1);
        g->setCursor(TERM_X + 4, TERM_Y + 12);
        g->print("lendo a tela do agente...");
        display::flush();
        return;
    }

    // Grid de largura fixa. A API ja cortou cada linha na largura pedida, entao
    // aqui nao ha quebra nem medida: uma linha, uma fileira.
    g->setTextColor(FG);
    g->setTextSize(1);
    const int cols = termCols();
    for (size_t i = 0; i < linhas.size() && (int)i < rows; i++) {
        const std::string &l = linhas[i];
        if (l.empty()) continue;
        g->setCursor(TERM_X, TERM_Y + (int)i * TERM_CH);
        for (int c = 0; c < (int)l.size() && c < cols; c++) g->write(l[c]);
    }
    display::flush();
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
