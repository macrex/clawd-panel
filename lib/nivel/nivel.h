#pragma once
#include <stdint.h>

// O nivel do Clawd: 1 a 99, crescendo com o uso real do Claude Code.
//
// Este modulo NAO sabe desenhar nem falar com a rede. Recebe numeros, devolve
// numeros, e le/grava um arquivo pequeno no cartao. E a unica parte do recurso
// com aritmetica que merece teste, e assim ela fica testavel sem placa.
namespace nivel {

// Os quatro numeros da calibracao, JUNTOS de proposito: recalibrar exige mexer
// em mais de um, e cacar constante espalhada e como se erra isso.
//
// Os pesos nao sao gosto. Saem do ritmo medido no livro-caixa (38 dias, 18,7
// turnos/dia, US$ 65,31/dia) e de 24 h/dia de contato, confirmadas no PC com
// 235 h de uptime continuo. Com eles os tres contadores contribuem
// 32% / 34% / 33% — que e a razao de existirem tres em vez de um. Um peso
// escolhido a esmo faria um caminho dominar e os outros dois viravam enfeite.
//
// XP_99 vem de 180 dias nesse ritmo: 57,4 XP/dia x 180 = 10.332, arredondado.
constexpr float PESO_TURNO = 1.0f;
constexpr float PESO_CUSTO = 0.30f;
constexpr float PESO_HORA  = 0.8f;
constexpr float XP_99      = 10300.0f;

constexpr int MIN = 1;
constexpr int MAX = 99;

float xp(long turnos, float custoUsd, float horasContato);

// XP -> nivel. A RAIZ e o que produz a sensacao de progressao: como o XP
// exigido cresce ao quadrado, os primeiros niveis passam voando (o primeiro dia
// ja cai no 8) e os ultimos custam semanas.
int calcular(long turnos, float custoUsd, float horasContato);

// O XP necessario para ATINGIR o nivel `n`. Inverso de `calcular`, usado pela
// barra de progresso da quarta tela.
float xpDoNivel(int n);

// O que mora no cartao. So o que APENAS a placa sabe: turnos e custo vem da
// API, que ja os tem inteiros e sobrevive a cartao morto.
struct Estado {
    uint32_t contatoS = 0;   // segundos de tela ligada COM contato vivo
    int      nivelMax = 0;   // trava para o bicho nunca envelhecer para tras

    // A MARCA DA VIRADA, para o XP do dia.
    //
    // "Quanto ganhei hoje" e uma subtracao, e o minuendo nao pode ser
    // calculado: o XP vem de tres contadores com donos diferentes (turnos e
    // custo na API, horas aqui), entao nao ha como perguntar retroativamente
    // quanto valia a meia-noite. So gravando na hora.
    //
    // `dia` e a data que a API mandou quando a marca foi feita, no formato
    // "DD/MM" do bloco `clock`. A placa nao tem relogio de bateria nem fuso —
    // quem sabe que dia e hoje e a API, como no resto do painel.
    float xpNaVirada = 0;
    char  dia[6]     = "";   // "DD/MM", vazio = nunca marcado
};

// Persistencia no cartao. DECLARADA aqui, IMPLEMENTADA em src/nivel_sd.cpp.
//
// A separacao nao e burocracia: `xp`, `calcular` e `xpDoNivel` sao aritmetica
// pura e rodam em qualquer lugar, enquanto isto aqui depende de SD_MMC e so
// existe na placa. Juntas no mesmo .cpp, testar a conta arrastava o cartao
// junto e `pio test -e native` nao ligava.
//
// Como o linker so cobra o que e referenciado, um teste que use so a aritmetica
// liga sem precisar de stub nenhum.
//
// Arquivo ausente ou ilegivel e tratado como zerado: um painel que nao sobe por
// causa de um contador seria pior do que um contador perdido.
bool carregar(Estado &e);
bool salvar(const Estado &e);

// Acerta a marca da virada e devolve o XP ganho HOJE.
//
// Chamar sempre que houver XP e data novos; ela decide sozinha se e virada.
// Devolve true quando a marca MUDOU — quem chama usa isso para saber que
// precisa gravar no cartao.
//
// Tres situacoes, e a diferenca entre elas importa:
//
//   primeira vez     marca sem ter o que comparar, e o dia comeca com 0 XP.
//                    Nao ha como inventar quanto ja se ganhou antes de existir
//                    uma marca.
//   mesmo dia        so subtrai. O XP do dia cresce ao longo do dia.
//   dia diferente    a marca passa a ser o XP de agora, e o dia recomeca do
//                    zero.
//
// XP MENOR que a marca nao vira numero negativo: se a API perder historia, o
// dia mostra 0 em vez de "-40 XP", que nao significaria nada para quem olha.
//
// `dataApi` vazia nao faz nada — sem saber que dia e, marcar seria chutar.
bool virada(Estado &e, float xpAgora, const char *dataApi, float &xpDoDia);

}
