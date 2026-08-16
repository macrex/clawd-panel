"""Poe o compilador de host de `toolchain/` na frente do PATH da build nativa.

Esta maquina nao tem gcc instalado. Os unicos toolchains presentes sao os
cross-compilers do xtensa, que geram binario para o ESP32 e o PC nao executa —
entao `pio test -e native` falhava com "'gcc' nao e reconhecido" e os testes
ficavam escritos mas nunca executados.

A plataforma `native` do PlatformIO nao consome pacote de toolchain: ela usa as
ferramentas padrao do SCons, que procuram no PATH. Por isso o caminho aqui e
mexer no PATH do processo de build, e nao declarar um `platform_packages`.

Na FRENTE e nao no fim: se algum dia houver outro gcc na maquina, o do projeto
tem que ganhar — senao a build passa a depender de qual compilador o PATH do
usuario resolve primeiro, que e o tipo de coisa que quebra so na maquina dos
outros.

Ausente, nao faz nada: quem tiver gcc de verdade instalado continua funcionando.
"""

import os

Import("env")  # noqa: F821  (injetado pelo SCons)

BIN = os.path.join(env.subst("$PROJECT_DIR"), "toolchain", "w64devkit", "bin")  # noqa: F821

if os.path.isdir(BIN):
    env.PrependENVPath("PATH", BIN)  # noqa: F821
    print(f"native: usando o compilador de {BIN}")
else:
    print(f"native: {BIN} nao existe — usando o gcc do PATH, se houver")
