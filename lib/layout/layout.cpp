#include "layout.h"

int prefixColumns(int x, int w, int margin, int screenW) {
    if (w <= 0 || screenW <= 0) return 0;
    if (x < 0) x = 0;
    if (margin < 0) margin = 0;

    // long: x+w+margin com valores absurdos estouraria int, e o grampeamento
    // logo abaixo so funciona se a soma sobreviver ate ele.
    const long fim = (long)x + w + margin;
    if (fim <= 0) return 0;
    if (fim >= screenW) return screenW;
    return (int)fim;
}

int rowWidth(const int *widths, int count, int gap) {
    if (!widths || count <= 0) return 0;
    if (gap < 0) gap = 0;

    int total = 0, vistos = 0;
    for (int i = 0; i < count; i++) {
        if (widths[i] <= 0) continue;     // slot vazio nao ocupa nem vao
        if (vistos) total += gap;
        total += widths[i];
        vistos++;
    }
    return total;
}

int rowSlotX(const int *widths, int count, int gap, int index, int x0) {
    if (!widths || index <= 0 || index >= count) return x0;
    // A largura de tudo que vem ANTES, com o vao que separa do slot pedido.
    const int antes = rowWidth(widths, index, gap);
    if (antes <= 0) return x0;            // nada antes: comeca na borda
    if (widths[index] <= 0) return x0 + antes;
    return x0 + antes + (gap < 0 ? 0 : gap);
}

int evenSlotX(int faixaW, int count, int index, int x0) {
    if (count <= 0 || faixaW <= 0) return x0;
    if (index <= 0) return x0;
    if (index >= count) index = count - 1;
    return x0 + (int)((long)index * faixaW / count);
}

int evenSlotW(int faixaW, int count, int index) {
    if (count <= 0 || faixaW <= 0) return 0;
    if (index < 0) index = 0;
    if (index >= count) index = count - 1;
    // A diferenca entre o inicio da PROXIMA fatia e o desta, para que o resto da
    // divisao caia dentro de alguma fatia em vez de sumir no fim da faixa.
    const long ini = (long)index * faixaW / count;
    const long fim = (long)(index + 1) * faixaW / count;
    return (int)(fim - ini);
}

int centerIn(int slotX, int slotW, int w) {
    if (w >= slotW) return slotX;
    return slotX + (slotW - w) / 2;
}

// A geometria da lista da primeira tela em pe, espelhada de drawSessoesRetrato.
// Duplicar tres numeros aqui e o preco de manter o hit-test testavel no PC — a
// funcao que desenha precisa de canvas e fonte, e arrastar isso para a lib
// traria o Arduino junto.
const int LST_Y0    = 228;   // primeiro cartao (topo da lista + 8 de respiro)
const int LST_PASSO = 44;
const int LST_ALT   = 40;    // o cartao; os outros 4 px sao o vao

int cartaoSessaoAt(int y, int quantos) {
    if (quantos <= 0 || y < LST_Y0) return -1;
    const int i = (y - LST_Y0) / LST_PASSO;
    if (i >= quantos) return -1;
    // Dentro do CARTAO, e nao do passo: o vao entre dois cartoes nao responde.
    if ((y - LST_Y0) - i * LST_PASSO >= LST_ALT) return -1;
    return i;
}

int breakAt(const char *txt, int cols) {
    if (!txt) return 0;
    int len = 0;
    while (txt[len]) len++;
    if (cols <= 0) return 0;
    if (len <= cols) return len;

    // Do limite para tras: o ultimo espaco que ainda cabe. `cols` entra na
    // busca de proposito — um espaco EXATAMENTE ali quebra sem sobrar nada
    // pendurado.
    for (int i = cols; i > 0; i--)
        if (txt[i] == ' ') return i;
    return cols;
}

// ---- Os alvos de toque da tela em pe ----
//
// As MESMAS constantes que o desenho usa (ver as R_* em src/ui.cpp). Elas foram
// repetidas aqui em vez de importadas porque `board_pins.h` e do firmware e nao
// compila no PC — e um alvo que so existe no lugar certo quando ninguem mexe nas
// duas copias nao valeria o teste. Por isso `test_alvos` confere os numeros
// contra uma FOTO da tela real: se o desenho mudar e o alvo nao, a medida
// denuncia.
namespace {

const int PAINEL_W  = 320;   // largura nativa do painel (retrato)
const int MARG      = 14;
const int LIM_Y     = 114;   // teto das duas colunas de limite
const int LIM_H     = 98;    // altura de cada coluna
const int LIM_W     = 143;   // largura de cada coluna
const int LIM_GAP   = 6;     // 14 + 143 + 6 + 143 + 14 = 320
// 180 e nao 76: na tela nova o canto superior esquerdo deixou de ser o bicho e
// passou a ser o NOME, que mede 162 px em corpo 3. Um alvo do tamanho do bicho
// cobria as quatro primeiras letras e o dedo caia fora nas outras cinco — num
// controle que e a UNICA saida do modo em pe.
//
// 180 e o nome mais folga, e para antes da HORA (que comeca em 186): o
// cabecalho inteiro menos o relogio. Nas paginas que ainda mostram o bicho ali
// o alvo so ficou generoso, que e o que ele sempre foi de proposito.
const int GIRO_W    = 180;
const int GIRO_H    = 46;

// A SEMANA deixou de ficar ABAIXO da sessao e passou a ficar AO LADO dela.
int xDaSemana() { return MARG + LIM_W + LIM_GAP; }       // 163

}   // namespace

bool dentro(const Alvo &a, int x, int y) {
    return x >= a.x && x < a.x + a.w && y >= a.y && y < a.y + a.h;
}

Alvo alvoIconeCabecalho() { return Alvo{0, 0, GIRO_W, GIRO_H}; }

// A ALTURA e a coluna INTEIRA, e nao um recorte dela como era nas faixas.
// La o alvo pegava 40 dos 70 px porque duas faixas empilhadas dividiam a mesma
// vertical e um alvo alto demais invadiria a vizinha. Em coluna as vizinhas
// estao lado a lado: a divisao que importa e horizontal, e a vertical fica
// inteira para o dedo — 98 px em vez de 40, num painel que se toca em pe.
Alvo alvoPctSessao() {
    return Alvo{MARG + LIM_W / 2, LIM_Y, LIM_W - LIM_W / 2, LIM_H};
}

Alvo alvoRotuloSemana() {
    return Alvo{xDaSemana(), LIM_Y, LIM_W / 2, LIM_H};
}

Alvo alvoPctSemana() {
    return Alvo{xDaSemana() + LIM_W / 2, LIM_Y, LIM_W - LIM_W / 2, LIM_H};
}
