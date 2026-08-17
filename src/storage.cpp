#include "storage.h"
#include "board_pins.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <algorithm>

namespace storage {

// Montar o cartao mais de uma vez antes de desistir.
//
// Visto em 14/08 e outra vez em 16/08: depois de uma gravacao de firmware, o
// `send_op_cond` passou a devolver 0x107 (timeout — o cartao nao responde ao
// comando de inicializacao) em TODO boot, e a placa ficou parada em SEM CONFIG
// ate a energia ser cortada de verdade. Com bateria, reset por software nao
// corta a alimentacao do cartao: ele fica no mesmo estado ruim de antes.
//
// A FREQUENCIA nao entra na conta, e isso nao e esquecimento: a inicializacao
// SEMPRE roda em 400 kHz (SDMMC_FREQ_PROBING) qualquer que seja o parametro —
// ele so vale depois que o cartao respondeu. Baixar a frequencia nao teria
// mudado nada neste erro, e cobraria o boot inteiro em cartao lento.
const int      SD_TENTATIVAS = 5;
const uint32_t SD_ESPERA_MS  = 300;
// O dreno CRESCE a cada tentativa: 200, 400, 800, 1600 ms.
//
// Medido nesta placa, em dois boots seguidos com o cartao no mesmo estado ruim:
// 200 ms resolveram numa vez e nao na outra. O tempo de descarga depende de
// quanta carga sobrou nos capacitores do modulo, e isso varia com ha quanto
// tempo a placa esta ligada — entao um numero fixo sempre vai errar em algum
// boot. Dobrar ate 1,6 s cobre a folga sem custar nada no caso normal, que e a
// PRIMEIRA tentativa montando sem dreno nenhum.
const uint32_t SD_DRENO_MS   = 200;

// Drena a alimentacao PARASITA do cartao.
//
// Esta e a peca que faltava, e ela explica por que tres tentativas identicas
// nunca resolveram: `SD_MMC.end()` desliga o host, mas deixa CLK, CMD e D0
// ALTOS. Um cartao SD se alimenta pelos diodos de protecao dos pinos de dados
// quando o VDD some — entao ele continua ligado, no mesmo estado ruim, e a
// tentativa seguinte encontra exatamente o que a anterior deixou.
//
// Era por isso que so o corte de energia de verdade funcionava: tirar o USB e
// desligar a bateria e a unica coisa que levava os pinos a zero junto.
//
// Levando os tres a LOW por um tempo, o cartao perde essa fonte e tem chance de
// resetar sem ninguem chegar perto da placa.
//
// MEDIDO, e o numero e honesto: em tres boots com o cartao travado em 0x107, o
// dreno resolveu UM. Ele nao substitui o corte de energia de verdade — a
// alimentacao parasita e uma das causas, nao a unica. Fica porque custa zero no
// caso normal (a primeira tentativa monta sem dreno nenhum) e porque um boot
// salvo em tres e a diferenca entre o painel voltar sozinho e alguem ter que
// desligar a bateria.
void drenarCartao(uint32_t ms) {
    const int pinos[] = {SD_CLK, SD_CMD, SD_D0};
    for (int p : pinos) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
    delay(ms);
    // De volta a entrada antes de devolver os pinos ao periferico: deixar um
    // push-pull em zero brigando com o driver do SD_MMC seria trocar um
    // problema por outro.
    for (int p : pinos) pinMode(p, INPUT);
    Serial.printf("sd: dreno de %lums\n", (unsigned long)ms);
}

bool begin() {
    if (!SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0)) return false;

    for (int i = 0; i < SD_TENTATIVAS; i++) {
        // A partir da SEGUNDA, o dreno entra. Se a primeira falhou, o cartao
        // esta em estado ruim, e repetir o mesmo caminho so repete a mesma
        // falha — foi o que tres tentativas identicas provaram na pratica, em
        // duas ocasioes diferentes.
        if (i) {
            drenarCartao(SD_DRENO_MS << (i - 1));
            // `setPins` de novo: o dreno mexeu nos pinos por fora do driver.
            if (!SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0)) return false;
        }

        if (SD_MMC.begin("/sdcard", true)) {   // 1-bit
            if (i) Serial.printf("sd: montou na tentativa %d (dreno resolveu)\n",
                                 i + 1);
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
