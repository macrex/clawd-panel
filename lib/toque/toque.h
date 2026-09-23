#pragma once
#include <stdint.h>

// Os dois avisos sonoros do painel, gerados amostra a amostra — sem arquivo de
// audio no cartao nem na flash.
//
// Este modulo NAO sabe nada de I2S nem de tarefa: recebe um indice, devolve uma
// amostra. O driver (src/som.cpp) so empurra isso para o amplificador. Assim a
// parte que decide se o som estala ou estoura pode ser testada no PC.
namespace toque {

// 16 kHz sobra para notas de ate ~1 kHz e o NS4168 aceita (8 a 96 kHz). Taxa
// maior so gastaria DMA e CPU sem mudar nada que se ouca num alto-falante desse
// tamanho.
constexpr uint32_t TAXA = 16000;

// O botao de volume. 8000 de 32767 e ~-12 dBFS, ~1/16 da potencia maxima do
// amplificador: se ouve do outro lado da mesa sem assustar. Calibre aqui,
// ouvindo na placa — alto-falante e caixa mudam muito o resultado.
constexpr int16_t PICO = 8000;

struct Nota {
    uint16_t hz;   // 0 = pausa
    uint16_t ms;
};

struct Melodia {
    const Nota *notas;
    int         n;
};

// Pronto = duas notas subindo (soa como "terminei").
// Espera = a mesma nota duas vezes, mais grave (soa como alguem batendo).
extern const Melodia PRONTO;
extern const Melodia ESPERA;

// Em amostras.
uint32_t duracao(const Melodia &m);

// A amostra `i` do aviso, ja com envelope. Depois do fim devolve 0: quem toca
// pode pedir alem da duracao para escrever silencio.
int16_t amostra(const Melodia &m, uint32_t i);

}
