#pragma once
#include <ArduinoJson.h>

// De onde sai a memoria do parser de JSON.
//
// O `/status` inteiro passa por um JsonDocument, e ele nascia no heap INTERNO —
// o mesmo de onde saem a pilha Wi-Fi, os buffers do TCP e o que o Arduino_GFX
// pede. O documento cresce por realloc durante o parse, entao cada poll de dois
// segundos abria e fechava um bloco de alguns milhares de bytes ali dentro: e a
// receita de fragmentacao no heap que menos pode se dar ao luxo dela.
//
// Esta placa tem 8 MB de PSRAM e o firmware ja usa PSRAM para os sprites e para
// o buffer da captura. O documento passa a nascer la, e o heap interno volta a
// ser so da rede e da tela.
//
// No PC (testes nativos) nao ha PSRAM nenhuma e a implementacao cai em
// malloc/realloc/free — o mesmo comportamento de antes desta mudanca.
ArduinoJson::Allocator *alocadorJson();
