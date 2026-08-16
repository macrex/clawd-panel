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
