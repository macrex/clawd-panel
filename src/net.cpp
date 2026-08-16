#include "net.h"
#include "fusao.h"
#include "neturl.h"
#include "display.h"
#include <Arduino.h>
#include <algorithm>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

namespace {

int g_lastCode = 0;

// DIAGNOSTICO: quantas voltas a tarefa de rede completou, e quando terminou a
// ultima. `lastHttpCode()` sozinho nao serve para saber se a rede esta viva —
// ele so e escrito DENTRO de fetchOnce, entao com a tarefa parada ele fica
// congelado no ultimo valor bom e um 200 velho passa por 200 atual. Foi
// exatamente essa leitura que fez um painel travado parecer conectado.
//
// Lidos do outro nucleo sem mutex, de proposito: sao uint32_t alinhados, cuja
// leitura e atomica no Xtensa, e valor de diagnostico atrasado em uma volta nao
// muda conclusao nenhuma. Pegar o mutex aqui poderia mascarar justamente o
// travamento que eles existem para revelar.
volatile uint32_t g_ciclos      = 0;
volatile uint32_t g_ultimoFimMs = 0;

// Estado partilhado entre a tarefa de rede e o laco principal. Tudo que cruza
// essa fronteira passa pelo mutex — inclusive o Status, que carrega
// std::string e std::vector e nao pode ser copiado durante uma escrita.
SemaphoreHandle_t g_mtx     = nullptr;
Status            g_status;
NetResult         g_result  = NetResult::NoWifi;
bool              g_novo    = false;

std::string g_url;
// A segunda fonte (o PC2). Vazia = uma fonte so, comportamento de sempre.
std::string g_url2;
uint32_t    g_pollMs = 2000;

// O PC2 caiu? Espera antes de tentar de novo: cada tentativa perdida custa ate
// 3 s de connect timeout DENTRO da volta, e pagar isso a cada poll deixaria o
// painel visivelmente mais lento por causa de uma maquina que nem esta ligada.
// O backoff so vale com o master saudavel — quando ELE cai, o PC2 e a unica
// fonte e ai se tenta sempre.
const uint32_t SLAVE_RETRY_MS = 15000;
uint32_t g_slaveProximaMs = 0;      // 0 = pode tentar ja

// Desde quando o master nao responde (millis). 0 = respondendo. E daqui que
// sai o "MASTER OFF HA Ns" do rodape quando o painel roda via PC2.
uint32_t g_masterFalhaMs = 0;

// Comando pendente, protegido pelo mesmo mutex do status.
std::string g_cmdSid;
std::string g_cmdNome;
bool        g_cmdPend = false;
int         g_cmdCode = 0;
int         g_cmdOrigem = 0;

// Resposta pendente a um prompt de aprovacao, no mesmo mutex. Separada do
// comando de proposito: as duas sao acoes do painel, mas um `compact` perdido
// custa um toque a mais e uma aprovacao perdida deixa o agente parado.
std::string g_respPane;
std::string g_respRotulo;
int         g_respN    = 0;
bool        g_respPend = false;
int         g_respCode = 0;
int         g_respOrigem = 0;

// Tela de terminal. Só existe enquanto o painel está no modo — fora dele a
// tarefa nem pede, e este bloco custa zero.
std::string   g_termPane;
int           g_termCols   = 0;
int           g_termRows   = 0;
// Rolagem ACUMULADA e ainda nao enviada, em linhas. Delta e nao posicao: quem
// guarda a posicao e o terminal do host, e inventar um numero aqui daria uma
// contagem que discorda da tela. Zera assim que sai — um pedido sem gesto nao
// pode rolar, senao a tela andaria sozinha entre dois toques.
int           g_termRolagem = 0;
bool          g_termAtivo  = false;
bool          g_termJa     = false;   // ha resultado novo para o laco colher
bool          g_termUrgente = false;  // rolou ou abriu: buscar sem esperar o poll
bool          g_termFechar = false;   // avisar a API que a tela fechou
TerminalTela  g_termTela;
int           g_termCode   = 0;
int           g_termOrigem = 0;   // a maquina do pane aberto

// ---- Foto da tela ----
// O quadro que vai para a API. Alocado uma vez em PSRAM e nunca liberado: um
// painel do qual se tirou uma foto tende a tirar outra, e devolver 300 KB para
// realocar depois e como se fragmenta a PSRAM que os sprites tambem usam.
uint8_t *g_capBuf     = nullptr;
size_t   g_capBytes   = 0;
bool     g_capPend    = false;   // ha quadro pronto esperando o POST
bool     g_capOcupado = false;   // buffer emprestado ao laco ou em envio
long     g_capId      = 0;
int      g_capOrigem  = 0;
bool     g_capRetrato = true;
int      g_capCode    = 0;
// O ultimo pedido atendido, POR FONTE (0 = master, 1 = PC2). Ver `Captura` em
// status.h para por que a comparacao e por desigualdade e nao por ordem.
long     g_capUltimo[2] = {0, 0};
bool     g_capVisto[2]  = {false, false};
uint32_t g_capTentativas = 0;    // quantas fotos ja foram pedidas a esta placa

// Codigos que nao vem do HTTP, para o pulso poder nomear a falha em vez de
// dizer so "nao foi".
const int SEM_BUFFER  = -2000;   // nao ha 300 KB contiguos na PSRAM
const int SEM_QUADRO  = -2001;   // o canvas nao devolveu o framebuffer

// ---- Atualizacao de arquivo do cartao ----
// O arquivo baixado espera aqui ate o laco gravar. Buffer temporario de
// PSRAM: alocado quando o pedido chega, liberado na confirmacao — ao contrario
// do buffer da captura, este fluxo e raro o bastante para nao valer 600 KB
// residentes.
uint8_t    *g_updBuf     = nullptr;
size_t      g_updBytes   = 0;
std::string g_updCaminho;
bool        g_updReiniciar = false;
bool        g_updPronto  = false;   // baixado e conferido; o laco pode gravar
bool        g_updOcupado = false;   // do pedido ate a confirmacao
long        g_updId      = 0;
int         g_updOrigem  = 0;
bool        g_updAvisar  = false;   // ha um POST /arquivo/ok para mandar
bool        g_updGravou  = false;   // o que o POST vai dizer
int         g_updCode    = 0;
long        g_updUltimo[2] = {0, 0};
bool        g_updVisto[2]  = {false, false};

const int UPD_SEM_PSRAM = -2100;
const int UPD_TRUNCADO  = -2101;
const int UPD_CRC       = -2102;
const int UPD_GRAVACAO  = -2103;

// CRC-32 (IEEE, o mesmo do zlib do outro lado), sem tabela: a folga de tempo
// aqui e enorme — o download leva segundos — e a tabela custaria 1 KB de RAM
// para um fluxo que roda uma vez por atualizacao.
uint32_t crc32Passo(uint32_t crc, const uint8_t *dados, size_t n) {
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc ^= dados[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (-(int32_t)(crc & 1)));
    }
    return ~crc;
}

// `urlIrma` mudou de casa: mora em lib/neturl, com teste. A conta e a mesma —
// a URL do comando sai da URL do status trocando o ULTIMO segmento, porque a
// config tem um endereco so e pedir dois seria pedir para eles sairem de
// sincronia num cartao gravado a mao.

// A base da URL de uma acao e a da MAQUINA do agente: responder na maquina
// errada nao acha o pane — ou, pior, acha um pane homonimo de outro agente.
const std::string &urlBase(int origem) {
    return (origem == 1 && !g_url2.empty()) ? g_url2 : g_url;
}

// Manda o comando. Roda DENTRO da tarefa de rede, nunca no laco.
//
// Cliente e HTTPClient proprios, separados dos do polling: compartilhar
// significaria uma requisicao entrando no meio da outra na mesma conexao
// reaproveitada.
void enviarComando(const std::string &sid, const std::string &nome, int origem) {
    if (WiFi.status() != WL_CONNECTED) { g_cmdCode = -1; return; }

    WiFiClient client;
    HTTPClient http;
    const std::string url = urlIrma(urlBase(origem), "command");
    if (!http.begin(client, url.c_str())) { g_cmdCode = -1000; return; }

    // JSON a mao: sao dois campos de conteudo conhecido (um id hexadecimal e um
    // nome da lista branca da API), entao nao ha o que escapar e nao vale
    // carregar um serializador para isso.
    String corpo = "{\"session_id\":\"";
    corpo += sid.c_str();
    corpo += "\",\"cmd\":\"";
    corpo += nome.c_str();
    corpo += "\"}";

    http.addHeader("Content-Type", "application/json");
    http.setConnectTimeout(3000);
    http.setTimeout(5000);
    g_cmdCode = http.POST(corpo);
    http.end();
    client.stop();
}

// Escapa uma string para dentro de JSON.
//
// O comando nao precisava disto — sao um id hexadecimal e um nome de lista
// branca. O ROTULO da opcao precisa: ele e texto que veio da tela de um
// terminal, e "Yes, and don't ask again for \"rm\" commands" quebraria o corpo
// da requisicao no meio. Um corpo quebrado aqui nao vira erro visivel: vira um
// 400 silencioso e uma aprovacao que nunca aconteceu.
String jsonEscapado(const std::string &s) {
    String out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    return out;
}

// Responde o prompt de aprovacao. Roda DENTRO da tarefa de rede.
//
// Manda o rotulo junto com o numero: a API confere os dois contra a tela do
// agente ANTES de apertar Enter. Se a pergunta mudou entre o toque aqui e a
// chegada la, nada e enviado — sem isso, um toque de dois segundos atras
// aprovaria a pergunta seguinte.
void enviarResposta(const std::string &pane, int n, const std::string &rotulo,
                    int origem) {
    if (WiFi.status() != WL_CONNECTED) { g_respCode = -1; return; }

    WiFiClient client;
    HTTPClient http;
    const std::string url = urlIrma(urlBase(origem), "responder");
    if (!http.begin(client, url.c_str())) { g_respCode = -1000; return; }

    String corpo = "{\"pane_id\":\"";
    corpo += jsonEscapado(pane);
    corpo += "\",\"n\":";
    corpo += n;
    corpo += ",\"rotulo\":\"";
    corpo += jsonEscapado(rotulo);
    corpo += "\"}";

    http.addHeader("Content-Type", "application/json");
    http.setConnectTimeout(3000);
    // Mais generoso que o comando: a API navega por setas e RELE a tela do
    // agente antes de confirmar, e essa releitura tem uma espera de 200 ms
    // embutida para o terminal redesenhar.
    http.setTimeout(8000);
    g_respCode = http.POST(corpo);
    http.end();
    client.stop();
}

// Manda o quadro. Roda DENTRO da tarefa de rede.
//
// O corpo e o framebuffer cru: 307.200 bytes fixos, RGB565 na ordem nativa. Sem
// compressao de proposito — um RLE levaria a tela tipica a 20-60 KB, mas custa
// um compressor, um modulo separado so para poder testa-lo no PC e um caso de
// estouro numa tela chapada. Tamanho fixo faz a validacao do outro lado ser uma
// comparacao de inteiros.
void enviarQuadro(long id, int origem, bool retrato, const uint8_t *buf,
                  size_t n) {
    if (WiFi.status() != WL_CONNECTED) { g_capCode = -1; return; }

    WiFiClient client;
    HTTPClient http;
    const std::string url = urlIrma(urlBase(origem), "tela");
    if (!http.begin(client, url.c_str())) { g_capCode = -1000; return; }

    // `rot` diz como a tela esta sendo OLHADA, e nao como o quadro esta na
    // memoria: o framebuffer e sempre 320x480 nativo, e girar so muda o espaco
    // de desenho. Quem monta o PNG usa isto para nao entregar a foto deitada.
    char cabecalho[96];
    snprintf(cabecalho, sizeof(cabecalho),
             "id=%ld; origem=%d; w=%d; h=%d; rot=%d; fmt=raw",
             id, origem, display::quadroW(), display::quadroH(),
             retrato ? 0 : 1);

    http.addHeader("Content-Type", "application/octet-stream");
    http.addHeader("X-Tela", cabecalho);
    http.setConnectTimeout(3000);
    // Generoso: 300 KB custam entre 0,5 e 1,5 s medidos nesta pilha Wi-Fi, e a
    // volta alongada pode acender "SEM CONTATO" por um ciclo. Isso e transitorio
    // e honesto; a tarefa presa nao seria.
    http.setTimeout(10000);
    g_capCode = http.POST(const_cast<uint8_t *>(buf), n);
    http.end();
    client.stop();
}

// Baixa o arquivo do pedido pendente da API. Roda DENTRO da tarefa de rede.
//
// Streaming para a PSRAM em blocos: getString() montaria uma String de 600 KB
// na RAM interna, que nao existe inteira. O CRC e somado durante a leitura —
// quando o total chega errado nao ha segunda passada a fazer.
bool baixarArquivo(const Atualizacao &a) {
    if (WiFi.status() != WL_CONNECTED) { g_updCode = -1; return false; }

    g_updBuf = (uint8_t *)ps_malloc(a.bytes);
    if (!g_updBuf) { g_updCode = UPD_SEM_PSRAM; return false; }

    WiFiClient client;
    HTTPClient http;
    const std::string url = urlIrma(urlBase(a.origem), "arquivo/baixar");
    if (!http.begin(client, url.c_str())) {
        g_updCode = -1000;
        free(g_updBuf); g_updBuf = nullptr;
        return false;
    }
    http.setConnectTimeout(3000);
    // 600 KB na velocidade tipica desta pilha Wi-Fi (300 KB em 0,5-1,5 s no
    // sentido contrario) cabem com folga em 20 s. Menos que isso apostaria
    // contra um AP ocupado.
    http.setTimeout(20000);

    const int code = http.GET();
    g_updCode = code;
    if (code != 200) { http.end(); client.stop();
                       free(g_updBuf); g_updBuf = nullptr; return false; }

    WiFiClient *stream = http.getStreamPtr();
    size_t   lidos = 0;
    uint32_t crc   = 0;
    const uint32_t inicioMs = millis();
    while (lidos < (size_t)a.bytes && (millis() - inicioMs) < 30000) {
        if (!stream->connected() && !stream->available()) break;
        const size_t n = stream->readBytes(
            g_updBuf + lidos,
            std::min((size_t)a.bytes - lidos, (size_t)4096));
        if (n == 0) continue;
        crc = crc32Passo(crc, g_updBuf + lidos, n);
        lidos += n;
    }
    http.end();
    client.stop();

    if (lidos != (size_t)a.bytes) { g_updCode = UPD_TRUNCADO; }
    else if (crc != a.crc)        { g_updCode = UPD_CRC; }
    else {
        Serial.printf("upd: baixado %s (%u bytes, crc ok)\n",
                      a.nome.c_str(), (unsigned)lidos);
        return true;
    }
    Serial.printf("upd: falha %d em %s (%u de %ld bytes)\n",
                  g_updCode, a.nome.c_str(), (unsigned)lidos, a.bytes);
    free(g_updBuf); g_updBuf = nullptr;
    return false;
}

// Avisa a API que o pedido terminou. Roda DENTRO da tarefa de rede.
void enviarAtualizacaoOk(long id, int origem, bool gravou) {
    if (WiFi.status() != WL_CONNECTED) { g_updCode = -1; return; }

    WiFiClient client;
    HTTPClient http;
    const std::string url = urlIrma(urlBase(origem), "arquivo/ok");
    if (!http.begin(client, url.c_str())) { g_updCode = -1000; return; }

    String corpo = "{\"id\":";
    corpo += (long)id;
    corpo += ",\"ok\":";
    corpo += gravou ? "true" : "false";
    corpo += "}";

    http.addHeader("Content-Type", "application/json");
    http.setConnectTimeout(3000);
    http.setTimeout(5000);
    g_updCode = http.POST(corpo);
    http.end();
    client.stop();
}

// Busca a tela do terminal. Roda DENTRO da tarefa de rede.
//
// Cliente proprio, como o do comando: a conexao persistente do /status e usada
// por outra requisicao, e duas requisicoes na mesma conexao reaproveitada se
// atropelam.
//
// A resposta tem ~3 KB (34 linhas de 78 colunas) contra os 1,4 KB do /status.
// Ela so acontece no modo terminal, entao o custo nao existe no uso normal.
bool buscarTerminal(const std::string &pane, int cols, int rows, int rolagem,
                    TerminalTela &out, int origem) {
    if (WiFi.status() != WL_CONNECTED) { g_termCode = -1; return false; }

    WiFiClient client;
    HTTPClient http;
    String url = urlIrma(urlBase(origem), "terminal").c_str();
    url += "?pane_id=";
    url += pane.c_str();             // "w0:p1": so hex, letras e dois-pontos
    url += "&cols="; url += cols;
    url += "&rows="; url += rows;
    url += "&rolagem="; url += rolagem;

    if (!http.begin(client, url)) { g_termCode = -1000; return false; }
    http.setConnectTimeout(3000);
    http.setTimeout(6000);
    g_termCode = http.GET();
    if (g_termCode != 200) { http.end(); client.stop(); return false; }

    const String corpo = http.getString();
    http.end();
    client.stop();

    // Documento generoso: 34 linhas de ate 78 caracteres mais as chaves. O
    // parser falha limpo se nao couber, e o painel mantem a tela anterior.
    JsonDocument doc;
    if (deserializeJson(doc, corpo)) return false;

    out.linhas.clear();
    for (JsonVariantConst l : doc["linhas"].as<JsonArrayConst>())
        out.linhas.push_back(l.as<const char *>() ? l.as<const char *>() : "");
    // A trava de resolucao subiu? Sem ela o texto vem na largura do host e a
    // tela fica ilegivel — e o painel precisa poder DIZER isso, em vez de
    // mostrar texto embaralhado e parecer defeito dele.
    out.travado = doc["travado"] | false;
    out.ok      = true;
    return true;
}

// Faz UMA requisicao. Roda apenas dentro da tarefa; o cliente e o HTTPClient
// sao dela e de mais ninguem.
// Reassocia quando o radio desiste.
//
// `setAutoReconnect` cobre a queda comum, mas nao todas: com o AP reiniciado,
// roaming entre canais ou concessao de DHCP vencida, o driver para de tentar e
// a placa fica desassociada PARA SEMPRE — sem crash, sem heap vazando, so um
// painel com `wifi=0` mostrando dado velho. Foi assim por 1000 s antes de
// alguem reparar.
//
// A tentativa e espacada: `WiFi.begin` durante uma varredura em andamento
// atrapalha em vez de ajudar, e o intervalo tambem evita martelar o AP quando
// ele e que esta fora.
void reassociarSePreciso() {
    static uint32_t ultimaMs = 0;
    const uint32_t RETRY_MS = 15000;

    if (WiFi.status() == WL_CONNECTED) { ultimaMs = 0; return; }

    const uint32_t agora = millis();
    if (ultimaMs && (agora - ultimaMs) < RETRY_MS) return;
    ultimaMs = agora ? agora : 1;

    Serial.println("wifi: desassociado, reconectando");
    WiFi.disconnect();
    WiFi.begin();          // reusa SSID e senha guardados por begin()
}

// Uma conexao persistente POR FONTE: o keep-alive e por host, e reusar o mesmo
// HTTPClient contra dois enderecos derrubaria e reabriria o socket a cada
// volta — justamente o custo que o setReuse existe para evitar.
struct Conexao {
    WiFiClient client;
    HTTPClient http;
    bool       init = false;
};
Conexao g_conMaster;
Conexao g_conSlave;

// Faz UMA requisicao numa fonte. Roda apenas dentro da tarefa. O check de
// radio mora em fetchCiclo: quando o WiFi caiu, nenhuma fonte merece tentativa.
NetResult fetchFonte(Conexao &con, const std::string &url, Status &out,
                     int &codeOut) {
    // Conexao PERSISTENTE. Sem isto cada poll abria um TCP novo, com aperto de
    // mao completo a cada dois segundos. A API responde HTTP/1.1 com
    // Content-Length e sem `Connection: close`, entao da para manter de pe.
    if (!con.init) {
        con.http.setReuse(true);
        con.http.setConnectTimeout(3000);
        con.http.setTimeout(3000);
        con.init = true;
    }

    if (!con.http.begin(con.client, url.c_str())) {
        codeOut = -1000;
        return NetResult::HttpError;
    }

    const int code = con.http.GET();
    codeOut = code;
    if (code != 200) {
        // Erro pode ter deixado a conexao num estado ruim: derruba para o
        // proximo poll comecar limpo, em vez de insistir num socket zumbi.
        con.http.end();
        con.client.stop();
        return NetResult::HttpError;
    }

    const String body = con.http.getString();
    con.http.end();             // com setReuse(true), NAO fecha o socket

    Status s = parseStatus(body.c_str());
    if (!s.valid) return NetResult::BadPayload;
    out = s;
    return NetResult::Ok;
}

// A volta inteira: master, PC2 se configurado, fusao, fallback.
NetResult fetchCiclo(Status &out) {
    if (WiFi.status() != WL_CONNECTED) {
        reassociarSePreciso();
        return NetResult::NoWifi;
    }

    Status s1;
    int code1 = 0;
    const NetResult r1 = fetchFonte(g_conMaster, g_url, s1, code1);
    g_lastCode = code1;             // o diagnostico de sempre fala do master

    const uint32_t agora = millis();
    if (r1 == NetResult::Ok)   g_masterFalhaMs = 0;
    else if (!g_masterFalhaMs) g_masterFalhaMs = agora ? agora : 1;

    // Sem segunda fonte, o ciclo e o de sempre.
    if (g_url2.empty()) {
        if (r1 == NetResult::Ok) out = s1;
        return r1;
    }

    // O PC2 entra: sempre que o master falhou (e a unica esperanca da volta),
    // ou quando o backoff de falha anterior ja venceu.
    const bool backoffVenceu = !g_slaveProximaMs ||
                               (int32_t)(agora - g_slaveProximaMs) >= 0;
    Status s2;
    NetResult r2 = NetResult::HttpError;
    if (r1 != NetResult::Ok || backoffVenceu) {
        int code2 = 0;
        r2 = fetchFonte(g_conSlave, g_url2, s2, code2);
        g_slaveProximaMs = (r2 == NetResult::Ok) ? 0 : agora + SLAVE_RETRY_MS;
    }

    if (r1 == NetResult::Ok) {
        out = s1;
        if (r2 == NetResult::Ok) fundirAgentes(out, s2);
        return NetResult::Ok;
    }
    if (r2 == NetResult::Ok) {
        // Fallback: o /status inteiro e o do PC2. Mesma conta, entao os
        // limites continuam verdadeiros; o selo VIA e o rodape contam o resto.
        out = s2;
        marcarOrigem(out, 1);
        out.viaSlave = true;
        out.masterSemContatoS =
            g_masterFalhaMs ? (int)((agora - g_masterFalhaMs) / 1000) : 0;
        return NetResult::Ok;
    }
    return r1;      // as duas mudas: o erro do master e o que conta a historia
}

void tarefaRede(void *) {
    for (;;) {
        // O comando vem ANTES do poll da volta: assim o /status seguinte ja
        // tende a trazer o `cleaning` aceso, e a turma comeca a varrer no
        // primeiro ciclo em vez de no segundo.
        std::string cmdSid, cmdNome, respPane, respRotulo;
        int respN = 0, cmdOrigem = 0, respOrigem = 0;
        if (xSemaphoreTake(g_mtx, portMAX_DELAY) == pdTRUE) {
            if (g_cmdPend) {
                cmdSid = g_cmdSid;
                cmdNome = g_cmdNome;
                cmdOrigem = g_cmdOrigem;
                g_cmdPend = false;
            }
            if (g_respPend) {
                respPane   = g_respPane;
                respRotulo = g_respRotulo;
                respN      = g_respN;
                respOrigem = g_respOrigem;
                g_respPend = false;
            }
            xSemaphoreGive(g_mtx);
        }
        // A foto sai antes do poll, junto do comando e da resposta: ela e a
        // acao mais lenta da volta, e mandar depois do /status atrasaria o dado
        // fresco que a tela mostra.
        long capId = 0;
        int  capOrigem = 0;
        bool capMandar = false;
        if (xSemaphoreTake(g_mtx, portMAX_DELAY) == pdTRUE) {
            if (g_capPend) {
                capId = g_capId;
                capOrigem = g_capOrigem;
                capMandar = true;
                g_capPend = false;
            }
            xSemaphoreGive(g_mtx);
        }
        if (capMandar) {
            enviarQuadro(capId, capOrigem, g_capRetrato, g_capBuf, g_capBytes);
            // Solta o buffer so DEPOIS do envio: enquanto o POST acontece, ele
            // e a fonte do corpo da requisicao. Emprestar ao laco no meio disso
            // mandaria meia tela antiga e meia nova.
            if (xSemaphoreTake(g_mtx, portMAX_DELAY) == pdTRUE) {
                g_capOcupado = false;
                xSemaphoreGive(g_mtx);
            }
        }

        if (!cmdNome.empty()) enviarComando(cmdSid, cmdNome, cmdOrigem);
        // A resposta vem antes do poll pela mesma razao do comando: o /status
        // seguinte ja tende a chegar sem o bloqueio, e a tela volta ao contexto
        // no primeiro ciclo em vez de no segundo.
        if (respN) enviarResposta(respPane, respN, respRotulo, respOrigem);

        Status s;
        const NetResult r = fetchCiclo(s);

        // Marcados ANTES do mutex: se a contencao pelo mutex for o problema,
        // estes dois precisam ja refletir que a volta terminou.
        g_ciclos++;
        g_ultimoFimMs = millis();

        if (xSemaphoreTake(g_mtx, portMAX_DELAY) == pdTRUE) {
            g_result = r;
            if (r == NetResult::Ok) g_status = s;
            g_novo = true;
            xSemaphoreGive(g_mtx);
        }

        // Pedido de arquivo novo? O download roda AQUI, na tarefa: e uma
        // requisicao como as outras, e o laco so entra quando ha algo pronto
        // para o cartao. O id por fonte segue a regra da captura.
        if (r == NetResult::Ok && s.atualizacao.known) {
            const Atualizacao &a = s.atualizacao;
            const int fonte = (a.origem == 1) ? 1 : 0;
            bool novoPedido = false;
            if (xSemaphoreTake(g_mtx, portMAX_DELAY) == pdTRUE) {
                if (!g_updOcupado && a.bytes > 0 && a.bytes <= 2 * 1024 * 1024 &&
                    (!g_updVisto[fonte] || g_updUltimo[fonte] != a.id)) {
                    g_updVisto[fonte]  = true;
                    g_updUltimo[fonte] = a.id;   // atendido AQUI: falha nao repete
                    g_updOcupado = true;
                    novoPedido = true;
                }
                xSemaphoreGive(g_mtx);
            }
            if (novoPedido) {
                const bool ok = baixarArquivo(a);
                if (xSemaphoreTake(g_mtx, portMAX_DELAY) == pdTRUE) {
                    if (ok) {
                        g_updCaminho   = "/clawd/" + a.nome;
                        g_updBytes     = (size_t)a.bytes;
                        g_updReiniciar = a.reiniciar;
                        g_updId        = a.id;
                        g_updOrigem    = fonte;
                        g_updPronto    = true;
                    } else {
                        // O aviso de falha sai mesmo assim: e ele que faz o
                        // disparador parar de esperar com a resposta certa.
                        g_updId     = a.id;
                        g_updOrigem = fonte;
                        g_updGravou = false;
                        g_updAvisar = true;
                    }
                    xSemaphoreGive(g_mtx);
                }
            }
        }

        // O laco confirmou (ou o download falhou): avisa a API e, se a
        // gravacao valeu e o pedido mandou reiniciar, reinicia — o aviso vai
        // ANTES para o pedido nao renascer no proximo boot.
        bool avisar = false, gravou = false, reiniciar = false;
        long updId = 0; int updOrigem = 0;
        if (xSemaphoreTake(g_mtx, portMAX_DELAY) == pdTRUE) {
            if (g_updAvisar) {
                avisar    = true;
                gravou    = g_updGravou;
                reiniciar = gravou && g_updReiniciar;
                updId     = g_updId;
                updOrigem = g_updOrigem;
                g_updAvisar  = false;
                g_updOcupado = false;
            }
            xSemaphoreGive(g_mtx);
        }
        if (avisar) {
            enviarAtualizacaoOk(updId, updOrigem, gravou);
            if (reiniciar) {
                Serial.println("upd: gravado, reiniciando");
                Serial.flush();
                vTaskDelay(pdMS_TO_TICKS(300));
                ESP.restart();
            }
        }

        // Tela de terminal, quando o painel esta nela.
        std::string tPane, tFechar;
        int tCols = 0, tRows = 0, tRolagem = 0, tOrigem = 0;
        if (xSemaphoreTake(g_mtx, portMAX_DELAY) == pdTRUE) {
            if (g_termAtivo) {
                tPane = g_termPane; tCols = g_termCols;
                tRows = g_termRows;
                tOrigem = g_termOrigem;
                // Consome a rolagem: ela vale uma vez. Repetida a cada poll, a
                // tela andaria sozinha entre dois toques.
                tRolagem = g_termRolagem;
                g_termRolagem = 0;
            } else if (g_termFechar) {
                // Sai da tela: um pedido com pane vazio, que e como a API sabe
                // que pode devolver o terminal ao tamanho original. Sem ele a
                // trava so cairia por inatividade, e ate la o terminal do
                // usuario ficaria estreito por nada.
                tFechar = "x";
                g_termFechar = false;
            }
            g_termUrgente = false;
            xSemaphoreGive(g_mtx);
        }
        if (!tPane.empty()) {
            TerminalTela tela;
            const bool ok = buscarTerminal(tPane, tCols, tRows, tRolagem, tela,
                                           tOrigem);
            if (ok && xSemaphoreTake(g_mtx, portMAX_DELAY) == pdTRUE) {
                g_termTela = tela;
                g_termJa   = true;
                xSemaphoreGive(g_mtx);
            }
        } else if (!tFechar.empty()) {
            TerminalTela lixo;
            buscarTerminal("", tCols, tRows, 0, lixo, tOrigem);
        }

        // Ritmo do poll medido do FIM de uma requisicao ao INICIO da proxima.
        // Uma requisicao lenta nao encurta a pausa seguinte, entao a tarefa
        // nunca vira um laco apertado quando a rede degrada.
        //
        // Dorme em FATIAS para poder acordar cedo quando o dedo rola a tela.
        // Esperar o poll inteiro faria cada arrasto levar ate dois segundos
        // para aparecer, o que na pratica e um controle que nao responde. As
        // fatias so existem no modo terminal: fora dele a condicao e falsa em
        // todas elas e o efeito e o mesmo `vTaskDelay` de antes.
        const uint32_t fatia = 50;
        for (uint32_t esperou = 0; esperou < g_pollMs; esperou += fatia) {
            vTaskDelay(pdMS_TO_TICKS(fatia));
            bool acordar = false;
            if (xSemaphoreTake(g_mtx, 0) == pdTRUE) {
                acordar = g_termUrgente;
                xSemaphoreGive(g_mtx);
            }
            if (acordar) break;
        }
    }
}

}  // namespace

namespace net {

void begin(const AppConfig &c) {
    // O `?campos=painel` entra AQUI, uma vez, e nao no cartao: ele nao e
    // configuracao, e o que ESTE firmware sabe ler. Um cartao com a query
    // gravada a mao teria que ser reescrito a cada campo que o painel
    // aprendesse — e a placa gravada com uma versao anterior continuaria
    // pedindo um corte que ela nao entende.
    //
    // Todas as rotas irmas passam por `urlIrma`, que descarta a query junto com
    // o caminho (ver lib/neturl, com teste): o POST /responder nao herda o
    // parametro.
    g_url    = urlDoStatus(c.url);
    g_url2   = urlDoStatus(c.url2);
    g_pollMs = c.pollMs;

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    // Desliga o modem sleep. Ele e o padrao e economiza energia deixando o
    // radio dormir entre beacons — mas cada transacao paga o despertar. A placa
    // vive na USB, entao a economia nao compra nada.
    WiFi.setSleep(false);

    WiFi.begin(c.ssid.c_str(), c.password.c_str());

    g_mtx = xSemaphoreCreateMutex();

    // O buffer da foto de tela, reservado AGORA e nao na primeira captura.
    // Sao 300 KB contiguos: aqui a PSRAM esta inteira, e mais tarde ela ja
    // estara picada pelos sprites que cada pagina carrega. Falhar aqui nao
    // impede nada — so a foto deixa de existir, e o pulso passa a dizer isso.
    g_capBytes = display::quadroBytes();
    g_capBuf   = (uint8_t *)ps_malloc(g_capBytes);
    if (!g_capBuf) g_capCode = SEM_BUFFER;

    // Nucleo 0: o Arduino roda o loop() no nucleo 1, entao a rede sai da frente
    // do desenho e da leitura do toque de verdade, e nao so por escalonamento.
    // Pilha de 8 KB porque HTTPClient e o parser de JSON alocam bastante.
    xTaskCreatePinnedToCore(tarefaRede, "rede", 8192, nullptr, 1, nullptr, 0);
}

bool connected()    { return WiFi.status() == WL_CONNECTED; }
int  lastHttpCode() { return g_lastCode; }
uint32_t cycles()   { return g_ciclos; }
uint32_t sinceLastFetchMs() {
    const uint32_t fim = g_ultimoFimMs;
    return fim ? (millis() - fim) : 0;
}
int  rssi()         { return WiFi.RSSI(); }

void sendCommand(const std::string &sessionId, const char *cmd, int origem) {
    if (!g_mtx || sessionId.empty() || !cmd || !cmd[0]) return;
    if (xSemaphoreTake(g_mtx, 0) != pdTRUE) return;   // ocupada: o toque se perde
    // Ja ha um esperando: o segundo toque nao vira um segundo comando.
    if (!g_cmdPend) {
        g_cmdSid    = sessionId;
        g_cmdNome   = cmd;
        g_cmdOrigem = origem;
        g_cmdPend   = true;
    }
    xSemaphoreGive(g_mtx);
}

int lastCommandCode() { return g_cmdCode; }

void responder(const std::string &paneId, int n, const std::string &rotulo,
               int origem) {
    if (!g_mtx || paneId.empty() || n <= 0) return;
    if (xSemaphoreTake(g_mtx, 0) != pdTRUE) return;
    // Uma so pendente, como no comando. Aqui o motivo e mais forte: duas
    // respostas na fila responderiam a segunda pergunta com a escolha da
    // primeira, porque a tela ja teria mudado no meio.
    if (!g_respPend) {
        g_respPane   = paneId;
        g_respN      = n;
        g_respRotulo = rotulo;
        g_respOrigem = origem;
        g_respPend   = true;
    }
    xSemaphoreGive(g_mtx);
}

int lastResponderCode() { return g_respCode; }

bool capturaPedida(long &id, int &origem) {
    if (!g_mtx) return false;
    if (xSemaphoreTake(g_mtx, 0) != pdTRUE) return false;

    bool novo = false;
    const Captura &c = g_status.captura;
    if (c.known && !g_capOcupado) {
        const int fonte = (c.origem == 1) ? 1 : 0;
        // Primeira vez que se ve esta fonte tambem conta como pedido novo: a
        // API so publica o campo quando alguem pediu, entao nao ha caso em que
        // ignorar o primeiro seja o certo.
        if (!g_capVisto[fonte] || g_capUltimo[fonte] != c.id) {
            g_capVisto[fonte]  = true;
            g_capUltimo[fonte] = c.id;
            // Marcado como atendido AQUI, antes de o quadro sair. Um envio que
            // falhe nao volta a ser tentado: a API deixa o pedido expirar, e
            // uma foto atrasada descreveria uma tela que ja mudou.
            id = c.id;
            origem = fonte;
            g_capOcupado = true;
            g_capTentativas++;
            novo = true;
        }
    }
    xSemaphoreGive(g_mtx);
    return novo;
}

uint8_t *capturaBuffer(size_t &bytes) {
    bytes = display::quadroBytes();
    // O buffer nasce no `begin`, e nao aqui. Ele precisa de 300 KB CONTIGUOS, e
    // a PSRAM desta placa vai sendo picada pelos sprites do Clawd conforme as
    // paginas sao visitadas: pedir tarde encontra memoria livre de sobra e
    // nenhum bloco inteiro. Medido: falhava calado, e a foto simplesmente nao
    // saia — o painel nunca soube dizer por que.
    if (!g_capBuf) {
        g_capCode = SEM_BUFFER;
        if (g_mtx && xSemaphoreTake(g_mtx, portMAX_DELAY) == pdTRUE) {
            g_capOcupado = false;
            xSemaphoreGive(g_mtx);
        }
        return nullptr;
    }
    return g_capBuf;
}

void enviarCaptura(long id, int origem, bool retrato) {
    if (!g_mtx || !g_capBuf) return;
    if (xSemaphoreTake(g_mtx, portMAX_DELAY) != pdTRUE) return;
    g_capId      = id;
    g_capOrigem  = origem;
    g_capRetrato = retrato;
    g_capPend    = true;
    xSemaphoreGive(g_mtx);
}

int lastCapturaCode() { return g_capCode; }

uint32_t capturasPedidas() { return g_capTentativas; }

void falhaCaptura() {
    g_capCode = SEM_QUADRO;
    if (!g_mtx) return;
    // Solta o buffer: sem isto, uma falha de copia deixaria a placa surda para
    // todo pedido seguinte, e o painel diria "pendente" para sempre.
    if (xSemaphoreTake(g_mtx, portMAX_DELAY) == pdTRUE) {
        g_capOcupado = false;
        xSemaphoreGive(g_mtx);
    }
}

bool atualizacaoPronta(std::string &caminho, const uint8_t *&buf, size_t &bytes,
                       bool &reiniciar) {
    if (!g_mtx) return false;
    if (xSemaphoreTake(g_mtx, 0) != pdTRUE) return false;
    const bool tinha = g_updPronto;
    if (tinha) {
        caminho   = g_updCaminho;
        buf       = g_updBuf;
        bytes     = g_updBytes;
        reiniciar = g_updReiniciar;
        g_updPronto = false;
    }
    xSemaphoreGive(g_mtx);
    return tinha;
}

void confirmarAtualizacao(bool gravou) {
    if (!g_mtx) return;
    if (xSemaphoreTake(g_mtx, portMAX_DELAY) != pdTRUE) return;
    if (g_updBuf) { free(g_updBuf); g_updBuf = nullptr; }
    if (!gravou) g_updCode = UPD_GRAVACAO;
    g_updGravou = gravou;
    g_updAvisar = true;          // g_updOcupado cai quando o aviso sair
    xSemaphoreGive(g_mtx);
}

int lastAtualizacaoCode() { return g_updCode; }

void terminalAbrir(const std::string &paneId, int cols, int rows, int origem) {
    if (!g_mtx || paneId.empty()) return;
    if (xSemaphoreTake(g_mtx, portMAX_DELAY) != pdTRUE) return;
    g_termPane   = paneId;
    g_termCols   = cols;
    g_termRows   = rows;
    g_termOrigem = origem;
    g_termRolagem = 0;           // abre onde o agente esta, sem rolar nada
    g_termAtivo  = true;
    g_termUrgente = true;        // nao espera o proximo poll para a primeira tela
    g_termFechar = false;
    g_termJa     = false;
    g_termTela   = TerminalTela();
    xSemaphoreGive(g_mtx);
}

void terminalRolar(int linhas) {
    if (!g_mtx || !linhas) return;
    if (xSemaphoreTake(g_mtx, portMAX_DELAY) != pdTRUE) return;
    if (g_termAtivo) {
        // ACUMULA em vez de substituir: dois arrastos rapidos dentro de uma
        // mesma volta da tarefa sao dois gestos, e engolir um deles faria o
        // segundo parecer que nao foi lido.
        g_termRolagem += linhas;
        g_termUrgente  = true;
    }
    xSemaphoreGive(g_mtx);
}

void terminalFechar() {
    if (!g_mtx) return;
    if (xSemaphoreTake(g_mtx, portMAX_DELAY) != pdTRUE) return;
    if (g_termAtivo) g_termFechar = true;   // avisa a API para soltar a trava
    g_termAtivo  = false;
    g_termUrgente = true;                   // solta AGORA, nao no proximo poll
    g_termPane.clear();
    // A tela fica guardada de proposito: reabrir o mesmo pane mostra o ultimo
    // conteudo enquanto a primeira requisicao nao volta, em vez de piscar preto.
    xSemaphoreGive(g_mtx);
}

bool takeTerminal(TerminalTela &out) {
    if (!g_mtx) return false;
    if (xSemaphoreTake(g_mtx, 0) != pdTRUE) return false;
    const bool tinha = g_termJa;
    if (tinha) { out = g_termTela; g_termJa = false; }
    xSemaphoreGive(g_mtx);
    return tinha;
}

int lastTerminalCode() { return g_termCode; }

bool takeResult(NetResult &result, Status &status) {
    if (!g_mtx) return false;
    // Nao bloqueia: se a tarefa estiver escrevendo agora, o laco pega na
    // proxima volta. Perder 20 ms de frescor nao vale travar o desenho.
    if (xSemaphoreTake(g_mtx, 0) != pdTRUE) return false;

    const bool tinha = g_novo;
    if (tinha) {
        result = g_result;
        if (g_result == NetResult::Ok) status = g_status;
        g_novo = false;
    }
    xSemaphoreGive(g_mtx);
    return tinha;
}

}
