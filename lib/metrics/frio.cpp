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
        f.historico = s.historico;
        return;
    }

    // Vieram omitidos. So reaproveita com o resumo BATENDO — e ele so bate
    // porque cobre o conteudo, entao qualquer mudanca nos blocos derruba a
    // igualdade sozinha.
    if (f.resumo.empty() || f.resumo != s.frio) return;

    s.works     = f.works;
    s.uso       = f.uso;
    s.vitalicio = f.vitalicio;
    // So quando o payload nao o trouxe. Fora do resumo ele viaja sempre, e o
    // que acabou de chegar vale mais do que o guardado; dentro, ele some junto
    // com os outros e volta daqui.
    if (!s.historico.known) s.historico = f.historico;
}
