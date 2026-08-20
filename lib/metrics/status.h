#pragma once
// <cstdint> por causa do `uint32_t` de Agent::apiMs. Ele so compilava porque o
// build real puxa o tipo por transitividade (Arduino, ArduinoJson); um
// consumidor que inclua so este cabecalho falhava. Um header tem que trazer o
// que ele proprio usa.
#include <cstdint>
#include <string>
#include <vector>

enum class Level { Green, Yellow, Red };

// Estado do agente, dito pelos hooks de ciclo de vida do Claude Code — nao mais
// adivinhado pelo tempo de silencio. Antes, "parou de publicar" era tratado
// como "bloqueado", o que confundia um agente travado numa pergunta com um
// Claude Code que simplesmente fechou.
enum class AgentState {
    Unknown,   // sessao sem hooks instalados: nao da para afirmar nada
    Working,   // Claude esta trabalhando
    Idle,      // turno acabou; a bola esta com o usuario, sem urgencia
    Blocked,   // travado numa pergunta ou permissao: PRECISA de voce
};

// Um agente vivo do Claude Code. Cada um tem o SEU proprio contexto, repo,
// branch e modelo — dois agentes podem ate rodar modelos diferentes.
struct Agent {
    std::string id;                  // 8 primeiros caracteres do session_id
    std::string repo;
    std::string branch;
    // Que ferramenta e ("claude", "codex", ...). Vazio quando a API nao manda.
    // O card de sessoes so exibe quando NAO e "claude", para nao repetir o
    // obvio em toda linha e destacar o agente que foge do padrao.
    std::string agent;
    std::string model;
    // Esforco de raciocinio ("High", "Medium", "XHigh"), ja capitalizado pela
    // API. Vazio quando a sessao publica sem o campo. Cada agente tem o seu:
    // da para ter um em High e outro em Low ao mesmo tempo.
    std::string effort;
    int         contextPct = 0;
    bool        hasContext = false;  // false quando a API manda context_pct null
    Level       level      = Level::Green;
    int         age        = 0;      // segundos desde a ultima publicacao dele
    // O heartbeat calou. Diz so isso — o motivo mora em `state`. Chamava-se
    // `idle`, e o nome mentia: da para estar ocioso publicando normalmente, e
    // trabalhando sem publicar.
    bool        stale      = false;
    // Este agente acabou de aparecer na lista? Nao vem da API: quem marca e o
    // laco principal, comparando cada poll com o anterior. Vale por alguns
    // segundos e some sozinho — e so para o olho achar quem chegou.
    bool        novo       = false;
    AgentState  state      = AgentState::Unknown;
    // Terminou e voce ainda nao viu. E o "done" do herdr: um turno encerrado que
    // ninguem abriu ainda nao e a mesma coisa que um agente parado ha horas, e a
    // bolinha diz isso em teal em vez de verde.
    bool        done       = false;

    // Contadores CUMULATIVOS da sessao, desde que ela abriu — nao do dia. Cada
    // um tem sua flag porque uma statusline anterior a esta versao nao os
    // publica, e zero seria uma afirmacao no lugar de "nao sei".
    float       costUsd      = 0;
    bool        hasCost      = false;
    int         linesAdded   = 0;
    int         linesRemoved = 0;
    bool        hasLines     = false;
    // Tempo em chamada de API. 32 bits aguentam 49 dias acumulados.
    uint32_t    apiMs        = 0;
    bool        hasApiMs     = false;

    // O pane do herdr deste agente. Vazio quando o herdr nao enxerga a sessao —
    // ela continua no painel, mas nao da para ler a tela dela nem responder por
    // ela, porque nao ha para onde apontar.
    std::string paneId;

    // De qual maquina este agente veio: 0 = master (api.url), 1 = PC2
    // (api.url2). Quem preenche e a fusao em net.cpp, nunca o parse — o JSON de
    // uma fonte nao sabe de onde veio. E o que roteia /responder, /command e
    // /terminal para a API certa, e o que escolhe a cor da tag na UI.
    int         origem = 0;
    // Como a maquina de origem se chama ("WIN", "PC2"): tres caracteres que a
    // API publica em `tag` e o parse copia para cada agente do payload dela.
    // Vazia contra uma API antiga, e ai simplesmente nao ha chip de maquina.
    std::string tag;
};

// Este caractere move o cursor por conta propria?
//
// O painel escreve texto que veio da tela de um terminal, e ele chega com
// quebras de linha de verdade. Quem desenha conta as linhas e poe o cursor onde
// quer — mas `print` trata '\n' sozinho e pula linha, furando o limite e
// escrevendo por cima do que vem abaixo. Foi assim que uma pergunta de
// brainstorm apagou o aviso de "sem opcoes numeradas".
//
// A defesa esta em TRES lugares de proposito: no parse (que limpa na entrada) e
// nas duas funcoes publicas de desenho, porque o proximo texto pode nao vir do
// parse. Sao tres trancas, e agora uma definicao so — sem isto, acrescentar um
// caractere a lista exigiria lembrar dos tres.
inline bool eControleDeCursor(char c) {
    return c == '\n' || c == '\r' || c == '\t';
}

struct Metric {
    int         pct   = 0;
    std::string resets;              // "2h35m" ou "3d06h", ja formatado pela API
    // O MESMO prazo em segundos, cru. A API sempre mandou (`session_resets_in`,
    // `week_resets_in`) e o firmware descartava, guardando so o texto — que
    // serve para ler, e nao para contar.
    //
    // E daqui que sai o RITMO da barra: quanto da janela ja passou e
    // (janela - resetsIn) / janela. Reparsear "3d01h" daria o mesmo numero com
    // resolucao de uma hora e amarrado a um formato que a API pode mudar.
    //
    // Zero quando a API nao manda o campo, e ai a barra volta a ser so o gasto:
    // um ritmo em zero seria a afirmacao de que a janela acabou de comecar.
    int         resetsIn = 0;
    // O MESMO instante, dito em relogio ("7:20pm") ou em data
    // ("01/08/2026 (Sabado)"). Vazio quando a API nao manda o campo — uma placa
    // nova contra uma API antiga simplesmente nao mostra a linha.
    // Quem converte o carimbo epoch e a API: a placa nao tem relogio nem fuso.
    std::string at;
    Level       level = Level::Green;
    bool        known = false;       // false quando limits_fresh == false
    // O percentual veio da VIRADA da janela, e nao de leitura: a API viu o
    // carimbo vencer e concluiu que o uso recomecou do zero. So a janela de 5h
    // usa isto — a de 7 dias desliza e nao "vira".
    //
    // E o unico aviso de que a janela virou que existe no instante em que ela
    // vira: o PRAZO da janela nova so aparece depois que alguem chamar a API,
    // que pode demorar horas.
    bool        inferred = false;    // `session_inferred` da API
};

// A hora dita pela API. Ela FOI a unica fonte de tempo do painel por muito
// tempo — a placa nao tem relogio de bateria e, no boot, nao sabe que dia e —,
// e o preco apareceu no dia em que o servidor calou: o relogio da tela parava,
// mostrando 14:32 as 15:10 com a mesma cara de sempre.
//
// Hoje quem manda e o SNTP da propria placa (ver src/hora.h) e isto e o plano
// B, valido enquanto ele nao sincronizou.
struct Clock {
    bool        known = false;
    std::string hm;                  // "16:14"
    std::string date;                // "30/07"
    std::string weekday;             // "QUI" — sem acento: a fonte so tem ASCII
    // O MESMO instante em epoch UTC. A API sempre mandou e o firmware
    // descartava, guardando so o texto — que serve para ler e nao para contar.
    //
    // E daqui que sai o relogio da placa quando o SNTP nao alcanca: numa rede
    // sem saida para a internet, o servidor local responde e o pool de NTP nao,
    // e sem este campo a placa ficaria sem hora — logo, sem poder envelhecer o
    // retrato do cartao, logo sem cache nenhum (ver lib/metrics/cache.h).
    long        epoch = 0;
};

struct Weather {
    bool        known = false;
    int         temp = 0;
    int         tmin = 0;
    int         tmax = 0;
    std::string text;                // "nublado", "chuva", "limpo"
    std::string city;
    // Segundos desde a leitura. A API mantem a ultima boa quando a rede dela
    // cai; sem isto, meia hora de defasagem passaria por atual.
    int         age = 0;
};

// O dia, vindo do livro-caixa da API. As metricas do agente falam da SESSAO,
// cumulativa desde que ela abriu; isto fala do DIA, somando todas as sessoes.
// Sao perguntas diferentes, e por isso moram em lugares diferentes da tela.
struct Works {
    bool known = false;       // a API nao manda o bloco: uma versao anterior
    int  trabalhos = 0;
    int  seconds = 0;         // somados
    int  blocked = 0;         // quanto do dia foi esperando por voce
    int  mediana = 0;         // o turno tipico
    float costUsd = 0;
    bool  hasCost = false;
};

// A quebra do DIA por modelo. `works` diz quanto o dia custou; isto diz de QUEM
// foi o custo. Mesma pergunta, outro eixo, e por isso viajam lado a lado.
//
// O valor e equivalente em preco de API, e NAO o que foi pago numa assinatura.
// E a mesma natureza do numero que o /cost do Claude Code mostra. Por isso o
// card se chama HOJE e a coluna e so `US$`, sem verbo: chamar de "gasto" seria
// afirmar uma coisa que ninguem mediu.
struct UsoModelo {
    std::string rotulo;       // "Opus 5", ja curto — quem encurta e o servidor
    float       costUsd = 0;
    bool        hasCost = false;   // modelo fora da tabela de preco
};

struct Uso {
    bool  known = false;      // a API nao manda o bloco: uma versao anterior
    float costUsd = 0;
    // Na ordem que a API mandou (maior custo primeiro). O firmware nao
    // reordena: a regra de ordenacao mora num lugar so, no servidor.
    std::vector<UsoModelo> modelos;
};

// Os totais de TODA a historia, vindos do livro-caixa da API. `works` fala do
// DIA e `uso` diz de quem foi o custo do dia; isto fala da vida inteira, e e o
// que alimenta o nivel do Clawd.
//
// Turnos e custo moram na API de proposito: ela ja os tem completos, e a placa
// somando snapshots diarios contaria em dobro a cada reboot e perderia todo dia
// em que estivesse desligada. As horas de convivio, que so a placa sabe, NAO
// vem daqui — moram no cartao (ver nivel.h).
struct Vitalicio {
    bool        known   = false;   // a API nao manda o bloco: versao anterior
    long        turnos  = 0;
    float       costUsd = 0;
    std::string desde;             // "AAAA-MM-DD" do registro mais antigo
};

// Uma opcao que o agente bloqueado esta oferecendo NA TELA DELE. Vira um botao
// na segunda pagina.
struct Opcao {
    int         n = 0;               // o numero que aparece na tela: "1.", "2."
    std::string rotulo;
    // Escolher esta opcao abre um campo de texto em vez de decidir. O toque nao
    // resolve o bloqueio, so troca de pergunta — e o botao diz isso.
    bool        texto = false;
};

// Quantas opcoes cabem na coluna esquerda da segunda pagina. A API pode mandar
// mais; o que passar daqui e descartado no parse, porque guardar o que nao cabe
// so gasta memoria — e um botao que nao aparece nao pode ser tocado.
const size_t BLOQUEIO_MAX_OPCOES = 4;

// O agente que parou esperando aprovacao, com a pergunta dele. A API le a tela
// do terminal e detecta as opcoes; a placa nunca ve terminal nenhum.
//
// `opcoes` vazio com `known` verdadeiro e um caso REAL e nao um erro: o agente
// esta bloqueado mas o que esta na tela dele nao e um seletor navegavel. Ali a
// pagina mostra a pergunta sem botao — melhor do que um botao que manda Enter
// numa tela que nao esperava Enter.
struct Bloqueio {
    bool        known = false;
    std::string agentId;             // casa com Agent::id
    std::string paneId;              // volta no POST da resposta
    std::string pergunta;
    std::vector<Opcao> opcoes;
    // Anda quando a PERGUNTA muda, e nao a cada leitura. E o que desarma o botao
    // na placa: sem ele, um segundo toque responderia a pergunta seguinte.
    int         seq = 0;
    // Origem do agente bloqueado (0 = master, 1 = PC2). O POST /responder tem
    // que ir para a maquina onde o pane existe.
    int         origem = 0;
    // A tag dessa maquina, para a pergunta dizer de onde vem sem ter que achar
    // o agente na lista.
    std::string tag;
};

// Alguem pediu uma foto da tela deste painel.
//
// O pedido viaja no /status por carona: a placa ja pergunta a cada 2 s, e uma
// rota so para isto significaria a placa aceitando conexao de entrada — porta
// aberta na LAN, sem autenticacao, dentro do laco de desenho.
//
// `id` e um contador da API que pediu, e a placa guarda o ultimo atendido POR
// FONTE: cada maquina tem o seu contador, e o mesmo numero em duas delas sao
// pedidos diferentes. A comparacao e por DESIGUALDADE e nao por ordem — a API
// reinicia a cada logon e o contador dela pode voltar atras.
struct Captura {
    bool known  = false;             // ha pedido nesta resposta?
    long id     = 0;
    int  origem = 0;                 // 0 = master, 1 = PC2 — para onde devolver
};

// Alguem quer por um arquivo novo no cartao desta placa.
//
// O pedido viaja no /status pela mesma carona da captura, e pelo mesmo motivo:
// a placa so faz requisicao de saida. Quem pede poe o arquivo na area de
// staging da API; a placa baixa, grava no cartao e avisa que terminou.
//
// `id` segue a regra da Captura: contador por fonte, comparado por
// desigualdade. `crc` e o CRC-32 do arquivo inteiro — 600 KB atravessando
// Wi-Fi e cartao merecem mais prova que o tamanho.
struct Atualizacao {
    bool        known  = false;
    long        id     = 0;
    int         origem = 0;
    std::string nome;                // so o nome; a placa grava em /clawd/
    long        bytes  = 0;
    uint32_t    crc    = 0;
    bool        reiniciar = true;
};

// O plano de UMA conta. Um vetor de pares e nao um mapa: sao tres ou quatro
// entradas, e um std::map custa alocacao de no por elemento numa placa onde a
// heap ja e disputada.
struct PlanoConta {
    // O nome vem do herdr, e e o MESMO de `Agent::agent` — e o que faz o plano
    // casar com o grupo. O Antigravity aparece como "agy" porque e assim que o
    // herdr o chama; o nome do produto nao existe neste campo.
    std::string agente;   // "claude", "codex", "agy"
    std::string rotulo;   // "Max 5x", "Free", "AI Pro"
};

struct Status {
    bool        valid    = false;    // false se o JSON nao pode ser parseado
    bool        online   = false;
    // Este retrato veio do CARTAO, e nao da rede (ver lib/metrics/cache.h).
    //
    // Existe porque "o servidor disse que nao ha sessao nenhuma" e "eu nao sei o
    // que ha" chegavam a tela como o mesmo estado — `online` falso com a lista
    // vazia — e sao recados opostos. A tela deitada respondia os dois com
    // "CLAUDIO OFFLINE / nenhuma sessao ativa", entao um retrato restaurado do
    // cartao aparecia como uma afirmacao sobre o presente, e escondia
    // justamente os limites que ele tinha acabado de trazer.
    bool        doCache  = false;
    int         sessions = 0;
    // Qual motor decidiu o estado das sessoes. Falso quando a API nao manda o
    // campo — uma placa nova contra uma API antiga se comporta como antes.
    bool        hooksEngine = false;
    // Alguma sessao esta compactando contexto AGORA, por ordem dada daqui. A
    // API acende quando aceita o comando e apaga quando o contexto encolhe (ou
    // quando o prazo estoura). E o que faz a turma do rodape varrer.
    bool        cleaning = false;
    std::string model;
    std::string repo;
    std::string branch;
    Metric      context;
    Metric      session;
    Metric      week;
    int         updated_ago = 0;
    // Como a maquina que respondeu se chama. Vazia numa API antiga.
    std::string tag;
    // O master falhou a volta e esta resposta INTEIRA veio do PC2 (mesma conta,
    // entao os limites continuam validos). Liga o selo VIA no cabecalho.
    bool        viaSlave = false;
    // Ha quanto tempo o master nao responde, em segundos. So significa algo
    // com viaSlave true. Preenchido pela rede, como viaSlave.
    int         masterSemContatoS = 0;
    Clock       clock;
    Weather     weather;
    Works       works;
    Uso         uso;
    Vitalicio   vitalicio;
    // O resumo dos tres blocos acima, como o servidor o publica. Enquanto ele
    // nao muda, eles nao viajam — a placa reaproveita os que ja tem (ver
    // lib/metrics/frio.h). Vazio contra uma API que nao manda o campo, e ai o
    // mecanismo inteiro fica desligado.
    std::string frio;
    Bloqueio    bloqueio;
    Captura     captura;
    Atualizacao atualizacao;
    // O plano de cada conta, como o servidor o publica. GLOBAL e nao por
    // agente: descreve a assinatura por tras das sessoes, nao a sessao.
    //
    // Na fusao de duas maquinas isto vem do MASTER, como o resto do cabecalho —
    // `fundirAgentes` so mexe em `agents` e no bloqueio. E o certo: as duas
    // maquinas usam a mesma conta.
    //
    // Vazio contra uma API antiga, e ai o painel simplesmente nao mostra plano.
    std::vector<PlanoConta> planos;
    // Na ordem que a API mandou (maior contexto primeiro). O firmware nao
    // reordena: a regra de ordenacao mora num lugar so, no servidor.
    std::vector<Agent> agents;
};

Status parseStatus(const char *json);
