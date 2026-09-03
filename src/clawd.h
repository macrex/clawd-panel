#pragma once
#include "status.h"
#include "display.h"
#include <stdint.h>

// O Clawd: o caranguejo animado do projeto clawd-tank (MIT), lido do cartao SD.
//
// Desenha no canvas, como todo o resto da interface. A primeira versao escrevia
// direto no painel para evitar o flush de tela cheia (63-70 ms medidos), o que
// custaria 10-20 ms por quadro em vez disso. Nao funciona nesta placa: com
// blocos de teste nos quatro cantos, os quatro cairam no MESMO ponto — o painel
// ignora janela de endereco parcial. Isso ficou invisivel por semanas porque o
// flush sempre escreve o quadro inteiro a partir da origem, que da o mesmo
// resultado com ou sem janela.
namespace clawd {

// Carrega os .clw do cartao para a PSRAM. Segue em frente se faltarem: o
// painel continua util sem o caranguejo, e a pagina dele avisa o que falta.
void begin();
bool ready();
const char *erro();          // vazio quando carregou tudo

// Escolhe quais animacoes rodar. Reiniciar do quadro zero so acontece quando a
// animacao muda de verdade — trocar de pagina nao pode fazer o Clawd gaguejar.
//
// Escolhe DUAS, e nao uma. O bicho do rodape sempre mostra o estado. O da
// pagina 3 acompanha o estado enquanto nada acontece, mas no instante em que o
// turno comeca ele SORTEIA entre typing, building, conducting e sweeping — e
// fica com essa cara ate o turno acabar. Antes era um indice so, e sortear na
// pagina 3 trocaria o bicho do rodape junto.
void select(const Status &s, bool temDados);

// Enfileira UMA comemoracao de turno concluido — a turma inteira troca para a
// cara de vitoria por 4 segundos e volta ao estado sozinha.
//
// Uma chamada por sessao que terminou: duas sessoes rendem duas comemoracoes
// EM SEQUENCIA, e nao uma. O aviso conta quantos turnos acabaram, e nao apenas
// que algo acabou.
//
// Nao desenha nada e nao bloqueia: quem toca a fila e `tick`.
void comemorar();

// ---- O elenco da fileira ----
// Quem sao os quatro bichos do topo. O tema padrao guarda o primeiro slot para
// o bicho do ESTADO; um tema de elenco fechado preenche os quatro com
// personagens que nao mudam de cara.
//
// A troca releda o cartao — leva os mesmos ~300 ms do boot e nao pode acontecer
// dentro de um desenho. Devolve false se a placa ainda nao carregou.
bool trocarTema();

// Nome do que esta no ar, para o log. Nunca nulo.
const char *temaEmCena();

// Sorteia OUTRA cara de trabalho para a pagina 3, agora. Devolve false quando
// nao ha nada a trocar: sem turno em andamento (fora do trabalho o bicho grande
// mostra o ESTADO, e trocar seria mentir sobre ele) ou com uma cara so no
// cartao.
//
// A escolha manual dura o resto do turno: `select` so sorteia na VIRADA para o
// trabalho, entao ela nao e desfeita pela proxima leitura da API.
bool trocarTrabalho();

// Ja passou o tempo do quadro atual? Quem chama usa isto para decidir se
// precisa redesenhar a pagina. Avanca o quadro quando devolve true.
bool tick(uint32_t nowMs);

// Desenha o quadro atual no canvas. Chamado de dentro do desenho da pagina.
void drawInto(Arduino_Canvas *g);

// A mesma cena com centro, chao e teto de quem chama — e o que a pagina 2 em
// pe usa: o bicho pisa mais baixo e o teto e a linha do cabecalho do retrato.
void drawIntoEm(Arduino_Canvas *g, int centroX, int chao, int teto);

// Retangulo que o Clawd ocupa, em coordenadas de DESENHO (paisagem).
void area(int &x, int &y, int &w, int &h);

// ---- A turma do rodape ----
// Tres bichos lado a lado. So o da ESQUERDA fala de estado sozinho; os outros
// dois acompanham, porque e o mesmo turno.
//
//   descanso   sleeping | idle     | marks_rest
//   trabalho   typing   | building | marks_hammering
//   ALERTA     alert    | alert    | alert
//   LIMPEZA    sweeping | sweeping | sweeping
//
// Nas duas ultimas os TRES mostram a mesma cara: bloqueio e limpeza de contexto
// sao eventos da maquina inteira, e nao papeis diferentes de cada bicho.
//
// Sem sessao nenhuma o da esquerda vai embora SOZINHO: o going_away e um quadro
// largo e baixo (170x61 nativos), e a turma inteira nao teria por que ficar.
//
// O rodape ja foi um selo unico e PARADO: cada quadro custava um redesenho de
// tela inteira (~64 ms). O flush de prefixo derrubou o custo para ~21 ms com os
// tres (ver display::flushPrefix), e tick() ainda limita a cadencia de envio.
// `faixaW` maior que zero espalha a turma por uma faixa dessa largura, em
// fatias IGUAIS, com cada bicho centrado na sua. Zero mantem a fileira compacta
// de sempre — larguras de slot com um vao fixo entre elas.
//
// A tela em PE passa a largura do card (292): la a faixa da turma esta dentro do
// prefixo de LINHAS, entao alargar nao custa quadro nenhum. A tela deitada
// continua em zero, porque la o prefixo e de COLUNAS e cada pixel a mais na
// fileira e enviado 14 vezes por segundo.
int  crewW(int faixaW = 0);                      // 0 se nao ha o que desenhar
int  crewH();                                    // caixa a limpar antes de blitar
bool drawCrewInto(Arduino_Canvas *g, int x, int chao, int faixaW = 0);

// A MESMA fileira com o bicho do cabecalho no MEIO — e a turma da tela nova em
// pe, onde o canto superior esquerdo passou a ser do nome e o bicho desceu para
// o centro do elenco. Durante a comemoracao o centro troca para o Clawd PULANDO
// (happy.clw) e volta sozinho: e o unico slot que muda de cara na festa, porque
// os quatro do elenco fechado ja tem a propria pose de vitoria.
//
// `crewComCentroH` e a caixa a limpar — o centro pode ser mais alto que o trio.
int  crewComCentroH();
bool drawCrewComCentroInto(Arduino_Canvas *g, int x, int chao, int faixaW);

// ---- Icone do cabecalho ----
// O wizard, recortado no personagem e reduzido para a altura do cabecalho. NAO
// depende do estado: e enfeite, do mesmo jeito que o logo da marca.
//
// Ele anima, e o lugar dele na tela e o que torna isso possivel. A faixa do
// flush de prefixo e uma tira de ALTURA INTEIRA (x=0..92), entao ela cobre o
// canto superior esquerdo e o rodape no MESMO envio. Por isso o mago vive a
// esquerda e o logo da marca foi para a direita: la, ao lado do relogio, o
// prefixo precisaria de 320 colunas e o enfeite custaria ~26% de CPU.
//
// Tem o proprio contador de quadros, no ritmo do proprio arquivo — nao esta
// amarrado ao selo do rodape.
//
// Quem esta em cena e o bicho do RODIZIO, sorteado a cada dez minutos; o mago e
// a reserva de quando nenhum carregou. Ja foi o Clawd do nivel atual, e nao e
// mais: aquele sprite saiu com os 99 arquivos que nao cabem na particao de
// flash.
//
// `naPaginaDoNivel` sobrevive na assinatura mas nao decide mais nada — era ele
// que trocava o bicho do nivel pelo mago na quarta pagina, para o mesmo desenho
// nao aparecer duas vezes na mesma tela.
//
// Devolve 0 quando nem o rodizio nem o mago carregaram, e ai o cabecalho cai no
// logo da marca.
int  iconW(bool naPaginaDoNivel);                // 0 se nao ha o que desenhar
int  iconH(bool naPaginaDoNivel);
bool drawIconInto(Arduino_Canvas *g, int x, int y, bool naPaginaDoNivel);

// ---- O Token da tela de limite estourado ----
// Quatro poses de 900 ms; os olhos giram, o corpo fica parado. tokenW() == 0
// significa que o cartao nao tem o arquivo — e ai a tela de limite nao toma o
// lugar de nada.
//
// `num`/`den` e a ESCALA do desenho, e existe por causa da tela deitada: o
// arquivo mede 224x336 e cabe em pe em tamanho nativo, mas deitado a caixa e de
// 240x250 e ele entra em 167x250. A escala e aplicada no blit (ver
// desenharIcone), entao nao ha PSRAM a mais nem uma segunda copia do sprite
// para manter em sincronia. Meça sempre com a MESMA razao do desenho.
int  tokenW(int num = 1, int den = 1);
int  tokenH(int num = 1, int den = 1);
bool tickToken(uint32_t nowMs);
bool drawTokenInto(Arduino_Canvas *g, int x, int y, int num, int den);

// ---- O mesmo bicho, com o servidor fora (retrato) ----
// O irmao do Token: mesmo desenho, mesmo enquadramento, mesmo gesto para
// dispensar — e outro recado na camisa. Ele nao diz "acabaram os tokens", diz
// que o outro lado calou, e a tela que ele ocupa mostra o ultimo retrato bom em
// vez de dado vivo.
//
// Ao contrario do Token, ele NAO fica residente: sao ~150 KB de PSRAM para uma
// tela que so aparece quando a API cai, e a leitura do cartao cabe no instante
// em que ela aparece — a tela nasce de um poll perdido, nunca no meio de uma
// animacao. `carregarOffline` devolve false quando o arquivo nao esta no cartao,
// e ai o painel se comporta como antes (a tela nao arma).
bool carregarOffline();
void soltarOffline();
int  offlineW(int num = 1, int den = 1);
int  offlineH(int num = 1, int den = 1);
bool tickOffline(uint32_t nowMs);
bool drawOfflineInto(Arduino_Canvas *g, int x, int y, int num, int den);

// ---- O bicho da tela de reset (retrato) ----
// O par do Token: um limite estourou, o outro acabou de liberar. Mesmas regras
// — resetW() == 0 quer dizer que o cartao nao tem o arquivo.
//
// Aqui o personagem MUDA a cada vez. `proximoReset` troca pelo seguinte da
// lista e devolve false so quando nenhum arquivo do rodizio esta no cartao;
// quem arma a tela chama isto ANTES de medi-la, porque o tamanho da caixa e do
// arquivo que entrou. Custa uma leitura de cartao — os arquivos sao grandes
// demais para ficarem todos na PSRAM (ver clawd.cpp).
bool proximoReset();
int  resetW();
int  resetH();
bool tickReset(uint32_t nowMs);
bool drawResetInto(Arduino_Canvas *g, int x, int y);

// O mesmo quadro, mas com o vazio JA pintado de `fundo` e blitado opaco.
//
// A versao de cima obriga quem chama a limpar a caixa antes: o blit pula os
// pixels da cor-chave, e sem limpar um quadro fica por baixo do outro. Aqui o
// decode preenche o buffer com a cor do fundo — a limpeza sai de graca no mesmo
// laco que ja percorre o quadro — e o blit vira copia de linha em vez de um
// teste de cor por pixel. Vale a pena so onde a caixa e grande e o quadro
// troca depressa, que e a danca do Cartman.
bool drawResetOpacoInto(Arduino_Canvas *g, int x, int y, uint16_t fundo);

// Troca o bicho do cabecalho a cada dez minutos, sorteando de uma lista fixa.
// Devolve true quando trocou (o chamador precisa redesenhar).
//
// Le do cartao: chame FORA do desenho e nunca com o dedo na tela — o engasgo
// da leitura engoliria o gesto. Ver `tickIconeCabecalho` em clawd.cpp.
bool tickIconeCabecalho(uint32_t nowMs, bool podeCarregar);

// ---- Icone de clima do cabecalho ----
// Cinco caras, uma por faixa de temperatura, ao lado da medicao do tempo.
//
// ANIMADO, em avaliacao — e o preco disso e alto e permanente.
//
// O lado direito do cabecalho esta FORA da faixa do flush de prefixo (x=0..92),
// onde so o mago mora. Cada quadro aqui obriga um flush de tela INTEIRA
// (~64 ms), e o cabecalho aparece em TODAS as paginas. Os arquivos rodam a
// 167 ms por quadro, ou seja 6 fps: 6 x 64 ms = ~38% de CPU permanentes, contra
// praticamente zero do quadro parado.
//
// A alternativa que estava aqui antes era quadro parado trocado por faixa de
// temperatura: blob liberado apos decodificar, icone de menos de 2 KB, blit
// direto. Se o custo medido nao compensar na tela, e para la que se volta.
//
// Animado, o blob FICA residente (ate 250 KB): animar exige todos os quadros.
// A faixa sai da temperatura ATUAL, com os extremos do dia por cima: no pico da
// maxima o bicho vai para "muito calor" e no fundo da minima para "muito frio",
// mesmo que o numero absoluto nao chegasse la. Estar no extremo do dia diz uma
// coisa que o grau sozinho nao diz — sem isso, um dia de 21 a 27 teria a manha
// mais fria e a tarde mais quente com exatamente a mesma cara.
void selecionarClima(int tempC, int tminC, int tmaxC);
int  climaW();                                   // 0 se nao ha o que desenhar
int  climaH();
// Contador proprio, e de proposito fora de `clawd::tick`: aquele junta os
// bichos que moram na faixa do prefixo e saem num envio barato. Este esta do
// outro lado da tela e cada quadro dele obriga um flush inteiro, entao quem
// chama tem que saber que o custo veio daqui.
bool tickClima(uint32_t nowMs);
bool drawClimaInto(Arduino_Canvas *g, int x, int y);

// ---- O sprite do nivel e o fundo da quarta pagina: SAIRAM ----
// Eram as duas maiores colecoes do cartao e sao as duas que nao cabem na
// particao de flash: 99 bichos de nivel (27 MB, o level_099 sozinho tem 438 KB)
// e 20 fundos por faixa de dezena (5,8 MB). Sem os dois, o resto da arte cabe
// com folga de 3,6 MB.
//
// A quarta pagina nao morreu junto — ela encolheu. Ficou com o numero do nivel,
// a linha de contexto, as colunas de estatistica e a barra de XP; o que a placa
// CONTA (turnos, custo, convivio guardado na NVS) nunca dependeu de sprite.
// Ver src/assets.h.

}
