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

// Onde comeca um desenho de largura `w` centrado num slot que comeca em `slotX`
// e mede `slotW`. Desenho maior que o slot fica alinhado a esquerda em vez de
// escapar para dentro do vizinho anterior.
int centerIn(int slotX, int slotW, int w);

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
