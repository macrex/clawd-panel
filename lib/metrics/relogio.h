#pragma once
#include <string>
#include "status.h"

// O tempo visto pela PLACA, e nao pelo servidor.
//
// Ate aqui as duas perguntas de tempo do painel — "que horas sao" e "quanto
// falta para a janela virar" — eram respondidas pela API: ela mandava
// `clock.hm` pronto e `week_resets_dh` pronto, e o firmware so imprimia. Isso
// tinha uma consequencia que so aparecia quando o servidor calava: os dois
// numeros CONGELAVAM. Fechar o Claude Code parava o relogio do painel, o que
// nao e um dado velho — e um relogio errado, que e pior, porque ele nao parece
// velho.
//
// O que mora aqui e so a aritmetica, e ela e pura de proposito: e o unico jeito
// de ter teste. A leitura do relogio do sistema (SNTP) e a configuracao de fuso
// ficam do lado da placa, em src/hora.cpp.

// Epoch abaixo do qual a placa nao sabe que dia e. Um ESP32 sem sincronia liga
// em 1970, e um cabecalho anunciando "01/01 QUI" com a mesma confianca de
// sempre e uma mentira silenciosa. 2020-01-01 e folgado o bastante para nunca
// rejeitar uma hora real e apertado o bastante para pegar qualquer resquicio de
// boot.
const long EPOCH_MINIMO = 1577836800L;

// Epoch LOCAL (fuso ja aplicado) -> os tres textos do cabecalho.
//
// `known` falso quando o epoch e anterior a EPOCH_MINIMO, e ai os campos ficam
// vazios: o cabecalho ja sabe esconder a linha nesse caso, porque ele fazia isso
// quando a API antiga nao mandava o bloco.
Clock relogioDe(long epochLocal);

// Segundos -> "3d06h", "4h54m", "39m", "<1m" ou "agora".
//
// A resolucao acompanha o numero, e essa e a correcao: a API formatava a semana
// SEMPRE em dias e horas, entao os ultimos 59 minutos de uma janela de 7 dias
// eram exibidos como "0d00h" — que qualquer um le como "acabou". As duas formas
// de cima sao identicas as da API (fmt_dh, fmt_hm) para a tela nao mudar de
// cara onde ela ja estava certa.
std::string prazoTexto(long segundos);

// Quanto falta AGORA, descontando a idade do dado.
//
// `idadeSeg` e ha quantos segundos este Status foi lido — zero com a rede boa,
// e o tempo desde o ultimo contato quando ela nao esta. Sem este desconto, um
// painel offline mostra o prazo do instante em que perdeu contato ate alguem
// reiniciar a placa.
//
// Nunca devolve negativo: quem le so precisa saber que venceu, e o prazo da
// janela seguinte a placa nao tem como saber sozinha — quem carimba e a API.
long prazoRestante(const Metric &m, long idadeSeg);

// O texto que vai para a TELA, com os dois "nao sei" ja resolvidos.
//
// Existe para o desenho nao ter que distinguir os dois zeros que prazoRestante
// devolve: "a janela venceu" e "nunca houve prazo" sao a mesma conta e recados
// opostos. Sem prazo nenhum a tela mostra o travessao que ela ja mostrava.
std::string prazoDaTela(const Metric &m, long idadeSeg);

// O INSTANTE da virada, escondido depois que ele passou.
//
// `at` e um carimbo absoluto — "7:20pm", "01/08/2026 (Sabado)" — e por isso NAO
// envelhece sozinho: ele continua afirmando a mesma coisa depois de a hora
// chegar, com a mesma cara de dado fresco. O prazo ao lado ja aprendeu a andar
// (prazoDaTela); o instante ficou parado no tempo da API.
//
// No retrato do cartao e pior, e foi de la que o defeito veio: um retrato lido
// dias depois traz `resetsIn` recalculado para zero e o texto do instante
// intacto, entao a tela mostra "vira as 7:20pm" sobre uma janela que virou
// ontem.
//
// Vazio quando a janela venceu ou quando nunca houve prazo. As tres telas que
// desenham o instante ja escondem a linha com `at` vazio — elas faziam isso
// para a API antiga, que nao mandava o campo.
std::string atDaTela(const Metric &m, long idadeSeg);
