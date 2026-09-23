#pragma once

// O alto-falante da placa, pelo amplificador I2S dela.
//
// O laco decide QUANDO tocar (uma sessao terminou o turno, ou parou pedindo
// resposta) e se o som esta ligado; este modulo so sabe COMO. `tocar` nao
// bloqueia o laco: o toque sai por uma tarefa propria, e o dedo continua sendo
// lido enquanto ele soa.
namespace som {

enum class Aviso {
    Pronto,    // uma sessao terminou o turno
    Espera,    // uma sessao parou pedindo resposta
};

// No setup. Falso = sem audio, e o painel segue mudo sem reclamar.
bool iniciar();

void tocar(Aviso a);

}
