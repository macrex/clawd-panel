#include "assetidx.h"
#include <cstring>

namespace {

uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// "/clawd/mago.clw" -> "mago". Sem alocar: devolve o pedaco e o tamanho.
void chave(const char *path, const char *&ini, size_t &n) {
    ini = path;
    if (const char *barra = strrchr(path, '/')) ini = barra + 1;
    n = strlen(ini);
    const size_t ext = 4;   // ".clw"
    if (n > ext && strcmp(ini + n - ext, ".clw") == 0) n -= ext;
    if (n >= ASSET_NOME) n = ASSET_NOME - 1;
}

}

AssetIdx parseAssetIdx(const uint8_t *buf, size_t len) {
    AssetIdx idx;
    if (!buf || len < ASSET_CABECALHO) return idx;
    if (memcmp(buf, "CLWA", 4) != 0)   return idx;
    if (u16(buf + 4) != 1)             return idx;

    const uint16_t n = u16(buf + 6);
    // A tabela INTEIRA tem que caber no que foi lido. Aceitar uma tabela
    // truncada faria a busca ler lixo como se fosse entrada — e numa particao
    // virgem (tudo 0xFF) isso seriam 65535 entradas de nada.
    if ((size_t)n * ASSET_ENTRADA + ASSET_CABECALHO > len) return idx;

    idx.valid  = true;
    idx.count  = n;
    idx.tabela = buf + ASSET_CABECALHO;
    return idx;
}

bool acharAsset(const AssetIdx &idx, const char *path, AssetEntry &out) {
    if (!idx.valid || !path) return false;

    const char *nome = nullptr;
    size_t      n    = 0;
    chave(path, nome, n);

    int lo = 0, hi = (int)idx.count - 1;
    while (lo <= hi) {
        const int      meio = lo + (hi - lo) / 2;
        const uint8_t *e    = idx.tabela + (size_t)meio * ASSET_ENTRADA;
        // Compara so os `n` primeiros e exige que o guardado termine ali: sem
        // isso "mago" casaria com "magnetic_regent".
        const char *guardado = (const char *)e;
        int cmp = strncmp(nome, guardado, n);
        if (cmp == 0 && guardado[n] != '\0') cmp = -1;

        if (cmp == 0) {
            out.offset     = u32(e + ASSET_NOME);
            out.comprimido = u32(e + ASSET_NOME + 4);
            out.cru        = u32(e + ASSET_NOME + 8);
            return true;
        }
        if (cmp < 0) hi = meio - 1;
        else         lo = meio + 1;
    }
    return false;
}
