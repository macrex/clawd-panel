#pragma once
#include <cstddef>
#include <cstdint>

// Leitor dos arquivos .clw gravados no cartao SD (ver tools/sprites_to_clw.py).
//
// ZERO COPIA, DE PROPOSITO: a maior animacao tem 92 KB e a RAM interna do
// ESP32-S3 e escassa — o framebuffer ja mora na PSRAM. Sprite aponta para
// dentro do blob que quem chama carregou; nao aloca nada e nao e dono de nada.
// O blob precisa continuar vivo enquanto o Sprite for usado.
struct Sprite {
    bool     valid   = false;
    uint16_t frames  = 0;
    uint16_t width   = 0;      // nativo, na orientacao original (paisagem)
    uint16_t height  = 0;
    uint16_t key     = 0;      // cor tratada como transparente
    uint16_t frameMs = 0;
    uint16_t scale   = 1;      // ampliacao inteira, aplicada na decodificacao

    const uint32_t *offsets  = nullptr;   // frames+1 entradas, em CORRIDAS
    const uint16_t *data     = nullptr;   // pares valor,repeticoes
    size_t          runCount = 0;
};

Sprite parseSprite(const uint8_t *blob, size_t len);

// Dimensoes do buffer decodificado, em coordenadas de DESENHO (paisagem), que
// e o espaco em que o canvas trabalha.
int    spriteBufW(const Sprite &s);        // width  * scale
int    spriteBufH(const Sprite &s);        // height * scale
size_t spriteBufPixels(const Sprite &s);

// Decodifica um frame para `out`, ampliado, na orientacao original.
//
// NAO gira. A primeira versao girava 90 graus aqui, para escrever direto no
// painel (retrato) e evitar o flush de tela cheia. Nao funciona nesta placa:
// medido com blocos de teste nos quatro cantos, o painel ignora janela de
// endereco parcial e todo blit cai na origem. Quem gira agora e o canvas, que
// e o unico caminho que esta placa respeita.
//
// Pixels transparentes recebem `bg`. Passando a propria cor-chave, o resultado
// serve direto para draw16bitRGBBitmapWithTranColor.
bool decodeFrame(const Sprite &s, int frame, uint16_t *out, size_t outPixels,
                 uint16_t bg);

// Onde esta o CORPO do personagem dentro do quadro: centro horizontal e base,
// em pixels nativos.
//
// Por que nao serve o centro do quadro: o recorte automatico do clawd-tank
// dimensiona o quadro pela UNIAO de todos os frames, entao ele reserva espaco
// para o balao de alerta e para os "Z" do sono — coisas que aparecem em poucos
// quadros. O resultado e um quadro alto com o bicho sempre embaixo. Medido: o
// centro vertical do desenho varia de 49% a 85% da altura, e nunca sobe acima
// de 49%. Centrar o quadro deixa o personagem visivelmente caido.
//
// Por que nao serve o centro do quadro ATUAL: variaria a cada frame e o bicho
// tremeria na tela.
//
// A definicao usada: corpo = pixels opacos em pelo menos 60% dos quadros. O que
// e transitorio cai fora e para de puxar a ancora.
//
// `scratch` precisa de width*height bytes; a funcao nao aloca nada.
bool spriteBodyAnchor(const Sprite &s, uint8_t *scratch, size_t scratchLen,
                      int &centerX, int &bottom);

// Recorte retangular dentro do quadro, em pixels nativos.
struct SpriteBox {
    int x = 0, y = 0, w = 0, h = 0;
};

// Onde esta o PERSONAGEM dentro do quadro, como caixa.
//
// Difere de spriteBodyAnchor por resolver um problema que a ancora nao tinha:
// para desenhar o bicho reduzido a um icone, o quadro inteiro nao serve. O
// wizard mede 72x129 nativos e o personagem ocupa so as linhas 63 a 119 — o
// resto e a varinha e faiscas soltas la em cima. Reduzido pelo quadro, o icone
// vira dois tercos de vazio com um borrao embaixo.
//
// Nem a caixa dos pixels estaveis resolve sozinha: algumas faiscas aparecem em
// quase todos os quadros, entao a caixa volta a esticar ate o topo. A regra
// usada e a MAIOR FAIXA CONTIGUA de linhas com pixels estaveis — o personagem e
// um bloco solido de dezenas de linhas, e o enfeite e um punhado de linhas
// separado dele por um vao. Medido no wizard: faixa do enfeite 22 linhas, faixa
// do personagem 57.
//
// `scratch` precisa de width*height bytes; a funcao nao aloca nada.
bool spriteBodyBox(const Sprite &s, uint8_t *scratch, size_t scratchLen,
                   SpriteBox &box);

// Caixa que contem TODOS os quadros.
//
// A outra regra (spriteBodyBox) acha o personagem pelos pixels ESTAVEIS, e isso
// falha quando ele se mexe muito: no `confused`, nada acima da linha 69 aparece
// em 60% dos quadros, entao a caixa do corpo pega so o tronco e o bicho sai
// decapitado. Aqui nada e perdido, ao preco de incluir todo o espaco por onde
// qualquer coisa passeia.
//
// NAO ha regra unica. Medido em tres sprites, renderizando os dois recortes
// fora da placa e olhando:
//   wizard    caixa do corpo — a da uniao viraria o quadro inteiro (faiscas)
//   beacon    caixa do corpo — a da uniao encolheria o bicho dentro do anel
//   confused  caixa da UNIAO — a do corpo decapita
// So o wizard continua na tela; beacon e confused foram um indicador de conexao
// que saiu. A medicao fica registrada porque e ela que justifica existirem duas
// regras, e nao uma.
//
// Nao precisa de scratch: so percorre as corridas somando posicoes.
bool spriteUnionBox(const Sprite &s, SpriteBox &box);

// O quadro com MAIS pixels opacos — a pose mais completa da animacao.
//
// Serve para escolher um quadro representativo quando so um vai ser mostrado.
// O quadro 0 costuma ser a pose de repouso, e em varias animacoes daqui ele e
// justamente o mais vazio.
int spriteBestFrame(const Sprite &s);

// ---- O relogio de uma animacao ----
// Onde a marca do ultimo quadro deve ficar depois de um quadro andar. Devolve o
// novo `ultimo`, e quem chama ja sabe que o quadro andou.
//
// A CONTA ACUMULA A SOBRA em vez de rearmar em `agora`, e essa e a diferenca
// entre a cadencia do arquivo e a do laco. Uma volta do `loop()` custa ~30-45 ms
// (o `delay` mais o envio do quadro) e os arquivos pedem 50-70 ms: rearmando em
// `agora`, todo intervalo era arredondado para o proximo multiplo da volta —
// 50 ms viravam ~84, e a fileira andava a 12 quadros por segundo no lugar dos
// 20 que a arte pede. Somando `frameMs` ao que ja passou, o atraso de uma volta
// entra como credito na proxima e a media volta para o ritmo do arquivo.
//
// O TETO DE ATRASO evita o efeito oposto: parado meio segundo (tela cheia,
// modo terminal, leitura de cartao), o credito acumulado dispararia uma rajada
// de quadros para "alcancar" um tempo que ninguem viu passar. Acima de dois
// quadros de atraso a animacao ressincroniza em `agora` e segue dali.
uint32_t spriteRearme(uint32_t ultimo, uint32_t agora, uint16_t frameMs);

// ---- O envio coalescido de varios contadores ----
// Varios bichos animam em ritmos proprios, e mandar um quadro para a tela a
// cada um que avança sairia caro (ate um envio por contador). Esta funcao
// junta tudo que avançou dentro de `envioMinMs` num envio so.
//
// Chame a CADA volta do laco, com `algumAvancou` dizendo se pelo menos um
// contador mudou de quadro NESTA volta. `pendente` e `ultimoEnvio` sao o
// estado entre chamadas — quem chama os guarda (globais ou membros) e passa
// por referencia; esta funcao os atualiza. Devolve true SO na volta em que o
// lote deve ser mandado.
//
// `envioMinMs` e um TETO, nao um alvo: ele so pode ATRASAR um envio que já
// estava pronto, nunca apressar um que não tinha nada para mostrar. Por isso
// ele tem que acompanhar o arquivo mais RAPIDO em jogo — maior que o
// `frameMs` dele e a fileira ganha um teto de fps mais baixo que o que o
// proprio arquivo pede, e nenhum ajuste no arquivo aparece na tela.
bool coalescerEnvio(bool algumAvancou, uint32_t nowMs, uint32_t envioMinMs,
                    bool &pendente, uint32_t &ultimoEnvio);

// Dimensoes do quadro REDUZIDO por um divisor inteiro.
int    spriteDownW(const Sprite &s, int div);
int    spriteDownH(const Sprite &s, int div);
size_t spriteDownPixels(const Sprite &s, int div);

// Decodifica um quadro reduzido por `div`, por vizinho mais proximo.
//
// Reducao de pixel-art normalmente estraga o desenho, mas esta arte ja e
// chunky: conferido fora da placa em /2 e /3, as cinco animacoes continuam
// reconheciveis e distinguiveis entre si. `scale` do arquivo e IGNORADO — ele
// existe para ampliar na pagina do Clawd, e aqui o objetivo e o oposto.
bool decodeFrameDown(const Sprite &s, int frame, int div, uint16_t *out,
                     size_t outPixels, uint16_t bg);

// Dimensoes de um RECORTE reduzido por um divisor inteiro.
int boxDownW(const SpriteBox &box, int div);
int boxDownH(const SpriteBox &box, int div);

// Como decodeFrameDown, mas so do que esta dentro de `box`. Serve para virar um
// icone de cabecalho: o quadro inteiro reduzido perderia a maior parte da
// altura para espaco vazio (ver spriteBodyBox).
bool decodeCropDown(const Sprite &s, int frame, const SpriteBox &box, int div,
                    uint16_t *out, size_t outPixels, uint16_t bg);

// O quadro INTEIRO, em escala 1:1, ja com a cor-chave trocada por `bg`.
//
// E o caminho rapido de decodeCropDown para o caso em que o recorte e o proprio
// quadro e o divisor e 1. Ali cada pixel de saida e escrito uma unica vez e em
// ordem, entao a escrita e sequencial: nao ha o pre-preenchimento do buffer,
// nem a conversao para coordenada de recorte, nem os quatro testes de limite e
// os dois restos de divisao que o generico paga POR PIXEL.
//
// Sai opaco de proposito. Quem desenha um quadro grande que troca depressa
// prefere blitar sem teste de transparencia, e a limpeza da caixa vem de graca
// aqui — as corridas cobrem o quadro todo, inclusive o vazio.
//
// Medido na danca antiga do Cartman (262x240, 24 quadros): o generico levava
// 32 ms por quadro, contra os 70 que aquele arquivo dava para desenhar E enviar.
bool decodeFullOpaque(const Sprite &s, int frame, uint16_t *out,
                      size_t outPixels, uint16_t bg);
