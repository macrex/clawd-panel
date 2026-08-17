#include "frio.h"

void aplicarFrios(BlocosFrios &f, Status &s) {
    // Sem resumo no payload nao ha acordo nenhum: uma API antiga manda os
    // blocos sempre, e guardar o que ela mandou so ocuparia memoria.
    if (s.frio.empty()) return;

    const bool veio = s.works.known || s.uso.known || s.vitalicio.known;

    if (veio) {
        f.resumo    = s.frio;
        f.works     = s.works;
        f.uso       = s.uso;
        f.vitalicio = s.vitalicio;
        return;
    }

    // Vieram omitidos. So reaproveita com o resumo BATENDO — e ele so bate
    // porque cobre o conteudo, entao qualquer mudanca nos blocos derruba a
    // igualdade sozinha.
    if (f.resumo.empty() || f.resumo != s.frio) return;

    s.works     = f.works;
    s.uso       = f.uso;
    s.vitalicio = f.vitalicio;
}
