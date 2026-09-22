#include "protocolo.h"
#include <cstdio>

size_t escreverComPrazo(const uint8_t *buf, size_t n, size_t bloco, uint32_t fimMs,
                        const Escritor &escrever,
                        const std::function<uint32_t()> &agora) {
    size_t enviado = 0;
    while (enviado < n && (int32_t)(agora() - fimMs) < 0) {
        const size_t pedaco = n - enviado < bloco ? n - enviado : bloco;
        const size_t r = escrever(buf + enviado, pedaco);
        if (!r) break;
        enviado += r;
    }
    return enviado;
}

uint32_t crc32Passo(uint32_t crc, const uint8_t *dados, size_t n) {
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc ^= dados[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (-(int32_t)(crc & 1)));
    }
    return ~crc;
}

std::string jsonEscapado(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    return out;
}

bool pedidoNovo(IdsPorFonte &s, int origem, long id) {
    const int fonte = (origem == 1) ? 1 : 0;
    if (s.visto[fonte] && s.ultimo[fonte] == id) return false;
    s.visto[fonte]  = true;
    s.ultimo[fonte] = id;
    return true;
}
