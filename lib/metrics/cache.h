#pragma once
#include <string>
#include "status.h"

// O retrato que sobrevive a um reboot sem servidor.
//
// O painel ja aguentava a API cair: `last` fica na memoria e a tela continua
// mostrando o ultimo valor bom, esmaecido e com a idade dita. O que ele NAO
// aguentava era o reboot — tirar da tomada com o PC desligado apagava tudo, e a
// placa voltava com "API FORA" e mais nada. O cartao resolve isso.
//
// Nao e o /status inteiro, e isso e uma escolha e nao economia. O que fica e o
// que ainda SIGNIFICA alguma coisa com a maquina desligada:
//
//   fica   os limites de 5h e 7 dias, o livro-caixa do dia e o vitalicio —
//          numeros sobre o passado, que continuam verdadeiros;
//   sai    a lista de agentes, o bloqueio, o contexto e o clima — retratos do
//          AGORA. Uma sessao viva num PC desligado nao e um dado velho, e uma
//          afirmacao falsa, e o painel nao pode fazer isso.
//
// Os prazos sao gravados como INSTANTE (epoch), e nao como "faltam 3600 s". O
// relativo obrigaria a saber quanto tempo a placa passou desligada, que e
// justamente o que ela nao sabe; o absoluto continua certo por si.

// O texto a gravar no cartao. `agoraLocal` e o epoch local do momento (ver
// hora::agoraLocal). Vazio quando nao ha hora — sem relogio nao da para
// carimbar o retrato, e um retrato sem data nao pode ser envelhecido depois.
std::string serializarCache(const Status &s, long agoraLocal);

// Reconstroi o retrato. Devolve false quando o texto nao e um cache legivel ou
// quando falta hora para envelhece-lo.
//
// `idadeSeg` sai com a idade do retrato em segundos, que e o que a tela usa
// para dizer "isto e de 14 minutos atras" e para descontar dos prazos.
bool lerCache(const char *json, long agoraLocal, Status &out, int &idadeSeg);
