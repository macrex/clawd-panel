# server/test_terminal.py
import unittest
from unittest import mock

import terminal


class TestePreparar(unittest.TestCase):

    def test_tira_o_padding_da_direita(self):
        # O herdr devolve a linha na largura do pane: 63 caracteres de texto
        # dentro de 838 de linha. Sem o rstrip, a quebra criaria uma dezena de
        # linhas em branco por linha de conteudo.
        linhas = terminal.preparar("ola" + " " * 800 + "\n", 40)
        self.assertEqual(linhas, ["ola"])

    def test_traduz_para_ascii_preservando_a_forma(self):
        # A moldura tem que continuar parecendo moldura: e ela que da estrutura
        # a tela do Claude Code.
        linhas = terminal.preparar("╭───╮\n│ ó │\n╰───╯\n", 40)
        self.assertEqual(linhas, ["+---+", "| o |", "+---+"])

    def test_quebra_dura_e_nao_por_palavra(self):
        # Conteudo de terminal e alinhado por COLUNA. Quebrar na palavra
        # desalinharia tabela, diff e barra de progresso.
        linhas = terminal.preparar("abcdefghij\n", 4)
        self.assertEqual(linhas, ["abcd", "efgh", "ij"])

    def test_linha_em_branco_no_meio_sobrevive(self):
        # Ela separa blocos. Comer linha vazia do meio gruda a resposta do
        # agente no comando que a gerou.
        linhas = terminal.preparar("a\n\nb\n", 10)
        self.assertEqual(linhas, ["a", "", "b"])

    def test_linhas_vazias_do_fim_caem(self):
        # O fim do buffer do terminal e quase todo vazio. Mante-las gastaria a
        # tela inteira mostrando nada.
        linhas = terminal.preparar("a\n\n\n   \n\n", 10)
        self.assertEqual(linhas, ["a"])

    def test_entrada_vazia_nao_levanta(self):
        for ruim in ("", None, "\n\n\n"):
            self.assertEqual(terminal.preparar(ruim, 40), [], repr(ruim))

    def test_tab_vira_espaco_e_nao_some(self):
        # A fonte nao desenha tab e o grid da placa e de largura fixa: um tab
        # que sumisse encolheria a linha e desalinharia a coluna.
        self.assertEqual(terminal.preparar("a\tb\n", 40), ["a    b"])

    def test_cols_invalido_nao_divide_por_zero(self):
        for c in (0, -5):
            self.assertEqual(terminal.preparar("abc\n", c), ["abc"])


class TesteJanela(unittest.TestCase):

    def setUp(self):
        self.linhas = [str(i) for i in range(100)]      # "0".."99"

    def test_offset_zero_e_o_fim(self):
        # O fim e onde esta o que acabou de acontecer: e ali que a tela abre.
        j, total = terminal.janela(self.linhas, 5, 0)
        self.assertEqual(j, ["95", "96", "97", "98", "99"])
        self.assertEqual(total, 100)

    def test_offset_sobe_no_historico(self):
        j, _ = terminal.janela(self.linhas, 5, 10)
        self.assertEqual(j, ["85", "86", "87", "88", "89"])

    def test_offset_alem_do_inicio_para_no_inicio(self):
        # Rolar demais nao pode devolver lista vazia: a tela ficaria preta sem
        # explicacao no exato momento em que o usuario chegou ao comeco.
        j, _ = terminal.janela(self.linhas, 5, 999)
        self.assertEqual(j, ["0", "1", "2", "3", "4"])

    def test_offset_negativo_e_tratado_como_zero(self):
        j, _ = terminal.janela(self.linhas, 5, -3)
        self.assertEqual(j, ["95", "96", "97", "98", "99"])

    def test_conteudo_menor_que_a_tela_sai_inteiro(self):
        j, total = terminal.janela(["a", "b"], 10, 0)
        self.assertEqual(j, ["a", "b"])
        self.assertEqual(total, 2)

    def test_sem_conteudo_devolve_vazio(self):
        j, total = terminal.janela([], 10, 0)
        self.assertEqual(j, [])
        self.assertEqual(total, 0)


class TesteLer(unittest.TestCase):
    """`travar` e `rolar` sao dublados: eles sobem um processo de verdade e
    mudam o tamanho de um terminal de verdade. O que se testa aqui e a
    COREOGRAFIA — quem e chamado, com o que, e em que ordem."""

    def setUp(self):
        self.lidos = []
        self.travas = []
        self.rolagens = []
        self.sonos = []
        self.tempo = 1000.0
        self.ok   = True                 # a trava esta de pe
        self.novo = True                 # ...e acabou de subir

        self.vivo = True                 # o controller sobreviveu a pausa

        self.ps = [mock.patch.object(terminal, "travar", self.travar),
                   mock.patch.object(terminal, "rolar", self.rolar),
                   mock.patch.object(terminal, "soltar", self.soltar),
                   mock.patch.object(terminal, "travado", lambda p: self.vivo)]
        for p in self.ps:
            p.start()
        self.soltou = 0

    def tearDown(self):
        for p in self.ps:
            p.stop()

    def travar(self, pane, cols, rows, agora=None):
        self.travas.append((pane, cols, rows))
        return self.ok, self.novo

    def rolar(self, pane, linhas, col=0, row=0):
        self.rolagens.append((pane, linhas))
        return True

    def soltar(self):
        self.soltou += 1

    def ler_fn(self, saida):
        # `formato` entrou quando a tela de terminal passou a pedir cor: a
        # leitura vai com "ansi" e o `sgr.py` cuida dos escapes. O default
        # "text" e o do formulario, que casa regex e nao pode ver SGR.
        def f(pane_id, linhas=50, source="recent", formato="text"):
            self.lidos.append((pane_id, linhas, source, formato))
            return saida
        return f

    def dormir(self, s):
        self.sonos.append(s)

    def chamar(self, pane, cols, rows, rolagem=0, saida="a\n"):
        return terminal.ler(pane, cols, rows, rolagem,
                            ler=self.ler_fn(saida),
                            agora=lambda: self.tempo, dormir=self.dormir)

    def test_trava_antes_de_ler_e_le_o_viewport(self):
        # A ordem importa: ler antes de travar traria a largura antiga, que e
        # exatamente o que a trava existe para evitar.
        r = self.chamar("w0:p1", 78, 34)
        self.assertEqual(self.travas, [("w0:p1", 78, 34)])
        self.assertEqual(self.lidos, [("w0:p1", 34, "visible", "ansi")])
        self.assertTrue(r["travado"])

    def test_espera_a_tui_reflowar_na_primeira_leitura(self):
        # Sem a pausa, a primeira leitura pega a largura NOVA com o conteudo
        # VELHO — a pior das duas telas possiveis.
        self.chamar("w0:p1", 78, 34)
        self.assertIn(terminal.ASSENTAR_S, self.sonos)

    def test_trava_ja_de_pe_nao_espera_de_novo(self):
        self.novo = False
        self.chamar("w0:p1", 78, 34)
        self.assertEqual(self.sonos, [])

    def test_rolagem_vai_para_o_host_antes_da_leitura(self):
        self.novo = False
        self.chamar("w0:p1", 78, 34, rolagem=-17)
        self.assertEqual(self.rolagens, [("w0:p1", -17)])
        self.assertIn(terminal.ROLAGEM_ASSENTA_S, self.sonos)

    def test_sem_rolagem_nao_manda_roda_nenhuma(self):
        # O painel pede a tela a cada dois segundos. Se um pedido sem gesto
        # rolasse, a tela andaria sozinha enquanto ninguem toca nela.
        self.novo = False
        self.chamar("w0:p1", 78, 34, rolagem=0)
        self.assertEqual(self.rolagens, [])

    def test_sem_pane_solta_a_trava_e_nao_le(self):
        # Fechar a tela e a deixa para devolver o terminal ao tamanho dele.
        # E o caminho normal de saida, nao um caso de erro.
        r = self.chamar("", 78, 34)
        self.assertEqual(self.soltou, 1)
        self.assertEqual(self.lidos, [])
        self.assertEqual(r["linhas"], [])
        self.assertFalse(r["travado"])

    def test_leitura_falha_devolve_vazio_sem_levantar(self):
        r = self.chamar("w0:p1", 78, 34, saida="")
        self.assertEqual(r["linhas"], [])

    def test_geometria_absurda_e_contida(self):
        # `cols` e `rows` chegam da REDE. Sem teto, um pedido de 9999 linhas
        # viraria uma resposta de megabytes para uma placa com PSRAM contada.
        bruto = "".join("linha %d\n" % i for i in range(500))
        r = self.chamar("w0:p1", 9999, 9999, saida=bruto)
        self.assertLessEqual(len(r["linhas"]), terminal.ROWS_MAX)
        self.assertEqual(self.travas[0][1], terminal.COLS_MAX)

    def test_trava_que_nao_subiu_e_anunciada(self):
        # herdr antigo, binario ausente, pane que sumiu: a tela vem na largura
        # do host e fica ilegivel. O painel precisa poder dizer o porque em vez
        # de mostrar texto embaralhado sem explicacao.
        self.ok = False
        self.novo = False
        r = self.chamar("w0:p1", 78, 34)
        self.assertFalse(r["travado"])
        self.assertTrue(self.lidos)          # le assim mesmo: algo e melhor que nada

    def test_controller_que_morreu_na_pausa_nao_conta_como_travado(self):
        # O Popen deu certo e o processo morreu logo em seguida — e o que
        # acontece com um pane_id que nao existe mais, visto ao usar um id
        # antigo depois de o herdr reiniciar. Confiar no retorno do Popen fazia
        # o painel afirmar que a largura estava ajustada com o controller ja
        # morto: mentira justamente no aviso que explica a tela quebrada.
        self.novo = True
        self.vivo = False
        r = self.chamar("w0:p1", 78, 34)
        self.assertFalse(r["travado"])


if __name__ == "__main__":
    unittest.main()
