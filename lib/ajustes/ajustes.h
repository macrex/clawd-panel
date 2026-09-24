#pragma once
#include <cstdint>
#include <string>
#include "layout.h"     // Alvo e dentro(): o mesmo retangulo de toque da casa
#include "status.h"

// O PAINEL DE AJUSTES e o MODO NOITE, sem o Arduino.
//
// Tudo o que aqui mora e uma decisao que da errado em silencio: um degrau de
// brilho que acende o quadrado vizinho, uma janela de noite que nao atravessa a
// meia-noite, um alvo de toque um botao para o lado. Na placa, a unica forma de
// descobrir e tocando no escuro — aqui, e um teste.
namespace ajustes {

// ---- O que o painel guarda na NVS ----
struct Escolhas {
    // -1 = o brilho nunca foi escolhido no painel, e vale o do config.json. O
    // painel so passa a mandar depois do primeiro toque num degrau.
    int  degrau = -1;
    bool som    = true;
    bool noite  = true;
};

// ---- Brilho em degraus ----
// Seis degraus e nao um valor livre: o dedo acerta um quadrado, nao um numero
// de 0 a 255. A tabela e quase geometrica porque o olho le brilho em razao, e
// nao em diferenca — degraus iguais em PWM seriam quatro quase iguais no topo.
const int DEGRAUS = 6;
uint8_t brilhoDoDegrau(int d);        // fora de 0..5 cai na ponta mais proxima
// O degrau mais perto de um PWM qualquer (o `brightness` do config.json), para
// o painel acender os quadrados certos antes de alguem ter escolhido.
int     degrauDoBrilho(uint8_t v);

// O backlight da tela da noite. E a calibracao a mexer na placa: abaixo dele a
// hora some, acima ele volta a iluminar o quarto. Era 8, e na placa a tela lia
// quase invisivel. 11 e o teto: o teste exige que fique abaixo do degrau 0 (12).
const uint8_t BRILHO_NOITE = 11;

// ---- A janela da noite ----
// 23:00 inclusive ate 07:00 exclusive, na hora LOCAL.
const int NOITE_INICIO_H = 23;
const int NOITE_FIM_H    = 7;

// `epochLocal` e o de hora::agoraLocal(): fuso aplicado, e 0 sem hora. Sem hora
// nao e noite — apagar a tela por um relogio que nao sabe que horas sao seria
// a tela escura as tres da tarde.
bool horaDaNoite(long epochLocal);

// A tela deve estar APAGADA agora? O ajuste ligado, a hora na janela, ninguem
// a acordou ha pouco (um toque ou um evento) e NENHUMA SESSAO aberta: com algo
// rodando o painel fica no ar a noite inteira. `semSessao` e a mesma regra que
// poe o Clawd para dormir (view_model::semSessao, ou o servidor calado).
bool telaDeNoite(bool ligado, long epochLocal, bool acordada, bool semSessao);

// ---- Prazos em millis ----
// O prazo `ateMs` ainda nao venceu? 0 = sem prazo. A comparacao e com sinal
// para atravessar a virada do millis(), aos 49 dias de placa ligada.
bool noPrazo(uint32_t ateMs, uint32_t agoraMs);
// Quantos minutos faltam, arredondando para cima: "falta 1m" ate o ultimo
// segundo, e nunca "falta 0m". 0 = vencido ou sem prazo.
int  minutosAte(uint32_t ateMs, uint32_t agoraMs);
// O prazo, ou 0 se ele ja venceu. O laco zera assim os prazos vencidos a cada
// volta: a comparacao com sinal so vale ate 2^31 ms (~24,9 dias) de distancia,
// e um prazo esquecido la voltaria a parecer futuro — o SILENCIO religaria
// sozinho, e a noite deixaria de vir.
uint32_t prazoVivo(uint32_t ateMs, uint32_t agoraMs);

// ---- O som de um poll ----
// Qual aviso tocar pelo que o poll trouxe. Um so por poll, e a ESPERA ganha do
// pronto: alguem parado pedindo resposta e o recado que nao pode se perder no
// meio de outro. Nada com o som desligado ou em silencio.
enum class Aviso { Nenhum, Pronto, Espera };
Aviso avisoDoPoll(int prontos, int esperas, bool somLigado, bool emSilencio);

// ---- Textos ----
// O sinal do Wi-Fi: "-62 dBm", "-84 dBm fraco" (abaixo de -80) ou "sem Wi-Fi".
const int SINAL_FRACO_DBM = -80;
bool        sinalFraco(bool conectado, int rssi);
std::string textoDoSinal(bool conectado, int rssi);

// A linha de estado da tela da noite: "2 sessoes rodando  -  nenhuma
// esperando". Rodando = Working; esperando = Blocked.
std::string linhaDaNoite(const Status &s);

// ---- A geometria do painel ----
// Os ids que ui::ajusteAt devolve. Os degraus sao 0..5; os botoes, 10..15, na
// ordem em que aparecem (da esquerda para a direita, de cima para baixo).
const int SOM = 10, NOITE = 11, GIRAR = 12, SILENCIO = 13, FOTO = 14, WIFI = 15;
const int BOTOES = 6;

// O retangulo de cada peca, nas duas orientacoes. Deitado e a maquete F1 (tres
// colunas); em pe, duas colunas e tres linhas. O DESENHO e o TOQUE saem daqui,
// e e isso que impede o alvo de discordar do quadrado.
Alvo painel(bool retrato);
Alvo quadradoDoDegrau(int d, bool retrato);
Alvo botao(int id, bool retrato);

// O ajuste sob o dedo: >= 0 e o ajuste, -1 e dentro do painel e fora de todos,
// -2 e fora do painel (o toque fecha). O degrau aceita um dedo um pouco fora do
// quadrado: 22 px de altura sao pouco para acertar numa tela de parede.
int noPonto(int x, int y, bool retrato);

}  // namespace ajustes
