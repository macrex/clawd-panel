#pragma once
#include "status.h"
#include "nivel.h"
#include <string>

namespace ui {

// 0 = limites, 1 = contexto por agente, 2 = Clawd, 3 = nivel.
const int PAGES = 4;

// staleSeconds > 0 desenha o mesmo conteudo esmaecido, com aviso de "sem
// contato ha Ns" — o dado antigo continua util, so precisa parecer velho.
//
// selectedId identifica o agente destacado na pagina de contexto. E o ID e nao
// o indice de proposito: a lista vem ordenada por contexto e reordena sozinha,
// entao um indice fixo faria a selecao pular de agente sem ninguem tocar nela.
//
// `botaoArmado` e o botao de limpeza esperando a confirmacao. Ele mora aqui e
// nao no desenho porque quem conta o tempo dele e o laco principal.
// `nivelNoCartao` traz o que so a placa sabe (as horas de convivio e a trava de
// nivel maximo). Turnos e custo chegam pelo `Status`, vindos da API — os tres
// juntos e que dao o nivel da quarta pagina.
// `xpDoDia` vem calculado de fora e nao e recalculado aqui: ele depende da
// marca da virada, que so o laco principal acompanha. Refazer a conta no desenho
// daria dois numeros que podem discordar.
// `opcaoArmada` e o NUMERO da opcao esperando confirmacao na pagina do
// bloqueio, ou 0. Numero e nao indice porque e ele que aparece na tela do
// agente e e ele que volta no POST — um indice exigiria traduzir nos dois
// sentidos, e uma traducao errada aqui aprova a opcao errada.
// `telaToken` liga a tela de limite estourado do retrato: abaixo do cabecalho,
// so o Token e os dois prazos. Quem decide quando liga-la (e quando o toque a
// dispensa) e o chamador — a UI so desenha.
// `telaReset` nao nulo liga a tela de RESET — a irma da do Token, com o bicho
// do rodizio —, e o texto e o nome da janela que liberou ("SESSAO" ou
// "SEMANA"). Ela ganha da do Token: liberar e a noticia mais nova das duas.
// `telaOffline` liga a tela de SERVIDOR FORA: o mesmo bicho do Token com outro
// recado na camisa. Ela ganha da do Token e perde da de reset — com a API muda,
// o limite estourado que a do Token anunciaria e uma leitura velha, e o que
// quem olha precisa saber e por que nada muda.
void drawStatus(const Status &s, int page, const std::string &selectedId,
                int staleSeconds = 0, bool botaoArmado = false,
                const nivel::Estado &nivelNoCartao = nivel::Estado(),
                float xpDoDia = 0, int opcaoArmada = 0, bool telaToken = false,
                const char *telaReset = nullptr, bool telaOffline = false);

// POR QUE o dado da tela esta velho. Muda so o texto do rodape, e a diferenca
// importa porque cada motivo pede uma acao diferente: o radio caido se resolve
// esperando, o processo da API morto pede um comando no PC, a maquina desligada
// pede alguem levantar. Antes as tres apareciam como "SEM CONTATO HA 40s".
//
// O texto sai de `motivoDaFalha` (lib/metrics/falha.h) e tem no maximo 11
// caracteres, que e a largura util do rodape. `nullptr` nao muda nada.
void marcarMotivo(const char *v);

// Ha DUAS fontes configuradas? So entao o chip com a tag da maquina aparece:
// com uma fonte so ele seria a mesma palavra em toda linha — o mesmo motivo
// pelo qual o chip `claude` nao aparece quando o agente e o esperado.
//
// O TEXTO da tag nao vem daqui: vem do proprio /status de cada maquina
// (Agent::tag). A placa so precisa saber se ha o que distinguir. Chamar uma
// vez no setup.
void duasFontes(bool v);

// Um dos limites (sessao ou semana) bateu 100%? A janela precisa ser
// conhecida: `known` falso nao estoura nada.
bool limiteEstourado(const Status &s);

// Redesenha SO o selo do rodape e envia so a faixa esquerda da tela.
//
// Custa ~9 ms contra os ~64 ms de um redesenho completo, e e isso que torna a
// animacao do rodape viavel. So funciona porque o selo mora na faixa esquerda:
// ver display::flushPrefix.
// `semTurma` e a tela do Token no ar: o cabecalho continua animando, mas a
// faixa da turma pertence a cabeca do bicho e nao pode ser redesenhada.
void redrawBadge(const Status &s, int staleSeconds, bool semTurma = false);

// Um quadro do bicho da tela de RESET, sem repintar o resto dela.
//
// Custa a caixa do bicho mais um prefixo que vai do topo ao pe dele (347 linhas
// no Cartman, 354 no Kenny), contra os 15,7 ms de `fillScreen`
// e os 48 do flush inteiro que o desenho completo paga. So vale depois que a
// tela ja foi desenhada por inteiro uma vez: os dois textos de baixo vem de la.
// O bicho do cabecalho vai junto, porque cai dentro do mesmo prefixo — enquanto
// esta tela esta no ar, `redrawBadge` nao deve ser chamado.
//
// Medido na placa com a danca antiga do Cartman, de caixa 262x240: 14,6 ms de
// desenho e 29,9 de envio, contra os 70 ms que aquele arquivo dava por quadro.
// As caixas de hoje sao mais altas e os intervalos mais largos (150 e 165 ms),
// entao a folga so aumentou.
void redrawReset();

void drawMessage(const char *title, const char *detail);

// Indice do item do menu de repos sob (x, y), ou -1 fora dele.
int agentIndexAt(const Status &s, int x, int y);

// Qual cartao da lista da PRIMEIRA tela em pe esta em (x,y). -1 fora dela.
//
// A geometria mora em lib/layout (`cartaoSessaoAt`, com teste nativo); aqui
// entram as duas coisas que so a `ui` sabe: se a tela esta em pe sem pergunta
// tomando o painel, e QUANTOS cartoes de fato couberam — o resto virou "+N" e
// nao tem alvo.
int cartaoSessaoRetratoAt(const Status &s, int x, int y);

// O toque caiu no botao de limpeza de contexto? So existe na pagina 1, e quem
// confere a pagina e quem chama.
//
// Falso tambem quando o botao NAO esta desenhado — abaixo de 50% de contexto
// ele nao existe, e um retangulo invisivel que responde ao toque seria pior do
// que nenhum botao.
bool cleanButtonAt(const Status &s, const std::string &selectedId, int x, int y);

// O NUMERO da opcao sob (x, y) na pagina do bloqueio, ou 0 fora dela.
//
// Devolve 0 tambem quando o bloqueio nao esta desenhado — porque nao ha
// bloqueio, ou porque ele e de outro agente que nao o selecionado. Um retangulo
// invisivel que responde ao toque aqui nao arma um botao a toa: aprova um
// pedido de permissao.
int opcaoAt(const Status &s, const std::string &selectedId, int x, int y);

// O NUMERO da opcao sob (x, y) na pergunta em tela cheia da P0, ou 0 fora dela.
// Sem selecao: usa o bloqueio corrente (s.bloqueio), o mesmo que a P0 desenha.
//
// Vale nas DUAS orientacoes: a geometria dos botoes sai de display::telaW/telaH,
// entao em pe eles simplesmente ficam maiores (~92 px com tres opcoes, contra
// ~47 deitado) sem nenhuma conta separada.
int perguntaP0At(const Status &s, int x, int y);

// O toque caiu no bicho do canto superior esquerdo? E o botao de GIRAR a tela.
//
// O alvo e generoso de proposito — bem maior que o desenho. Ele e a unica saida
// do modo em pe (la nao ha troca de pagina), e um controle sem saida alternativa
// nao pode exigir pontaria. Como o cabecalho e o mesmo em todas as paginas, o
// gesto vale em todas elas.
bool iconeCabecalhoAt(int x, int y);

// O toque caiu na FILEIRA de bichos? E o botao de trocar o elenco.
//
// Precisa da pagina porque deitado a fileira some nas de bicho grande, e ali o
// mesmo ponto da tela pertence a outra coisa. Em pe existe uma pagina so, e o
// argumento e ignorado.
bool turmaAt(int x, int y, int page);

// O toque caiu no percentual da SEMANA da primeira tela em pe? E o atalho que
// abre a tela do Token sem esperar o limite estourar.
bool pctSemanaAt(int x, int y);

// O gemeo dele na faixa da SESSAO: ensaia a tela de RESET por 5 s, para dar
// para ver sem esperar a janela virar.
bool pctSessaoAt(int x, int y);

// O rotulo da faixa da SEMANA: ensaia a morte do Kenny. So faz efeito num tema
// que TENHA um Kenny — no padrao nao ha o que matar.
bool rotuloSemanaAt(int x, int y);

// ---- Modo terminal ----
// A tela do agente em tela cheia. Nao e uma quinta pagina: e um estado a parte,
// e por isso os swipes horizontais nao trocam de pagina enquanto ele esta
// aberto — la eles nao significam nada, e o vertical e que rola o texto.

// Geometria da grade, em CELULAS. Estes dois numeros viajam ate a API no
// pedido: quem quebra as linhas e ela, porque e ela que sabe traduzir o texto
// do terminal para o que esta fonte desenha. Quebrar de la tambem evita mandar
// pela rede o que nao caberia mesmo.
int termCols();
int termRows();

// `linhas` ja vem cortada na largura e na altura pela API — aqui nao ha medida
// nem quebra, uma linha e uma fileira. Lista vazia desenha "lendo...", que e o
// estado real entre abrir a tela e a primeira resposta chegar.
//
// `travado` falso avisa na tela que a API nao conseguiu ajustar a largura do
// pane. O texto continua sendo desenhado: quebrado e melhor do que nada, e sem
// o aviso a quebra pareceria defeito do painel.
void drawTerminal(const std::vector<std::string> &linhas, const char *titulo,
                  bool travado);

// O toque caiu no botao de sair? A area sensivel e MAIOR que o desenho: sair e
// a acao desta tela que nao pode exigir pontaria.
bool terminalSairAt(int x, int y);

// Qual botao da barra de rolagem do terminal esta em (x,y). -1 fora dela.
// 0 = topo, 1 = uma tela para tras, 2 = uma para frente, 3 = fim.
int terminalBotaoAt(int x, int y);

// Solta o grid de cor do terminal (PSRAM). Chamado ao sair da tela: ele so
// existe enquanto ela esta aberta.
void gridSoltar();

// O toque caiu no botao que abre o terminal, na pagina de contexto? Falso
// quando ele nao esta desenhado — inclusive quando o agente selecionado nao tem
// pane do herdr, caso em que nao ha tela nenhuma para abrir.
bool terminalButtonAt(const Status &s, const std::string &selectedId, int x, int y);

// O pane do herdr do agente selecionado, ou vazio. E o que o modo terminal
// precisa saber para pedir a tela certa.
std::string paneDoSelecionado(const Status &s, const std::string &selectedId);

}
