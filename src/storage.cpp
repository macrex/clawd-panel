#include "storage.h"
#include "board_pins.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <algorithm>

namespace storage {

// Montar o cartao TRES vezes antes de desistir.
//
// Visto em 14/08: depois de uma gravacao de firmware, o `send_op_cond` passou a
// devolver 0x107 (timeout — o cartao nao responde ao comando de inicializacao)
// em todo boot, e a placa ficou parada em SEM CONFIG ate a energia ser cortada
// de verdade. Com bateria, reset por software nao corta a alimentacao do
// cartao: ele fica no mesmo estado ruim de antes do reset.
//
// `SD_MMC.end()` desliga o host entre as tentativas, que e o mais perto de um
// corte de energia que o firmware consegue, e a espera da ao cartao tempo de
// voltar ao estado ocioso.
//
// A FREQUENCIA nao entra na conta, e isso nao e esquecimento: a inicializacao
// SEMPRE roda em 400 kHz (SDMMC_FREQ_PROBING) qualquer que seja o parametro —
// ele so vale depois que o cartao respondeu. Baixar a frequencia nao teria
// mudado nada neste erro, e cobraria o boot inteiro em cartao lento.
const int      SD_TENTATIVAS = 3;
const uint32_t SD_ESPERA_MS  = 300;

bool begin() {
    if (!SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0)) return false;
    for (int i = 0; i < SD_TENTATIVAS; i++) {
        if (SD_MMC.begin("/sdcard", true)) {   // 1-bit
            if (i) Serial.printf("sd: montou na tentativa %d\n", i + 1);
            return true;
        }
        SD_MMC.end();
        delay(SD_ESPERA_MS);
    }
    return false;
}

bool readFile(const char *path, std::string &out) {
    File f = SD_MMC.open(path, FILE_READ);
    if (!f || f.isDirectory()) return false;
    out.clear();
    out.reserve(f.size());
    while (f.available()) out.push_back((char)f.read());
    f.close();
    return true;
}

uint8_t *readFileToPsram(const char *path, size_t &len) {
    len = 0;
    File f = SD_MMC.open(path, FILE_READ);
    if (!f || f.isDirectory()) return nullptr;

    const size_t size = f.size();
    if (!size) { f.close(); return nullptr; }

    uint8_t *buf = (uint8_t *)ps_malloc(size);
    if (!buf) { f.close(); return nullptr; }

    const size_t lidos = f.read(buf, size);
    f.close();
    // Leitura parcial devolve nada. Meio sprite na PSRAM so viraria lixo na
    // tela alguns quadros depois, longe da causa.
    if (lidos != size) { free(buf); return nullptr; }

    len = size;
    return buf;
}

bool writeFileAtomic(const char *path, const std::string &dados) {
    const std::string tmp = std::string(path) + ".tmp";

    File f = SD_MMC.open(tmp.c_str(), FILE_WRITE);
    if (!f) return false;
    const size_t escritos = f.write((const uint8_t *)dados.data(), dados.size());
    // flush ANTES do close: sem ele o rename poderia acontecer com o conteudo
    // ainda em buffer, que e exatamente o corte que este caminho evita.
    f.flush();
    f.close();
    if (escritos != dados.size()) {
        SD_MMC.remove(tmp.c_str());
        return false;
    }

    // O FAT nao troca um arquivo por outro atomicamente: o rename falha se o
    // destino existir. Remover antes abre uma janela sem arquivo nenhum — mas
    // ela dura microssegundos e o `.tmp` ja esta inteiro no cartao, entao o
    // pior caso e perder o contador, nunca ler lixo como se fosse dado bom.
    SD_MMC.remove(path);
    if (!SD_MMC.rename(tmp.c_str(), path)) {
        SD_MMC.remove(tmp.c_str());
        return false;
    }
    return true;
}

bool writeBufferAtomic(const char *path, const uint8_t *dados, size_t len) {
    const std::string tmp = std::string(path) + ".tmp";

    File f = SD_MMC.open(tmp.c_str(), FILE_WRITE);
    if (!f) return false;

    // Em blocos, e nao numa chamada so: o driver fatia de qualquer jeito, mas
    // blocos de 16 KB dao ao resto do sistema (toque, desenho) espaco entre as
    // esperas do cartao — uma escrita de 600 KB inteirica segura o laco por
    // segundos sem respirar.
    size_t escritos = 0;
    while (escritos < len) {
        const size_t bloco = std::min(len - escritos, (size_t)16384);
        if (f.write(dados + escritos, bloco) != bloco) break;
        escritos += bloco;
    }
    f.flush();
    f.close();
    if (escritos != len) {
        SD_MMC.remove(tmp.c_str());
        return false;
    }

    SD_MMC.remove(path);
    if (!SD_MMC.rename(tmp.c_str(), path)) {
        SD_MMC.remove(tmp.c_str());
        return false;
    }
    return true;
}

}
