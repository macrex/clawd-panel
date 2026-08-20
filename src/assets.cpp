#include "assets.h"
#include "assetidx.h"
#include <Arduino.h>
#include <cstring>
#include <esp_partition.h>
// <miniz.h> e nao <rom/miniz.h>: o segundo era o caminho do IDF 4.x. A funcao e
// a mesma da ROM (`tinfl_decompress_mem_to_mem = 0x4000084c` no
// esp32s3.rom.ld), so o header mudou de lugar.
#include <miniz.h>

namespace assets {
namespace {

// O subtipo 0x40 da tabela: qualquer valor fora dos reservados serve, e este
// so precisa casar com partitions_clawd.csv.
const esp_partition_subtype_t SUBTIPO = (esp_partition_subtype_t)0x40;

const esp_partition_t *g_part = nullptr;

// A TABELA fica na RAM; os dados ficam na flash e sao lidos sob demanda.
//
// A tabela sao 62 entradas de 36 bytes — 2 KB, lidos uma vez no boot. Os dados
// sao 2,7 MB e seria absurdo copia-los; cada sprite e lido no instante em que
// alguem o pede, com uma leitura de particao e uma descompressao.
uint8_t *g_tabela = nullptr;
AssetIdx g_idx;

}   // namespace

bool begin() {
    g_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, SUBTIPO, "assets");
    if (!g_part) {
        Serial.println("assets: particao ausente");
        return false;
    }

    // Cabecalho primeiro, para saber o tamanho da tabela antes de aloca-la.
    // Nao da para validar com `parseAssetIdx` aqui: ele exige que a tabela
    // INTEIRA caiba no buffer, e o buffer ainda e so o cabecalho. A validacao
    // de verdade vem depois, com a tabela lida.
    uint8_t cab[ASSET_CABECALHO];
    if (esp_partition_read(g_part, 0, cab, sizeof(cab)) != ESP_OK) return false;

    if (memcmp(cab, "CLWA", 4) != 0) {
        // Virgem (0xFF) ou de outro formato: nao e erro, e ausencia. Acontece
        // em placa que nunca recebeu o assets.bin, e o painel segue lendo tudo
        // do cartao como sempre leu.
        Serial.println("assets: particao sem blob — tudo vem do cartao");
        return false;
    }

    const uint16_t n   = (uint16_t)(cab[6] | (cab[7] << 8));
    const size_t   tam = ASSET_CABECALHO + (size_t)n * ASSET_ENTRADA;

    g_tabela = (uint8_t *)malloc(tam);
    if (!g_tabela) return false;
    if (esp_partition_read(g_part, 0, g_tabela, tam) != ESP_OK) {
        free(g_tabela);
        g_tabela = nullptr;
        return false;
    }

    g_idx = parseAssetIdx(g_tabela, tam);
    if (!g_idx.valid) {
        free(g_tabela);
        g_tabela = nullptr;
        return false;
    }

    Serial.printf("assets: %u sprites na flash\n", (unsigned)g_idx.count);
    return true;
}

namespace {

// Descomprime com o estado do tinfl no HEAP, e nao na pilha.
//
// POR QUE NAO `tinfl_decompress_mem_to_mem`, QUE SERIA UMA LINHA
// Porque ela derruba a placa. Aquela funcao declara um `tinfl_decompressor`
// como variavel LOCAL, e essa struct carrega as tres tabelas de Huffman do
// deflate: cada uma tem um look-up de 1024 entradas mais uma arvore de 1152,
// as duas de 16 bits, e sao ~14 KB somados. A `loopTask` do Arduino tem 8 KB
// (ARDUINO_LOOP_STACK_SIZE, no core), entao a primeira descompressao estoura a
// pilha antes de descomprimir coisa alguma.
//
// MEDIDO NA PLACA, e o sintoma nao aponta para ca: a serial deu
// `Guru Meditation Error: Core 1 panic'ed (Unhandled debug exception)` com
// `Stack canary watchpoint triggered (loopTask)`, e a placa entrou em boot
// loop. Nada na mensagem fala em compressao — o backtrace morre dentro da ROM.
//
// A saida nao e aumentar a pilha de todo o firmware por causa de uma funcao
// que roda por alguns milissegundos: e chamar a API de baixo nivel com o
// estado alocado no heap, que e exatamente o que a versao `mem_to_mem` faz,
// menos o lugar onde ela poe a struct. Sai pelo mesmo caminho que entrou.
size_t inflarNoHeap(uint8_t *destino, size_t destinoLen, const uint8_t *comp,
                    size_t compLen) {
    tinfl_decompressor *r =
        (tinfl_decompressor *)malloc(sizeof(tinfl_decompressor));
    if (!r) return 0;
    tinfl_init(r);

    size_t entrou = compLen;
    size_t saiu   = destinoLen;
    // Deflate RAW: sem TINFL_FLAG_PARSE_ZLIB_HEADER, porque o gerador usa wbits
    // negativo. O flag de saida nao-circular vale porque `destino` ja tem o
    // tamanho cru inteiro — sem ele o tinfl trabalharia numa janela de 32 KB e
    // seria mais lento por nada. E sem HAS_MORE_INPUT: o buffer comprimido
    // inteiro ja esta aqui.
    const tinfl_status st = tinfl_decompress(
        r, comp, &entrou, destino, destino, &saiu,
        TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);

    free(r);
    return (st == TINFL_STATUS_DONE) ? saiu : 0;
}

// O caminho comum das duas leituras: le o comprimido da flash e descomprime no
// destino que ja veio dimensionado.
bool descomprimir(const char *path, uint8_t *destino, size_t destinoLen,
                  const AssetEntry &e) {
    // PSRAM e nao a RAM interna, pela mesma razao que separou readFileToPsram de
    // readFile (ver src/storage.h): o maior comprimido do blob e o
    // `sp_cartman_reset`, com 154 KB. Pedir isso a RAM interna, que e escassa e
    // ja sustenta a pilha e o Wi-Fi, falharia justo nos sprites maiores — e a
    // falha seria silenciosa, caindo de volta para o cartao que a particao veio
    // substituir.
    uint8_t *comp = (uint8_t *)ps_malloc(e.comprimido);
    if (!comp) return false;

    bool ok = esp_partition_read(g_part, e.offset, comp, e.comprimido) == ESP_OK;
    if (ok) {
        // Deflate RAW: sem TINFL_FLAG_PARSE_ZLIB_HEADER, porque o gerador usa
        // wbits negativo. O flag de saida nao-circular vale porque `destino` ja
        // tem o tamanho cru inteiro — sem ele o tinfl trabalharia numa janela
        // de 32 KB e seria mais lento por nada.
        const size_t saiu = inflarNoHeap(destino, destinoLen, comp,
                                         e.comprimido);
        ok = (saiu == e.cru);
        if (!ok)
            Serial.printf("assets: %s descomprimiu %u de %u\n", path,
                          (unsigned)saiu, (unsigned)e.cru);
    }
    free(comp);
    return ok;
}

}   // namespace

bool ler(const char *path, std::string &out) {
    AssetEntry e;
    if (!g_idx.valid || !acharAsset(g_idx, path, e)) return false;

    // Um teto para o que pode ir para a RAM interna.
    //
    // Esta funcao devolve uma std::string, que mora na RAM interna — 327 KB no
    // total, sustentando pilha e Wi-Fi. O maior sprite do blob tem 649 KB
    // descomprimidos, e pedir isso aqui nao degrada: aborta. Quem le sprite usa
    // `lerParaPsram`, e e por isso que as duas funcoes existem separadas (a
    // mesma razao que separou `readFile` de `readFileToPsram` em storage.h).
    //
    // 64 KB e folgado para o que legitimamente passa por aqui — config e JSON
    // pequeno — e barra o resto ANTES de alocar.
    const size_t TETO_RAM_INTERNA = 64 * 1024;
    if (e.cru > TETO_RAM_INTERNA) {
        Serial.printf("assets: %s tem %u bytes, grande demais para a RAM "
                      "interna — use lerParaPsram\n",
                      path, (unsigned)e.cru);
        return false;
    }

    out.assign(e.cru, '\0');
    if (!descomprimir(path, (uint8_t *)&out[0], e.cru, e)) {
        out.clear();
        return false;
    }
    return true;
}

uint8_t *lerParaPsram(const char *path, size_t &len) {
    len = 0;
    AssetEntry e;
    if (!g_idx.valid || !acharAsset(g_idx, path, e)) return nullptr;

    uint8_t *buf = (uint8_t *)ps_malloc(e.cru);
    if (!buf) return nullptr;

    if (!descomprimir(path, buf, e.cru, e)) {
        free(buf);
        return nullptr;
    }
    len = e.cru;
    return buf;
}

}   // namespace assets
