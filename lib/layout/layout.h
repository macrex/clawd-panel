#pragma once

// Geometria do FLUSH DE PREFIXO.
//
// Este painel ignora a janela de escrita: medido, quatro tarjas mandadas para
// linhas diferentes (40, 130, 235 e 355) cairam TODAS na linha 0, e so a ultima
// ficou visivel. A consequencia util e que escrever as N PRIMEIRAS linhas esta
// sempre certo — e ali que elas pertencem.
//
// Na rotacao 1 do Arduino_Canvas, linha do painel = coluna X da paisagem:
//
//     fb[lx * 320 + (319 - ly)]  ==  fb[panel_y * 320 + panel_x]
//     ->  panel_y = lx        panel_x = 319 - ly
//
// Entao "as N primeiras linhas do painel" sao "as N colunas mais a esquerda da
// tela", e SO o que estiver nessa faixa pode ser atualizado barato. Se o selo
// do rodape mudar de canto, a tecnica morre junto.
//
// Sem Arduino de proposito: assim a conta roda nos testes nativos.

// Quantas colunas da esquerda precisam ser enviadas para cobrir o retangulo
// [x, x+w) mais uma margem. Devolve 0 quando nao ha o que enviar.
//
// Passar da largura da tela nao e erro: vira a tela inteira.
int prefixColumns(int x, int w, int margin, int screenW);

// ---- Fileira de caixas de largura FIXA ----
// O rodape mostra tres bichos lado a lado, e cada um pode trocar de animacao
// sem aviso. Se a posicao de cada um saisse da largura do desenho CORRENTE, uma
// troca de estado empurraria os vizinhos pela tela — o selo do meio andaria
// sozinho quando o da esquerda trocasse de sleeping para typing.
//
// Por isso a largura do slot e a MAIOR entre as caras que ele pode mostrar, e o
// desenho fica centrado dentro dela. Slots de largura zero (animacao que faltou
// no cartao) somem da fileira inteira, sem deixar buraco nem vao.

// Largura total da fileira, com os vaos entre os slots que existem.
int rowWidth(const int *widths, int count, int gap);

// Onde comeca o slot `index`, contando de `x0`.
int rowSlotX(const int *widths, int count, int gap, int index, int x0);

// ---- Fileira de fatias IGUAIS ----
// A fileira acima distribui por LARGURA DE SLOT com um vao fixo entre eles, e o
// resultado nao e uniforme aos olhos: cada slot mede o maior sprite que ele pode
// mostrar, o desenho fica centrado dentro dele, e o espaco visual entre dois
// bichos vira "vao + sobra da esquerda + sobra da direita". Medido na placa com
// a turma do South Park: 47, 45 e 27 px entre os quatro. O ultimo colava no
// penultimo e sobrava faixa vazia na direita.
//
// Aqui a faixa inteira e dividida em `count` partes iguais e cada desenho e
// centrado na SUA parte. Os centros ficam equidistantes por construcao, seja
// qual for a largura de cada bicho.
//
// A conta e `index * faixaW / count`, e nao `index * (faixaW / count)`: com a
// divisao por ultimo o resto se distribui entre as fatias, e a soma das larguras
// fecha exatamente `faixaW` — sem isso a fileira encolhe alguns pixels e deixa
// de encostar na margem direita, que e metade do defeito que ela veio corrigir.

// Onde comeca a fatia `index` de uma faixa de `faixaW` dividida em `count`.
int evenSlotX(int faixaW, int count, int index, int x0);

// A largura da fatia `index`. As fatias diferem em no maximo 1 px entre si.
int evenSlotW(int faixaW, int count, int index);

// Onde comeca um desenho de largura `w` centrado num slot que comeca em `slotX`
// e mede `slotW`. Desenho maior que o slot fica alinhado a esquerda em vez de
// escapar para dentro do vizinho anterior.
int centerIn(int slotX, int slotW, int w);

// ---- Os cartoes da lista de sessoes, em pe ----
// Qual cartao esta em `y`, ou -1 quando o dedo caiu fora da lista.
//
// POR QUE ELE EXISTE
// O hit-test que ja havia (`ui::agentIndexAt`) mira a lista da PAGINA 1 em pe —
// o menu de agentes da tela de contexto, com passo 26 a partir de outro topo. A
// lista da primeira tela nunca teve alvo: tocar num cartao dela nao fazia nada,
// e ele e o maior retangulo livre da tela (292x40, cinco deles).
//
// So o `y` importa: o cartao ocupa a largura util inteira, entao qualquer x
// dentro das margens cai nele. O vao de 4 px entre cartoes NAO pertence a
// ninguem — um dedo ali nao pode abrir a sessao de baixo por meio pixel.
int cartaoSessaoAt(int y, int quantos);

// ---- Quebra de linha ----
// Quantos caracteres cabem na PRIMEIRA linha de `txt` numa caixa de `cols`
// caracteres, quebrando no ultimo espaco que ainda cabe. Devolve o tamanho
// inteiro quando o texto cabe de uma vez.
//
// Existe por causa da data do reset: "01/08/2026 (Sabado)" mede 19 caracteres e
// o card comporta 15 na fonte grande. Ou quebra, ou volta a ser a letra miuda
// que motivou o aumento.
//
// Sem espaco aproveitavel a palavra e cortada seco em `cols`: uma linha cortada
// e ruim, escrever para fora do card e pior.
int breakAt(const char *txt, int cols);

// ---- Os alvos de toque da tela EM PE ----
//
// Um alvo errado nao da erro: ele so nao responde, ou responde no lugar do
// vizinho. E quando o painel esta na parede, a unica forma de descobrir e
// tocando — que e exatamente o que nao da para fazer quando o cartao trava e a
// placa nao sobe.
//
// Por isso os retangulos moram aqui, longe do canvas: assim eles sao conferidos
// no PC, contra as posicoes MEDIDAS numa foto da tela real (ver
// test/test_alvos). O `ui.cpp` continua dono do desenho e da pergunta "estamos
// em pe?"; o que saiu foi so a aritmetica dos cantos.
struct Alvo {
    int x = 0, y = 0, w = 0, h = 0;
};

bool dentro(const Alvo &a, int x, int y);

// O bicho do cabecalho, que gira a tela. Canto superior esquerdo, e o alvo mais
// forte de todos (ver lib/gesture/acao.h) porque em pe ele e a unica saida.
//
// 76x46 e nao o tamanho do desenho: o bicho muda de LARGURA conforme o nivel e o
// sorteio do rodizio. Medidos na placa: 33x32, 51x30, 64x35 — um alvo do tamanho
// do desenho encolheria junto com ele, e acertar 33 px de largura com o dedo num
// painel de parede nao acontece. Se um sorteio trouxer um bicho mais largo que
// 76, o excesso simplesmente nao gira, e o dedo continua tendo alvo de sobra.
Alvo alvoIconeCabecalho();

// ---- Os tres atalhos de ensaio ----
// Os limites deixaram de ser duas faixas EMPILHADAS e viraram duas COLUNAS lado
// a lado de 143x98. Os alvos seguiram: a regra continua "percentual a direita,
// rotulo a esquerda", so que agora dentro da coluna de cada janela, e nao dentro
// da largura do painel.
//
// A coluna da SESSAO tem a metade esquerda livre de proposito — o rotulo dela
// nunca teve ensaio, e um alvo sem acao ali roubaria o toque de quem erra a mira
// no percentual ao lado.

// O percentual da janela de 5h: metade DIREITA da coluna da esquerda. Duplo
// toque ali ensaia a tela de reset.
Alvo alvoPctSessao();

// O rotulo da janela de 7 dias: metade ESQUERDA da coluna da direita. Duplo
// toque ali ensaia a morte do Kenny.
Alvo alvoRotuloSemana();

// O percentual da janela de 7 dias: metade DIREITA da coluna da direita. Duplo
// toque ali abre a tela do Token sem esperar o limite estourar.
Alvo alvoPctSemana();

// ---- Os dois alvos da primeira tela DEITADA ----
// Deitado nao ha coluna de limite: cada janela e um ANEL de 136 px de diametro,
// com o rotulo ("SESSAO", "SEMANA") escrito no miolo dele. O alvo e o anel
// INTEIRO e nao a palavra — 36x8 px nao e alvo de dedo, e o miolo do anel nao
// pertence a mais nada.
//
// Eles ficam mais alto que os alvos em pe (y 50..186 contra 114..212), e essa e
// a unica razao de existirem separados: a geometria deitada nao e a mesma tela
// mais larga, e um alvo herdado apontaria para o vazio entre o anel e a lista.
Alvo alvoAnelSessaoDeitado();
Alvo alvoAnelSemanaDeitado();

// ---- Em que escala um desenho cabe numa caixa ----
// A escala e uma RAZAO e nao um divisor inteiro, e a diferenca e o tamanho do
// bicho na tela: o Token mede 224x336 e a faixa deitada tem 240x250. Por
// divisor inteiro so havia 336 (nao cabe) e 168 — metade da tela util jogada
// fora. Por razao ele entra em 167x250, que e o que a caixa comporta.
//
// A razao e devolvida sem simplificar: quem desenha so faz `v * num / den`, e
// reduzir a fracao nao mudaria um pixel do resultado.
//
// Ela AMPLIA quando o desenho e menor que a caixa (o Clawd dormindo tem 192x169
// e sobra espaco), e e por isso que a caixa e o parametro, e nao um teto: um
// bicho que so encolhe deixa metade da faixa vazia quando ele e o pequeno.
struct Escala {
    int num = 1, den = 1;
};

// Entrada degenerada (qualquer lado <= 0) devolve 1:1 — desenhar em tamanho
// nativo e melhor do que multiplicar por uma escala que ninguem calculou.
Escala escalaParaCaber(int w, int h, int caixaW, int caixaH);

// ---- A grade de botoes da pergunta ----
// Deitada, a tela de pergunta tem 158 px de miolo contra 292 em pe. Com quatro
// opcoes o botao fica em 33 px — dois pixels abaixo do limiar que libera o
// corpo 2 — e o rotulo cai para a grade de 8 px. O que sobra deitado e LARGURA:
// 440 px para um rotulo que enche metade da linha.
//
// A grade troca largura por altura: quatro opcoes deitadas viram duas colunas
// de 216x75 em vez de quatro faixas de 33, sem tirar a fileira de bichos do
// rodape. O preco esta no texto, que passa a ter metade das colunas — e por
// isso a segunda coluna so aparece deitada e a partir de QUATRO opcoes. Com
// tres ou menos a faixa inteira ainda da altura de sobra, e reparti-la so
// encolheria o rotulo em troca de nada.

// Quantas colunas a grade usa. Em pe e sempre uma: la a altura e o que sobra.
int gradeColunas(int total, bool deitado);

// A area util da grade e o teto de cada celula.
struct Grade {
    int x = 0, larg = 0;      // faixa horizontal
    int topo = 0, fim = 0;    // faixa vertical
    int gap = 0;              // vao entre celulas, nas duas direcoes
    int altMin = 0;           // celula mais baixa que isto nao vale desenhar
    int cols = 1;
};

// A celula `i` de `total`, ou false quando ela nao cabe — ou quando o indice
// esta fora da contagem.
//
// A ULTIMA celula de uma contagem impar ocupa a largura inteira: numa grade de
// duas colunas com cinco opcoes ela ficaria sozinha na fileira de baixo, com
// metade da tela vazia ao lado, e o rotulo dela pagaria por um espaco que
// ninguem esta usando.
bool gradeCelula(const Grade &g, int i, int total, Alvo &out);
