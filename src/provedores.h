#pragma once
#include <stdint.h>
#include <string>

// O icone e a cor de cada CLI, para o cabecalho de grupo do card de sessoes.
//
// POR QUE ICONE E NAO O NOME ESCRITO
// O rotulo textual custava 36 px ("CLAUDE") numa faixa de 12 px de altura, num
// card com 292 px de largura onde o plano tambem precisa caber. O icone faz o
// mesmo trabalho em 12 px, e o olho o reconhece sem ler.
//
// POR QUE MASCARA DE ALFA E NAO 1 BIT
// Foi 1 bit ate aqui, e o knot da OpenAI reprovava: seis alcas finas
// entrelacadas, reduzidas a preto-ou-branco, viram ruido em 9, 11, 13, 16 e 20
// px (medido). So em 24 px a forma voltava — e 24 px nao cabem numa faixa de
// cabecalho. O que salva o traco fino nao e tamanho, e MEIO-TOM: guardando o
// alfa de 8 bits e misturando com o fundo na hora de desenhar, o mesmo knot se
// le em 12 px. A cor continua sendo uma so, a da marca; o alfa so decide quanto
// dela entra em cada pixel.
namespace provedores {

// Uma mascara de cobertura, linha por linha, 0 = fundo, 255 = cheio.
struct Icone {
    const uint8_t *alfa;
    int            w;
    int            h;
};

// O icone de um agente do herdr ("claude", "codex", "agy"). Qualquer outro
// identificador — e o herdr reconhece dezenove — cai na lhama do Ollama, o
// icone do grupo "uma CLI que este painel nao tem arte para distinguir".
// Nunca devolve vazio; quem desenha ainda checa `w`, que custa nada.
Icone iconeDe(const std::string &agente);

// A cor da marca daquele fornecedor. Cinza para quem nao tem uma.
uint16_t corDe(const std::string &agente);

// O nome de EXIBICAO. E aqui, e so aqui, que `agy` vira "Antigravity": o
// identificador do herdr atravessa o servidor e o firmware inteiros sem
// traducao — casar plano com agente depende disso —, e a unica coisa que le
// "agy" e uma pessoa olhando a tela. Quem nao tem nome proprio cai no
// identificador em maiusculas, que e o que `grupos::Linha::rotulo` ja traz.
std::string nomeDe(const std::string &agente);

}
