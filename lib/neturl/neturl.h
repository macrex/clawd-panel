#pragma once
#include <string>

// As URLs que a placa monta a partir de UMA so, a do `/status` no config.json.
//
// Pedir duas no cartao seria pedir para elas sairem de sincronia num arquivo
// gravado a mao. O preco e esta aritmetica de string, que morava dentro do
// net.cpp — 997 linhas sem um unico teste — e agora mora aqui pelo motivo de
// sempre: errar aqui nao da erro visivel. Manda um `POST /responder` para o host
// errado, ou, pior, para um pane homonimo de outro agente.

// Troca o ULTIMO segmento da URL base pelo `segmento` dado.
//
// Fica com o host e descarta caminho E query: `http://h:8787/status?campos=x`
// com "command" devolve `http://h:8787/command`. E o que permite a URL do
// /status carregar parametros sem contamina-los.
std::string urlIrma(const std::string &base, const char *segmento);

// A URL do polling, com o pedido de payload enxuto (ver server/painel.py).
//
// Escolhe `?` ou `&` conforme a base ja tenha query: um cartao pode trazer
// `http://host:8787/status?x=1`, e concatenar `?` ali produziria uma URL que o
// servidor recusa. Base vazia devolve vazia — sem endereco nao ha o que pedir.
std::string urlDoStatus(const std::string &base);
