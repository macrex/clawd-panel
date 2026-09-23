#include "som.h"
#include "toque.h"
#include <Arduino.h>
#include <ESP_I2S.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// O amplificador e um NS4168: I2S mono classe D, sem I2C, sem MCLK. Ele tira o
// relogio do proprio BCLK/LRCLK e toca o que chega em SDATA.
//
// Os pinos vem do demo de fabrica da Guition (DEMO_MP3/pincfg.h do pacote
// JC3248W535EN: AUDIO_I2S_BCK_IO 42, AUDIO_I2S_LRCK_IO 2, AUDIO_I2S_DO_IO 41,
// MCK -1), e batem com o PINS_JC3248W535.h do autor do Arduino_GFX. Nao ha pino
// de habilitacao ligado ao ESP32: o CTRL do NS4168 (desliga/esquerdo/direito,
// por nivel de tensao) e fixo na placa.
namespace {

const int8_t PINO_BCLK = 42;
const int8_t PINO_LRCK = 2;
const int8_t PINO_DOUT = 41;

// Sem pino de habilitacao, o silencio de verdade e parar o relogio: sem BCLK o
// NS4168 nao tem o que tocar e para de chavear. Zeros com o relogio correndo
// deixariam o amplificador ligado — chiado baixo e bateria gasta entre avisos
// que podem estar horas separados. Por isso o canal liga so durante o aviso.
//
// Religado, o amplificador precisa de um instante para acordar; a primeira nota
// comecando junto com o relogio perderia o ataque.
const uint32_t ACORDAR = toque::TAXA * 30 / 1000;

// Parar o canal descarta o que ainda estiver na DMA. `write` volta quando o
// ultimo bloco ENTROU na DMA, nao quando saiu no alto-falante — entao, sem esta
// cauda, o fim da ultima nota seria cortado. Ela cobre a DMA inteira do
// ESP_I2S (6 descritores de 240 quadros, I2S_DEFAULT_CFG), ~90 ms: quando a
// ultima escrita volta, todo o som de verdade ja saiu.
const uint32_t CAUDA = 6 * 240;

const int BLOCO = 128;   // quadros por escrita; 512 bytes na pilha

I2SClass      g_i2s;
QueueHandle_t g_fila = nullptr;

void tocarAgora(const toque::Melodia &m) {
    if (i2s_channel_enable(g_i2s.txChan()) != ESP_OK) return;

    // Estereo com a MESMA amostra nos dois lados: o CTRL da placa escolhe o
    // lado que o NS4168 toca, e nao sabemos qual. Assim tanto faz.
    int16_t buf[BLOCO * 2];
    const uint32_t total = ACORDAR + toque::duracao(m) + CAUDA;
    uint32_t i = 0;
    while (i < total) {
        int q = 0;
        for (; q < BLOCO && i < total; q++, i++) {
            // `amostra` devolve 0 alem do fim: a cauda sai de graca.
            const int16_t s = i < ACORDAR ? 0 : toque::amostra(m, i - ACORDAR);
            buf[2 * q] = buf[2 * q + 1] = s;
        }
        if (g_i2s.write(buf, q * 2 * sizeof(int16_t)) == 0) break;
    }

    i2s_channel_disable(g_i2s.txChan());
}

// Prioridade 1, a mesma da rede e do laco, e nao mais: o som pode atrasar uns
// milissegundos, o dedo nao. Passa quase todo o tempo parada na fila, e durante
// o aviso parada em `write`, esperando a DMA — o calculo das amostras e
// migalha perto disso.
void tarefaSom(void *) {
    som::Aviso a;
    for (;;) {
        if (xQueueReceive(g_fila, &a, portMAX_DELAY) != pdTRUE) continue;
        tocarAgora(a == som::Aviso::Pronto ? toque::PRONTO : toque::ESPERA);
    }
}

}

namespace som {

bool iniciar() {
    g_i2s.setPins(PINO_BCLK, PINO_LRCK, PINO_DOUT);
    if (!g_i2s.begin(I2S_MODE_STD, toque::TAXA, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
        Serial.printf("som: I2S NAO subiu (bclk=%d lrck=%d dout=%d), painel mudo\n",
                      PINO_BCLK, PINO_LRCK, PINO_DOUT);
        return false;
    }
    // O begin deixa o relogio correndo; o canal so liga durante um aviso.
    i2s_channel_disable(g_i2s.txChan());

    // Dois lugares: cabe um Pronto e um Espera chegando na mesma volta do laco.
    // Mais que isso seria fila de sirene; o que nao couber e descartado.
    g_fila = xQueueCreate(2, sizeof(Aviso));
    // Nucleo 0, como a rede: o laco (nucleo 1) e quem le o dedo.
    if (!g_fila || xTaskCreatePinnedToCore(tarefaSom, "som", 3072, nullptr, 1, nullptr, 0) != pdPASS) {
        Serial.println("som: sem memoria para a tarefa, painel mudo");
        g_i2s.end();
        return false;
    }

    Serial.printf("som: I2S ok, NS4168 em bclk=%d lrck=%d dout=%d, %u Hz\n",
                  PINO_BCLK, PINO_LRCK, PINO_DOUT, (unsigned)toque::TAXA);
    return true;
}

void tocar(Aviso a) {
    if (g_fila) xQueueSend(g_fila, &a, 0);   // 0: fila cheia descarta, nunca espera
}

}
