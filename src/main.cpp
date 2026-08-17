#include <Arduino.h>
#include <utility>
#include "display.h"
#include "storage.h"
#include "touch_axs.h"
#include "net.h"
#include "ui.h"
#include "app_config.h"
#include "gesture.h"
#include "clawd.h"
#include "nivel.h"
#include "reset_watch.h"
#include "relogio.h"
#include "view_model.h"     // semSessao: quem decide se o Clawd dorme
#include "cache.h"
#include "hora.h"

namespace {

AppConfig       cfg;
GestureDetector gestos;
Status          last;              // ultimo status bom conhecido
bool            haveLast   = false;
uint32_t        lastOkMs   = 0;    // quando a API respondeu pela ultima vez
uint32_t        lastPollMs = 0;
int             staleSec   = 0;    // 0 = dado fresco
bool            semWifi    = false;  // o stale e por queda de rede?
int             page       = 0;
bool            fatal      = false;  // sem config: nao ha o que fazer

// A hora que a API mandou, guardada separada do resto do payload.
//
// Ela nao vive mais dentro de `last.clock`: quem escreve ali agora e o relogio
// da placa, uma vez por segundo. Este aqui e o PLANO B — vale enquanto o SNTP
// nao sincronizou, que e o caso de uma rede sem saida para a internet.
Clock           clockDaApi;
uint32_t        ultimoRelogioMs = 0;

// Os prazos CRUS das duas janelas, como a API os mandou. Eles existem porque o
// envelhecimento nao pode comer a si mesmo: `last.session`/`last.week` sao
// reescritos a cada segundo, e a conta tem que partir sempre do valor original
// menos a idade — nunca do valor ja envelhecido.
Metric          sessionDaApi, weekDaApi;

// O retrato do ultimo status bom, para o boot com o servidor fora.
const char     *CACHE_ARQ    = "/clawd/ultimo_status.json";
bool            cacheTentado = false;
uint32_t        cacheSalvoMs = 0;    // 0 = nunca gravou nesta ligada
// Quantos segundos o dado ja tinha quando entrou na memoria. Zero no caminho
// normal (a rede acabou de responder) e a idade do arquivo quando ele veio do
// cartao — sem isto, um retrato de tres horas apareceria como recem-chegado.
int             idadeBaseSeg = 0;

// O nivel do Clawd. Só o tempo de convivio mora aqui — turnos e custo vem da
// API, que ja os tem inteiros.
nivel::Estado   nivelEstado;
uint32_t        contatoDesdeMs = 0;   // 0 = nao esta contando agora
uint32_t        salvoEmMs      = 0;
uint32_t        contatoSalvo   = 0;   // o que ja foi para o cartao
float           xpDoDia        = 0;   // ganho desde a virada, para a quarta tela

// Guarda o ID e nao o indice: a lista vem ordenada por contexto e reordena
// sozinha, entao um indice fixo faria a selecao pular de agente sem ninguem
// tocar nela. Vazio = mostra o primeiro.
std::string selectedId;

// Botao de limpeza: instante em que o primeiro toque armou, ou 0 se desarmado.
//
// Dois toques, e nao um. O painel vive na parede e este e o unico controle que
// AGE sobre uma sessao — uma manga de camisa passando por cima nao pode
// interromper um turno de trabalho. Desarma sozinho em BTN_ARMADO_MS, para nao
// ficar uma armadilha carregada esperando o proximo esbarrao.
uint32_t armadoMs = 0;
const uint32_t ARMADO_MS = 3000;

// Quando o ultimo comando saiu. O botao fica surdo por um instante depois de
// disparar: a API leva um ciclo de poll para acender `cleaning`, e sem esta
// trava um dedo hesitante mandaria a segunda compactacao dentro dessa janela,
// quando a tela ainda nao tem como mostrar que a primeira ja foi.
uint32_t disparoMs = 0;
const uint32_t SURDO_MS = 6000;

// Opcao do bloqueio esperando confirmacao: o NUMERO dela, ou 0. Junto com o
// `seq` da pergunta em que ela foi armada.
//
// O `seq` e o que impede o pior caso desta tela: a pergunta troca entre o
// primeiro toque e o segundo, e o segundo aprova a pergunta NOVA com a escolha
// da antiga. Guardar o seq e compara-lo antes de enviar transforma isso em um
// botao que simplesmente desarma.
int      opcaoArmada  = 0;
int      opcaoSeq     = 0;
uint32_t opcaoDesdeMs = 0;
// O seq da ultima pergunta ja respondida daqui. Fecha a pergunta na hora, sem
// esperar o servidor confirmar que ela saiu no proximo /status.
int      respondidoSeq = -1;

// ---- Modo terminal ----
// Estado a parte e nao uma quinta pagina: aqui os swipes horizontais nao
// significam nada e o vertical rola o texto, o que e o oposto das paginas.
bool         modoTerminal = false;
std::string  termPane;
std::string  termTitulo;
TerminalTela termTela;

// O que derrubou o boot, guardado para a serial poder repetir.
//
// A tela sozinha nao serve a quem acabou de gravar pela USB: ali o que se tem
// na mao e o monitor serial, e um boot fatal deixava a serial COMPLETAMENTE
// muda. De fora, isso e indistinguivel de um app que nunca subiu — e leva a
// procurar defeito na gravacao quando o problema e o cartao.
const char *fatalTitulo  = nullptr;
std::string fatalDetalhe;

void bootFatal(const char *title, const std::string &detail) {
    fatal = true;
    fatalTitulo = title;
    fatalDetalhe = detail;
    ui::drawMessage(title, detail.c_str());
}

// A origem de um agente pelo id. 0 quando nao achar: mandar para o master e o
// comportamento de sempre, e um id sumido no meio do toque nao pode derrubar nada.
int origemDe(const std::string &id) {
    for (const Agent &a : last.agents)
        if (a.id == id) return a.origem;
    return 0;
}

void abrirTerminal() {
    const std::string pane = ui::paneDoSelecionado(last, selectedId);
    if (pane.empty()) return;          // sem pane nao ha tela para pedir

    modoTerminal = true;
    termPane     = pane;
    termTela     = TerminalTela();

    // Titulo: o repo do agente, que e como o usuario o chama. Cai no pane
    // quando o agente nao tem repo (um orfao que o herdr ve e nos nao).
    // A origem sai do MESMO agente: a tela dele mora na maquina dele.
    termTitulo = pane;
    int origem = 0;
    for (const Agent &a : last.agents) {
        if (a.paneId == pane) {
            origem = a.origem;
            if (!a.repo.empty()) termTitulo = a.repo;
            break;
        }
    }
    net::terminalAbrir(pane, ui::termCols(), ui::termRows(), origem);
}

void fecharTerminal() {
    modoTerminal = false;
    termPane.clear();
    net::terminalFechar();
}

// Um toque num botao de opcao. Mesmo desenho do botao de limpeza: o primeiro
// arma, o segundo envia — o painel vive na parede, e aqui um esbarrao nao
// interrompe um turno, aprova uma permissao.
void tocarOpcao(int n, uint32_t now) {
    if (n <= 0 || !last.bloqueio.known) return;

    if (opcaoArmada != n) {
        opcaoArmada  = n;
        opcaoSeq     = last.bloqueio.seq;
        opcaoDesdeMs = now ? now : 1;
        return;
    }
    // A pergunta mudou debaixo do dedo. Desarma sem enviar: a escolha de uma
    // pergunta nao vale para outra, mesmo que o numero da opcao coincida.
    if (opcaoSeq != last.bloqueio.seq) {
        opcaoArmada = 0;
        return;
    }

    // O rotulo vai junto do numero, e e ele que a API confere contra a tela do
    // agente antes de apertar Enter.
    for (const Opcao &o : last.bloqueio.opcoes) {
        if (o.n != n) continue;
        net::responder(last.bloqueio.paneId, o.n, o.rotulo, last.bloqueio.origem);
        break;
    }
    // Fecha a pergunta na hora: guarda o seq respondido e apaga o bloqueio
    // corrente. Ele so reaparece se o servidor mandar um seq diferente.
    respondidoSeq = last.bloqueio.seq;
    last.bloqueio.known = false;
    opcaoArmada  = 0;
    opcaoDesdeMs = 0;
}

// Um toque no botao de limpeza. Chamado pelo toque simples E pelo duplo, e essa
// e a correcao de um defeito real: os dois toques da confirmacao sao, para o
// detector de gestos, um duplo toque — que na pagina 1 ja significava "proximo
// agente" desde antes do botao existir. Confirmar a limpeza trocava de agente.
void tocarBotaoLimpeza(uint32_t now) {
    // Ja esta varrendo, ou acabou de disparar: um segundo /compact no meio de
    // uma compactacao nao adianta nada e pode virar texto solto no prompt.
    if (last.cleaning) return;
    if (disparoMs && (now - disparoMs) < SURDO_MS) return;

    if (!armadoMs) {
        armadoMs = now ? now : 1;      // 0 significa desarmado
        return;
    }
    // O segundo toque dispara. A confirmacao de que funcionou vem do proximo
    // /status, no `cleaning` — e nao da nossa lembranca de ter mandado.
    const std::string alvo = (selectedId.empty() && !last.agents.empty())
                                 ? last.agents[0].id : selectedId;
    net::sendCommand(alvo, "compact", origemDe(alvo));
    armadoMs  = 0;
    disparoMs = now ? now : 1;
}

// Gira a tela. Dois toques no bicho do cabecalho, em qualquer pagina.
//
// A escolha nao e guardada no cartao: a placa liga sempre em pe, a pedido (ver
// display::begin). Isso e proposital e nao esquecimento — um painel que liga
// numa orientacao que depende do ultimo uso e um painel que se olha e nao se
// sabe por que esta assim.
void girarTela();   // implementada abaixo de tokenManual, que ela cancela

// ---- A tela do Token (limite estourado, so em pe) ----
// `tokenDispensado` guarda o toque duplo que mandou a tela embora. Ele rearma
// SOZINHO quando nenhum limite esta mais em 100% — o proximo estouro e um
// evento novo e merece a tela de novo.
bool tokenDispensado = false;

// ---- A tela de RESET (retrato) ----
// Uma janela de limite liberou: o bicho da vez toma a tela por 6 s, ou sai no
// toque. A do Token fica ate voce dispensar porque ela pede acao; esta so
// avisa que a acao voltou a ser possivel. Cada danca tem ~1,7 s de ciclo: em
// 5 s ela mal passava tres vezes, e em 10 o aviso ja tinha sido lido havia
// muito — 6 s dao as tres voltas e devolvem o painel.
const uint32_t RESET_MS = 6000;
uint32_t    resetAteMs  = 0;
const char *resetQual   = nullptr;
// Quem reconhece a virada da janela, poll a poll. Mora em lib/metrics porque
// e a unica forma de ter teste: aqui dentro do laco, o gatilho ficou dois dias
// escrito de um jeito que nunca disparava fora do ensaio.
ResetWatch resetWatch;

// ---- A morte do Kenny ----
// Uma sessao que cala mata o Kenny da fileira. `stale` e o campo que a API
// publica quando o heartbeat para de chegar.
bool algumaSessaoCaida(const Status &s) {
    for (const Agent &a : s.agents)
        if (a.stale) return true;
    return false;
}

// O ENSAIO da morte: mata por alguns segundos, sem depender de sessao muda.
// Enquanto ele vale, o estado real da API nao mexe no Kenny — senao o proximo
// poll o levantaria antes de dar para olhar.
const uint32_t KENNY_ENSAIO_MS = 10000;
uint32_t kennyEnsaioAteMs = 0;

// ---- O cartaz de sessao nova ----
// Ids ja vistos, para saber quem chegou agora. Vale por NOVO_MS depois da
// primeira aparicao.
const uint32_t NOVO_MS = 3000;
const size_t   VISTOS_MAX = 16;
std::string vistos[VISTOS_MAX];
uint32_t    vistosMs[VISTOS_MAX] = {0};
size_t      vistosN = 0;

// Marca em `s` quem apareceu agora. O primeiro poll NAO marca ninguem: no boot
// a lista inteira e nova, e um painel piscando cinco linhas no primeiro
// segundo nao avisa nada.
void marcarNovos(Status &s, uint32_t now, bool primeiro) {
    for (Agent &a : s.agents) {
        if (a.id.empty()) continue;
        size_t i = 0;
        for (; i < vistosN; i++) if (vistos[i] == a.id) break;
        if (i == vistosN) {
            if (vistosN < VISTOS_MAX) {
                vistos[vistosN]   = a.id;
                vistosMs[vistosN] = primeiro ? 0 : (now ? now : 1);
                vistosN++;
            } else {
                // Lista cheia: descarta a marca mais velha. Perder a marca de
                // "novo" e barato; perder o id faria o agente piscar de novo.
                size_t velho = 0;
                for (size_t k = 1; k < VISTOS_MAX; k++)
                    if (vistosMs[k] < vistosMs[velho]) velho = k;
                vistos[velho]   = a.id;
                vistosMs[velho] = primeiro ? 0 : (now ? now : 1);
            }
            i = vistosN ? vistosN - 1 : 0;
        }
        for (size_t k = 0; k < vistosN; k++)
            if (vistos[k] == a.id)
                a.novo = vistosMs[k] && (now - vistosMs[k]) < NOVO_MS;
    }
}
// A tela tambem abre POR VONTADE, com o duplo toque no percentual da semana —
// para conferir os prazos sem esperar o estouro. O manual nao rearma nada: ele
// nasce e morre pelo toque.
bool tokenManual = false;

bool telaResetAtiva(uint32_t now) {
    if (!display::retrato() || !haveLast || clawd::resetW() <= 0) return false;
    return resetAteMs && now < resetAteMs;
}

// A tela do Cartman acabou de sair de cena?
//
// Ela morre pelo RELOGIO, e nao por um toque, e essa e a diferenca que custou
// um defeito: sem ninguem pedindo o redesenho completo no instante da saida, a
// tela dele ficava no ar e o redrawBadge — que volta a desenhar a turma assim
// que ela nao esta mais ativa — ia pintando os quatro por cima da cabeca dele.
// Os icones voltavam antes do resto da tela.
bool resetEstavaAtiva = false;

bool resetAcabouAgora(uint32_t now) {
    const bool ativa = telaResetAtiva(now);
    const bool saiu  = resetEstavaAtiva && !ativa;
    resetEstavaAtiva = ativa;
    return saiu;
}

bool telaTokenAtiva() {
    if (!display::retrato() || !haveLast || clawd::tokenW() <= 0) return false;
    if (tokenManual) return true;
    return !tokenDispensado && ui::limiteEstourado(last);
}

// ---- A tela de SERVIDOR FORA ----
// O outro lado calou. Nao e "a rede esta lenta": e o Claude Code fechado, a API
// derrubada ou o PC desligado, e a diferenca entre isso e um painel travado e a
// unica coisa que quem olha precisa saber naquele momento.
//
// 45 s, e NAO os tres polls perdidos que acendem o aviso do rodape.
//
// Os dois limiares mediam a mesma coisa e pagavam precos muito diferentes. O
// rodape esmaece um numero e escreve a idade: errar por um soluco de rede custa
// nada. Esta aqui TOMA A TELA INTEIRA, e com as duas fontes configuradas uma
// volta ruim ja custa ate 6 s sozinha — 3 s de connect timeout no master mais
// 3 s no PC2 (ver net.cpp). Com o limiar de 6 s, um poll lento entrava no
// criterio por construcao, e o painel piscaria a tela cheia por nada.
//
// 45 s tambem tem significado proprio: passou disso, nao foi uma volta ruim.
const uint32_t OFFLINE_MS = 45000;

bool servidorFora(uint32_t now) {
    return (now - lastOkMs) > OFFLINE_MS;
}

// O toque duplo mandou a tela embora. Rearma SOZINHO quando o servidor volta —
// a proxima queda e um evento novo e merece o aviso de novo, pela mesma regra
// do `tokenDispensado`.
bool offlineDispensado = false;

// O arquivo nao esta residente (ver clawd.h): a primeira queda le o cartao, e a
// volta do servidor devolve os ~150 KB. Este par de flags evita tentar a leitura
// a cada volta do laco quando o arquivo simplesmente nao existe.
bool offlineCarregado = false;
bool offlineFalhou    = false;

// O Clawd dorme por DOIS motivos, e a tela e a mesma porque o recado e o mesmo:
// nada esta acontecendo. Ou o servidor calou (acima), ou ele responde e nao ha
// sessao nenhuma publicando (`semSessao`).
//
// O segundo e o comum — fechar o Claude Code —, e era justamente o que nao
// acionava a tela: ele caia num aviso de texto na P0, e o bicho so aparecia na
// queda de rede, que quase nunca acontece.
bool clawdDorme(uint32_t now) {
    // `haveLast` como na tela do Token: esta tela e a porta para o ultimo
    // retrato bom, e sem retrato nenhum ela nao guarda nada — ali quem fala e a
    // mensagem de boot, que diz o que esta faltando em vez de mostrar um bicho
    // na frente de uma tela vazia.
    if (!display::retrato() || !haveLast || offlineDispensado) return false;
    if (!servidorFora(now) && !semSessao(last)) return false;
    return clawd::offlineW() > 0;
}

void girarTela() {
    const bool novoEmPe = !display::retrato();
    display::setRetrato(novoEmPe);
    // A pagina FICA. Quando em pe so existia a P0, o giro resetava para ela;
    // agora que as quatro existem nos dois lados, resetar perderia o lugar.
    //
    // O manual do Token morre no giro: deitado a tela dele nao existe, e
    // voltar a ficar em pe com ela armada seria um susto sem causa.
    tokenManual = false;
}

// Avanca para o proximo agente, circulando.
void proximoAgente() {
    if (last.agents.empty()) return;
    size_t atual = 0;
    for (size_t i = 0; i < last.agents.size(); i++)
        if (last.agents[i].id == selectedId) { atual = i; break; }
    selectedId = last.agents[(atual + 1) % last.agents.size()].id;
}

}  // namespace

void setup() {
    Serial.begin(115200);

    // NUNCA bloquear escrevendo na serial.
    //
    // Com USB CDC nativo, se ninguem estiver lendo do outro lado, o buffer de
    // transmissao enche e Serial.printf ESPERA. Medido: 4 segundos de laco
    // parado, com rede e desenho zerados. O diagnostico que eu tinha acabado de
    // adicionar para caçar travamentos virou a maior causa de travamento — e so
    // aparecia quando o monitor estava FECHADO, ou seja, exatamente quando eu
    // nao estava olhando.
    //
    // Timeout zero: se nao cabe, o texto e descartado. Perder uma linha de log
    // e irrelevante; perder o toque por 4 segundos nao e.
    Serial.setTxTimeoutMs(0);

    if (!display::begin(200)) { Serial.println("display falhou"); return; }
    touch::begin();
    ui::drawMessage("INICIANDO", "");

    if (!storage::begin()) {
        bootFatal("SEM CONFIG", "cartao SD nao montou");
        return;
    }

    std::string raw;
    if (!storage::readFile("/config.json", raw)) {
        bootFatal("SEM CONFIG", "config.json nao encontrado");
        return;
    }

    cfg = parseConfig(raw.c_str());
    if (!cfg.valid) {
        bootFatal("SEM CONFIG", cfg.error);
        return;
    }

    display::setBrightness(cfg.brightness);

    // Arquivo ausente ou ilegivel deixa o estado zerado, e isso e proposital:
    // um painel que nao sobe por causa de um contador seria pior do que um
    // contador perdido.
    nivel::carregar(nivelEstado);
    contatoSalvo = nivelEstado.contatoS;

    // Depois da config e antes da rede: se faltar, o painel funciona igual e a
    // pagina 3 diz o que esta faltando. Um caranguejo ausente nao e motivo
    // para a placa parar.
    clawd::begin();
    if (!clawd::ready()) Serial.printf("clawd: %s\n", clawd::erro());

    ui::drawMessage("CONECTANDO", cfg.ssid.c_str());
    net::begin(cfg);
    // Logo depois da rede, e nao dentro dela: o SNTP nao e assunto da API. Nao
    // espera associacao — o cliente do lwIP acorda sozinho quando a interface
    // sobe (ver src/hora.h).
    hora::begin(cfg);
    // Ha segunda fonte? So entao o chip com a tag da maquina aparece — com uma
    // fonte so nao ha origem para distinguir. O TEXTO da tag vem do /status de
    // cada maquina, nao daqui.
    ui::duasFontes(!cfg.url2.empty());
}

void loop() {
    if (fatal) {
        // O painel parou e a tela ja diz por que. Esta linha diz o mesmo para
        // quem esta do lado da USB — e no mesmo formato do pulso normal, para
        // que exista um leitor so (ver a skill `esp32-flash`, que grava e
        // depois le a serial para decidir o que quebrou).
        static uint32_t ultimoFatal = 0;
        const uint32_t agoraFatal = millis();
        if (ultimoFatal == 0 || agoraFatal - ultimoFatal >= 5000) {
            Serial.printf("pulso fatal=%s motivo=%s\n",
                          fatalTitulo ? fatalTitulo : "?",
                          fatalDetalhe.c_str());
            ultimoFatal = agoraFatal;
        }
        delay(1000);
        return;
    }

    const uint32_t now = millis();
    bool redraw = false;

    // --- toque: sempre responsivo, independente do ciclo de rede ---
    TouchPoint t = touch::read();
    Gesture g = gestos.update(t.pressed, t.x, t.y, now);

    // O modo terminal consome o gesto ANTES do resto e devolve: la os swipes
    // horizontais nao trocam de pagina (nao ha para onde ir) e os verticais,
    // que fora daqui nao significam nada, rolam o texto.
    if (modoTerminal) {
        // Meia tela por gesto. Uma tela inteira nao deixa referencia nenhuma
        // entre um arrasto e o outro, e ler texto corrido assim e desconfortavel.
        const int passo = ui::termRows() / 2;
        switch (g.kind) {
            case GestureKind::Tap:
            case GestureKind::DoubleTap:
                if (ui::terminalSairAt(g.x, g.y)) { fecharTerminal(); redraw = true; }
                break;
            // Arrastar para CIMA leva o texto para cima, ou seja, mostra o que
            // esta ABAIXO — e abaixo esta o mais recente. E a mesma direcao de
            // qualquer lista rolavel; inverter aqui brigaria com o dedo.
            case GestureKind::SwipeUp:   net::terminalRolar(+passo); break;
            case GestureKind::SwipeDown: net::terminalRolar(-passo); break;
            // Swipe horizontal tambem sai: e o gesto que a mao ja aprendeu
            // neste painel, e nao ter saida nenhuma alem de um botao pequeno
            // seria uma armadilha.
            case GestureKind::SwipeRight:
                fecharTerminal(); redraw = true;
                break;
            default: break;
        }
    } else
    switch (g.kind) {
        case GestureKind::SwipeLeft:
            // As quatro paginas existem nas DUAS orientacoes: cada uma tem a
            // geometria da sua, entao o swipe voltou a valer em pe.
            if (page < ui::PAGES - 1) { page++; redraw = true; }
            break;
        case GestureKind::SwipeRight:
            if (page > 0) { page--; redraw = true; }
            break;
        case GestureKind::DoubleTap:
            // Duplo toque avanca para o proximo agente. So faz sentido na
            // pagina de contexto — e nao vale DENTRO do botao, onde o segundo
            // toque e a confirmacao da limpeza.
            // A confirmacao de um botao e, para o detector de gestos, um duplo
            // toque — e na pagina 1 o duplo toque ja significava "proximo
            // agente". Sem esta ordem, confirmar uma aprovacao trocaria de
            // agente em vez de aprovar. Foi assim que o botao de limpeza
            // quebrou quando nasceu.
            //
            // O BICHO DO CABECALHO vem PRIMEIRO de todos, e vale em qualquer
            // pagina: em pe ele e a unica saida da tela, e uma saida que perde
            // para outro alvo em alguma pagina e uma saida que nao existe. Na
            // pagina do Clawd isso tira dele o canto superior esquerdo (la o
            // duplo toque troca o trabalhador e vale a tela inteira) — 64x46 px
            // de 480x320, e o unico lugar onde os dois gestos se encontram.
            if (ui::iconeCabecalhoAt(g.x, g.y)) {
                girarTela(); redraw = true;
            } else if (telaResetAtiva(now)) {
                // Um toque ja dispensa: a tela nao pede decisao nenhuma, e
                // exigir dois seria cerimonia para fechar um cartaz.
                resetAteMs = 0;
                redraw = true;
            } else if (clawdDorme(now)) {
                // Aqui em cima, e nao ao lado da tela do Token: com esta no ar,
                // QUALQUER duplo toque abaixo do cabecalho e "ja vi, devolve o
                // painel". Os alvos de ensaio logo abaixo pertencem ao layout da
                // P0, que neste momento nao esta na tela — deixa-los na frente
                // faria um toque no lugar errado matar o Kenny em vez de sair.
                //
                // O painel que aparece atras e o ultimo retrato bom: dado velho,
                // com a idade dita no cabecalho.
                //
                // A PSRAM volta na hora. Quem dispensou nao vai reabrir a tela
                // desta queda, e 150 KB parados esperando isso nao se justificam.
                offlineDispensado = true;
                if (offlineCarregado) {
                    clawd::soltarOffline();
                    offlineCarregado = false;
                }
                redraw = true;
            } else if (haveLast && !last.bloqueio.known
                       && ui::rotuloSemanaAt(g.x, g.y)) {
                // Ensaio da morte do Kenny. Sem efeito no tema padrao, que nao
                // tem Kenny nenhum — e por isso a troca de tema fica a um
                // duplo toque de distancia, nos proprios bichos.
                kennyEnsaioAteMs = (now ? now : 1) + KENNY_ENSAIO_MS;
                clawd::matarKenny(true);
                redraw = true;
            } else if (haveLast && !last.bloqueio.known
                       && ui::pctSessaoAt(g.x, g.y)) {
                // O ENSAIO da tela de reset: os mesmos segundos da de verdade
                // (RESET_MS), para dar para ver sem esperar a janela virar.
                // Espelha o atalho do Token, que mora na faixa de baixo.
                //
                // E o mesmo rodizio da de verdade: cada ensaio traz o proximo
                // personagem. E este o jeito de ver os quatro sem esperar
                // quatro janelas virarem.
                resetQual  = "SESSAO";
                resetAteMs = clawd::proximoReset() ? (now ? now : 1) + RESET_MS : 0;
                redraw = true;
            } else if (telaTokenAtiva()) {
                // Com o Token na tela, qualquer duplo toque abaixo do
                // cabecalho e "ja vi, devolve o painel". Nao ha outro alvo
                // para disputar: a tela e dele.
                tokenManual     = false;
                tokenDispensado = true;
                redraw = true;
            } else if (haveLast && !last.bloqueio.known
                       && ui::pctSemanaAt(g.x, g.y)) {
                // O atalho: duplo toque no percentual da SEMANA abre a tela do
                // Token sem esperar o estouro. Com bloqueio na tela o alvo nao
                // existe — la quem mora naquele canto e a pergunta.
                tokenManual = true;
                redraw = true;
            } else if (ui::turmaAt(g.x, g.y, page)) {
                // A fileira troca o ELENCO. Fica ANTES dos alvos de pagina
                // porque ela e o unico controle daquela faixa: deitado o rodape
                // so tem os bichos e as bolinhas de pagina, e em pe a turma
                // mora sozinha entre as duas divisorias.
                //
                // A troca le o cartao e leva ~300 ms. Acontece aqui, no gesto, e
                // nunca dentro de um desenho.
                if (clawd::trocarTema()) redraw = true;
            } else if ((page == 0 || display::retrato()) && haveLast
                       && ui::perguntaP0At(last, g.x, g.y)) {
                // Pergunta em tela cheia da P0: o segundo toque confirma, do
                // mesmo jeito que na aba de contexto.
                tocarOpcao(ui::perguntaP0At(last, g.x, g.y), now);
                redraw = true;
            } else if (page == 1 && haveLast && ui::opcaoAt(last, selectedId, g.x, g.y)) {
                tocarOpcao(ui::opcaoAt(last, selectedId, g.x, g.y), now);
                redraw = true;
            } else if (page == 1 && haveLast
                       && ui::terminalButtonAt(last, selectedId, g.x, g.y)) {
                // O duplo toque cai aqui tambem: dois toques rapidos no botao
                // de terminal sao um duplo toque para o detector, e sem esta
                // linha eles trocariam de agente em vez de abrir a tela.
                abrirTerminal(); redraw = true;
            } else if (page == 1 && haveLast && ui::cleanButtonAt(last, selectedId, g.x, g.y)) {
                tocarBotaoLimpeza(now); redraw = true;
            } else if (page == 1 && !display::retrato()) {
                // Deitado, o duplo toque em qualquer lugar da pagina avanca o
                // agente. Em pe NAO: la a lista mostra os quatro e o toque
                // simples seleciona, entao o duplo toque fica livre para os
                // alvos proprios — a fileira de bichos, que troca o tema.
                proximoAgente(); redraw = true;
            } else if (page == 2) {
                // Duplo toque na pagina do Clawd troca o trabalhador em cena.
                // Vale a pagina inteira e nao so o retangulo do sprite: nao ha
                // mais nada para tocar aqui, e a moldura varia de 152 a 440 px
                // de largura — exigir o alvo certo faria o gesto falhar
                // justamente com os bichos menores.
                if (clawd::trocarTrabalho()) redraw = true;
            }
            break;
        case GestureKind::Tap: {
            if ((page == 0 || display::retrato()) && haveLast
                && last.bloqueio.known) {
                // Pergunta em tela cheia: na P0 das duas orientacoes, e em pe
                // em QUALQUER pagina — la o bloqueio toma a tela inteira.
                const int op = ui::perguntaP0At(last, g.x, g.y);
                if (op) { tocarOpcao(op, now); redraw = true; }
            } else if (page == 1 && haveLast) {
                // Toque simples no menu da direita seleciona aquele repo.
                const int i = ui::agentIndexAt(last, g.x, g.y);
                const int op = ui::opcaoAt(last, selectedId, g.x, g.y);
                if (i >= 0) { selectedId = last.agents[i].id; redraw = true; }
                else if (op) { tocarOpcao(op, now); redraw = true; }
                else if (ui::terminalButtonAt(last, selectedId, g.x, g.y)) {
                    // Um toque so, sem armar. Abrir uma tela de LEITURA nao
                    // muda nada no agente — a confirmacao existe onde o toque
                    // age, e aqui ele nao age.
                    abrirTerminal(); redraw = true;
                }
                else if (ui::cleanButtonAt(last, selectedId, g.x, g.y)) {
                    tocarBotaoLimpeza(now); redraw = true;
                }
            }
            break;
        }
        default: break;
    }

    // --- o retrato do cartao, quando nao ha nada melhor ---
    //
    // Uma placa que liga com o PC desligado nao tinha nada a dizer: `haveLast`
    // falso, tela "API FORA" e fim. O cartao guarda o ultimo retrato bom (ver
    // lib/metrics/cache.h) e ele entra aqui, uma vez, no boot.
    //
    // SO depois do SNTP: sem hora nao da para saber de quando e o arquivo, e um
    // retrato sem idade na tela e um dado velho com cara de fresco — que e
    // exatamente o defeito que este trabalho inteiro foi corrigir.
    //
    // Uma tentativa so. Se o arquivo nao existe ou nao serve, ele nao vai passar
    // a servir na volta seguinte, e reler o cartao a 50 Hz custaria caro.
    if (!haveLast && !cacheTentado && hora::sincronizada()) {
        cacheTentado = true;
        std::string  txt;
        Status       doCartao;
        int          idade = 0;
        if (storage::readFile(CACHE_ARQ, txt) &&
            lerCache(txt.c_str(), hora::agoraLocal(), doCartao, idade)) {
            last         = doCartao;
            haveLast     = true;
            lastOkMs     = now;
            idadeBaseSeg = idade;
            staleSec     = idade;
            sessionDaApi = last.session;
            weekDaApi    = last.week;
            last.clock   = hora::daTela(clockDaApi);
            redraw       = true;
            Serial.printf("cache: retrato de %d s atras\n", idade);
        } else {
            Serial.println("cache: sem retrato utilizavel no cartao");
        }
    }

    // --- rede: colhe o que a tarefa do outro nucleo ja trouxe ---
    // Nao bloqueia e nao espera. A requisicao custa 170-250 ms nesta placa e
    // antes rodava aqui dentro, cegando o toque a cada dois segundos.
    NetResult r;
    Status    s;
    if (net::takeResult(r, s)) {
        if (r == NetResult::Ok) {
            // ---- Quem acabou de terminar um turno? ----
            // ANTES de `last = s`, que e a unica janela em que os dois estados
            // existem ao mesmo tempo.
            //
            // Isto existe SO para a comemoracao da turma, que e um evento e
            // portanto precisa de uma borda. O realce da linha nao passa por
            // aqui: ele desenha por `done`, que a API ja publica e que continua
            // verdadeiro enquanto ninguem viu.
            //
            // So conta quem CONTINUA na lista. Uma sessao que termina e some no
            // mesmo poll nao tem linha nem estado para acompanhar a festa.
            if (haveLast) {
                for (const Agent &a : s.agents) {
                    if (a.state == AgentState::Working) continue;
                    for (const Agent &b : last.agents) {
                        if (b.id != a.id) continue;
                        // Uma comemoracao POR SESSAO: com dois turnos
                        // terminando juntos a turma comemora duas vezes, em
                        // sequencia.
                        if (b.state == AgentState::Working) clawd::comemorar();
                        break;
                    }
                }
            }

            // A janela virou? O detector guarda o poll anterior e devolve qual
            // delas — as regras, e o porque de cada uma, moram no
            // reset_watch.h. Ele e chamado SEMPRE, mesmo com a tela indisponivel:
            // e ele quem carrega o valor de comparacao do proximo poll.
            if (const char *qual = detectarReset(resetWatch, s)) {
                // O personagem muda a cada vez, e a leitura do cartao vem
                // ANTES do prazo: sem arquivo nenhum no rodizio nao ha tela
                // para armar.
                resetQual  = qual;
                resetAteMs = clawd::proximoReset() ? (now ? now : 1) + RESET_MS : 0;
                Serial.printf("clawd: janela %s virou -> tela de reset %s\n",
                              qual, resetAteMs ? "armada" : "SEM SPRITE");
            }

            marcarNovos(s, now, !haveLast);
            if (!kennyEnsaioAteMs || now >= kennyEnsaioAteMs) {
                kennyEnsaioAteMs = 0;
                clawd::matarKenny(algumaSessaoCaida(s));
            }

            // A hora da API vira reserva. Sem isto o proximo poll devolveria o
            // relogio congelado da API para dentro de `last`, desfazendo o que
            // o contador da placa acabou de escrever. O mesmo para os prazos:
            // a partir daqui quem conta e a placa.
            //
            // As tres copias sobem para ANTES do `last = std::move(s)`: o move
            // esvazia `s`, e ler dele depois seria ler lixo. Um `Status` cheio
            // e uma lista de agentes mais vinte strings, e ele era copiado
            // inteiro aqui a cada dois segundos.
            clockDaApi   = s.clock;
            sessionDaApi = s.session;
            weekDaApi    = s.week;
            // Numa rede sem saida para a internet o SNTP nunca chega e o
            // servidor local responde. O carimbo dele acerta o relogio, e e o
            // que destrava o cache: sem hora a placa nao sabe de quando e o
            // retrato do cartao, e um retrato sem idade nao entra na tela.
            // Nao faz nada quando o SNTP ja acertou.
            hora::semear(s.clock.epoch);

            // A resposta boa zera a idade de base: o que veio do cartao deixou
            // de ser a fonte no instante em que a rede voltou a falar.
            last = std::move(s); haveLast = true; lastOkMs = now; staleSec = 0;
            idadeBaseSeg = 0;
            last.clock = hora::daTela(clockDaApi);
            last.session.resets = prazoDaTela(sessionDaApi, 0);
            last.week.resets    = prazoDaTela(weekDaApi, 0);
            semWifi = false;
            ui::marcarSemWifi(false);
            // O limite liberou (reset chegou): a tela do Token rearma sozinha.
            // O proximo estouro e um evento novo e merece a tela de novo.
            if (!ui::limiteEstourado(last)) tokenDispensado = false;
            // Ja respondi esta pergunta? Suprime ate o servidor mandar outra
            // (seq diferente) ou remover o bloqueio — evita ela piscar de volta
            // no poll seguinte, antes de o servidor registrar a resposta.
            if (last.bloqueio.known && last.bloqueio.seq == respondidoSeq)
                last.bloqueio.known = false;
            clawd::select(last, true);
            redraw = true;

            // Pergunta NOVA traz a selecao para quem perguntou. Sem isto, a
            // pergunta ficaria escondida atras de uma selecao antiga e o painel
            // so mostraria "bloqueado" no menu, que e o que ele ja fazia.
            //
            // So a selecao, e nao a pagina: trocar de tela debaixo de quem esta
            // olhando outra coisa e mais intrusivo do que util, e o menu ja
            // pinta o bloqueado em vermelho de qualquer jeito.
            static int ultimoBloqSeq = 0;
            if (last.bloqueio.known && last.bloqueio.seq != ultimoBloqSeq) {
                ultimoBloqSeq = last.bloqueio.seq;
                for (const Agent &a : last.agents) {
                    if (a.id == last.bloqueio.agentId) {
                        selectedId = a.id;
                        break;
                    }
                }
            } else if (!last.bloqueio.known) {
                ultimoBloqSeq = 0;
            }
        } else if (haveLast) {
            // A API caiu, mas o ultimo valor ainda informa. Mantem na tela,
            // esmaecido, com a idade explicita — nunca um numero que finge
            // estar fresco.
            staleSec = idadeBaseSeg + (int)((now - lastOkMs) / 1000);
            redraw = true;
            // Sem WIFI e outra coisa de "API demorando", e a diferenca muda o
            // que fazer: uma se resolve esperando, a outra nao. Com dado velho
            // na tela o aviso vinha sempre como "sem contato ha Ns", e uma
            // queda de rede se disfarcava de servidor lento.
            semWifi = (r == NetResult::NoWifi);
            ui::marcarSemWifi(semWifi);
        } else if (r == NetResult::NoWifi) {
            ui::drawMessage("SEM WIFI", cfg.ssid.c_str());
            delay(20);
            return;
        } else {
            // Nunca houve resposta: nao ha o que esmaecer.
            ui::drawMessage("API FORA", cfg.url.c_str());
            delay(20);
            return;
        }
    }

    // O botao armado desarma sozinho. Sem isto ele ficaria carregado ate o
    // proximo toque, que pode ser dias depois e por acidente.
    if (armadoMs && (now - armadoMs) > ARMADO_MS) { armadoMs = 0; redraw = true; }

    // A opcao armada desarma pelos mesmos dois motivos, e um terceiro que so
    // existe aqui: o bloqueio saiu da tela (alguem respondeu no teclado) ou a
    // pergunta mudou. Deixar armada uma escolha de uma pergunta que nao esta
    // mais la e o unico jeito de este painel aprovar a coisa errada.
    if (opcaoArmada) {
        const bool sumiu = !last.bloqueio.known;
        const bool outra = last.bloqueio.seq != opcaoSeq;
        const bool velha = opcaoDesdeMs && (now - opcaoDesdeMs) > ARMADO_MS;
        if (sumiu || outra || velha) {
            opcaoArmada  = 0;
            opcaoDesdeMs = 0;
            redraw = true;
        }
    }

    // Sem resposta nova ha muito tempo: o numero na tela precisa envelhecer
    // sozinho, senao ele finge estar fresco enquanto a rede esta fora.
    //
    // Este bloco JA teve um `staleSec == 0` na condicao, e ele congelava o aviso
    // em "SEM CONTATO HA 6s" para sempre. O 6 nao era a idade do dado: era o
    // limiar (pollMs * 3 = 6000 ms) gravado na unica vez que a condicao podia
    // ser verdadeira. Da segunda em diante `staleSec` ja era diferente de zero e
    // barrava a si mesmo, entao o numero parava enquanto a placa seguia viva —
    // com o laco a 170 voltas por 5 s. Um aviso que mente a idade e pior do que
    // nao ter aviso: ele fazia o contato perdido parecer recem-perdido, horas
    // depois, e mandava a investigacao atras de um travamento que nao existia.
    if (haveLast && (now - lastOkMs) > cfg.pollMs * 3) {
        const int idade = idadeBaseSeg + (int)((now - lastOkMs) / 1000);
        // So quando o SEGUNDO vira. Sem esta comparacao o redesenho aconteceria
        // a cada volta do laco, e na pagina do Clawd cada um custa ~64 ms.
        if (idade != staleSec) { staleSec = idade; redraw = true; }
    }

    // O relogio do cabecalho anda pelo contador DA PLACA, e nao pelo payload.
    //
    // Era este o defeito mais silencioso do painel: fechar o Claude Code parava
    // a hora. Nao esmaecia, nao avisava — ficava marcando o minuto em que a API
    // calou, com a mesma cara de um relogio certo. Agora o `clockDaApi` so vale
    // enquanto o SNTP nao sincronizou (ver src/hora.h).
    //
    // Uma vez por segundo, e nao a cada volta: montar um Clock sao tres
    // std::string, e o laco roda a 50 Hz.
    if (haveLast && (uint32_t)(now - ultimoRelogioMs) >= 1000) {
        ultimoRelogioMs = now;
        const Clock agora = hora::daTela(clockDaApi);
        // Redesenha na virada do MINUTO. O cabecalho so mostra hora e minuto: um
        // redesenho por segundo seria 59 quadros iguais por minuto, e na pagina
        // do Clawd cada um custa ~64 ms.
        if (agora.hm != last.clock.hm || agora.known != last.clock.known) {
            last.clock = agora;
            redraw = true;
        }

        // Os prazos das duas janelas tambem andam sozinhos.
        //
        // Eles vinham prontos da API ("3d06h") e o firmware so imprimia, entao
        // congelavam junto com ela — e congelavam numa forma que mente: com 39
        // minutos para a virada, "0d00h" se le como "acabou". Agora a conta e
        // daqui: o prazo cru menos a idade do dado, escrito na resolucao que o
        // numero pede (ver lib/metrics/relogio.h).
        //
        // A conta parte de `sessionDaApi`/`weekDaApi`, e nao de `last`: sem uma
        // copia crua, cada segundo descontaria a idade de novo sobre o valor ja
        // descontado, e o prazo desabaria.
        const std::string sess = prazoDaTela(sessionDaApi, staleSec);
        const std::string sem  = prazoDaTela(weekDaApi, staleSec);
        if (sess != last.session.resets || sem != last.week.resets) {
            last.session.resets = sess;
            last.week.resets    = sem;
            redraw = true;
        }
        // `resetsIn` acompanha porque ele nao e so o prazo: e dele que sai o
        // ritmo da barra e a hora absoluta do reset. Se so o texto envelhecesse,
        // a barra ficaria dizendo que a janela parou de correr.
        last.session.resetsIn = (int)prazoRestante(sessionDaApi, staleSec);
        last.week.resetsIn    = (int)prazoRestante(weekDaApi, staleSec);
    }

    // Tempo de tela ligada EM CONTATO. Nao e uptime: com a API fora o relogio
    // para, porque e isso que "se comunicando com o servidor" quer dizer. E o
    // unico dos tres contadores do nivel que so a placa sabe — turnos e custo
    // vem da API, que ja os tem inteiros.
    //
    // Medido por diferenca de instantes e nao somando um intervalo fixo por
    // volta: assim uma volta lenta do laco nao inventa nem perde segundo. O
    // resto abaixo de um segundo fica guardado em `contatoDesdeMs` para a
    // proxima volta, senao a fracao seria descartada 50 vezes por segundo e o
    // contador andaria mais devagar do que o relogio.
    const bool emContato = haveLast && staleSec == 0;
    if (emContato) {
        if (!contatoDesdeMs) contatoDesdeMs = now;
        const uint32_t decorrido = (now - contatoDesdeMs) / 1000;
        if (decorrido) {
            nivelEstado.contatoS += decorrido;
            contatoDesdeMs += decorrido * 1000;
        }
    } else {
        contatoDesdeMs = 0;
    }

    // A trava de nivel maximo. Calculada AQUI e nao no desenho: o desenho so
    // roda com a quarta pagina visivel, e o nivel tem que ficar registrado
    // mesmo que ninguem nunca abra aquela tela.
    if (haveLast && last.vitalicio.known) {
        int n = nivel::calcular(last.vitalicio.turnos,
                                last.vitalicio.costUsd,
                                nivelEstado.contatoS / 3600.0f);
        if (n > nivelEstado.nivelMax) {
            nivelEstado.nivelMax = n;
            contatoSalvo = 0;          // forca a proxima gravacao periodica
        }
        if (n < nivelEstado.nivelMax) n = nivelEstado.nivelMax;

        // O XP ganho HOJE. A marca da virada mora no cartao junto do resto, e a
        // data vem da API — a placa nao tem relogio de bateria nem fuso, entao
        // quem sabe que dia e hoje e ela, como no resto do painel.
        const float xpAgora = nivel::xp(last.vitalicio.turnos,
                                        last.vitalicio.costUsd,
                                        nivelEstado.contatoS / 3600.0f);
        if (nivel::virada(nivelEstado, xpAgora, last.clock.date.c_str(),
                          xpDoDia)) {
            contatoSalvo = 0;      // marca nova: forca a proxima gravacao
            redraw = true;         // o numero do dia mudou na tela
        }

        // Escolhido AQUI e nao no desenho da quarta pagina: o bicho do nivel
        // agora tambem e o icone do cabecalho, que aparece em TODAS as telas.
        // Deixar a carga a cargo da pagina faria o cabecalho ficar sem bicho
        // ate alguem deslizar ate la — e voltar ao logo depois de cada boot.
        clawd::selecionarNivel(n);
        clawd::selecionarFundo(n);
    }

    // Grava a cada 60 s, e SO quando mudou. O cartao nao precisa de escrita a
    // 50 Hz, e desgastar celula para regravar o mesmo numero seria trabalho
    // puro. Perder ate 60 s de convivio num corte de energia e barato; perder o
    // arquivo por escrever demais nao seria.
    if (now - salvoEmMs > 60000) {
        salvoEmMs = now;
        if (nivelEstado.contatoS != contatoSalvo) {
            if (nivel::salvar(nivelEstado)) contatoSalvo = nivelEstado.contatoS;
        }
    }

    // O retrato para o proximo boot.
    //
    // Fora do bloco de 60 s acima porque a PRIMEIRA gravacao nao pode esperar um
    // minuto: uma placa que liga, pega um /status bom e ve o PC desligar aos 40 s
    // nao teria nada no cartao — e esse e exatamente o caso que o cache existe
    // para cobrir.
    //
    // Depois da primeira, uma a cada CINCO minutos. O cache descreve um passado
    // que vai ser lido horas depois — cinco minutos de defasagem nele nao mudam
    // nenhuma decisao de quem olha o painel, e a cadencia de um minuto seriam
    // 1440 pares de remove+rename por dia no FAT para reescrever quase sempre o
    // mesmo texto.
    //
    // Cinco minutos e nao "so quando mudou": o carimbo `em` muda a cada
    // gravacao por construcao, entao comparar o texto exigiria compara-lo
    // ignorando um campo — codigo que sabe a ordem do JSON para economizar uma
    // escrita de 400 bytes.
    //
    // So com dado FRESCO. Regravar um retrato ja velho faria o carimbo mentir a
    // idade dele, que e a unica coisa que este arquivo precisa acertar.
    if (haveLast && staleSec == 0 && hora::sincronizada()
        && (!cacheSalvoMs || (now - cacheSalvoMs) > 5UL * 60UL * 1000UL)) {
        const std::string txt = serializarCache(last, hora::agoraLocal());
        if (!txt.empty() && storage::writeFileAtomic(CACHE_ARQ, txt))
            cacheSalvoMs = now ? now : 1;
    }

    // Tarefa de rede travada: reinicia a placa. Ela e quem faz a requisicao,
    // entao nao ha como destrava-la daqui — sem isto o painel fica horas com
    // dado velho, que foi o estado em que ele foi encontrado.
    //
    // A condicao e VOLTA NAO COMPLETADA, e nao "sem resposta boa". Sao coisas
    // diferentes: com a API fora ou o Wi-Fi caido, fetchOnce devolve erro na
    // hora e o contador SOBE. Congelado, ele so pode significar bloqueio dentro
    // da requisicao. Por isso uma API legitimamente fora nao vira laco de boot:
    // ali o painel continua de pe, esmaecido, com a idade crescendo.
    //
    // 60 s com poll de 2 s sao 30 voltas perdidas, e o pior caso legitimo de
    // uma volta e de poucos segundos (connect 3 s + leitura 3 s).
    static uint32_t ciclosVistos  = 0;
    static uint32_t ciclosMudouMs = 0;
    const uint32_t ciclosAgora = net::cycles();
    if (ciclosAgora != ciclosVistos) {
        ciclosVistos  = ciclosAgora;
        ciclosMudouMs = now;
    } else if (ciclosMudouMs && (now - ciclosMudouMs) > 60000) {
        Serial.printf("rede travada ha %lums na volta %lu — reiniciando\n",
                      (unsigned long)(now - ciclosMudouMs),
                      (unsigned long)ciclosAgora);
        // delay e nao flush: com setTxTimeoutMs(0) a escrita nao espera, e
        // flush() sem ninguem lendo do outro lado e justamente o bloqueio que
        // aquela configuracao existe para evitar.
        delay(50);
        ESP.restart();
    }

    // --- animacao do Clawd ---
    // Dois custos bem diferentes, e por isso dois caminhos:
    //
    //   pagina 3   o Clawd grande ocupa x~150..330, FORA da faixa esquerda que
    //              o flush de prefixo alcanca. Cada quadro custa um redesenho
    //              de tela inteira, ~64 ms.
    //   rodape     a turma mora em x=14..200, dentro da faixa. Cada quadro
    //              custa ~21 ms (ver display::flushPrefix), e clawd::tick
    //              limita a no maximo um envio a cada 100 ms.
    //
    // O DEDO TEM PRIORIDADE, mas so onde o quadro e caro. Durante um redesenho
    // de 64 ms o toque nao e lido, e um swipe precisa de varias amostras
    // seguidas para ser classificado — gestos rapidos se perdiam inteiros
    // dentro de um quadro. Por isso a pagina 3 congela enquanto ha dedo na
    // tela. O rodape nao precisa disso: a 9 ms o gesto atravessa sem sentir, e
    // congelar o selo durante o swipe seria visivel a toa.
    static uint32_t ultimoToqueMs = 0;
    if (t.pressed) ultimoToqueMs = now;
    const bool mexendo = ultimoToqueMs && (now - ultimoToqueMs) < 600;

    // `!modoTerminal`: la a tela e do agente, e um quadro de animacao do rodape
    // escreveria o selo por cima do texto. Os contadores param junto, e isso
    // nao se ve — voltar do terminal cai no quadro em que a animacao estava.
    if (!redraw && haveLast && !modoTerminal) {
        // Nas paginas 3 e 4 o bicho ocupa a tela e o quadro custa um redesenho
        // inteiro (~64 ms), que cega o toque — por isso a animacao para
        // enquanto ha dedo na tela. No rodape um quadro custa ~21 ms com o
        // flush de prefixo, no maximo 10 vezes por segundo: parar seria visivel
        // e nao compraria responsividade nenhuma.
        // O bicho do cabecalho troca a cada dez minutos, e a troca LE DO CARTAO.
        // Nunca com o dedo na tela: o engasgo da leitura engoliria o gesto, e
        // dez minutos de espera nao tem pressa nenhuma para insistir agora.
        if (clawd::tickIconeCabecalho(now, !mexendo)) redraw = true;

        const bool caro = (page == 2 || page == 3);
        if (!(caro && mexendo)) {
            // `clawd::tick` avanca o selo, o trio e o bicho do cabecalho. Roda
            // em TODA pagina, inclusive onde eles nao aparecem: congelar o que
            // esta escondido faria a animacao saltar ao trocar de pagina.
            const bool avancouFaixa = clawd::tick(now);

            if (page == 3) {
                // A QUARTA PAGINA anima SO o bicho do nivel e o fundo.
                //
                // O mago e o bicho do clima ficam parados aqui de proposito.
                // Cada quadro deles custaria um flush de tela INTEIRA para
                // mexer um enfeite de canto — e esta pagina ja paga duas
                // animacoes grandes, que sao as que a pagina existe para
                // mostrar. O resultado de `tick` e ignorado pelo mesmo motivo:
                // nada do que ele avanca aparece aqui.
                //
                // O fundo tem cadencia propria (4 fps contra 6 do bicho), mas
                // os dois pedem o MESMO redesenho inteiro — entao pedir junto e
                // de graca.
                if (clawd::tickNivel(now)) redraw = true;
                if (clawd::tickFundo(now)) redraw = true;
            } else {
                // A saida da tela do Cartman pede a tela inteira de volta, e
                // pede ANTES de qualquer redesenho parcial deste ciclo.
                if (resetAcabouAgora(now)) redraw = true;

                if (avancouFaixa && !redraw) {
                    if (caro) {
                        redraw = true;
                    } else if (!telaResetAtiva(now)) {
                        // Em pe a turma esta SEMPRE no topo nas paginas
                        // baratas — a inicial e a de contexto usam o mesmo
                        // lugar (ver ui.cpp, R1_C1_Y) —, e a faixa barata do
                        // flush a alcanca nas duas: ~11 ms por quadro.
                        // A tela do Token e a excecao: la o topo e da cabeca
                        // do bicho, e sem isto o redrawBadge pintava a turma
                        // por cima dela.
                        //
                        // Na tela do RESET este caminho nao roda: os 50 mil
                        // pixels que ele enviaria ja vao dentro do prefixo do
                        // `redrawReset`, e o bicho do cabecalho e desenhado la
                        // — pagar um envio proprio seria tirar 6 ms de um
                        // orcamento de 70.
                        ui::redrawBadge(last, staleSec,
                                        telaTokenAtiva() || clawdDorme(now));
                    }
                }
                // O bicho do CLIMA e caso a parte, e caro. Ele vive no
                // cabecalho, fora da faixa do flush de prefixo, entao
                // `redrawBadge` nao o alcanca: so um redesenho INTEIRO o
                // mostra.
                //
                // Em pe ele nao esta na tela (o cabecalho la e so o bicho e a
                // hora), e um quadro de algo invisivel custaria os mesmos 48 ms
                // do flush inteiro — pagos por nada, seis vezes por segundo.
                // Em pe o clima so aparece na P2 — que e cara e ja redesenha
                // por conta do bicho grande, entao o tick so precisa avancar o
                // quadro.
                if ((!display::retrato() || page == 2) && clawd::tickClima(now))
                    redraw = true;
                // O Token da tela de limite: 900 ms por pose, e cada pose e um
                // flush inteiro — ~48 ms a cada 900, so enquanto a tela dele
                // esta no ar.
                // O ensaio da morte acaba sozinho, sem esperar poll: com o
                // painel parado o Kenny ficaria caido ate a proxima resposta.
                if (kennyEnsaioAteMs && now >= kennyEnsaioAteMs) {
                    kennyEnsaioAteMs = 0;
                    clawd::matarKenny(haveLast && algumaSessaoCaida(last));
                    redraw = true;
                }
                if (telaTokenAtiva() && clawd::tickToken(now)) redraw = true;
                // O bicho da tela de servidor fora anima na mesma cadencia: e o
                // mesmo arquivo, com outra camisa. Sem isto ele ficaria parado
                // na tela, o que se le como painel travado — justamente o que
                // esta tela existe para desmentir.
                if (clawdDorme(now) && clawd::tickOffline(now))
                    redraw = true;
                // A tela de reset e curta, mas o bicho dela dança: 12 quadros a
                // 150 ms no Cartman, 10 a 165 no Kenny. Cada quadro repinta SO
                // a caixa dele e envia um prefixo — o desenho completo gastaria
                // quase metade do intervalo so em limpar e enviar a tela
                // inteira (ver ui::redrawReset).
                //
                // `!redraw` por ultimo de proposito: o tick precisa avancar o
                // quadro mesmo quando a volta ja vai redesenhar tudo, senao a
                // danca engasga justamente quando chega status novo.
                if (telaResetAtiva(now) && clawd::tickReset(now) && !redraw)
                    ui::redrawReset();
            }
        }
    }

    // --- o sprite do Clawd dormindo entra e sai com o estado ---
    //
    // Ele nao mora na PSRAM o dia inteiro (ver clawd.h): tanto a queda da API
    // quanto o intervalo sem sessao sao eventos de ESPERA, e a leitura do
    // cartao cabe dentro deles. Voltar a ter contato (ou sessao) devolve a
    // memoria e rearma tudo — inclusive a tentativa de leitura, para um arquivo
    // que chegou pelo ar depois do boot entrar na proxima vez sem reboot.
    {
        const bool dorme = servidorFora(now) || semSessao(last);
        if (dorme && haveLast && display::retrato() && !offlineDispensado) {
            if (!offlineCarregado && !offlineFalhou) {
                offlineCarregado = clawd::carregarOffline();
                offlineFalhou    = !offlineCarregado;
                if (offlineCarregado) redraw = true;
            }
        } else if (!dorme) {
            if (offlineCarregado) { clawd::soltarOffline(); redraw = true; }
            offlineCarregado  = false;
            offlineFalhou     = false;
            offlineDispensado = false;
        }
    }

    // A tela do terminal chega pela mesma tarefa de rede, em outra requisicao.
    if (modoTerminal) {
        TerminalTela nova;
        if (net::takeTerminal(nova)) { termTela = nova; redraw = true; }
        // Nao depende de `haveLast`: a tela do terminal se sustenta sozinha, e
        // ficar preso na mensagem de boot por causa do /status seria esconder
        // justamente o que se abriu para ler.
        if (redraw)
            ui::drawTerminal(termTela.linhas, termTitulo.c_str(),
                             termTela.travado);
    } else if (redraw && haveLast)
        ui::drawStatus(last, page, selectedId, staleSec, armadoMs != 0,
                       nivelEstado, xpDoDia, opcaoArmada, telaTokenAtiva(),
                       telaResetAtiva(now) ? resetQual : nullptr,
                       clawdDorme(now));

    // --- a foto da tela, quando alguem pede ---
    //
    // Aqui, e nao na tarefa de rede: o framebuffer pertence ao laco. Copiado do
    // outro nucleo, ele viria pela metade num redesenho em andamento — fundo ja
    // limpo, texto ainda nao escrito — e a foto mostraria uma tela que nunca
    // existiu. Neste ponto o quadro esta inteiro por construcao, sem trava.
    //
    // O que ESTA no quadro e o instante em que ele foi tirado, inclusive um
    // bicho no meio do pulo. Isso e desejado: e justamente o que uma descricao
    // em texto nao consegue transmitir.
    {
        long capId = 0;
        int  capOrigem = 0;
        if (net::capturaPedida(capId, capOrigem)) {
            size_t bytes = 0;
            uint8_t *buf = net::capturaBuffer(bytes);
            if (buf) {
                if (display::copiarQuadro(buf, bytes))
                    net::enviarCaptura(capId, capOrigem, display::retrato());
                else
                    net::falhaCaptura();
            }
        }
    }

    // Arquivo novo baixado para o cartao? Quem grava e o LACO, e nao a tarefa
    // de rede: o SD e deste nucleo — os sprites saem dele no meio do desenho —
    // e duas tarefas dentro do mesmo FatFS e uma aposta que ninguem cobre. A
    // gravacao segura o desenho por ~1-2 s, o que num fluxo de manutencao e
    // preco justo; quem confirma e reinicia e a rede, DEPOIS de avisar a API.
    {
        std::string caminho;
        const uint8_t *buf = nullptr;
        size_t bytes = 0;
        bool reiniciar = false;
        if (net::atualizacaoPronta(caminho, buf, bytes, reiniciar)) {
            const bool ok = storage::writeBufferAtomic(caminho.c_str(), buf,
                                                       bytes);
            Serial.printf("upd: %s %s (%u bytes)\n",
                          ok ? "gravado" : "FALHOU",
                          caminho.c_str(), (unsigned)bytes);
            net::confirmarAtualizacao(ok);
        }
    }

    // O PULSO — contrato de diagnostico desta placa, e nao codigo temporario.
    //
    // A skill `esp32-flash` grava e depois LE esta linha para separar "gravou"
    // de "gravou e esta funcionando": `wifi=1 http=200` na ultima linha da
    // janela e o veredito dela. Apagar este printf faz a skill reprovar placa
    // saudavel, e o defeito aparece longe daqui.
    //
    // Ele nasceu temporario, cacando um congelamento com "SEM CONTATO HA 6s", e
    // ficou porque provou seu valor na primeira hora de vida.
    //
    // Este pulso ja derrubou a primeira teoria, que estava escrita aqui: que
    // contador parado significava LACO parado. Medido na placa no estado morto,
    // com 2h39 de uptime, o laco rodava a ~170 voltas por 5 s e a serial
    // falava. O contador parava por um `staleSec == 0` na condicao que o
    // envelhecia (ja corrigido acima), e nao por travamento nenhum.
    //
    // O que sobra e a tarefa de rede parar de entregar resultado. `ciclos` e
    // `fetch` existem para dizer se ela esta bloqueada na requisicao ou viva e
    // falhando — `http` sozinho nao serve, porque so e escrito dentro dela.
    //
    // Serial.printf nao bloqueia (setTxTimeoutMs(0) no setup), entao imprimir
    // com o monitor fechado e seguro — foi essa exata armadilha que causou um
    // travamento de 4 s numa investigacao anterior.
    static uint32_t ultimoPulso = 0;
    static uint32_t voltas = 0;
    voltas++;
    if (now - ultimoPulso >= 5000) {
        Serial.printf("pulso t=%lus voltas=%lu wifi=%d rssi=%d http=%d "
                      "ciclos=%lu fetch=%lums contato=%lus "
                      "spr(clima=%d nivel=%d fundo=%d) "
                      "cap=%lu/%d "
                      "heap=%u min=%u psram=%u stale=%d pg=%d\n",
                      (unsigned long)(now / 1000), (unsigned long)voltas,
                      (int)net::connected(), net::rssi(), net::lastHttpCode(),
                      (unsigned long)net::cycles(),
                      (unsigned long)net::sinceLastFetchMs(),
                      (unsigned long)nivelEstado.contatoS,
                      // Sprite que nao carrega falha em SILENCIO — a mensagem
                      // de erro sai uma vez so, no primeiro desenho, e quem
                      // abre o monitor depois disso nunca a ve. Aqui o estado
                      // fica visivel o tempo todo.
                      clawd::climaW() ? 1 : 0, clawd::nivelW() ? 1 : 0,
                      clawd::fundoPronto() ? 1 : 0,
                      // Fotos pedidas / codigo da ultima. 200 = a foto chegou
                      // no PC; -2000 = nao ha 300 KB contiguos de PSRAM;
                      // -2001 = o canvas nao devolveu o quadro. Sem isto, a
                      // captura que falha e a que ninguem pediu sao a mesma
                      // coisa vista daqui.
                      (unsigned long)net::capturasPedidas(),
                      net::lastCapturaCode(),
                      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                      (unsigned)ESP.getFreePsram(), staleSec, page);
        ultimoPulso = now;
        voltas = 0;
    }

    // ~50 leituras de touch por segundo. Enquanto o bicho da tela de reset
    // dança, nao: ali cada volta E um quadro, e os 20 ms parados comeriam um
    // terço dos 70 que o arquivo mais rapido da. A tela e curta e nao tem gesto
    // para perder — ela morre pelo relogio, e o unico controle que sobra e o
    // giro, que continua sendo lido a cada volta.
    delay(telaResetAtiva(now) ? 2 : 20);
}
