#include "clawd.h"
#include "sprite.h"
#include "storage.h"
#include "display.h"
#include "board_pins.h"
#include "view_model.h"
#include "layout.h"
#include <Arduino.h>
#include <esp_random.h>

namespace {

// Uma animacao por estado, e depois delas as do sorteio. A ordem tem que casar
// com ARQUIVOS abaixo.
//
// A fronteira S_SORTEIO importa: dali para a frente as animacoes so aparecem
// GRANDES, na pagina 3. Elas nunca chegam ao rodape, e contar a largura delas
// engordaria a fileira — e o custo do flush — por nada.
enum Slot {
    S_IDLE, S_ALERT, S_TYPING, S_SLEEPING, S_AWAY, S_VITORIA,
    S_BUILDING, S_CONDUCTING, S_SWEEPING, S_PERGAMINHO,
    S_MARTELANDO, S_ARANHA, S_TEMPESTADE, S_REGENTE, S_LANTERNA,
    S_COUNT
};
const int S_SORTEIO = S_BUILDING;

// De onde sai o sorteio quando o turno comeca. O typing entra junto: ele e uma
// das quatro caras do trabalho, e nao o padrao com tres alternativas.
// O sweeping SAIU daqui. Ele virou o sinal de limpeza de contexto, e uma cara
// que aparece sorteada no meio de um turno normal nao pode ser a mesma que diz
// "estou compactando agora" — o mesmo desenho passaria a significar duas
// coisas. O lugar dele no sorteio e do Fawkes lendo o pergaminho.
const Slot TRABALHO[] = {
    S_TYPING, S_BUILDING, S_CONDUCTING, S_PERGAMINHO,
    S_MARTELANDO, S_ARANHA, S_TEMPESTADE, S_REGENTE, S_LANTERNA,
};
const int  N_TRABALHO = (int)(sizeof(TRABALHO) / sizeof(TRABALHO[0]));

// Altura alvo de cada bicho do rodape.
//
// Caiu de 52 para 42 quando o selo unico virou um TRIO, e o numero nao e
// estetico: e o orcamento do flush de prefixo. Cada coluna enviada custa ~100
// us, entao a largura total da fileira e o custo por quadro. Larguras em px de
// tela (nativo / divisor), com as caras que cada slot pode mostrar:
//
//   esquerda  sleeping 31 | alert 60 | typing 43 | sweeping 69  -> slot 69
//   meio      idle     36 | alert 60 | building 62 | sweeping 69 -> slot 69
//   direita   marks 48    | alert 60 | marks 48  | sweeping 69  -> slot 69
//   sonic     descanso 39 | alerta 35 | trabalho 37              -> slot 39
//
// 69*3 + 39, mais tres vaos de 6 = 264 px, e o prefixo fica em ~284 colunas
// (~28 ms). Eram 219 px e 239 colunas antes do Sonic: ele custa ~5 ms por
// quadro. Ele e estreito de proposito — os arquivos saem do conversor ja com
// 38 px de altura e recorte justo, entao nao puxam o slot para os 69 dos
// vizinhos, que sao governados pelo sweeping.
//
// A fileira comeca em x=14 e agora termina em 278. O aviso de idade do dado
// comeca em ~339 (ver drawFooter), entao ainda ha folga; um quinto slot nao
// caberia sem disputar espaco com ele.
//
// 42 e um ponto de degrau — a reducao e por divisor INTEIRO. De 42 para 43 o
// debugger volta para /1 (86 px) e a fileira estoura. Mexer aqui exige refazer
// esta conta, e nao so olhar a tela.
const int SELO_H = 42;

// Vao entre um bicho e o proximo.
const int VAO = 6;

// Quais caras podem aparecer no slot da ESQUERDA. Ela e a unica que fala de
// estado, entao a lista e o vocabulario dela — e e por esta lista que sai a
// largura do slot, ou seja, o custo de cada quadro.
//
// O building e o conducting NAO entram: eles so existem grandes, na pagina 3.
// O sweeping entra desde que virou o sinal de limpeza de contexto.
// O `happy` entra porque a comemoracao acontece NO RODAPE — e por esta lista
// que sai a largura do slot. Medido: 49 px reduzido, contra os 69 que o
// sweeping ja impoe, entao ele nao engorda a fileira nem o custo do quadro.
const Slot NO_RODAPE[] = {S_IDLE, S_ALERT, S_TYPING, S_SLEEPING, S_AWAY,
                          S_VITORIA, S_SWEEPING};

// `substituto` e para onde cair quando o arquivo nao esta no cartao. Um cartao
// gravado por uma versao anterior perde uma animacao, nao a pagina inteira.
struct Arquivo { const char *path; Slot substituto; };

const Arquivo ARQUIVOS[S_COUNT] = {
    {"/clawd/idle.clw",       S_IDLE},      // turno encerrado
    {"/clawd/alert.clw",      S_TYPING},    // bloqueado: precisa de voce
    {"/clawd/typing.clw",     S_IDLE},      // trabalhando
    {"/clawd/sleeping.clw",   S_IDLE},      // estado desconhecido
    // "nenhuma sessao" usa going_away (o bicho indo embora). A `disconnected`
    // do clawd-tank desenha um logo de Bluetooth enorme: la ela significa
    // "perdi o link BLE", que nao e o que falta aqui.
    {"/clawd/going_away.clw", S_SLEEPING},  // nenhuma sessao
    // Turno CONCLUIDO: a turma comemora por alguns segundos e volta ao estado.
    // Cai no idle se faltar, que e para onde o turno encerrado ia antes de
    // existir comemoracao — o painel perde o aviso, nao a fileira.
    {"/clawd/happy.clw",      S_IDLE},      // acabou de terminar um turno

    // Dai para baixo, so a pagina 3: as outras caras do trabalho. Todas caem no
    // typing se faltarem, que e a que sempre esteve la.
    //
    // O sweeping continua carregado mesmo tendo saido do sorteio: ele e a cara
    // da limpeza de contexto, no rodape e aqui.
    {"/clawd/building.clw",   S_TYPING},
    {"/clawd/conducting.clw", S_TYPING},
    {"/clawd/sweeping.clw",   S_TYPING},
    // Dai para baixo, as caras geradas por imagem. Vao nas versoes PLENAS.
    //
    // A economica foi tentada aqui primeiro e ficou feia, por um motivo que vale
    // registrar: ela reduz para 110x50 e o conversor devolve isso ampliado 4x,
    // entao cada pixel do arquivo vira um bloco de 4x4 na tela. Em arte que
    // nasceu como pixel-art isso e o efeito desejado; nesta, que foi gerada com
    // gradiente e detalhe, e so um mosaico grosso.
    //
    // As plenas desenham em 160-192 px, a mesma faixa do typing (172x156) e do
    // conducting (152x156), com escala 1 — sem ampliar e sem reduzir, que e o
    // unico jeito de nao maltratar a arte. Custam ~1,2 MB de PSRAM somadas, de
    // 6,7 MB livres.
    //
    // A economica continua sendo a escolha certa no RODAPE, onde o sprite e
    // reduzido por divisor sobre o tamanho nativo. Sao dois caminhos de desenho
    // com incentivos opostos.
    {"/clawd/fawkes_scroll_reading.clw",  S_TYPING},
    {"/clawd/marks_hammering.clw",        S_TYPING},
    {"/clawd/arachnid.clw",               S_TYPING},
    {"/clawd/stormcaller.clw",            S_TYPING},
    {"/clawd/magnetic_regent.clw",        S_TYPING},
    {"/clawd/fawkes_lantern_signal.clw",  S_TYPING},
};

// Onde o bicho pisa. Todas as animacoes alinham a BASE DO CORPO nesta linha, e
// nao o centro do quadro: assim o Clawd fica na mesma altura em todas elas, em
// vez de subir e descer conforme o tamanho do quadro.
const int CHAO = 238;

uint8_t *blobs[S_COUNT] = {nullptr};
Sprite   sprites[S_COUNT];
bool     ok[S_COUNT] = {false};
int      ancoraX[S_COUNT] = {0};    // centro do corpo, em px nativos
int      ancoraBase[S_COUNT] = {0}; // base do corpo, em px nativos
bool     carregado = false;
char     falha[64]  = "";

// Resolve um slot para algum que exista de fato, seguindo os substitutos.
int resolver(int slot) {
    for (int passo = 0; passo < S_COUNT; passo++) {
        if (ok[slot]) return slot;
        const int prox = (int)ARQUIVOS[slot].substituto;
        if (prox == slot) break;          // cadeia terminou sem achar
        slot = prox;
    }
    for (int i = 0; i < S_COUNT; i++) if (ok[i]) return i;
    return -1;
}

uint16_t *scratch     = nullptr;    // um quadro decodificado, em PSRAM
size_t    scratchCap  = 0;

// DOIS bichos escolhidos, e nao um: o do rodape sempre diz o estado, e o da
// pagina 3 sorteia uma cara do trabalho quando o turno comeca. Antes era o
// mesmo indice, e sortear na pagina 3 trocaria o bicho do rodape junto.
//
// Cada um tem o proprio contador de quadros porque agora sao animacoes
// diferentes, com contagens de quadros diferentes: um indice so, compartilhado,
// apontaria para fora do outro arquivo e o quadro sairia em branco.
int      atual        = -1;         // rodape: o estado
int      quadroSelo   = 0;
uint32_t ultimoSelo   = 0;

int      grande       = -1;         // pagina 3: o estado, ou o sorteado
int      quadroGrande = 0;
uint32_t ultimoGrande = 0;
bool     trabalhava   = false;      // para sortear na VIRADA, e nao a cada leitura

int ultimoTrabalho = -1;   // a cara do turno ANTERIOR, nao a que esta em cena

// Sorteia uma cara do trabalho para a pagina 3.
//
// Fora a do turno anterior: com quatro opcoes, um quarto dos turnos cairia no
// mesmo bicho e pareceria que ninguem sorteou nada. O sorteio aqui e enfeite, e
// nao estatistica — o que ele precisa entregar e mudanca visivel.
//
// Tem que ser o turno ANTERIOR, e nao o que esta em cena: entre dois turnos o
// bicho volta para o sleeping, entao excluir "o que esta na tela" excluiria o
// sleeping — que nem esta no sorteio — e nao excluiria nada de verdade.
//
// A lista sai por resolver(), entao um arquivo que faltou no cartao vira o
// substituto dele; a busca por repetido tira as duplicatas que isso cria.
int sortearTrabalho() {
    int cand[N_TRABALHO];
    int n = 0;
    for (int i = 0; i < N_TRABALHO; i++) {
        const int r = resolver((int)TRABALHO[i]);
        if (r < 0 || r == ultimoTrabalho) continue;
        bool repetido = false;
        for (int k = 0; k < n; k++) if (cand[k] == r) { repetido = true; break; }
        if (!repetido) cand[n++] = r;
    }
    // Sobrou so a do turno passado (cartao com uma animacao de trabalho so):
    // repetir e melhor do que ficar sem bicho nenhum.
    if (!n) return resolver((int)S_TYPING);
    ultimoTrabalho = cand[esp_random() % (uint32_t)n];
    return ultimoTrabalho;
}

uint16_t *seloBuf = nullptr;     // buffer proprio: o selo e desenhado na mesma
size_t    seloCap = 0;           // passada que o sprite grande

// Largura FIXA de cada slot da fileira e altura da faixa toda. Fixa porque um
// slot que encolhesse ao trocar de cara empurraria os vizinhos pela tela — ver
// rowSlotX em lib/layout.
const int SLOTS = 4;
int  larguraSlot[SLOTS] = {0};
int  alturaTrio = 0;

// Altura alvo do ALERT, que e maior de proposito.
//
// O alert parecia menor que os vizinhos, e parecia porque era: a reducao mede o
// QUADRO, e no alert o quadro carrega o balao. Medido nos arquivos do cartao,
// em pixels de tela com o alvo de 42:
//
//   alert   quadro 120x98, corpo 40 de altura, divisor 3  -> caranguejo 13 px
//   typing  quadro  86x78, corpo 40,           divisor 2  -> caranguejo 20 px
//   debugger quadro 86x43, corpo 41,           divisor 2  -> caranguejo 20 px
//
// O balao ocupa 59% do quadro do alert, entao o mesmo alvo entrega um bicho 35%
// menor. Com alvo 49 o divisor cai para 2 e o caranguejo vai a 20 px, igual aos
// vizinhos — ao custo de 60 px de largura em vez de 40.
//
// Altura e de graca: a faixa do flush de prefixo tem ALTURA INTEIRA, entao so a
// largura entra no custo por quadro. Ver o orcamento em SELO_H.
const int SELO_ALERT = 49;

int seloAlvo(int slot) { return slot == S_ALERT ? SELO_ALERT : SELO_H; }

// Menor divisor inteiro que faz o quadro caber na altura pedida.
int seloDiv(const Sprite &sp, int alvo) {
    const int d = (sp.height + alvo - 1) / alvo;
    return d < 1 ? 1 : d;
}

// ---- Icone do cabecalho ----
// O wizard, recortado no personagem, no canto superior ESQUERDO.
//
// O lado importa e nao e estetica: a faixa do flush de prefixo e uma tira de
// ALTURA INTEIRA (x=0..92, y=0..319), entao ela cobre o topo e o rodape ao
// mesmo tempo. Um sprite a esquerda entra no envio que ja acontece pelo rodape
// e anima de graca. No lugar antigo, ao lado do relogio (x~284), o prefixo
// teria que ir ate 320 colunas: 32 ms por quadro, ~26% de CPU so por enfeite.
//
// Qual recorte usar. NAO ha regra unica — medido em tres sprites:
//
//   CORPO   pixels estaveis, maior faixa contigua. Descarta enfeite que passeia
//           longe (as faiscas do wizard, o anel do beacon).
//   UNIAO   tudo por onde qualquer quadro passou. Necessario quando o
//           personagem se mexe demais para ter pixel estavel: no confused nada
//           acima da linha 69 aparece em 60% dos quadros, e a regra do corpo
//           entrega um tronco sem cabeca.
//
//   QUADRO  nenhum recorte. Serve para os bichos do rodape: la o quadro inteiro
//           reduzido ja e o caminho das cinco animacoes de estado, e escolher
//           recorte exigiria olhar cada arquivo renderizado fora da placa.
//
// A escolha e por arquivo e sai de olhar os dois renderizados fora da placa.
enum Recorte { R_CORPO, R_UNIAO, R_QUADRO };

// Um sprite pequeno e animado. O blob FICA residente: animar exige todos os
// quadros. Custa ~100 KB por icone, contra 8 MB de PSRAM.
struct Icone {
    uint8_t  *blob = nullptr;
    Sprite    sp;
    SpriteBox box;
    int       div = 1;
    uint16_t *buf = nullptr;
    int       w = 0, h = 0;
    int       quadro = 0;
    uint32_t  ultimoMs = 0;
};

bool carregarIcone(const char *path, int alturaAlvo, Recorte modo, Icone &ic) {
    size_t len = 0;
    ic.blob = storage::readFileToPsram(path, len);
    if (!ic.blob) return false;

    ic.sp = parseSprite(ic.blob, len);
    if (!ic.sp.valid) { free(ic.blob); ic.blob = nullptr; return false; }

    // A caixa e calculada UMA vez e vale para todos os quadros: recalcular por
    // quadro faria o bicho pular de tamanho a cada frame.
    bool temCaixa = false;
    if (modo == R_QUADRO) {
        // O quadro inteiro. decodeCropDown com esta caixa e exatamente
        // decodeFrameDown, entao nao ha caminho de codigo separado para manter.
        ic.box.x = 0; ic.box.y = 0;
        ic.box.w = ic.sp.width; ic.box.h = ic.sp.height;
        temCaixa = true;
    } else if (modo == R_UNIAO) {
        temCaixa = spriteUnionBox(ic.sp, ic.box);
    } else {
        const size_t nativo = (size_t)ic.sp.width * ic.sp.height;
        uint8_t *conta = (uint8_t *)ps_malloc(nativo);
        temCaixa = conta && spriteBodyBox(ic.sp, conta, nativo, ic.box);
        if (conta) free(conta);
    }
    if (!temCaixa) { free(ic.blob); ic.blob = nullptr; return false; }

    ic.div = (ic.box.h + alturaAlvo - 1) / alturaAlvo;
    if (ic.div < 1) ic.div = 1;
    ic.w = boxDownW(ic.box, ic.div);
    ic.h = boxDownH(ic.box, ic.div);

    ic.buf = (uint16_t *)ps_malloc((size_t)ic.w * ic.h * sizeof(uint16_t));
    if (!ic.buf) { free(ic.blob); ic.blob = nullptr; return false; }
    return true;
}

bool tickIcone(Icone &ic, uint32_t nowMs) {
    if (!ic.buf || (nowMs - ic.ultimoMs) < ic.sp.frameMs) return false;
    ic.ultimoMs = spriteRearme(ic.ultimoMs, nowMs, ic.sp.frameMs);
    ic.quadro   = (ic.quadro + 1) % ic.sp.frames;
    return true;
}

// A ESCALA E APLICADA NO BLIT, e nao na decodificacao, e a razao e memoria: o
// buffer do icone tem o tamanho do quadro decodificado, entao decodificar
// AMPLIADO nao caberia nele — e o Clawd dormindo (192x169) e justamente o que
// precisa crescer para ocupar a faixa deitada.
//
// Aqui o quadro e decodificado como sempre e reamostrado linha a linha na saida
// (vizinho mais proximo), com uma linha de rascunho de 480 px — a largura
// maxima da tela. Fica em `static` por causa da pilha da loopTask, que ja
// estourou uma vez com um buffer grande de funcao (ver o tinfl em storage.cpp).
uint16_t g_linhaEscala[480];

// `num`/`den` e a escala do desenho (1:1 = tamanho nativo). Ela reduz ou
// amplia; quem escolhe a razao e quem sabe o tamanho da caixa (ver
// escalaParaCaber, em lib/layout).
bool desenharIcone(Icone &ic, Arduino_Canvas *g, int x, int y,
                   int num = 1, int den = 1) {
    if (!ic.buf) return false;
    if (!decodeCropDown(ic.sp, ic.quadro, ic.box, ic.div,
                        ic.buf, (size_t)ic.w * ic.h, ic.sp.key))
        return false;

    if (num == den || num < 1 || den < 1) {
        g->draw16bitRGBBitmapWithTranColor(x, y, ic.buf, ic.sp.key, ic.w, ic.h);
        return true;
    }

    int ow = ic.w * num / den, oh = ic.h * num / den;
    if (ow < 1 || oh < 1) return false;
    if (ow > (int)(sizeof(g_linhaEscala) / sizeof(g_linhaEscala[0])))
        ow = sizeof(g_linhaEscala) / sizeof(g_linhaEscala[0]);

    for (int oy = 0; oy < oh; oy++) {
        const uint16_t *src = ic.buf + (size_t)(oy * den / num) * ic.w;
        for (int ox = 0; ox < ow; ox++) g_linhaEscala[ox] = src[ox * den / num];
        g->draw16bitRGBBitmapWithTranColor(x, y + oy, g_linhaEscala, ic.sp.key,
                                           ow, 1);
    }
    return true;
}

// Altura dos icones do cabecalho. Cabe entre o topo e a linha do cabecalho (42).
//
// O canto superior ESQUERDO vive dentro da faixa do envio de prefixo, entao o
// que estiver ali anima por ~9 ms por quadro em vez dos ~64 de um flush
// inteiro. E o mesmo alvo para o mago e para os bichos do rodizio.
const int ICONE_H = 36;

// O mago: a reserva do cabecalho, para quando nenhum bicho do rodizio carregou.
Icone mago;

// O Clawd da COMEMORACAO — o centro da fileira da tela nova durante a festa.
// Residente como os outros icones da faixa: ele anima, entao o blob inteiro
// fica. Ausente no cartao/flash, o centro simplesmente nao troca.
//
// A ESCOLHA SAIU DE MEDIR O ELENCO INTEIRO, e nao de gosto. A fatia do centro
// tem ~58x52 px, e o que decide o tamanho na tela nao e o arquivo: e quanto
// dele e PERSONAGEM. O divisor da reducao e inteiro, entao um sprite com muito
// vazio chega pequeno mesmo ocupando um quadro grande.
//
//   happy (o Clawd pulando)   124x89   64% de ar   ->  62x44, personagem ~16 px
//   grooving                  110x60   12% de ar   ->  55x30, baixo demais
//   eureka                    108x90   51% de ar   ->  54x45, personagem ~22 px
//   marks_celebrate           192x166  28% de ar   ->  48x41, personagem ~30 px
//
// O `happy` foi a primeira tentativa e e o pior caso: o pulo e as faiscas
// espalham o desenho pelo quadro todo, entao o bicho chegava com metade da
// altura dos vizinhos — foi essa desproporcao que apareceu na tela. Nenhum
// sprite do Clawd COMUM comemorando cabe proporcional aqui; o do Marx cabe, e
// com 41 px ele fica na altura exata de Stan, Kenny e Kyle.
Icone festaIc;

// O Token da tela de limite estourado (retrato). Quatro poses de 900 ms — os
// olhos giram, o corpo nao se mexe. O arquivo ja vem no tamanho da tela
// (224x336), entao o divisor e 1 e o blob decodifica direto no buffer do
// icone: ~150 KB de PSRAM residente, o preco de aparecer sem ler cartao na
// hora em que o limite estoura.
Icone tokenIc;

// O mesmo bicho da tela do Token, com outro recado na camisa: e ele que aparece
// quando a API para de responder.
//
// Este NAO fica residente, e o Token fica. A diferenca e o gatilho: o limite
// estoura no meio de um poll normal, e ler 292 KB de cartao naquele instante
// atrasaria a tela do aviso; o servidor cair ja e um evento de espera, e a
// leitura cabe dentro dela. Sao ~150 KB de PSRAM que ficam livres o dia inteiro
// para a foto de tela.
Icone offlineIc;

// O bicho da tela de RESET, o par do Token: um limite estourou e o outro acabou
// de liberar.
//
// A lista e um RODIZIO — cada vez que a tela e armada ela troca de personagem,
// e o recado embaixo nao muda, so quem o entrega. Hoje ela tem UM: a danca do
// Cartman saiu porque nao ficou boa na tela, e um rodizio de um so continua
// sendo a forma certa aqui. Quem entrar depois entra nesta lista e mais nada.
//
// Um por vez porque cada arquivo destes passa de 400 KB: um rodizio residente
// seriam megabytes de PSRAM parados o dia inteiro por uma tela que aparece 6
// segundos.
//
// O preco e uma leitura de cartao no instante em que a tela abre. E o mesmo
// preco da troca de tema, e cabe: a tela nasce de um toque ou de um poll, nunca
// no meio de uma animacao.
const char *RESET_ARQ[] = {
    "/clawd/sp_kenny_reset.clw",
};
const int RESET_N = (int)(sizeof(RESET_ARQ) / sizeof(RESET_ARQ[0]));
int       resetAtual = 0;

// Teto de altura do bicho da tela de RESET, e nao a altura de nenhum deles: cada
// arquivo ja vem no tamanho em que vai aparecer, e o alvo so existe para o
// divisor sair 1. Quem passar disso e reduzido em vez de invadir o recado —
// R_RESET_Y (54) mais 336 para em 390, e o texto comeca em 406.
const int RESET_ALT_MAX = 336;

Icone resetIc;

// ---- Rodizio do icone do cabecalho ----
// Um bicho diferente a cada dez minutos, sorteado desta lista. Ele ANIMA: este
// canto mora na faixa do flush de prefixo, onde o quadro custa ~9 ms em vez dos
// ~64 de uma tela inteira — foi por isso que o icone do cabecalho nasceu aqui.
const char *RODIZIO[] = {
    "arachnid", "beacon", "builder", "building", "carrying",
    "clawd_cold", "clawd_counting_money", "clawd_eating_tokens", "clawd_hot",
    "clawd_level_001", "clawd_marks", "clawd_swim_briefs", "clawd_swimwear",
    "clawd_very_cold", "clawd_very_hot", "conducting", "debugger",
    "hat_mishap", "magnetic_regent", "solar_mind", "ruby_visor",
    "stormcaller", "sweeping",
};
const int RODIZIO_N   = sizeof(RODIZIO) / sizeof(RODIZIO[0]);
const uint32_t RODIZIO_MS = 10UL * 60UL * 1000UL;      // dez minutos

Icone    rodizio;
int      rodizioAtual = -1;      // indice em cena, para nao sortear o mesmo
uint32_t rodizioMs    = 0;       // quando entrou em cena

// ---- Icone de clima ----
// As cinco faixas, do frio para o quente. `ateC` e o teto INCLUSIVE da faixa; a
// ultima pega tudo o que sobrar.
//
// A roupa de banho cobre o tempo ameno de proposito: nao existe sprite de "dia
// sem graca", e deixar buraco no cabecalho seria pior do que esticar um pouco o
// sentido do desenho.
// `alturaC` e a altura ALVO daquele arquivo, e nao uma so para os cinco.
//
// A reducao e por divisor INTEIRO, entao um alvo unico nao entrega tamanhos
// parecidos: com 30 para todos, medido na placa, saiam
//
//   very_cold 31x25 (div 6)   cold 37x28 (div 3)   swimwear 48x26 (div 4)
//   hot       44x23 (div 4)   very_hot 27x27 (div 6)
//
// Dois ficavam visivelmente menores, e nao pela altura — que estava na media —
// mas pela LARGURA, porque o recorte do corpo deles sai estreito e alto:
// very_hot com 27 px e very_cold com 31, contra os 44 e 48 dos vizinhos. Subir
// o alvo dos dois para 34 derruba o divisor de 6 para 5 e os leva a ~32x32 e
// ~37x30.
//
// Mexer aqui exige MEDIR de novo, e nao olhar a tela: entre dois alvos o
// divisor so muda em degraus, entao mudar o numero quase sempre nao faz nada e
// de vez em quando faz muito.
struct FaixaClima { int ateC; const char *arq; int alturaC; };
const FaixaClima CLIMA[] = {
    {11,  "/clawd/clawd_very_cold.clw", 34},
    {19,  "/clawd/clawd_cold.clw",      30},
    {27,  "/clawd/clawd_swimwear.clw",  30},
    {34,  "/clawd/clawd_hot.clw",       30},
    {999, "/clawd/clawd_very_hot.clw",  34},
};
const int CLIMA_N = (int)(sizeof(CLIMA) / sizeof(CLIMA[0]));

// ANIMADO, em teste. Comecou como quadro parado (IconeParado) porque o lado
// direito do cabecalho esta fora da faixa do flush de prefixo: cada quadro aqui
// custa uma tela inteira, e o cabecalho aparece em TODAS as paginas. A 6 fps do
// arquivo isso da ~38% de CPU permanentes. Medir na placa e o unico jeito de
// decidir se o bicho vale o preco.
Icone clima;
int   climaFaixa = -1;          // -1 = nenhuma carregada ainda

// Indices na tabela CLIMA, para o override dos extremos poder aponta-los pelo
// nome em vez de por numero solto.
const int F_MUITO_FRIO = 0;
const int F_MUITO_CALOR = CLIMA_N - 1;

// A faixa ABSOLUTA, so pela temperatura. E o piso da decisao.
int faixaAbsoluta(int tempC) {
    for (int i = 0; i < CLIMA_N; i++)
        if (tempC <= CLIMA[i].ateC) return i;
    return CLIMA_N - 1;
}

// A faixa final: a absoluta, com os EXTREMOS DO DIA por cima.
//
// Estar no pico do dia diz uma coisa que o numero sozinho nao diz. Com 21-30,
// 30 graus e o momento mais quente daquele dia e merece a cara de muito calor;
// 29, no mesmo dia, e so calor. Sem isto, um dia inteiro de 21 a 27 nunca sairia
// da roupa de banho, e o frio da manha e o pico da tarde ficariam iguais.
//
// `>=` e `<=` e nao igualdade exata: se a maxima prevista era 30 e o dia bateu
// 31, isso e ainda MAIS extremo. Com igualdade estrita, justamente o momento
// mais quente perderia o sprite mais quente.
//
// Sem amplitude (minima igual a maxima) o override nao vale: "extremo" nao
// significa nada quando nao ha intervalo, e ali os dois testes dariam verdade
// ao mesmo tempo.
int faixaDe(int tempC, int tminC, int tmaxC) {
    if (tmaxC > tminC) {
        if (tempC >= tmaxC) return F_MUITO_CALOR;
        if (tempC <= tminC) return F_MUITO_FRIO;
    }
    return faixaAbsoluta(tempC);
}


// ---- Os dois companheiros do rodape ----
// Eles NAO indicam estado sozinhos: acompanham o bicho da esquerda, porque e o
// mesmo turno. Quatro caras:
//
//   descanso   sleeping | idle     | marks_rest
//   trabalho   typing   | building | marks_hammering
//   ALERTA     alert    | alert    | alert        <- os tres, e nao so um
//   LIMPEZA    sweeping | sweeping | sweeping     <- os tres, idem
//
// Nas duas ultimas os tres mostram o MESMO arquivo de proposito. Bloqueio e
// limpeza sao eventos da maquina inteira, nao papeis diferentes de cada bicho:
// com um so avisando, o recado ficava do tamanho de um caranguejo num rodape de
// 480 px, e era exatamente essa a queixa.
//
// Cada arquivo repetido custa uma copia na PSRAM (o alert sao 44 KB, o sweeping
// 24 KB) contra 8 MB disponiveis — o preco de nao ter dois donos para o mesmo
// blob.
const int CARAS = 5;
enum Cara { C_DESCANSO = 0, C_TRABALHO = 1, C_ALERTA = 2, C_LIMPEZA = 3,
            C_VITORIA = 4 };
// As duas caras do Marks entram na variante _eco3, e a escolha e medida. O
// rodape reduz o quadro por divisor INTEIRO e ignora o campo de ampliacao,
// entao o que vale aqui e o tamanho NATIVO.
//
// O que importa e o PERSONAGEM desenhado, e nao a moldura. Com o alvo de 42:
//
//   vizinhos            typing 40x39 | building 55x37 | idle 36x25
//   rest _eco4  110x50 /2 -> 35x24    hammering _eco4  110x50 /2 -> 27x24
//   rest _eco3  146x66 /2 -> 46x32    hammering _eco3  146x66 /2 -> 37x32
//   rest plena  192x129/4 -> 46x32    hammering plena  192x165/4 -> 47x41
//
// As economicas normalizam TODA pose para a mesma tela — 110x50 ou 146x66 — e
// quem tem pose estreita gasta metade dela em vazio: o martelo sai com 37 px de
// largura onde o descanso sai com 46. As plenas nao: elas vem recortadas justas
// (crop_sprites), entao cada uma usa a moldura inteira.
//
// Por isso aqui vao as PLENAS, contra a regra que vale para a pagina 3. O
// recorte justo faz as duas caberem em 48 px de largura — mais estreitas que as
// economicas de 73 — e o slot da direita volta a ser governado pelo sweeping,
// em 69. Custam 359 KB de PSRAM contra 36, de 7 MB disponiveis.
// A terceira coluna e o Sonic, e ele so tem TRES caras: o jogo nao tem pose de
// limpeza, entao a de limpeza repete a de trabalho — melhor um bicho ocupado do
// que um buraco no rodape quando a compactacao roda.
//
// As poses e a cadencia foram escolhidas pelo que LE a 38 px:
//   descanso  espera do jogo ($05) — fica parado e bate o pe, impaciente
//   trabalho  correndo ($01) a 70 ms — a unica corrida que o jogo tem
//   alerta    chamando com o dedo ($13) — o unico gesto que chama alguem
//
// A corrida so tem QUATRO quadros, entao o que a faz parecer corrida e a
// cadencia: a 110 ms ela parecia um trote, e a 70 ms as pernas em oito giram.
// O spindash foi tentado no trabalho e nao convenceu. O ficar de pe ($0A) saiu
// do descanso por nao ter gesto nenhum para notar.
//
// Os arquivos dele ja saem do conversor com 38 px de altura, a mesma dos
// vizinhos (typing 39, building 37): a placa reduz por divisor INTEIRO, e a
// altura nativa de 40-48 cairia em divisor 2 e sairia pela metade.
// A quinta cara e a COMEMORACAO. Como o alerta e a limpeza, ela vale para a
// turma inteira: um turno que acaba e um evento da maquina, e nao um papel
// diferente de cada bicho.
//
// O sonic_vitoria e o AniSonic22, a pose de fim de fase (braco erguido). Ele
// sai do conversor com 30x38 e nao puxa o slot do Sonic, que e o mais estreito
// da fileira — se puxasse, a comemoracao custaria colunas de prefixo em TODA
// volta do laco, e nao so enquanto durasse.
// ---- Temas da fileira ----
// Um tema diz QUEM sao os quatro do topo. O padrao guarda o slot 0 para o bicho
// do ESTADO, que nao vem de arquivo proprio: ele reaproveita a animacao grande
// ja carregada e reduzida, e por isso a primeira linha da tabela fica vazia.
//
// Um tema de ELENCO FECHADO preenche os quatro e nao troca de cara: quem conta
// o estado ali e o resto da tela — a bolinha, a barra, o realce da linha. Foi a
// escolha para o South Park, onde nao existe uma pose de "compactando" que se
// reconheca a 40 px.
struct Tema {
    const char *nome;
    bool        estadoNoSlot0;
    const char *arq[SLOTS][CARAS];
};

const Tema TEMAS[] = {
    {"padrao", true, {
        {nullptr, nullptr, nullptr, nullptr, nullptr},
        {"/clawd/idle.clw",           "/clawd/building.clw",
         "/clawd/alert.clw",          "/clawd/sweeping.clw",
         "/clawd/grooving.clw"},
        {"/clawd/marks_rest.clw",     "/clawd/marks_hammering.clw",
         "/clawd/alert.clw",          "/clawd/sweeping.clw",
         "/clawd/marks_celebrate.clw"},
        {"/clawd/sonic_descanso.clw", "/clawd/sonic_trabalho.clw",
         "/clawd/sonic_alerta.clw",   "/clawd/sonic_trabalho.clw",
         "/clawd/sonic_vitoria.clw"},
    }},
    // Os quatro do desenho, com as cinco caras cada. Todos de CORPO INTEIRO e
    // na mesma escala — a cabeca mede 28 px em todos os vinte arquivos, entao
    // quem senta fica menor que quem esta de pe, como no desenho. As alturas
    // ficam entre 30 e 42, abaixo do alvo: divisor 1, tamanho nativo na tela.
    //
    // Continua elenco fechado no sentido que importa: ninguem vai embora
    // sozinho. Mas as caras agora EXISTEM, entao estadoNoSlot0 continua false
    // so porque o slot 0 e do Cartman, nao do bicho do estado.
    {"south park", false, {
        {"/clawd/sp_cartman_descanso.clw", "/clawd/sp_cartman_trabalho.clw",
         "/clawd/sp_cartman_alerta.clw",   "/clawd/sp_cartman_limpeza.clw",
         "/clawd/sp_cartman_vitoria.clw"},
        {"/clawd/sp_stan_descanso.clw",    "/clawd/sp_stan_trabalho.clw",
         "/clawd/sp_stan_alerta.clw",      "/clawd/sp_stan_limpeza.clw",
         "/clawd/sp_stan_vitoria.clw"},
        {"/clawd/sp_kenny_descanso.clw",   "/clawd/sp_kenny_trabalho.clw",
         "/clawd/sp_kenny_alerta.clw",     "/clawd/sp_kenny_limpeza.clw",
         "/clawd/sp_kenny_vitoria.clw"},
        {"/clawd/sp_kyle_descanso.clw",    "/clawd/sp_kyle_trabalho.clw",
         "/clawd/sp_kyle_alerta.clw",      "/clawd/sp_kyle_limpeza.clw",
         "/clawd/sp_kyle_vitoria.clw"},
    }},
};

const int N_TEMAS = (int)(sizeof(TEMAS) / sizeof(TEMAS[0]));

// Com qual tema o painel ABRE. E o South Park: o padrao continua na roda do
// duplo toque, mas quem liga a placa quer ver a turma do desenho.
//
// Nao basta comecar com `temaAtual` neste valor, e a razao esta em
// `larguraPadrao`: a grade da fileira e a do tema 0, e ela e medida com os
// icones dele carregados. Por isso o boot ainda ABRE no padrao, mede, e so
// entao troca — ver o fim de `begin()`.
const int TEMA_INICIAL = 1;

// Qual esta no ar. E VARIAVEL: o duplo toque na fileira roda os temas, e a troca
// acontece com a placa ligada — libera os icones antigos, carrega os novos e
// remede a fileira.
int temaAtual = 0;

const Tema &tema() { return TEMAS[temaAtual]; }
// Primeiro slot que vem de arquivo. No tema padrao o zero e do bicho do estado.
int slot0() { return tema().estadoNoSlot0 ? 1 : 0; }

Icone comp[SLOTS][CARAS];

// O bicho do ESTADO, medido uma vez no carregamento e guardado aqui. Ele vale
// so no tema padrao, mas e medido sempre: sem isso, voltar do South Park para o
// padrao exigiria varrer as animacoes de novo com a placa ligada.
int seloW = 0, seloH = 0;

// Carrega os quatro do tema em cena. Mora fora do begin() porque a troca de
// tema, com a placa ligada, precisa exatamente disto outra vez.
void carregarTurma() {
    for (int i = slot0(); i < SLOTS; i++)
        for (int c = 0; c < CARAS; c++) {
            // Cara repetida dentro do mesmo slot: um Icone so, copiado. Um tema
            // de elenco fechado aponta as cinco para o mesmo arquivo, e carregar
            // cinco vezes seriam cinco copias do blob para desenhar sempre o
            // mesmo bicho.
            int igual = -1;
            for (int k = 0; k < c; k++)
                if (tema().arq[i][k] == tema().arq[i][c]) { igual = k; break; }
            if (igual >= 0) { comp[i][c] = comp[i][igual]; continue; }
            carregarIcone(tema().arq[i][c], c == C_ALERTA ? SELO_ALERT : SELO_H,
                          R_QUADRO, comp[i][c]);
        }
}

// Devolve a PSRAM da turma. Cara repetida aponta para o MESMO blob — liberar
// por posicao daria dois `free` no mesmo ponteiro, e o segundo derruba a placa.
void liberarTurma() {
    for (int i = 0; i < SLOTS; i++)
        for (int c = 0; c < CARAS; c++) {
            uint8_t *blob = comp[i][c].blob;
            if (!blob) { comp[i][c] = Icone(); continue; }

            // Zera as copias ANTES de liberar, olhando para a frente. Olhar
            // para tras nao funciona: as caras ja visitadas foram zeradas, o
            // ponteiro delas virou nulo e a comparacao nunca casa — era esse o
            // free duplo que reiniciava a placa no primeiro duplo toque.
            uint16_t *buf = comp[i][c].buf;
            for (int k = c; k < CARAS; k++)
                if (comp[i][k].blob == blob) comp[i][k] = Icone();
            free(blob);
            free(buf);
        }
}

// A grade do tema PADRAO, guardada na primeira medida — e o boot sempre comeca
// nele, entao ela existe antes de qualquer troca.
//
// Os outros temas ADOTAM esta grade em vez de sair da propria medida. Os quatro
// do South Park sao bem mais estreitos que os do padrao (42+33+32+30 contra
// 69+69+69+sonic), e medidos por si mesmos a fileira encolhia para menos de dois
// tercos: os bichos ficavam amontoados na margem esquerda, cada um numa posicao
// que nao correspondia a nenhuma do padrao. Com a grade emprestada, cada um cai
// CENTRADO no slot do colega que ele substitui, e trocar de tema deixa de mover
// a fileira de lugar.
//
// O preco e o custo por quadro, que passa a ser o do padrao em todos os temas —
// o prefixo do flush sai da largura da fileira. E o mesmo custo que a placa ja
// paga desde sempre no tema que abre.
int larguraPadrao[SLOTS] = {0};

// Larguras de slot e altura da faixa, do zero. Cada slot fica com a maior das
// caras dele, para nao deslizar quando o turno vira.
void medirFileira() {
    for (int i = 0; i < SLOTS; i++) larguraSlot[i] = 0;
    alturaTrio = 0;
    if (tema().estadoNoSlot0) { larguraSlot[0] = seloW; alturaTrio = seloH; }
    for (int i = slot0(); i < SLOTS; i++)
        for (int c = 0; c < CARAS; c++) {
            if (!comp[i][c].buf) continue;
            if (comp[i][c].w > larguraSlot[i]) larguraSlot[i] = comp[i][c].w;
            if (comp[i][c].h > alturaTrio)     alturaTrio = comp[i][c].h;
        }

    // A grade sai do PADRAO (ver larguraPadrao). O tema 0 e ele, e e sempre o
    // primeiro a ser medido — os outros so alargam o slot quando o proprio
    // sprite nao caberia nele, porque `centerIn` alinha a esquerda no que
    // estoura e o bicho invadiria o vizinho.
    if (temaAtual == 0) {
        for (int i = 0; i < SLOTS; i++) larguraPadrao[i] = larguraSlot[i];
    } else {
        for (int i = 0; i < SLOTS; i++)
            if (larguraPadrao[i] > larguraSlot[i]) larguraSlot[i] = larguraPadrao[i];
    }
}

int  cara    = C_DESCANSO;
bool sozinho = false;    // sem sessao: o bicho da esquerda vai embora sozinho

// Cadencia MAXIMA de envio da faixa.
//
// ERA 100 ms (10 envios/s), calibrado quando os arquivos mais rapidos pediam
// 125-167 ms por quadro — nenhum chegava perto do teto, e ele so evitava o
// desperdicio de mandar quadros quase identicos.
//
// O CHAO DA FILEIRA BAIXOU (ver tools/ritmo_sprite.py: frame_ms 33, 30 fps) e
// este numero NAO acompanhou. Resultado: os CONTADORES avancavam certos por
// baixo do pano, mas o que ia para a tela continuava preso em 10 por segundo —
// a fileira parecia tao lenta quanto antes mesmo depois do arquivo e do laco
// ficarem mais rapidos, porque o gargalo real nunca foi nenhum dos dois, foi
// este teto. Baixado para acompanhar o arquivo mais rapido, e nao mais para
// segura-lo.
const uint32_t ENVIO_MIN_MS = 33;
uint32_t ultimoEnvio = 0;
bool     pendente    = false;

Slot slotDe(const Status &s, bool temDados) {
    if (!temDados || s.agents.empty()) return S_AWAY;
    const AgentState st = overallState(s);

    // Bloqueio na frente de tudo: "precisa de voce" e a unica coisa desta tela
    // que nao pode ser encoberta por outra.
    if (st == AgentState::Blocked) return S_ALERT;

    // Limpeza em curso ganha do resto. Fica ANTES do switch, e nao dentro do
    // caso `working`, porque o estado publicado durante uma compactacao nao e
    // garantido — e o retorno do botao nao pode depender disso.
    if (s.cleaning) return S_SWEEPING;

    switch (st) {
        case AgentState::Working: return S_TYPING;
        // Todos ociosos: o bicho DORME. A animacao `idle` do clawd-tank e um
        // "idle vivo", com o caranguejo se mexendo — o que sugere atividade
        // justamente quando nao ha nenhuma.
        case AgentState::Idle:    return S_SLEEPING;
        // Sem hooks instalados: nao da para afirmar nada. Fica o idle vivo, que
        // diz "tem alguem ai" sem alegar o que essa pessoa esta fazendo.
        default:                  return S_IDLE;
    }
}

}  // namespace

namespace clawd {

void begin() {
    // Primeiro e independentes: cabecalho e rodape aparecem em TODAS as
    // paginas, entao os icones deles nao podem depender das animacoes de
    // estado terem carregado.
    // O mago e PARADO (ver o tick). Fica no quadro de maior area opaca e nao no
    // zero: em animacao de personagem o quadro 0 costuma ser a pose de repouso,
    // a mais vazia, e o icone sairia quase em branco.
    if (carregarIcone("/clawd/wizard.clw", ICONE_H, R_CORPO, mago))
        mago.quadro = spriteBestFrame(mago.sp);
    // O alvo de altura e a propria altura do arquivo: divisor 1, sem reducao.
    carregarIcone("/clawd/sp_token.clw", 336, R_QUADRO, tokenIc);
    carregarIcone(RESET_ARQ[resetAtual], RESET_ALT_MAX, R_QUADRO, resetIc);
    // 46 e nao SELO_H (42), e a diferenca de 4 px vale um degrau INTEIRO de
    // reducao: com 42 o divisor sobe para 4 e a caixa cai para 48x41; com 46
    // ele fica em 4 tambem, mas o alvo passa a ser a altura que os vizinhos
    // tem de fato. Ver a tabela acima — o numero foi calibrado contra eles.
    carregarIcone("/clawd/marks_celebrate.clw", 46, R_QUADRO, festaIc);
    carregarTurma();

    size_t maiorQuadro = 0, maiorNativo = 0;
    int    faltando = 0;
    for (int i = 0; i < S_COUNT; i++) {
        size_t len = 0;
        blobs[i] = storage::readFileToPsram(ARQUIVOS[i].path, len);
        if (blobs[i]) sprites[i] = parseSprite(blobs[i], len);
        // Uma animacao ausente cai no substituto; nao derruba a pagina inteira.
        // Assim um cartao gravado por uma versao anterior continua servindo.
        if (!blobs[i] || !sprites[i].valid) {
            if (blobs[i]) { free(blobs[i]); blobs[i] = nullptr; }
            if (!faltando) snprintf(falha, sizeof(falha), "sem %s", ARQUIVOS[i].path);
            faltando++;
            continue;
        }
        ok[i] = true;
        const size_t px = spriteBufPixels(sprites[i]);
        if (px > maiorQuadro) maiorQuadro = px;
        const size_t nat = (size_t)sprites[i].width * sprites[i].height;
        if (nat > maiorNativo) maiorNativo = nat;
    }
    if (!maiorQuadro) {
        snprintf(falha, sizeof(falha), "cartao sem /clawd");
        return;
    }
    if (faltando > 1)
        snprintf(falha, sizeof(falha), "%d animacoes faltando", faltando);

    // Ancora do corpo, calculada AQUI e nao no conversor: assim um .clw ja
    // gravado num cartao continua valendo, sem precisar reescrever o cartao a
    // cada ajuste de enquadramento.
    uint8_t *conta = (uint8_t *)ps_malloc(maiorNativo);
    if (!conta) {
        snprintf(falha, sizeof(falha), "sem PSRAM p/ ancora");
        return;
    }
    const uint32_t t0 = millis();
    for (int i = 0; i < S_COUNT; i++)
        if (!ok[i] || !spriteBodyAnchor(sprites[i], conta, maiorNativo,
                                        ancoraX[i], ancoraBase[i])) {
            // Sem ancora, cai no centro do quadro: pior enquadramento, mas
            // ainda desenha.
            ancoraX[i]    = sprites[i].width / 2;
            ancoraBase[i] = sprites[i].height;
        }
    const uint32_t custo = millis() - t0;
    free(conta);

    // Um scratch so, do tamanho do maior quadro: cinco buffers separados
    // custariam PSRAM a toa, e nunca ha dois em tela ao mesmo tempo.
    scratch = (uint16_t *)ps_malloc(maiorQuadro * sizeof(uint16_t));
    if (!scratch) {
        snprintf(falha, sizeof(falha), "sem PSRAM p/ %u px", (unsigned)maiorQuadro);
        return;
    }
    scratchCap = maiorQuadro;
    carregado  = true;

    // Bicho da esquerda: reaproveita as animacoes ja carregadas, reduzidas. Sem
    // arquivo novo e sem PSRAM extra alem de um buffer pequeno.
    //
    // So as de NO_RODAPE. O building e o conducting so aparecem grandes, na
    // pagina 3; medi-los aqui inflaria o slot da esquerda para acomodar bichos
    // que nunca pisam no rodape, e cada pixel de largura e uma coluna a mais no
    // flush de cada quadro.
    //
    // A medida sai daqui mesmo sob um tema que nao desenha o selo: e ela que
    // permite VOLTAR ao padrao com a placa ligada, sem varrer as animacoes de
    // novo. Quem decide se ela entra na fileira e medirFileira().
    size_t maiorSelo = 0;
    for (int k = 0; k < (int)(sizeof(NO_RODAPE) / sizeof(NO_RODAPE[0])); k++) {
        const int i = (int)NO_RODAPE[k];
        if (!ok[i]) continue;
        const int div = seloDiv(sprites[i], seloAlvo(i));
        const size_t px = spriteDownPixels(sprites[i], div);
        if (px > maiorSelo) maiorSelo = px;

        const int dw = spriteDownW(sprites[i], div);
        const int dh = spriteDownH(sprites[i], div);
        if (dh > seloH) seloH = dh;

        // O going_away fica FORA da largura do slot: ele aparece sozinho, e o
        // quadro dele e largo e baixo (170x61 nativos, 85 px reduzido). Contado
        // aqui, ele dobraria o slot do estado e a fileira inteira andaria para
        // a direita para acomodar uma cara que nunca aparece com as outras.
        if (i != S_AWAY && dw > seloW) seloW = dw;
    }
    if (maiorSelo) {
        seloBuf = (uint16_t *)ps_malloc(maiorSelo * sizeof(uint16_t));
        if (seloBuf) seloCap = maiorSelo;
    }

    medirFileira();

    // A troca para o tema de abertura acontece AQUI, e nao no valor inicial de
    // `temaAtual`: a medida acima e a que grava `larguraPadrao`, a grade que
    // todos os outros temas emprestam. Comecar direto no South Park deixaria a
    // grade zerada e a fileira sairia amontoada na margem esquerda.
    //
    // O preco e uma segunda leitura de cartao no boot — os mesmos ~300 ms que
    // o duplo toque na fileira ja custa, pagos uma vez ao ligar.
    if (TEMA_INICIAL != temaAtual && TEMA_INICIAL < N_TEMAS) {
        liberarTurma();
        temaAtual = TEMA_INICIAL;
        carregarTurma();
        medirFileira();
    }

    Serial.printf("clawd: %d/%d animacoes, scratch %u KB, ancoras em %lums\n",
                  S_COUNT - faltando, S_COUNT,
                  (unsigned)(maiorQuadro * 2 / 1024), (unsigned long)custo);
    Serial.printf("clawd: tema %s, fileira %d+%d+%d+%d = %d px (prefixo ~%d col)\n",
                  tema().nome, larguraSlot[0], larguraSlot[1], larguraSlot[2],
                  larguraSlot[3], rowWidth(larguraSlot, SLOTS, VAO),
                  prefixColumns(14, rowWidth(larguraSlot, SLOTS, VAO), 6, SCREEN_W));
    // So o que exige acao: um arquivo que faltou no cartao.
    for (int i = 0; i < S_COUNT; i++)
        if (!ok[i])
            Serial.printf("  %s AUSENTE -> usa %s\n",
                          ARQUIVOS[i].path, ARQUIVOS[resolver(i)].path);
}

bool ready()       { return carregado; }
const char *erro() { return falha; }

const char *temaEmCena() { return tema().nome; }

// ---- A comemoracao de turno concluido ----
//
// UMA POR SESSAO, EM SEQUENCIA. Dois turnos que acabam no mesmo poll rendem
// duas comemoracoes seguidas, e nao uma — o aviso conta quantos terminaram, e
// nao apenas que algo terminou. A fila existe por isso; sem ela o segundo
// evento se perderia dentro do primeiro.
//
// Ela mora no rodape porque ali e a faixa do flush de prefixo: um quadro custa
// ~11 ms em vez dos ~64 de uma tela inteira. Comemorar no meio da tela seria o
// mesmo desenho pelo preco de 6x — e cegaria o toque enquanto durasse.
// 10 s, e nao 4. Em 4 a comemoracao acabava antes de quem estava de costas para
// a mesa virar para olhar — e o aviso existe justamente para quem nao estava
// olhando na hora. Como ela mora na faixa do flush de prefixo, o tempo extra
// nao custa quase nada: sao ~11 ms por quadro, contra os ~64 de uma tela
// inteira.
const uint32_t FESTA_MS = 10000;
// Teto da fila. Com 10 s por comemoracao, seis sessoes terminando juntas dao um
// minuto de festa — e ai a turma para de anunciar e vira enfeite.
const int      FESTA_MAX = 6;

int      festaFila  = 0;
uint32_t festaAteMs = 0;          // 0 = nao ha festa agora
Slot     alvoEstado = S_AWAY;     // o que o ESTADO pede, ignorando a festa

void aplicar(Slot alvo);

void select(const Status &s, bool temDados) {
    alvoEstado = slotDe(s, temDados);
    // Enquanto a festa dura, ela manda. O estado nao se perde: fica em
    // `alvoEstado` e volta sozinho quando a ultima comemoracao termina.
    if (festaAteMs) return;
    aplicar(alvoEstado);
}

void comemorar() {
    if (festaFila < FESTA_MAX) festaFila++;
}

// Comeca a proxima comemoracao, ou devolve a turma ao estado. Devolve true
// quando trocou alguma coisa — quem chama precisa redesenhar a faixa.
bool tickFesta(uint32_t nowMs) {
    if (festaAteMs && nowMs < festaAteMs) return false;

    if (festaFila > 0) {
        festaFila--;
        festaAteMs = nowMs + FESTA_MS;
        aplicar(S_VITORIA);
        return true;
    }
    if (festaAteMs) {           // acabou a ultima: volta ao que o estado pede
        festaAteMs = 0;
        aplicar(alvoEstado);
        return true;
    }
    return false;
}

void aplicar(Slot alvo) {
    // A turma inteira vira de uma vez: e o mesmo turno. Decidido pelo estado
    // PEDIDO e nao pelo resolvido — se o typing faltar no cartao, o bicho da
    // esquerda cai no substituto, mas os companheiros continuam sabendo que
    // ha trabalho acontecendo.
    int novaCara = C_DESCANSO;
    if      (alvo == S_ALERT)    novaCara = C_ALERTA;
    else if (alvo == S_SWEEPING) novaCara = C_LIMPEZA;
    else if (alvo == S_TYPING)   novaCara = C_TRABALHO;
    else if (alvo == S_VITORIA)  novaCara = C_VITORIA;
    if (novaCara != cara) {
        cara = novaCara;
        // A cara que entra comeca do quadro zero; a que sai fica congelada onde
        // parou, e nao anda enquanto estiver escondida.
        for (int i = slot0(); i < SLOTS; i++) {
            comp[i][cara].quadro   = 0;
            comp[i][cara].ultimoMs = 0;
        }
    }
    // Ir embora sozinho e um gesto do bicho do estado. Sem ele na fileira, a
    // turma inteira fica — foi o combinado do tema: os quatro em toda situacao.
    sozinho = tema().estadoNoSlot0 && (alvo == S_AWAY);

    if (!carregado) return;

    // ---- Rodape: sempre o estado ----
    const int novo = resolver((int)alvo);
    if (novo >= 0 && novo != atual) {   // mesma animacao: nao reinicia o ciclo
        atual      = novo;
        quadroSelo = 0;
        ultimoSelo = 0;
    }

    // ---- Pagina 3: parado e o estado, trabalhando e sorteado ----
    // O sorteio acontece na VIRADA para o trabalho, e nao a cada leitura da
    // API: sorteando toda vez, o bicho trocaria de cara a cada dois segundos.
    const bool trabalhando = (alvo == S_TYPING);
    int alvoGrande = grande;
    if (!trabalhando)          alvoGrande = atual;
    else if (!trabalhava)      alvoGrande = sortearTrabalho();

    if (alvoGrande >= 0 && alvoGrande != grande) {
        grande       = alvoGrande;
        quadroGrande = 0;
        ultimoGrande = 0;
    }
    trabalhava = trabalhando;
}

// Passa para o proximo tema, com a placa ligada.
//
// Recarregar do cartao e o unico caminho: os icones ja vem reduzidos e
// recortados para o alvo do slot, entao manter os dois temas na memoria seria
// pagar PSRAM permanente por um gesto que acontece de vez em quando. A troca
// custa o que custa uma leitura de cartao, os mesmos ~300 ms do boot.
//
// A ordem importa: liberar ANTES de carregar. Ao contrario, os dois temas
// coexistiriam no pico e a soma poderia nao caber.
bool trocarTema() {
    if (!carregado || N_TEMAS < 2) return false;
    liberarTurma();
    temaAtual = (temaAtual + 1) % N_TEMAS;
    carregarTurma();
    medirFileira();
    // A cara e o "ir embora sozinho" saem do TEMA, e nao so do estado: quem
    // entrou precisa reler o estado atual com as regras dele.
    cara = C_DESCANSO;
    aplicar(alvoEstado);
    Serial.printf("clawd: tema %s, fileira %d+%d+%d+%d = %d px (prefixo ~%d col)\n",
                  tema().nome, larguraSlot[0], larguraSlot[1], larguraSlot[2],
                  larguraSlot[3], rowWidth(larguraSlot, SLOTS, VAO),
                  prefixColumns(14, rowWidth(larguraSlot, SLOTS, VAO), 6, SCREEN_W));
    return true;
}

bool trocarTrabalho() {
    // `trabalhava` guarda o que o ultimo `select` viu. Fora do trabalho o bicho
    // grande esta mostrando o ESTADO — dormindo, alertando, varrendo — e trocar
    // ali seria contradizer a tela.
    if (!carregado || !trabalhava) return false;
    const int novo = sortearTrabalho();
    if (novo < 0 || novo == grande) return false;
    grande       = novo;
    quadroGrande = 0;
    ultimoGrande = 0;
    return true;
}

void area(int &x, int &y, int &w, int &h) {
    if (!carregado || grande < 0) { x = y = w = h = 0; return; }
    const Sprite &sp = sprites[grande];
    const int sc = sp.scale;
    w = sp.width  * sc;
    h = sp.height * sc;

    // Enquadra pelo CORPO, nao pelo quadro. O quadro reserva espaco para o
    // balao de alerta e para os "Z" do sono, que aparecem em poucos frames —
    // centrar por ele deixava o bicho no canto inferior esquerdo.
    //
    // A ancora e por arquivo, entao as quatro caras do trabalho pisam todas na
    // mesma linha: o sorteio troca o bicho sem mover o chao.
    x = SCREEN_W / 2 - ancoraX[grande] * sc;
    y = CHAO - ancoraBase[grande] * sc;

    // O quadro nao pode invadir o cabecalho: ele pinta o proprio fundo e
    // apagaria o titulo.
    if (y < 46) y = 46;
}

void debugBlock(int lx, int ly, int lw, int lh, uint16_t cor) {
    if (!scratch) return;
    const size_t px = (size_t)lw * lh;
    if (px > scratchCap) return;
    for (size_t i = 0; i < px; i++) scratch[i] = cor;
    // Exatamente a mesma conversao de draw(): se estiver errada aqui, esta
    // errada la, e o bloco aparece deslocado do contorno que o canvas desenhou.
    display::raw()->draw16bitRGBBitmap(PANEL_W - ly - lh, lx, scratch, lh, lw);
}

// O icone do cabecalho: o bicho sorteado do rodizio, com o mago de reserva.
// Este canto mora na faixa do flush de prefixo, entao animar aqui custa ~9 ms
// por quadro em vez dos ~64 de uma tela inteira — foi por isso que o icone do
// cabecalho nasceu neste canto.
//
// Devolve um icone sem `buf` quando nenhum dos dois carregou, e ai o cabecalho
// cai no logo da marca.
Icone &iconeDoCabecalho(bool naPaginaDoNivel) {
    // O rodizio manda quando esta carregado; sem ele, o mago.
    //
    // A reserva era o bicho do nivel, derivado do sprite que a quarta pagina
    // carregava. Esse sprite saiu — sao 99 arquivos e 27 MB que nao cabem na
    // particao de flash, e a pagina do nivel deixou de mostrar o bicho. O mago
    // ja estava aqui e ja era a arte desta pagina.
    (void)naPaginaDoNivel;
    if (rodizio.buf) return rodizio;
    return mago;
}

// Sorteia e carrega o proximo bicho do cabecalho. Devolve true quando trocou.
//
// Le do cartao (ate 244 KB) e por isso NAO roda no laco de desenho: e um
// engasgo de algumas centenas de ms, tolerado uma vez a cada dez minutos, mas
// que arruinaria um swipe se acontecesse junto com um gesto.
bool trocarIconeCabecalho() {
    int i = rodizioAtual;
    // Nunca repete o que ja esta em cena: com 23 nomes, repetir seguido pareceria
    // que a troca falhou.
    for (int t = 0; t < 8 && i == rodizioAtual; t++)
        i = (int)(esp_random() % (uint32_t)RODIZIO_N);
    if (i == rodizioAtual) i = (rodizioAtual + 1) % RODIZIO_N;

    char path[48];
    snprintf(path, sizeof(path), "/clawd/%s.clw", RODIZIO[i]);

    Icone novo;
    if (!carregarIcone(path, ICONE_H, R_CORPO, novo)) {
        // Arquivo ausente ou ilegivel: fica com o que estava. Um cabecalho que
        // some porque um sprite faltou seria pior do que um bicho repetido.
        Serial.printf("cabecalho: falhou %s\n", path);
        return false;
    }
    // ANIMADO: o blob fica residente (ate 244 KB de PSRAM) porque cada quadro e
    // decodificado na hora de desenhar. Sai caro em memoria e de graca em CPU —
    // este canto mora na faixa do flush de prefixo, onde o quadro custa ~9 ms
    // em vez dos ~64 de uma tela inteira. Ver display::flushPrefix.
    //
    // Comeca no quadro de maior area opaca para que o primeiro desenho ja
    // mostre o bicho inteiro, e nao a pose de repouso, que costuma ser vazia.
    novo.quadro = spriteBestFrame(novo.sp);

    // So agora solta o anterior: se o carregamento falhasse no meio, o antigo
    // ainda estaria de pe.
    if (rodizio.blob) free(rodizio.blob);
    if (rodizio.buf)  free(rodizio.buf);
    rodizio = novo;
    rodizioAtual = i;
    Serial.printf("cabecalho: %s (%dx%d)\n", RODIZIO[i], rodizio.w, rodizio.h);
    return true;
}

int iconW(bool naPaginaDoNivel) {
    const Icone &ic = iconeDoCabecalho(naPaginaDoNivel);
    return ic.buf ? ic.w : 0;
}
int iconH(bool naPaginaDoNivel) {
    const Icone &ic = iconeDoCabecalho(naPaginaDoNivel);
    return ic.buf ? ic.h : 0;
}
bool drawIconInto(Arduino_Canvas *g, int x, int y, bool naPaginaDoNivel) {
    return desenharIcone(iconeDoCabecalho(naPaginaDoNivel), g, x, y);
}

bool tickIconeCabecalho(uint32_t nowMs, bool podeCarregar) {
    if (!podeCarregar) return false;
    // O relogio conta desde a ultima TENTATIVA, e nao desde o ultimo bicho que
    // entrou em cena. A diferenca so aparece quando a carga falha, e ai ela e
    // enorme: a guarda antiga era `rodizio.buf && ...`, entao sem cartao o
    // `buf` ficava nulo, a guarda nunca valia, e este tick tentava reabrir um
    // arquivo A CADA VOLTA DO LOOP. Medido na serial com o cartao travado:
    // centenas de `cabecalho: falhou` por segundo, cada uma um `open()` num
    // sistema de arquivos que nao esta montado.
    //
    // `rodizioMs` comeca em zero, entao a primeira troca continua imediata —
    // esperar dez minutos para o rodizio comecar faria a placa parecer que ele
    // nao existe.
    if (rodizioMs && (nowMs - rodizioMs) < RODIZIO_MS) return false;
    rodizioMs = nowMs ? nowMs : 1;
    return trocarIconeCabecalho();
}

void selecionarClima(int tempC, int tminC, int tmaxC) {
    const int f = faixaDe(tempC, tminC, tmaxC);
    if (f == climaFaixa) return;             // mesma faixa: nada a fazer

    // Libera blob E buffer: animado, o blob fica residente (ate 250 KB), entao
    // trocar de faixa sem soltar o anterior vazaria a cada virada de tempo.
    if (clima.blob) { free(clima.blob); clima.blob = nullptr; }
    if (clima.buf)  { free(clima.buf);  clima.buf  = nullptr; }
    clima = Icone();
    // A faixa e marcada ANTES de tentar carregar. Se o arquivo faltar no
    // cartao, nao adianta tentar de novo a cada leitura de status: seriam
    // centenas de leituras falhas de SD por hora dentro do laco de desenho.
    climaFaixa = f;
    if (carregarIcone(CLIMA[f].arq, CLIMA[f].alturaC, R_CORPO, clima)) {
        // A altura RESULTANTE, e nao a alvo. A reducao e por divisor INTEIRO,
        // entao dois arquivos com alturas de corte diferentes caem em degraus
        // diferentes e um sai visivelmente menor que o outro. So medindo da
        // para escolher o alvo de cada um.
        Serial.printf("clima: %s  %dx%d  div=%d  (alvo %d)\n",
                      CLIMA[f].arq, clima.w, clima.h, clima.div,
                      CLIMA[f].alturaC);
    } else {
        Serial.printf("clima: FALHOU %s\n", CLIMA[f].arq);
    }
}

int  climaW() { return clima.buf ? clima.w : 0; }
int  climaH() { return clima.buf ? clima.h : 0; }

// Contador proprio, no ritmo do arquivo. NAO entra em `clawd::tick`: aquele
// junta os bichos da faixa do prefixo, que sao enviados baratos juntos. Este
// mora do outro lado da tela e obriga um redesenho inteiro, entao quem chama
// precisa saber que foi ele.
bool tickClima(uint32_t nowMs) { return tickIcone(clima, nowMs); }

bool drawClimaInto(Arduino_Canvas *g, int x, int y) {
    return desenharIcone(clima, g, x, y);
}

// O proximo personagem da tela de RESET, lido do cartao na hora.
//
// Libera ANTES de carregar, pelo mesmo motivo da troca de tema: com os dois
// vivos ao mesmo tempo o pico soma os dois arquivos, e cada um deles ja passa
// de 400 KB.
//
// Um arquivo que falta nao apaga a tela: o laco tenta os seguintes e, se
// nenhum servir, volta para o que estava. So quando o cartao nao tem nenhum e
// que `resetW()` fica em zero — e ai quem chama ja sabe nao abrir a tela.
bool proximoReset() {
    const int      anterior = resetAtual;
    const uint32_t t0       = millis();
    if (resetIc.blob) { free(resetIc.blob); free(resetIc.buf); }
    resetIc = Icone();

    for (int i = 1; i <= RESET_N; i++) {
        const int k = (anterior + i) % RESET_N;
        if (carregarIcone(RESET_ARQ[k], RESET_ALT_MAX, R_QUADRO, resetIc)) {
            resetAtual = k;
            // O custo desta leitura e o atraso entre o toque e a tela. Fica na
            // serial porque e o unico jeito de saber que um arquivo novo do
            // rodizio ficou pesado demais para o gesto.
            Serial.printf("clawd: reset %s (%dx%d, %d quadros) em %lums\n",
                          RESET_ARQ[k], resetIc.w, resetIc.h, resetIc.sp.frames,
                          (unsigned long)(millis() - t0));
            return true;
        }
        resetIc = Icone();
    }
    Serial.printf("clawd: rodizio de reset vazio (%d arquivos, nenhum no cartao)\n",
                  RESET_N);
    return false;
}

int  resetW() { return resetIc.buf ? resetIc.w : 0; }
int  resetH() { return resetIc.buf ? resetIc.h : 0; }
bool tickReset(uint32_t nowMs) { return tickIcone(resetIc, nowMs); }
bool drawResetInto(Arduino_Canvas *g, int x, int y) {
    return desenharIcone(resetIc, g, x, y);
}

bool drawResetOpacoInto(Arduino_Canvas *g, int x, int y, uint16_t fundo) {
    if (!resetIc.buf) return false;

    const size_t cap = (size_t)resetIc.w * resetIc.h;
    // Este sprite e carregado com o quadro inteiro e alvo igual a altura dele
    // (R_QUADRO, 240), entao o divisor sai 1 e o caminho rapido vale. A
    // pergunta fica escrita mesmo assim: se um dia a arte mudar de tamanho, o
    // generico continua correto — so volta a custar os 32 ms por quadro.
    const bool inteiro = resetIc.div == 1 &&
                         resetIc.box.x == 0 && resetIc.box.y == 0 &&
                         resetIc.box.w == (int)resetIc.sp.width &&
                         resetIc.box.h == (int)resetIc.sp.height;

    const bool ok =
        inteiro ? decodeFullOpaque(resetIc.sp, resetIc.quadro, resetIc.buf,
                                   cap, fundo)
                : decodeCropDown(resetIc.sp, resetIc.quadro, resetIc.box,
                                 resetIc.div, resetIc.buf, cap, fundo);
    if (!ok) return false;
    g->draw16bitRGBBitmap(x, y, resetIc.buf, resetIc.w, resetIc.h);
    return true;
}

int  tokenW(int num, int den) { return tokenIc.buf ? tokenIc.w * num / den : 0; }
int  tokenH(int num, int den) { return tokenIc.buf ? tokenIc.h * num / den : 0; }
bool tickToken(uint32_t nowMs) { return tickIcone(tokenIc, nowMs); }
bool drawTokenInto(Arduino_Canvas *g, int x, int y, int num, int den) {
    return desenharIcone(tokenIc, g, x, y, num, den);
}

bool carregarOffline() {
    if (offlineIc.buf) return true;              // ja esta em cena
    const uint32_t t0 = millis();
    if (!carregarIcone("/clawd/sp_offline.clw", 336, R_QUADRO, offlineIc)) {
        offlineIc = Icone();
        Serial.println("clawd: sp_offline.clw ausente — sem tela de servidor fora");
        return false;
    }
    Serial.printf("clawd: offline (%dx%d, %d quadros) em %lums\n",
                  offlineIc.w, offlineIc.h, offlineIc.sp.frames,
                  (unsigned long)(millis() - t0));
    return true;
}

void soltarOffline() {
    if (!offlineIc.buf) return;
    if (offlineIc.blob) free(offlineIc.blob);
    free(offlineIc.buf);
    offlineIc = Icone();
}

int  offlineW(int num, int den) { return offlineIc.buf ? offlineIc.w * num / den : 0; }
int  offlineH(int num, int den) { return offlineIc.buf ? offlineIc.h * num / den : 0; }
bool tickOffline(uint32_t nowMs) { return tickIcone(offlineIc, nowMs); }
bool drawOfflineInto(Arduino_Canvas *g, int x, int y, int num, int den) {
    return desenharIcone(offlineIc, g, x, y, num, den);
}

int crewW(int faixaW) {
    // Nenhum slot com arte = nada a desenhar, e a faixa pedida nao muda isso:
    // devolver a largura cheia aqui faria o `fillRect` de limpeza apagar a
    // divisoria do cabecalho sem nada tomar o lugar.
    int presentes = 0;
    for (int i = 0; i < SLOTS; i++) if (larguraSlot[i] > 0) presentes++;
    if (!presentes) return 0;

    // Elenco fechado: a fileira e sempre a mesma, e nao depende do selo ter
    // carregado — ele nem aparece.
    if (!tema().estadoNoSlot0)
        return faixaW > 0 ? faixaW : rowWidth(larguraSlot, SLOTS, VAO);
    if (!seloBuf || atual < 0) return 0;
    // Indo embora, ele vai sozinho — e o quadro dele e mais largo que o slot.
    // Neste caso a faixa nao se aplica: nao ha fileira, ha um bicho so.
    if (sozinho)
        return spriteDownW(sprites[atual], seloDiv(sprites[atual], seloAlvo(atual)));
    return faixaW > 0 ? faixaW : rowWidth(larguraSlot, SLOTS, VAO);
}

int crewH() {
    if (!tema().estadoNoSlot0) return alturaTrio;
    if (!seloBuf || atual < 0) return 0;
    // A altura da faixa toda, e nao a do quadro corrente: e por ela que o
    // retangulo e limpo, e limpar so o quadro atual deixaria resto do anterior.
    return alturaTrio;
}

bool drawCrewInto(Arduino_Canvas *g, int x, int chao, int faixaW) {
    // Onde comeca o slot `i` e quanto ele mede. Com faixa, sao fatias iguais da
    // largura pedida; sem ela, a fileira compacta de sempre.
    //
    // AS FATIAS CONTAM SO OS SLOTS QUE EXISTEM, e nao SLOTS fixo. Um slot sem
    // arte na maquina (o Sonic do tema padrao, cujos .clw nunca entram no git
    // por serem da SEGA) some da fileira compacta sem deixar buraco, porque
    // `rowWidth` ignora largura zero. Dividir a faixa em quatro com tres bichos
    // devolveria exatamente esse buraco, agora no fim da linha.
    int presentes = 0, ordem[SLOTS];
    for (int i = 0; i < SLOTS; i++) {
        ordem[i] = presentes;
        if (larguraSlot[i] > 0) presentes++;
    }
    auto slotX = [&](int i) {
        return faixaW > 0 ? evenSlotX(faixaW, presentes, ordem[i], x)
                          : rowSlotX(larguraSlot, SLOTS, VAO, i, x);
    };
    auto slotW = [&](int i) {
        return faixaW > 0 ? evenSlotW(faixaW, presentes, ordem[i])
                          : larguraSlot[i];
    };

    if (tema().estadoNoSlot0) {
        if (!seloBuf || atual < 0) return false;

        const Sprite &sp = sprites[atual];
        const int div = seloDiv(sp, seloAlvo(atual));
        const int w = spriteDownW(sp, div), h = spriteDownH(sp, div);

        // Um quadro corrente, e nao um quadro fixo: o rodape voltou a animar,
        // agora que o flush de prefixo tornou o custo por quadro viavel.
        if (!decodeFrameDown(sp, quadroSelo, div, seloBuf, seloCap, sp.key))
            return false;
        // Base no chao, e nao centro: os quadros tem alturas diferentes e
        // centrar faria os bichos flutuarem em alturas distintas.
        g->draw16bitRGBBitmapWithTranColor(centerIn(slotX(0), slotW(0), w), chao - h,
                                           seloBuf, sp.key, w, h);
        if (sozinho) return true;
    }

    for (int i = slot0(); i < SLOTS; i++) {
        Icone &ic = comp[i][cara];
        if (!ic.buf) continue;
        desenharIcone(ic, g, centerIn(slotX(i), slotW(i), ic.w), chao - ic.h);
    }
    return true;
}

int crewComCentroH() {
    int h = crewH();
    Icone &centro = iconeDoCabecalho(false);
    if (centro.buf && centro.h > h) h = centro.h;
    if (festaIc.buf && festaIc.h > h) h = festaIc.h;
    return h;
}

bool drawCrewComCentroInto(Arduino_Canvas *g, int x, int chao, int faixaW) {
    // As fatias sao as da fileira normal MAIS UMA, e o centro fica na do meio:
    // com os quatro do elenco, Cartman e Stan de um lado, Kenny e Kyle do
    // outro. A conta reusa `evenSlot*` para o vao entre bichos continuar sendo
    // um so — a fileira nao pode ter dois ritmos de espacamento.
    int presentes = 0, ordem[SLOTS];
    for (int i = 0; i < SLOTS; i++) {
        ordem[i] = presentes;
        if (larguraSlot[i] > 0) presentes++;
    }
    if (!presentes) return false;

    const int fatias = presentes + 1;
    const int meio   = presentes / 2;
    auto fatiaDe = [&](int i) {
        const int o = ordem[i];
        return o >= meio ? o + 1 : o;
    };

    // Durante a festa o centro e o Clawd pulando; fora dela, o bicho que morava
    // no cabecalho. Sem nenhum dos dois carregado a fatia fica vazia — melhor
    // um vao no meio do que a fileira inteira sumir.
    Icone &centro = (cara == C_VITORIA && festaIc.buf)
                        ? festaIc : iconeDoCabecalho(false);
    if (centro.buf)
        desenharIcone(centro, g,
                      centerIn(evenSlotX(faixaW, fatias, meio, x),
                               evenSlotW(faixaW, fatias, meio), centro.w),
                      chao - centro.h);

    // O selo do estado (tema padrao) entra na fatia dele como na fileira
    // normal; o elenco fechado nem passa por aqui.
    if (tema().estadoNoSlot0) {
        if (!seloBuf || atual < 0) return centro.buf != nullptr;
        const Sprite &sp = sprites[atual];
        const int div = seloDiv(sp, seloAlvo(atual));
        const int w = spriteDownW(sp, div), h = spriteDownH(sp, div);
        if (decodeFrameDown(sp, quadroSelo, div, seloBuf, seloCap, sp.key))
            g->draw16bitRGBBitmapWithTranColor(
                centerIn(evenSlotX(faixaW, fatias, fatiaDe(0), x),
                         evenSlotW(faixaW, fatias, fatiaDe(0)), w),
                chao - h, seloBuf, sp.key, w, h);
        if (sozinho) return true;
    }

    for (int i = slot0(); i < SLOTS; i++) {
        Icone &ic = comp[i][cara];
        if (!ic.buf) continue;
        desenharIcone(ic, g,
                      centerIn(evenSlotX(faixaW, fatias, fatiaDe(i), x),
                               evenSlotW(faixaW, fatias, fatiaDe(i)), ic.w),
                      chao - ic.h);
    }
    return true;
}

// Avanca um contador no ritmo do proprio arquivo. Devolve true se andou.
static bool andar(int slot, int &quadro, uint32_t &ultimo, uint32_t nowMs) {
    if (!carregado || slot < 0) return false;
    const Sprite &sp = sprites[slot];
    if ((nowMs - ultimo) < sp.frameMs) return false;
    ultimo = spriteRearme(ultimo, nowMs, sp.frameMs);
    quadro = (quadro + 1) % sp.frames;
    return true;
}

bool tick(uint32_t nowMs) {
    // Cada bicho anda no ritmo do proprio arquivo — o da esquerda, os dois
    // companheiros e o mago do cabecalho. Todos moram na mesma faixa do
    // prefixo, entao um envio serve a todos.
    //
    // A festa entra ANTES: ela troca a cara de todo mundo, e trocar depois de
    // avancar faria o primeiro quadro da comemoracao ser pulado.
    bool avancou = tickFesta(nowMs);

    // Os dois contadores andam SEMPRE, mesmo o que esta fora da pagina em cena.
    // Congelar o escondido faria a animacao dar um salto ao trocar de pagina, e
    // o custo de andar e um modulo.
    if (andar(atual,  quadroSelo,   ultimoSelo,   nowMs)) avancou = true;
    if (andar(grande, quadroGrande, ultimoGrande, nowMs)) avancou = true;

    // O bicho sorteado do rodizio anda no ritmo do proprio arquivo. Ele esta na
    // faixa do prefixo, entao sai no mesmo envio dos outros — animar aqui custa
    // ~9 ms por quadro, e nao os ~64 de uma tela inteira. Sprite de um quadro so
    // simplesmente nao anda, sem caso especial.
    //
    // Anda SEMPRE, mesmo com o cabecalho fora da pagina em cena — congelar o
    // escondido faria a animacao dar um salto ao trocar de pagina, que e a mesma
    // razao dos contadores do selo e do bicho grande.
    if (tickIcone(rodizio, nowMs)) avancou = true;
    // O Clawd pulando so anda durante a festa: fora dela ele nao esta em cena
    // em lugar nenhum, e andar escondido nao compraria nada — o pulo recomeca
    // de onde parou, que num loop de um segundo nao se percebe.
    if (cara == C_VITORIA && tickIcone(festaIc, nowMs)) avancou = true;
    // O mago NAO anda, de proposito: ele e a reserva de quando o rodizio nao
    // carregou, e fica no quadro de maior area opaca escolhido no carregamento.
    if (!sozinho)
        for (int i = slot0(); i < SLOTS; i++)
            if (tickIcone(comp[i][cara], nowMs)) avancou = true;

    // Quem avancou ja avancou; o que esta em jogo aqui e QUANDO isso vai para o
    // painel. `coalescerEnvio` junta tudo que caiu dentro de ENVIO_MIN_MS num
    // envio so (ver lib/sprite, testado la).
    return coalescerEnvio(avancou, nowMs, ENVIO_MIN_MS, pendente, ultimoEnvio);
}

void drawIntoEm(Arduino_Canvas *g, int centroX, int chao, int teto) {
    if (!carregado || grande < 0) return;
    const Sprite &sp = sprites[grande];

    // Decodifica com a propria cor-chave nos vazios: assim o canvas pula esses
    // pixels e o fundo da tela aparece atras do bicho, sem moldura nenhuma.
    if (!decodeFrame(sp, quadroGrande, scratch, scratchCap, sp.key)) return;

    // Mesma conta de area(), com o centro e o chao de quem chama: em pe o
    // bicho pisa mais baixo e o cabecalho fica mais alto.
    const int sc = sp.scale;
    int lx = centroX - ancoraX[grande] * sc;
    int ly = chao - ancoraBase[grande] * sc;
    if (ly < teto) ly = teto;
    g->draw16bitRGBBitmapWithTranColor(lx, ly, scratch, sp.key,
                                       spriteBufW(sp), spriteBufH(sp));
}

void drawInto(Arduino_Canvas *g) { drawIntoEm(g, SCREEN_W / 2, CHAO, 46); }

}
