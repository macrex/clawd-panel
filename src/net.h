#pragma once
#include <string>
#include <vector>
#include "app_config.h"
#include "status.h"

enum class NetResult { Ok, NoWifi, HttpError, BadPayload };

// Uma janela da tela de um agente, ja em ASCII e ja quebrada na largura que o
// painel pediu. Quem prepara e a API — ver server/terminal.py.
struct TerminalTela {
    bool                     ok      = false;
    std::vector<std::string> linhas;
    // A API conseguiu travar o pane na largura desta tela? Sem a trava o texto
    // chega na largura do host e a tela fica ilegivel — e o painel diz isso, em
    // vez de mostrar texto embaralhado e parecer defeito dele.
    bool                     travado = false;
};

namespace net {

// Conecta ao Wi-Fi e sobe a tarefa de polling no OUTRO nucleo.
//
// Por que uma tarefa, e nao uma chamada no laco: medido nesta placa, uma
// requisicao de 1401 bytes na LAN custa 170 a 250 ms — tres a quatro vezes o
// redesenho da tela inteira. Nao e culpa do servidor, que responde em 2 a 14 ms
// medidos do PC; e a pilha Wi-Fi do ESP32 mesmo. Dentro do laco, isso cegava o
// toque a cada poll e engolia gestos inteiros. O ESP32-S3 tem dois nucleos e o
// projeto so usava um.
void begin(const AppConfig &c);

bool connected();
int  lastHttpCode();          // para diagnostico
int  rssi();                  // forca do sinal, para explicar latencia alta

// DIAGNOSTICO. `lastHttpCode()` nao diz se a rede esta viva AGORA: ele so e
// escrito dentro da requisicao, entao com a tarefa parada um 200 velho passa
// por atual. Estes dois separam os dois casos que aquele codigo confunde:
//
//   ciclos parado  + idade crescendo -> a tarefa esta BLOQUEADA na requisicao
//   ciclos subindo + resultado ruim  -> a tarefa esta viva e a rede e que falha
uint32_t cycles();              // voltas completadas pela tarefa de rede
uint32_t sinceLastFetchMs();    // desde o fim da ultima; 0 = nenhuma ainda

// Pega o resultado mais recente da tarefa, se houver um novo desde a ultima
// chamada. Nao bloqueia. `status` so e preenchido quando devolve NetResult::Ok.
bool takeResult(NetResult &result, Status &status);

// ---- Comandos: o painel deixando de so olhar ----
// Enfileira um POST /command para a tarefa de rede mandar. NAO bloqueia e NAO
// espera resposta: o laco principal desenha a 50 Hz e um POST custa centenas de
// milissegundos, entao ele nunca pode acontecer aqui dentro.
//
// UM comando por vez, e o segundo enquanto o primeiro nao saiu e DESCARTADO.
// Fila nao serve: o segundo toque de um dedo hesitante viraria uma segunda
// compactacao alguns segundos depois da primeira, sem ninguem ter pedido.
//
// A confirmacao de que funcionou NAO vem daqui: vem do proximo /status, no
// campo `cleaning`. Quem diz que a limpeza esta acontecendo e a API, e nao a
// nossa lembranca de ter mandado.
//
// `origem` diz a qual maquina o agente pertence (Agent::origem): o POST vai
// para a API dela — o pane so existe la.
void sendCommand(const std::string &sessionId, const char *cmd, int origem);

// Codigo HTTP do ultimo comando, para diagnostico. 0 = nenhum ainda.
int lastCommandCode();

// Enfileira a resposta a um prompt de aprovacao: POST /responder. Mesma fila e
// mesmas regras do comando — nao bloqueia, nao espera resposta, e uma so por
// vez.
//
// O `rotulo` viaja junto com o numero porque quem confere e a API, contra a
// tela do agente no instante em que o POST chega. Se a pergunta mudou no
// caminho, ela nao envia nada — um toque de dois segundos atras nao pode
// aprovar a pergunta seguinte.
//
// A confirmacao NAO vem daqui: vem do proximo /status, onde o bloqueio some
// porque o agente deixou de estar bloqueado.
//
// `origem` roteia o POST para a maquina do agente, como no comando.
void responder(const std::string &paneId, int n, const std::string &rotulo,
               int origem);

int lastResponderCode();

// ---- Foto da tela ----
// Alguem pediu uma captura do painel? Devolve true UMA VEZ por pedido novo, com
// o `id` e a maquina que pediu.
//
// O pedido chega pelo proprio /status (campo `captura`), por carona no polling
// que ja acontece: a placa nunca aceita conexao de entrada, e por isso nao ha
// porta aberta nem servidor dentro do laco de desenho.
//
// O ultimo id atendido e guardado POR FONTE e comparado por DESIGUALDADE. Cada
// API tem o seu contador (o mesmo numero em duas maquinas sao pedidos
// diferentes) e ela reinicia a cada logon, entao "maior que o ultimo" congelaria
// a captura ate o contador alcancar o valor antigo.
//
// Chame do LACO: quem tira a foto e quem desenha (ver display::copiarQuadro).
bool capturaPedida(long &id, int &origem);

// O buffer de envio, alocado em PSRAM na primeira captura. `nullptr` quando nao
// ha memoria ou quando o envio anterior ainda nao terminou — nos dois casos o
// pedido simplesmente nao e atendido, e a API o deixa expirar. Um diagnostico
// nunca pode ser o motivo de o painel parar.
uint8_t *capturaBuffer(size_t &bytes);

// Enfileira o POST /tela com o que esta no buffer. Nao bloqueia: 300 KB custam
// entre meio segundo e um segundo e meio na pilha Wi-Fi desta placa, e isso
// dentro do laco cegaria o toque.
//
// Sem fila e sem retentativa: se o envio falhar, o pedido morre. Uma foto que
// chega tres segundos depois descreve uma tela que ja mudou.
void enviarCaptura(long id, int origem, bool retrato);

// Codigo do ultimo envio: HTTP quando saiu, ou -2000 (sem os 300 KB contiguos
// de PSRAM) e -2001 (o canvas nao devolveu o framebuffer). 0 = nenhuma foto
// ainda. Sai no pulso, ao lado do numero de fotos pedidas: sem isso a captura
// que nao acontece e indistinguivel da que ninguem pediu.
int lastCapturaCode();

uint32_t capturasPedidas();

// A copia do quadro falhou: registra o motivo e devolve o buffer. Sem isto uma
// falha deixaria a placa surda para todos os pedidos seguintes.
void falhaCaptura();

// ---- Atualizacao de arquivo do cartao ----
// O download acontece DENTRO da tarefa de rede: ela ve o pedido no /status
// (campo `arquivo`), baixa o binario para um buffer temporario de PSRAM e o
// deixa pronto. Quem GRAVA no cartao e o laco — o SD e dele, e duas tarefas
// escrevendo no mesmo FatFS e um risco que ninguem precisa correr.
//
// O id segue a regra da captura: guardado por fonte, comparado por
// desigualdade. Um download que falha nao e retentado — a API deixa o pedido
// expirar e quem pediu ve o estado.

// Ha um arquivo baixado e conferido esperando gravacao? Devolve true UMA VEZ.
// `caminho` ja vem completo ("/clawd/<nome>"); `buf` pertence a rede e e
// devolvido em confirmarAtualizacao.
bool atualizacaoPronta(std::string &caminho, const uint8_t *&buf, size_t &bytes,
                       bool &reiniciar);

// O laco terminou de gravar (ou falhou). Libera o buffer e enfileira o POST
// /arquivo/ok para a API encerrar o pedido. Com `reiniciar` e gravacao boa, a
// tarefa reinicia a placa DEPOIS de avisar a API.
void confirmarAtualizacao(bool gravou);

// Codigo do ultimo ciclo de atualizacao, para o pulso: HTTP do download ou do
// aviso, -2100 (sem PSRAM para o buffer), -2101 (download truncado),
// -2102 (CRC nao confere), -2103 (gravacao no cartao falhou). 0 = nenhum.
int lastAtualizacaoCode();

// ---- Modo terminal ----
// A tarefa passa a buscar TAMBEM a tela do pane, na mesma volta do /status. Ela
// so faz isso entre `terminalAbrir` e `terminalFechar` — fora do modo o custo e
// zero, e por isso a tela de terminal nao pesa nas quatro paginas normais.
//
// `cols` e `rows` sao a geometria da TELA DA PLACA, e vao no pedido porque quem
// quebra as linhas e a API: ela e quem sabe traduzir o texto do terminal para o
// que esta fonte desenha, e quebrar de la evita mandar pela rede o que nao
// caberia mesmo.
//
// `origem` fixa a maquina da tela: a leitura e a de fechar vao para a mesma API
// enquanto o modo estiver aberto.
void terminalAbrir(const std::string &paneId, int cols, int rows, int origem);

// Rola a tela DO HOST: `linhas` negativo volta no historico, positivo avanca.
// E um delta e nao uma posicao porque quem guarda a posicao e o terminal do
// outro lado — a API so repassa a roda de mouse.
//
// A tarefa acorda na hora em vez de esperar o proximo poll: com a espera cheia,
// cada arrasto levaria ate dois segundos para aparecer.
void terminalRolar(int linhas);

// Fecha a tela E avisa a API, que solta a trava de resolucao. Sem o aviso o
// terminal do usuario so voltaria ao tamanho normal quando a concessao
// vencesse — oito segundos de janela estreita por nada.
void terminalFechar();

// Pega a tela mais recente, se houver uma nova. Nao bloqueia.
bool takeTerminal(TerminalTela &out);

int lastTerminalCode();

}
