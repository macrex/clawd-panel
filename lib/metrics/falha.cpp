#include "falha.h"

const char *motivoDaFalha(int httpCode, int duracaoMs, bool wifiOk) {
    // O radio vem primeiro: sem ele, o codigo da ultima tentativa fala de um
    // mundo que nao existe mais.
    if (!wifiOk) return "SEM WIFI";

    // 4xx e 5xx: a API esta VIVA e respondeu errado. E a unica familia em que
    // olhar o servidor resolve — nas outras nao ha servidor para olhar.
    if (httpCode >= 400) return "ERRO NA API";

    if (httpCode == HTTP_LEITURA_LENTA) return "API TRAVADA";
    if (httpCode == HTTP_URL_RUIM)      return "URL RUIM";

    if (httpCode == HTTP_RECUSADA || httpCode == HTTP_PERDIDA) {
        // Respondeu rapido: alguem do outro lado mandou um RST, entao a maquina
        // esta de pe e foi o processo da API que morreu. E a unica leitura
        // segura das duas.
        //
        // Demorou: NINGUEM atendeu, e ai o painel nao sabe quem esta fora. Foi
        // medido na placa — com a API derrubada no Windows a tentativa consome
        // o timeout inteiro, porque o firewall descarta o SYN em silencio em vez
        // de recusar a porta. Chamar isso de "PC FORA" mandaria procurar a
        // maquina errada. "SEM CONTATO" e o texto de sempre, e ele e verdade.
        return duracaoMs < LIMIAR_RESPOSTA_MS ? "API FORA" : "SEM CONTATO";
    }

    // Codigo que este firmware nao sabe ler (200 com payload ruim, 0 de nenhuma
    // tentativa ainda, erro novo do core). O texto de sempre e honesto: ha algo
    // errado e o painel nao sabe dizer o que.
    return "SEM CONTATO";
}
