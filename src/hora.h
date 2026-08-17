#pragma once
#include "app_config.h"
#include "status.h"

// O relogio da PLACA, sincronizado pela internet.
//
// Ate aqui a hora do cabecalho vinha inteira da API, dentro do mesmo /status
// que traz as sessoes. Isso e economico e foi a escolha certa por muito tempo —
// a placa nao tem relogio de bateria, ja conversa com a API a cada dois
// segundos e a hora chegava de carona.
//
// O problema apareceu no dia em que o servidor calou: sem payload, o relogio
// PARA. E ele nao para de um jeito visivel, como um dado que esmaece; ele fica
// mostrando 14:32 as 15:10, com a mesma cara de sempre. Um numero velho que se
// parece com um numero fresco e a unica coisa que um painel nao pode fazer.
//
// A troca e simples: o ESP32 tem contador proprio, e um SNTP no boot basta para
// carimba-lo. Dai em diante a hora anda sozinha, com a rede fora, com o Claude
// Code fechado, com a API desligada — e so nao anda se faltar energia, que e
// quando o painel tambem nao esta na mesa.
//
// O FUSO nao vem da internet, vem do cartao (config.json). Fuso e onde o painel
// mora, e isso nao muda; ir buscar fora seria trocar uma dependencia de rede por
// outra para responder uma pergunta que a mesa ja responde.
namespace hora {

// Arma o SNTP. Nao bloqueia e nao espera Wi-Fi: o cliente do lwIP tenta sozinho
// assim que a interface sobe, e continua tentando se falhar.
void begin(const AppConfig &c);

// A placa ja tem uma hora utilizavel? Enquanto for falso, quem pergunta as
// horas deve usar o que a API mandou.
//
// "Utilizavel" e nao "veio do NTP": o carimbo da API (ver `semear`) vale tanto
// quanto, e quem chama sempre quis saber a mesma coisa — se da para carimbar um
// retrato e envelhece-lo depois.
bool sincronizada();

// Acerta o relogio pelo carimbo da API, se ele ainda nao estiver acertado.
//
// Existe para a rede que tem servidor e nao tem internet — a de casa com o
// roteador de pe e o link caido, ou a que so resolve nomes locais. Ali o SNTP
// nao alcanca o pool e a placa ficaria sem hora; sem hora ela nao consegue
// dizer de quando e o retrato do cartao, e sem isso o cache inteiro nao entra
// (ver lib/metrics/cache.h). Um segundo de imprecisao no carimbo da API nao
// muda nada disso.
//
// Nao faz nada quando ja ha hora: o SNTP e mais preciso e continua mandando
// quando chega. `epochUtc <= 0` (API antiga, sem o campo) tambem nao faz nada.
void semear(long epochUtc);

// Epoch com o fuso JA aplicado, ou 0 sem sincronia. E o formato que
// `relogioDe()` espera (ver lib/metrics/relogio.h).
long agoraLocal();

// ---- O piso guardado na NVS ----
//
// O ultimo instante que esta placa viu, gravado na memoria interna do chip. Ele
// sobrevive a queda de energia E ao cartao que nao monta — que nesta placa nao e
// hipotese: o `0x107` ja travou o SD duas vezes, e com ele vai embora o retrato
// do cache junto com tudo o mais que mora la.
//
// O que ele NAO e: uma fonte de hora. A placa nao tem relogio de bateria, entao
// o piso mais o uptime nao dizem que horas sao — dizem que horas eram quando ela
// desligou. Acender o cabecalho com isso seria o defeito que este modulo inteiro
// existe para evitar: um numero velho com cara de numero fresco. Por isso
// `sincronizada()` continua falsa e o cache continua fora ate o SNTP ou a API
// falarem.
//
// O que ele E: o limite inferior do que a placa aceita como "agora". Um carimbo
// anterior a ele e erro de quem mandou, e passava sem discussao.

// Le o piso da NVS. Zero quando nunca houve gravacao (placa nova, NVS apagada).
long piso();

// Grava o instante atual como piso novo. Nao faz nada sem hora sincronizada —
// gravar 1970 apagaria o piso bom com um valor pior.
//
// Chame ESPACADO: a NVS tem vida finita de escrita, e o laco roda a 50 Hz.
void guardarPiso();

// O cabecalho pronto: o relogio da placa quando ele existe, e o da API quando
// nao. A ordem e essa de proposito — o da placa e o unico que anda quando o
// outro lado cala, e e por isso que ele existe.
Clock daTela(const Clock &daApi);

}  // namespace hora
