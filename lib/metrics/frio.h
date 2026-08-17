#pragma once
#include <string>
#include "status.h"

// Os blocos que mudam devagar, guardados entre um poll e o proximo.
//
// `works` (o dia), `uso` (de quem foi o custo) e `vitalicio` (a vida inteira)
// mudam quando um TURNO termina, e nao a cada dois segundos. Somados dao ~200
// dos 1459 bytes do payload enxuto e atravessavam a rede 1800 vezes por hora
// sem ter mudado nada.
//
// O acordo com o servidor esta em server/painel.py: ele sempre publica `frio`,
// um resumo do conteudo dos tres. A placa devolve esse resumo no pedido
// seguinte, e quando os dois batem os blocos nao viajam — mas o resto do
// documento vem inteiro, porque 87% dele muda a cada poll.
//
// A invalidacao e automatica, e e por isso que este mecanismo e seguro: o
// resumo cobre o CONTEUDO. Um turno novo muda `works`, muda o resumo, e o bloco
// volta a viajar sem ninguem ter que lembrar de expirar nada. A virada do dia
// entra pela mesma porta.
//
// POR FONTE, e nao um so: com duas APIs configuradas, o master e o PC2 tem cada
// um o seu livro-caixa e o seu resumo. Um so faria a placa mandar para uma
// fonte o resumo que aprendeu da outra — e ai ou o bloco viajaria sempre (caso
// bom) ou viria o do PC errado (caso ruim).
struct BlocosFrios {
    std::string resumo;     // vazio = nao ha nada guardado
    Works       works;
    Uso         uso;
    Vitalicio   vitalicio;
};

// Guarda o que veio, ou devolve o que estava guardado.
//
// Chame com o `Status` recem-parseado, ANTES de qualquer outro uso dele:
//
//   - o payload trouxe os blocos      -> guarda os tres e o resumo novo;
//   - o payload os omitiu e o resumo bate -> preenche `s` com os guardados;
//   - o resumo nao bate, ou nao ha nada guardado -> nao inventa nada. `s` fica
//     como veio, e os `known` falsos fazem as telas esconderem os cards, que e
//     o que elas ja faziam contra uma API antiga.
//
// A pergunta "o payload trouxe os blocos?" e respondida por `known`: os tres o
// tem, e o parse so o liga quando o bloco existe de verdade no JSON.
void aplicarFrios(BlocosFrios &f, Status &s);
