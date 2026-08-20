#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// Os sprites que moram na FLASH, e nao no cartao.
//
// POR QUE ELES SAIRAM DO CARTAO
// O cartao desta placa trava em 0x107 e nao volta por software: dreno de
// pinos, dreno encadeado, pulsos de clock e parar de escrever nele foram os
// quatro medidos e reprovados, e a JC3248W535 nao tem GPIO que corte o VDD do
// cartao (ver src/storage.h). Enquanto os sprites moram la, cada travamento
// deixa o painel desenhando buracos ate alguem desligar a bateria.
//
// O QUE TORNOU ISSO POSSIVEL
// Pixel art comprime muito: a arte que o firmware referencia sao 8,1 MB crus e
// 2,7 MB em deflate. E o ESP32-S3 traz `tinfl_decompress_mem_to_mem` na ROM,
// entao descomprimir custa zero byte de flash e nenhuma dependencia nova.
//
// A FLASH MANDA. Quem procura um sprite passa por aqui primeiro e so cai para o
// cartao quando nao acha (ver a cascata em src/storage.cpp). O cartao continua
// util para o que nao coube — os 99 bichos de nivel, por exemplo.
namespace assets {

// Le o indice da particao. Falso quando ela esta virgem (nenhum `assets.bin`
// gravado) — e ai tudo cai para o cartao, que e o comportamento antigo.
bool begin();

// Descomprime para uma std::string. Falso quando o nome nao esta no blob.
bool ler(const char *path, std::string &out);

// Descomprime para um buffer novo na PSRAM. Devolve nullptr quando nao acha; o
// chamador vira dono do buffer e o libera com free().
uint8_t *lerParaPsram(const char *path, size_t &len);

}
