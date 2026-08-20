#pragma once
#include <cstddef>
#include <cstdint>

// O indice do `assets.bin`: onde cada sprite mora dentro do blob da particao.
//
// AQUI E NAO NO src/ porque isto e aritmetica sobre bytes e nao depende de nada
// do Arduino — e a regra da casa manda que decisao testavel no PC nasca em
// lib/. Quem toca a particao e descomprime e src/assets.cpp.
//
// ZERO COPIA: `AssetIdx` aponta para dentro do buffer de quem chamou, que
// precisa continuar vivo. A tabela do painel tem 62 entradas de 36 bytes — 2 KB
// que nao vale a pena duplicar.
struct AssetEntry {
    uint32_t offset     = 0;   // do inicio do arquivo
    uint32_t comprimido = 0;
    uint32_t cru        = 0;
};

struct AssetIdx {
    bool           valid = false;
    uint16_t       count = 0;
    const uint8_t *tabela = nullptr;   // primeira entrada
};

const size_t ASSET_NOME      = 24;
const size_t ASSET_ENTRADA   = ASSET_NOME + 12;   // nome + 3 u32
const size_t ASSET_CABECALHO = 12;

// Le cabecalho e valida a tabela inteira contra `len`. Invalido quando o magic
// nao bate, a versao e outra, ou a tabela nao cabe no que foi lido.
AssetIdx parseAssetIdx(const uint8_t *buf, size_t len);

// Busca binaria por nome. Aceita "/clawd/nome.clw" ou "nome" — o firmware pede
// com caminho completo, o container guarda so o nome.
bool acharAsset(const AssetIdx &idx, const char *path, AssetEntry &out);
