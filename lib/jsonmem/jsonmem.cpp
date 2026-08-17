#include "jsonmem.h"
#include <cstdlib>

#ifdef BOARD_HAS_PSRAM
#include <esp_heap_caps.h>

namespace {

struct AlocadorPsram : ArduinoJson::Allocator {
    void *allocate(size_t n) override {
        void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
        // PSRAM cheia ou fragmentada NAO pode custar o parse: o poll cai no heap
        // interno, que e exatamente o que ele fazia antes. Uma otimizacao de
        // memoria que derruba o painel quando falha nao vale o que economiza.
        return p ? p : std::malloc(n);
    }

    // `heap_caps_free` e `free` sao a mesma porta no ESP-IDF: os dois acham o
    // heap dono do bloco. Por isso o fallback acima nao precisa ser lembrado
    // aqui — o ponteiro do heap interno volta certo por este caminho.
    void deallocate(void *p) override { heap_caps_free(p); }

    void *reallocate(void *p, size_t n) override {
        void *q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM);
        // Falhou: `p` continua VALIDO (e o contrato do realloc), entao a segunda
        // tentativa no heap interno parte dele em vez de vazar o bloco.
        return q ? q : std::realloc(p, n);
    }
};

AlocadorPsram g_alocador;

}   // namespace

#else

namespace {

// O padrao da biblioteca, escrito a mao. Chamar
// `ArduinoJson::detail::DefaultAllocator::instance()` daria no mesmo, mas
// `detail` e o porao da biblioteca e nao promete estabilidade entre versoes.
struct AlocadorPadrao : ArduinoJson::Allocator {
    void *allocate(size_t n) override { return std::malloc(n); }
    void  deallocate(void *p) override { std::free(p); }
    void *reallocate(void *p, size_t n) override { return std::realloc(p, n); }
};

AlocadorPadrao g_alocador;

}   // namespace

#endif

ArduinoJson::Allocator *alocadorJson() { return &g_alocador; }
