#pragma once
#include <Arduino_GFX_Library.h>
#include <stdint.h>

namespace display {
bool            begin(uint8_t brightness);
Arduino_Canvas *canvas();     // 480x320 deitado, 320x480 em pe
void            flush();      // envia o framebuffer para o painel

// ---- Orientacao ----
// O painel nao rotaciona em hardware, e nem precisa: o framebuffer e SEMPRE
// 320x480 nativo, e a rotacao do Arduino_Canvas age so no espaco de desenho —
// ela muda como (x,y) vira indice, nao o que e enviado. Por isso girar nao
// realoca nada e nao custa nada: `flush` continua mandando WIDTH x HEIGHT.
//
// O QUE MUDA DE VERDADE e onde fica a faixa barata do flush de prefixo. Ela e
// sempre as N primeiras LINHAS DO PAINEL, e o painel esta de pe:
//
//   deitado (rotacao 1)   linha do painel = coluna da tela  -> faixa ESQUERDA
//   em pe   (rotacao 0)   linha do painel = linha  da tela  -> faixa do TOPO
//
// Ou seja, girar a tela move a regiao barata de canto. E por isso que em pe a
// turma do rodape sobe para logo abaixo do cabecalho: no rodape ela sairia da
// faixa e cada quadro passaria a custar um flush inteiro (~48 ms) em vez de
// ~11 ms. Ver ui::drawRetrato.
bool            retrato();
void            setRetrato(bool v);
int             telaW();      // 320 em pe, 480 deitado
int             telaH();      // 480 em pe, 320 deitado

// ---- O quadro, para quem precisa dele fora da tela ----
// As dimensoes do FRAMEBUFFER, que sao sempre as nativas do painel (320x480)
// nas duas orientacoes — ao contrario de telaW/telaH, que sao as do espaco de
// desenho e trocam ao girar. Quem envia o quadro pela rede precisa das de baixo.
int             quadroW();
int             quadroH();
size_t          quadroBytes();

// Copia o framebuffer inteiro para `dst`. Falso se nao couber ou nao houver
// framebuffer.
//
// COPIA, e nao um ponteiro para dentro do canvas: o desenho reescreve aquele
// buffer o tempo todo, e uma leitura vinda de outro nucleo pegaria a tela pela
// metade — o fundo ja limpo e o texto ainda nao escrito. Um quadro que nunca
// esteve no painel e pior do que nenhum, porque parece defeito de desenho.
//
// Chame do LACO, entre um desenho e o outro: ali o quadro esta inteiro por
// construcao, sem precisar de trava nenhuma.
bool            copiarQuadro(uint8_t *dst, size_t n);

// Envia so as `n` primeiras LINHAS DO PAINEL, sem janela de escrita.
//
// Este painel IGNORA CASET/RASET: medido, quatro tarjas mandadas para linhas
// diferentes cairam todas na linha 0. Escrever um PREFIXO do framebuffer e a
// unica escrita parcial que funciona, porque a origem e justamente onde ele
// pertence.
//
// O que `n` significa na TELA depende da orientacao (ver acima): deitado sao as
// n colunas da esquerda, em pe sao as n linhas do topo.
//
// Custo medido: ~100 us por linha, contra 48 ms do envio de tela inteira.
//
// n <= 0 nao faz nada; acima de PANEL_H vira o envio inteiro.
void            flushPrefix(int n);

// O painel CRU, sem o canvas na frente. Serve para escrever um retangulo
// pequeno direto na tela, sem pagar o flush inteiro de 63-70 ms.
//
// Cuidado: aqui as coordenadas sao de PAINEL (retrato, 320x480) e nao as de
// desenho (paisagem). O canvas nao sabe do que foi escrito por este caminho —
// o proximo flush apaga tudo.
Arduino_GFX    *raw();
void            setBrightness(uint8_t v);
}
