#pragma once
#include <cstdint>
#include <string>

struct AppConfig {
    bool        valid = false;
    std::string error;          // motivo, exibido na tela "SEM CONFIG"
    std::string ssid;
    std::string password;
    std::string url;
    // Segunda fonte de /status (o PC2). Vazia = sem segunda maquina, tudo como
    // antes. Quem funde as duas respostas e a placa — ver lib/metrics/fusao.h.
    std::string url2;
    // As TAGS das maquinas NAO moram aqui de proposito: quem se identifica e o
    // servico (campo `tag` do /status), e nao o cartao. Gravar nome de maquina
    // no SD obrigaria a abrir a placa a cada maquina nova.
    uint32_t    pollMs     = 2000;
    uint8_t     brightness = 200;

    // O fuso ONDE O PAINEL ESTA, em formato POSIX TZ. O padrao e o de Sao
    // Paulo, que desde 2019 nao tem horario de verao — por isso `<-03>3` e nao
    // uma regra com datas de virada.
    //
    // Isto e configuracao e nao dado: a hora vem da internet (SNTP), mas o fuso
    // e a mesa em que o painel mora, e ir busca-lo na rede seria trocar uma
    // dependencia externa por outra para responder o que ninguem precisa
    // perguntar duas vezes.
    std::string tz  = "<-03>3";
    // O servidor de hora preferido. Os dois de reserva sao fixos no codigo (ver
    // src/hora.cpp): quem troca este aqui quer um NTP da propria rede, e nao
    // quer perder o da internet como plano B.
    std::string ntp = "pool.ntp.org";
};

AppConfig parseConfig(const char *json);
