# server/test_herdr.py
import subprocess
import threading
import time
import unittest
from unittest import mock

import herdr

# Saida real de `herdr agent list` nesta maquina, reduzida a tres agentes.
SAIDA = ('{"id":"cli:agent:list","result":{"agents":['
         '{"agent":"claude","agent_status":"idle","cwd":"E:\\\\OneDrive\\\\projetos\\\\pc",'
         '"focused":false,"pane_id":"w14:p1","tab_id":"w14:t1","workspace_id":"w14"},'
         '{"agent":"claude","agent_status":"working","cwd":"E:\\\\OneDrive\\\\projetos\\\\esp32-s3",'
         '"focused":true,"pane_id":"w0:p1","tab_id":"w0:t1","workspace_id":"w0"},'
         '{"agent":"codex","agent_status":"done","cwd":"/home/x/proj",'
         '"focused":false,"pane_id":"w2:p3","tab_id":"w2:t1","workspace_id":"w2"}'
         '],"type":"agent_list"}}')


class TesteParse(unittest.TestCase):

    def test_le_a_saida_real(self):
        ags = herdr.parse_agentes(SAIDA)
        self.assertEqual(len(ags), 3)
        self.assertEqual(ags[0]["pane_id"], "w14:p1")
        self.assertEqual(ags[0]["state"], "idle")
        self.assertEqual(ags[0]["repo"], "pc")
        self.assertEqual(ags[1]["state"], "working")
        self.assertTrue(ags[1]["focused"])
        self.assertEqual(ags[2]["agent"], "codex")
        self.assertEqual(ags[2]["repo"], "proj")

    def test_entrada_podre_devolve_lista_vazia(self):
        # Nunca levanta: uma CLI que mudou de formato nao pode derrubar a API.
        for ruim in ("", "nao e json", "[]", "{}", '{"result":{}}',
                     '{"result":{"agents":"x"}}', None,
                     # "result" truthy mas do tipo errado — nao tem `.get`.
                     '{"result": [1,2,3]}', '{"result": "oops"}',
                     '{"result": true}', '{"result": 5}'):
            self.assertEqual(herdr.parse_agentes(ruim), [], repr(ruim))

    def test_agente_sem_os_campos_obrigatorios_e_descartado(self):
        # Sem pane_id nao ha como casar; sem estado nao ha o que dizer.
        s = ('{"result":{"agents":[{"agent":"claude"},'
             '{"pane_id":"w1:p1"},'
             '{"pane_id":"w1:p2","agent_status":"idle"}]}}')
        ags = herdr.parse_agentes(s)
        self.assertEqual(len(ags), 1)
        self.assertEqual(ags[0]["pane_id"], "w1:p2")

    def test_repo_sai_do_cwd_nos_dois_separadores(self):
        self.assertEqual(herdr.repo_de(r"E:\OneDrive\projetos\esp32-s3"), "esp32-s3")
        self.assertEqual(herdr.repo_de("/home/x/proj/"), "proj")
        self.assertEqual(herdr.repo_de(""), "")
        self.assertEqual(herdr.repo_de(None), "")


class TesteConsulta(unittest.TestCase):

    def setUp(self):
        self.real = subprocess.run
        herdr._retrato = {"online": False, "agents": [], "ts": 0.0}

    def tearDown(self):
        subprocess.run = self.real

    def falso_run(self, saida="", codigo=0, excecao=None):
        def run(*a, **k):
            # Guarda o que foi chamado para os testes que afirmam sobre os
            # argumentos (creationflags, timeout, comando) poderem inspecionar.
            self.chamada = (a, k)
            if excecao:
                raise excecao
            return subprocess.CompletedProcess(a[0], codigo, saida, "")
        return run

    def test_consulta_boa_devolve_agentes(self):
        subprocess.run = self.falso_run(SAIDA)
        self.assertEqual(len(herdr.consultar("herdr")), 3)

    def test_chama_com_creationflags_e_timeout_corretos(self):
        # creationflags=SEM_JANELA e a unica linha que impede uma janela de
        # console piscar a cada 2 segundos sob pythonw.exe. Uma edicao futura
        # que apague esse argumento nao pode deixar a suite verde.
        subprocess.run = self.falso_run(SAIDA)
        herdr.consultar("herdr")
        args, kwargs = self.chamada
        self.assertEqual(args[0], ["herdr", "agent", "list"])
        self.assertEqual(kwargs["creationflags"], herdr.SEM_JANELA)
        self.assertEqual(kwargs["timeout"], herdr.TIMEOUT_S)

    def test_codigo_de_saida_ruim_devolve_None(self):
        # None e "nao deu para saber"; [] e "nenhum agente". Confundir os dois
        # faria o painel esvaziar sozinho quando o herdr piscasse.
        subprocess.run = self.falso_run("", codigo=1)
        self.assertIsNone(herdr.consultar("herdr"))

    def test_timeout_devolve_None(self):
        subprocess.run = self.falso_run(excecao=subprocess.TimeoutExpired("herdr", 2))
        self.assertIsNone(herdr.consultar("herdr"))

    def test_binario_sumiu_devolve_None(self):
        subprocess.run = self.falso_run(excecao=FileNotFoundError())
        self.assertIsNone(herdr.consultar("herdr"))

    def test_lista_vazia_com_servidor_no_ar_nao_e_falha(self):
        subprocess.run = self.falso_run('{"result":{"agents":[]}}')
        self.assertEqual(herdr.consultar("herdr"), [])

    def test_uma_falha_preserva_o_retrato_anterior(self):
        # Uma consulta ruim isolada nao pode piscar o painel.
        herdr._aplicar(herdr.parse_agentes(SAIDA))
        self.assertTrue(herdr.snapshot()["online"])
        herdr._aplicar(None)
        r = herdr.snapshot()
        self.assertFalse(r["online"])
        self.assertEqual(len(r["agents"]), 3)   # o que sabiamos continua la

    def test_snapshot_e_copia(self):
        # Quem le nao pode mexer no que a thread escreve.
        herdr._aplicar(herdr.parse_agentes(SAIDA))
        r = herdr.snapshot()
        r["agents"].append({"pane_id": "invadido"})
        self.assertEqual(len(herdr.snapshot()["agents"]), 3)

    def test_callback_recebe_o_online_de_cada_ciclo(self):
        vistos = []
        herdr._aplicar(herdr.parse_agentes(SAIDA), ao_consultar=vistos.append)
        herdr._aplicar(None, ao_consultar=vistos.append)
        self.assertEqual(vistos, [True, False])

    def test_callback_que_explode_nao_derruba_o_sensor(self):
        # O sensor nunca pode morrer por causa de quem ele avisa.
        def ruim(_):
            raise RuntimeError("boom")
        herdr._aplicar(herdr.parse_agentes(SAIDA), ao_consultar=ruim)
        self.assertTrue(herdr.snapshot()["online"])

    def test_sem_callback_continua_funcionando(self):
        herdr._aplicar(herdr.parse_agentes(SAIDA))
        self.assertTrue(herdr.snapshot()["online"])


class TesteFrescor(unittest.TestCase):
    """`snapshot()` nao pode devolver online:True para um retrato congelado —
    ver o comentario de RETRATO_MAX_IDADE_S. `_laco` tambem nao pode morrer
    numa excecao nao prevista, porque e essa morte que congela o retrato."""

    def setUp(self):
        self.retrato_original = herdr._retrato
        herdr._retrato = {"online": False, "agents": [], "ts": 0.0}

    def tearDown(self):
        herdr._retrato = self.retrato_original

    def test_retrato_recente_continua_online(self):
        herdr._retrato = {"online": True, "agents": herdr.parse_agentes(SAIDA),
                           "ts": time.time()}
        self.assertTrue(herdr.snapshot()["online"])

    def test_retrato_velho_vira_online_false(self):
        # A ultima consulta boa foi ha mais tempo do que RETRATO_MAX_IDADE_S
        # permite: dado morto nao pode passar por vivo.
        velho = time.time() - herdr.RETRATO_MAX_IDADE_S - 1
        herdr._retrato = {"online": True, "agents": herdr.parse_agentes(SAIDA),
                           "ts": velho}
        r = herdr.snapshot()
        self.assertFalse(r["online"])
        # A lista velha continua visivel: online e que muda, nao os dados.
        self.assertEqual(len(r["agents"]), 3)

    def test_laco_sobrevive_a_excecao_e_continua_consultando(self):
        # Se `_laco` morresse na primeira excecao, a segunda chamada a
        # `consultar` nunca aconteceria e `online` ficaria False para sempre —
        # exatamente o bug oposto ao que este branch foi escrito para
        # corrigir (um retrato congelado em True).
        chamadas = {"n": 0}

        def consultar_falho(binario):
            chamadas["n"] += 1
            if chamadas["n"] == 1:
                raise RuntimeError("falha simulada, nao prevista")
            return herdr.parse_agentes(SAIDA)

        with mock.patch.object(herdr, "consultar", side_effect=consultar_falho), \
             mock.patch.object(herdr, "CONSULTA_S", 0.01):
            t = threading.Thread(target=herdr._laco, args=("herdr-fake",), daemon=True)
            t.start()
            deadline = time.time() + 2.0
            while chamadas["n"] < 2 and time.time() < deadline:
                time.sleep(0.01)

        self.assertGreaterEqual(chamadas["n"], 2, "a thread morreu na 1a excecao")
        self.assertTrue(herdr.snapshot()["online"])


class TesteLerPane(unittest.TestCase):

    def setUp(self):
        self.real = subprocess.run
        herdr._binario_ativo = "herdr-fake"

    def tearDown(self):
        subprocess.run = self.real
        herdr._binario_ativo = None

    def falso_run(self, saida="", codigo=0, excecao=None):
        def run(*a, **k):
            self.chamada = (a, k)
            if excecao:
                raise excecao
            return subprocess.CompletedProcess(a[0], codigo, saida, "")
        return run

    def test_devolve_o_texto_cru_da_tela(self):
        # `pane read` nao devolve JSON, ao contrario de `agent list`: sai o
        # texto do terminal direto no stdout. Verificado na CLI 0.7.1.
        subprocess.run = self.falso_run("linha 1\nlinha 2\n")
        self.assertEqual(herdr.ler_pane("w0:p1"), "linha 1\nlinha 2\n")

    def test_pede_a_tela_recente_em_texto(self):
        # `--source recent` e nao `visible`: a pergunta pode ter rolado para
        # fora da area visivel do pane. `--format text` porque o parser casa
        # regex, e codigos de cor no meio quebrariam o match.
        subprocess.run = self.falso_run("x")
        herdr.ler_pane("w0:p1", linhas=30)
        args, kwargs = self.chamada
        self.assertEqual(args[0], ["herdr-fake", "pane", "read", "w0:p1",
                                   "--source", "recent", "--lines", "30",
                                   "--format", "text"])
        self.assertEqual(kwargs["creationflags"], herdr.SEM_JANELA)

    def test_falha_devolve_string_vazia(self):
        # "" e "nao consegui ler". Quem chama trata como "sem formulario", que
        # e o comportamento seguro: mostra sem botao em vez de botao errado.
        for run in (self.falso_run("", codigo=1),
                    self.falso_run(excecao=subprocess.TimeoutExpired("herdr", 2)),
                    self.falso_run(excecao=FileNotFoundError())):
            subprocess.run = run
            self.assertEqual(herdr.ler_pane("w0:p1"), "")

    def test_sem_pane_nao_chama_nada(self):
        subprocess.run = self.falso_run("x")
        self.chamada = None
        self.assertEqual(herdr.ler_pane(""), "")
        self.assertIsNone(self.chamada)

    def test_decodifica_como_utf8_e_nao_como_o_locale(self):
        # A tela vem cheia de caixa (│ ╭ ╰ ❯) e acento. Sem `encoding="utf-8"` o
        # subprocess decodifica pelo locale — cp1252 nesta maquina — e estoura
        # UnicodeDecodeError DENTRO da thread leitora, que engole a excecao e
        # devolve stdout=None. Aconteceu na primeira leitura real.
        subprocess.run = self.falso_run("x")
        herdr.ler_pane("w0:p1")
        self.assertEqual(self.chamada[1]["encoding"], "utf-8")
        self.assertEqual(self.chamada[1]["errors"], "replace")

    def test_stdout_nulo_vira_string_vazia(self):
        # O caso acima: returncode 0 e stdout None. Devolver None faria quem
        # chama estourar em `len()`, longe daqui.
        def run(*a, **k):
            return subprocess.CompletedProcess(a[0], 0, None, "")
        subprocess.run = run
        self.assertEqual(herdr.ler_pane("w0:p1"), "")


class TesteEnviarTeclas(unittest.TestCase):

    def setUp(self):
        self.real = subprocess.run
        herdr._binario_ativo = "herdr-fake"
        self.chamada = None

    def tearDown(self):
        subprocess.run = self.real
        herdr._binario_ativo = None

    def falso_run(self, codigo=0, excecao=None):
        def run(*a, **k):
            self.chamada = (a, k)
            if excecao:
                raise excecao
            return subprocess.CompletedProcess(a[0], codigo, "", "")
        return run

    def test_manda_as_teclas_numa_chamada_so(self):
        # Uma chamada e nao uma por tecla: entre duas chamadas o agente pode
        # redesenhar, e as setas passariam a contar a partir de outra tela.
        subprocess.run = self.falso_run()
        self.assertTrue(herdr.enviar_teclas("w0:p1", ["Down", "Down"]))
        self.assertEqual(self.chamada[0][0],
                         ["herdr-fake", "pane", "send-keys", "w0:p1",
                          "Down", "Down"])

    def test_tecla_fora_da_lista_branca_e_recusada_sem_chamar(self):
        # A recusa mora AQUI, no modulo que age, e nao em quem chama. Uma tecla
        # arbitraria vinda da rede seria um teclado apontado para os seus
        # agentes — e este e o unico ponto por onde ela passaria.
        subprocess.run = self.falso_run()
        for ruim in (["rm -rf /"], ["Enter", "y"], ["C-c"], ["Left"], []):
            self.assertFalse(herdr.enviar_teclas("w0:p1", ruim), repr(ruim))
        self.assertIsNone(self.chamada)

    def test_falha_devolve_False(self):
        for run in (self.falso_run(codigo=1),
                    self.falso_run(excecao=subprocess.TimeoutExpired("herdr", 2))):
            subprocess.run = run
            self.assertFalse(herdr.enviar_teclas("w0:p1", ["Enter"]))


if __name__ == "__main__":
    unittest.main(verbosity=2)
