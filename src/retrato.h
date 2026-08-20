#pragma once
#include <string>

// O ultimo /status bom, guardado para o proximo boot.
//
// Uma placa que liga com o PC desligado nao tinha nada a dizer: tela "API FORA"
// e fim. Este retrato entra uma vez, no boot, e o painel ao menos mostra o que
// era verdade da ultima vez, com a idade escrita ao lado.
//
// POR QUE NA NVS E NAO NO CARTAO
// Ele morava em `/clawd/ultimo_status.json` e era reescrito a cada cinco
// minutos. A razao de ter saido de la esta em src/storage.h, junto com a
// medicao que derrubou metade dela — em resumo: nao previne o 0x107, como se
// esperava, mas faz o retrato sobreviver ao cartao travado. E ai ele passa a
// cobrir tambem o caso que mais aparece na pratica: boot com o cartao fora do
// ar, que antes era exatamente quando o retrato tambem sumia.
//
// O formato NAO mudou: continua o mesmo JSON de lib/metrics/cache.h. O que
// mudou foi so onde ele fica.
namespace retrato {

// Guarda o JSON serializado. Falso quando a NVS recusou (cheia, ou texto grande
// demais) — o chamador so precisa saber para nao marcar como salvo.
bool guardar(const std::string &json);

// O JSON guardado. Falso quando nunca houve gravacao.
bool ler(std::string &json);

}
