#pragma once
#include "status.h"

// Reconhece a VIRADA de uma janela de limite comparando dois polls seguidos.
//
// Morava dentro do laco do main.cpp, que nenhum teste alcanca — e foi
// exatamente ali que o gatilho ficou escrito de um jeito que nunca dispara no
// caminho real (so no ensaio do duplo toque, que arma a tela por fora).
//
// O estado e o do poll ANTERIOR: uma virada e uma borda, e sem o valor de
// antes nao ha borda nenhuma.
struct ResetWatch {
    int  sessaoIn  = 0;         // prazo da janela de 5h no poll anterior
    int  semanaIn  = 0;         // idem, 7 dias
    bool sessaoZerada = false;  // o `session_inferred` do poll anterior
    bool visto     = false;     // ja houve um poll com o que comparar
};

// "SESSAO", "SEMANA" ou nullptr. Atualiza `w` com o poll que acabou de chegar.
//
// O primeiro poll nunca dispara: no boot nao ha valor anterior, e um "resetou"
// ali seria chute.
const char *detectarReset(ResetWatch &w, const Status &s);

// ---- A virada que a placa ve SOZINHA ----
//
// `detectarReset` compara dois polls, entao ele so existe enquanto ha polls. Com
// o servidor fora o painel congelava sem saber que a cota tinha voltado — e a
// virada de uma janela de 5h e exatamente a noticia que interessa a quem esta
// esperando poder trabalhar de novo.
//
// Desde a leva do relogio o prazo anda na propria placa (ver relogio.h), entao
// ela sabe a hora em que ele chega a zero sem perguntar a ninguem.
//
// Isto roda SO com dado velho. Com a API viva quem avisa e ela: o servidor se
// recusa a inventar o horario de uma janela que ninguem carimbou, entao o prazo
// tambem cai a zero na virada e os dois detectores disparariam juntos — a mesma
// tela, duas vezes.
struct PrazoWatch {
    long sessaoAntes = -1;    // -1 = ainda nao ha valor anterior
    long semanaAntes = -1;
};

// "SESSAO", "SEMANA" ou nullptr. Os prazos sao os que a placa acabou de contar
// (`prazoRestante`), ja descontados da idade do dado.
//
// So a BORDA conta: positivo virando zero. Um prazo que ja estava em zero
// quando esta funcao viu pela primeira vez nao dispara — no boot com dado velho
// nao ha como saber se a janela virou agora ou ontem.
const char *virouSemApi(PrazoWatch &w, long sessaoAgora, long semanaAgora);
