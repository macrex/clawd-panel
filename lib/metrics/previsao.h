#pragma once
#include <string>
#include "status.h"

// A PREVISAO de uma janela de limite no ritmo de agora: com quanto ela fecha,
// ou em quantos segundos ela bate 100%.
//
// A conta e a mais simples que responde a pergunta: o gasto dividido pelo tempo
// que ja correu e a velocidade, e ela e suposta constante ate o fim da janela.
// Nao e modelo de consumo — quem trabalha em bloco queima a janela em rajadas —,
// e e por isso que ela so sai com BASE: no comeco da janela uma rajada de dez
// minutos projetaria o triplo do que vai acontecer.
struct Previsao {
    enum Tipo { Nenhuma, Fecha, Estoura };
    Tipo tipo     = Nenhuma;
    int  pctFinal = 0;    // Fecha: o percentual com que a janela vira (< 100)
    long seg100   = 0;    // Estoura: segundos daqui ate 100%, antes da virada
};

// Quanto da janela tem que ter corrido para haver previsao, em porcento. 10% e
// meia hora na janela de 5h e ~17h na semana. E o botao de calibrar: se a
// previsao pular demais no comeco da janela, e aqui que ela sobe.
const int PREVISAO_BASE_PCT = 10;

// Sem previsao (Nenhuma) quando nao ha o que projetar: janela desconhecida ou
// em memoria (numero velho nao tem ritmo de agora), sem prazo, recem-aberta,
// gasto zero (nao ha velocidade), ou ja em 100% (o numero vermelho ja diz).
Previsao preverJanela(const Metric &m, long janelaSeg);

// A previsao escrita para a tela: "fecha 72%", ou "100% Sex 11:05h" (o dia so
// nas janelas maiores que um dia; na de 5h, "100% 20:10h"). Vazio sem previsao.
// `perigo` diz se e o estouro, que a tela pinta de vermelho. Dia e hora saem do
// relogio do cabecalho, pela mesma conta do instante do reset.
std::string textoDaPrevisao(const Metric &m, long janelaSeg, const Clock &rel,
                            bool &perigo);
