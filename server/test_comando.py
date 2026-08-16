#!/usr/bin/env python3
"""Testes da lista branca de comandos.

Este e o primeiro ponto do sistema que AGE em vez de so contar, e a API escuta
em 0.0.0.0 sem autenticacao. A lista branca e a unica coisa entre a rede local e
o teclado dos seus agentes, entao ela merece prova propria.
"""

import unittest

import claude_metrics_api as api


class TesteListaBranca(unittest.TestCase):

    def test_compact_esta_permitido(self):
        self.assertEqual(api.COMANDOS["compact"], "/compact")

    def test_clear_NAO_esta_permitido(self):
        # Fica de fora ate o modelo de confirmacao na tela estar decidido: ele
        # apaga contexto sem volta, e um toque acidental e indistinguivel de um
        # intencional.
        self.assertNotIn("clear", api.COMANDOS)

    def test_nada_de_texto_livre(self):
        # O perigo real nao e um comando errado, e um comando ARBITRARIO. Se
        # alguem trocar o dicionario por uma funcao que aceita qualquer coisa,
        # esta prova cai.
        for tentativa in ("rm -rf /", "/clear", "", None, "COMPACT",
                          "compact ", "/compact"):
            self.assertIsNone(api.COMANDOS.get(tentativa), repr(tentativa))

    def test_todo_valor_e_um_comando_de_barra(self):
        # Um valor sem a barra viraria texto digitado no prompt do agente, e nao
        # um comando — sairia como mensagem para o Claude em vez de acao.
        for nome, texto in api.COMANDOS.items():
            self.assertTrue(texto.startswith("/"), nome)
            self.assertNotIn("\n", texto, nome)   # nada de multilinha


if __name__ == "__main__":
    unittest.main(verbosity=2)
