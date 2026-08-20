#!/usr/bin/env python3
"""O modelo em uso de um agente que nao e o Claude Code.

Este arquivo e a fronteira com o jeito de nomear de TRES empresas diferentes.
Formato novo que aparecer no disco entra aqui como caso, e a regra de `bonito`
so cresce quando um nome real quebra — nunca por antecipacao.
"""

import json
import os
import sqlite3
import tempfile
import unittest
from unittest import mock

import modelos


class TestBonito(unittest.TestCase):
    # Os tres primeiros sao os casos MEDIDOS nesta maquina em 19/08/2026. Nao ha
    # regra generica aqui de proposito: cada CLI nomeia o modelo do seu jeito, e
    # inventar um formatador universal para nomes que ainda nao existem e a
    # definicao de especular.
    def test_codex_terra(self):
        self.assertEqual(modelos.bonito("gpt-5.6-terra"), "GPT-5.6 Terra")

    def test_antigravity_flash_perde_o_esforco(self):
        # O `-high` e o esforco de raciocinio, e ele tem chip proprio na tela.
        # Aqui ele so ocuparia largura repetindo o que ja esta ao lado.
        self.assertEqual(modelos.bonito("gemini-3.7-flash-high"),
                         "Gemini 3.7 Flash")

    def test_antigravity_rodando_claude(self):
        # O Antigravity roda modelo de outro provedor. O nome tem que sair certo
        # mesmo assim: `4-6` e a versao 4.6, nao dois numeros soltos.
        self.assertEqual(modelos.bonito("claude-opus-4-6-thinking"),
                         "Claude Opus 4.6")

    # A sigla cola na versao ("GPT-5.6"); o nome por extenso separa por espaco
    # ("Gemini 3.7"). Esta dupla existe para travar essa diferenca: uma regra
    # que colasse pelos dois lados passaria no teste do GPT e sairia com
    # "Gemini-3.7 Flash" na tela. Versao de um digito so, para nao repetir a
    # entrada de test_codex_terra.
    def test_sigla_cola_na_versao(self):
        self.assertEqual(modelos.bonito("gpt-5-codex"), "GPT-5 Codex")

    def test_nome_por_extenso_separa_da_versao(self):
        self.assertEqual(modelos.bonito("gemini-3.6-flash"), "Gemini 3.6 Flash")

    def test_sufixos_de_esforco_todos_saem(self):
        for s in modelos.ESFORCOS:
            self.assertEqual(modelos.bonito(f"gemini-3.6-flash-{s}"),
                             "Gemini 3.6 Flash", s)

    def test_modelo_desconhecido_atravessa_capitalizado(self):
        self.assertEqual(modelos.bonito("mistral-large"), "Mistral Large")

    # Nada levanta: entrada torta vira string vazia, e quem desenha poe "—".
    def test_vazio_e_none_nao_levantam(self):
        self.assertEqual(modelos.bonito(""), "")
        self.assertEqual(modelos.bonito(None), "")
        self.assertEqual(modelos.bonito(123), "")
        self.assertEqual(modelos.bonito([]), "")

    # Um identificador que e so o sufixo de esforco nao pode virar string vazia
    # silenciosa nem estourar indice.
    def test_so_o_sufixo_nao_estoura(self):
        self.assertEqual(modelos.bonito("high"), "High")

    # A Tarefa 3 alimenta `bonito` com o que uma regex arranca de um blob binario
    # de SQLite. Ela pode cortar no meio de um hifen ou capturar hifen duplo: nao
    # e entrada hipotetica, e o painel nao pode mostrar espaco solto por isso.
    def test_hifen_solto_nao_vira_espaco(self):
        self.assertEqual(modelos.bonito("-"), "")
        self.assertEqual(modelos.bonito("--"), "")
        self.assertEqual(modelos.bonito("gemini--flash"), "Gemini Flash")
        self.assertEqual(modelos.bonito("gemini-3.7-"), "Gemini 3.7")
        self.assertEqual(modelos.bonito("gpt-"), "GPT")

    # Hifen no fim nao pode desligar o corte do esforco: o chip ja esta ao lado.
    def test_hifen_no_fim_nao_salva_o_esforco(self):
        self.assertEqual(modelos.bonito("gemini-3.7-flash-high-"), "Gemini 3.7 Flash")


# Um "agora" fixo, para os testes de idade e de TTL nao dependerem do relogio
# de parede. Teste que espera o tempo passar e teste lento e intermitente; aqui
# o `_agora` do modulo e trocado e as datas dos arquivos saem deste instante.
INSTANTE = 2_000_000_000.0


class _ComRaiz(unittest.TestCase):
    """Base das duas sondas: cada teste ganha uma raiz de mentirinha.

    A raiz e parametro das funcoes justamente para isto — sem ela os testes
    leriam o `~/.codex` e o `~/.gemini` DE VERDADE da maquina de quem roda, e
    passariam ou falhariam conforme o que estivesse aberto na hora.

    O cache de varredura tambem e zerado a cada caso: ele e memoria de processo,
    e um caso herdando a lista de arquivos do anterior seria um falso positivo.
    """

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.raiz = self._tmp.name
        modelos._esquecer()

    def tearDown(self):
        self._tmp.cleanup()
        modelos._esquecer()


class TestModeloCodex(_ComRaiz):
    def _escrever(self, nome, texto, mtime, dia="19"):
        """Um arquivo de sessao com conteudo literal, para os casos de formato."""
        pasta = os.path.join(self.raiz, "sessions", "2026", "08", dia)
        os.makedirs(pasta, exist_ok=True)
        f = os.path.join(pasta, nome)
        with open(f, "w", encoding="utf-8", newline="") as fh:
            fh.write(texto)
        os.utime(f, (mtime, mtime))
        return f

    def _rollout(self, dia, nome, cwd, modelo, mtime):
        """Um arquivo de sessao do Codex, no formato que ele grava de verdade."""
        linhas = [
            {"type": "session_meta",
             "payload": {"cwd": cwd, "model_provider": "openai"}},
            {"type": "turn_context", "payload": {"model": modelo}},
        ]
        return self._escrever(nome, "\n".join(json.dumps(x) for x in linhas),
                              mtime, dia=dia)

    def test_acha_o_modelo_pelo_cwd(self):
        self._rollout("19", "rollout-a.jsonl", r"D:\workspace\saas",
                      "gpt-5.6-terra", 1_000_000)
        self.assertEqual(
            modelos.modelo_codex(r"D:\workspace\saas", raiz=self.raiz),
            "GPT-5.6 Terra")

    def test_cwd_sem_sessao_e_none(self):
        self._rollout("19", "rollout-a.jsonl", r"D:\workspace\saas",
                      "gpt-5.6-terra", 1_000_000)
        self.assertIsNone(
            modelos.modelo_codex(r"D:\workspace\outro", raiz=self.raiz))

    # O caso que a spec assume como custo: dois agentes no MESMO diretorio. O
    # desempate e por mtime, e este teste fixa qual dos dois ganha.
    def test_dois_no_mesmo_cwd_ganha_o_mais_recente(self):
        self._rollout("19", "rollout-velho.jsonl", r"D:\workspace\saas",
                      "gpt-5.6-terra", 1_000_000)
        self._rollout("19", "rollout-novo.jsonl", r"D:\workspace\saas",
                      "gpt-5.7-alpha", 2_000_000)
        self.assertEqual(
            modelos.modelo_codex(r"D:\workspace\saas", raiz=self.raiz),
            "GPT-5.7 Alpha")

    # O `cwd` do herdr e o do pane; a caixa e o separador podem divergir do que
    # o Codex gravou. Casar sem normalizar perderia o agente em metade dos casos.
    def test_casa_ignorando_caixa_e_separador(self):
        self._rollout("19", "rollout-a.jsonl", "D:\\workspace\\SaaS",
                      "gpt-5.6-terra", 1_000_000)
        self.assertEqual(modelos.modelo_codex("d:/workspace/saas",
                                              raiz=self.raiz),
                         "GPT-5.6 Terra")

    # O ULTIMO turn_context manda: trocar de modelo no meio da sessao e comum, e
    # a tela tem que mostrar o que esta valendo agora.
    def test_pega_o_ultimo_turn_context(self):
        self._escrever("rollout-a.jsonl", "\n".join([
            json.dumps({"type": "session_meta", "payload": {"cwd": r"D:\x"}}),
            json.dumps({"type": "turn_context",
                        "payload": {"model": "gpt-5.6-terra"}}),
            json.dumps({"type": "turn_context",
                        "payload": {"model": "gpt-5.7-alpha"}}),
        ]), 1_000_000)
        self.assertEqual(modelos.modelo_codex(r"D:\x", raiz=self.raiz),
                         "GPT-5.7 Alpha")

    # O `cwd` CASA de proposito: um arquivo corrompido cujo diretorio nao casaria
    # de qualquer jeito prova apenas que a funcao nao levanta, sem nunca entrar
    # no parse da cauda. Aqui a cabeca e valida, o casamento acontece, e e o
    # miolo podre que tem que ser engolido em silencio.
    def test_arquivo_corrompido_nao_levanta(self):
        self._escrever("rollout-a.jsonl", "\n".join([
            json.dumps({"type": "session_meta", "payload": {"cwd": r"D:\x"}}),
            "{isto nao e json",
            "\x00\x01 lixo binario \x02",
            '{"type": "turn_context", "payload": {"model": ',
        ]), 1_000_000)
        self.assertIsNone(modelos.modelo_codex(r"D:\x", raiz=self.raiz))

    # Um arquivo VAZIO nao pode estourar indice na leitura da primeira linha.
    def test_arquivo_vazio_nao_estoura(self):
        self._escrever("rollout-a.jsonl", "", 1_000_000)
        self.assertIsNone(modelos.modelo_codex(r"D:\x", raiz=self.raiz))

    # A sessao existe e casa, mas nunca teve um turn_context. E o estado de uma
    # sessao aberta e nao usada: sem modelo, e sem erro.
    def test_sessao_sem_turn_context_e_none(self):
        self._escrever("rollout-a.jsonl", json.dumps(
            {"type": "session_meta", "payload": {"cwd": r"D:\x"}}), 1_000_000)
        self.assertIsNone(modelos.modelo_codex(r"D:\x", raiz=self.raiz))

    def test_raiz_inexistente_e_none(self):
        self.assertIsNone(modelos.modelo_codex(
            r"D:\x", raiz=os.path.join(self.raiz, "nao-existe")))

    # `cwd` vazio nao pode casar com sessao nenhuma: sem esta guarda, um pane
    # sem diretorio herdaria o modelo da primeira sessao da lista.
    def test_cwd_vazio_nao_casa_com_nada(self):
        self._rollout("19", "rollout-a.jsonl", r"D:\workspace\saas",
                      "gpt-5.6-terra", 1_000_000)
        self.assertIsNone(modelos.modelo_codex("", raiz=self.raiz))
        self.assertIsNone(modelos.modelo_codex(None, raiz=self.raiz))

    # --- C1: campo truthy do tipo errado ---
    # `(x or {}).get(...)` so protege contra None e falsy. Um `payload` que seja
    # string, lista ou numero passa direto e estoura AttributeError no `.get` —
    # e este modulo prometeu que nada levanta.
    def test_payload_de_tipo_errado_na_cabeca_nao_levanta(self):
        for payload in ("uma string", ["uma", "lista"], 42, True):
            with self.subTest(payload=payload):
                modelos._esquecer()
                self._escrever("rollout-a.jsonl", "\n".join([
                    json.dumps({"type": "session_meta", "payload": payload}),
                    json.dumps({"type": "turn_context",
                                "payload": {"model": "gpt-5.6-terra"}}),
                ]), 1_000_000)
                self.assertIsNone(modelos.modelo_codex(r"D:\x", raiz=self.raiz))

    def test_payload_de_tipo_errado_no_turn_context_nao_levanta(self):
        for payload in ("uma string", ["uma", "lista"], 42, True):
            with self.subTest(payload=payload):
                modelos._esquecer()
                self._escrever("rollout-a.jsonl", "\n".join([
                    json.dumps({"type": "session_meta",
                                "payload": {"cwd": r"D:\x"}}),
                    json.dumps({"type": "turn_context", "payload": payload}),
                ]), 1_000_000)
                self.assertIsNone(modelos.modelo_codex(r"D:\x", raiz=self.raiz))

    # --- C2: a cauda e lida por seek, nao pelo arquivo inteiro ---
    # Estreitamento deliberado: "o ultimo turn_context do ARQUIVO" virou "o
    # ultimo da CAUDA". Alem de CAUDA_LINHAS do fim, o modelo deixa de existir
    # para esta sonda — e isso e escolha, nao acidente.
    def test_turn_context_alem_das_linhas_da_cauda_e_none(self):
        enchimento = [json.dumps({"type": "event", "n": i})
                      for i in range(modelos.CAUDA_LINHAS + 10)]
        self._escrever("rollout-a.jsonl", "\n".join(
            [json.dumps({"type": "session_meta", "payload": {"cwd": r"D:\x"}}),
             json.dumps({"type": "turn_context",
                         "payload": {"model": "gpt-5.6-terra"}})]
            + enchimento), 1_000_000)
        self.assertIsNone(modelos.modelo_codex(r"D:\x", raiz=self.raiz))

    # A janela de BYTES corta independente da de linhas: poucas linhas, muitos
    # bytes. Sem esta, `CAUDA_BYTES` poderia ser removida sem nenhum teste cair.
    def test_turn_context_alem_dos_bytes_da_cauda_e_none(self):
        gordo = json.dumps({"type": "event", "x": "y" * 3000})
        self._escrever("rollout-a.jsonl", "\n".join([
            json.dumps({"type": "session_meta", "payload": {"cwd": r"D:\x"}}),
            json.dumps({"type": "turn_context",
                        "payload": {"model": "gpt-5.6-terra"}}),
            gordo, gordo, gordo,
        ]), 1_000_000)
        with mock.patch.object(modelos, "CAUDA_BYTES", 4096):
            self.assertIsNone(modelos.modelo_codex(r"D:\x", raiz=self.raiz))

    def test_dentro_dos_bytes_da_cauda_ainda_acha(self):
        magro = json.dumps({"type": "event", "x": "y" * 300})
        self._escrever("rollout-a.jsonl", "\n".join([
            json.dumps({"type": "session_meta", "payload": {"cwd": r"D:\x"}}),
            json.dumps({"type": "turn_context",
                        "payload": {"model": "gpt-5.6-terra"}}),
            magro, magro, magro,
        ]), 1_000_000)
        with mock.patch.object(modelos, "CAUDA_BYTES", 4096):
            self.assertEqual(modelos.modelo_codex(r"D:\x", raiz=self.raiz),
                             "GPT-5.6 Terra")

    # Um arquivo MAIOR que a janela: a cabeca vem do inicio, a cauda vem de um
    # seek, e o meio nunca e tocado.
    def test_arquivo_maior_que_a_janela_acha_pela_cauda(self):
        gordo = json.dumps({"type": "event", "x": "y" * 5000})
        self._escrever("rollout-a.jsonl", "\n".join(
            [json.dumps({"type": "session_meta", "payload": {"cwd": r"D:\x"}})]
            + [gordo] * 40
            + [json.dumps({"type": "turn_context",
                           "payload": {"model": "gpt-5.6-terra"}})]),
            1_000_000)
        with mock.patch.object(modelos, "CAUDA_BYTES", 4096):
            self.assertEqual(modelos.modelo_codex(r"D:\x", raiz=self.raiz),
                             "GPT-5.6 Terra")

    # Entrar pelo meio do arquivo pega o RABO de uma linha que comecou antes do
    # buffer. Esse rabo pode ser JSON valido por acidente — e o de baixo e: a
    # linha inteira nao parseia (dois objetos colados), mas o pedaco que sobra
    # depois do corte parseia e mentiria um modelo. Por isso a primeira linha
    # do buffer e descartada quando houve seek.
    def test_linha_partida_pelo_corte_e_descartada(self):
        rabo = json.dumps({"type": "turn_context",
                           "payload": {"model": "gpt-nao-devia-aparecer"}})
        fim = json.dumps({"type": "event", "fim": True})
        texto = "\n".join([
            json.dumps({"type": "session_meta", "payload": {"cwd": r"D:\x"}}),
            "{\"lixo\": \"" + "z" * 200 + "\"}" + rabo,
            fim,
        ])
        self._escrever("rollout-a.jsonl", texto, 1_000_000)
        # Corte exatamente no inicio do rabo: janela = rabo + "\n" + fim.
        janela = len(rabo) + 1 + len(fim)
        with mock.patch.object(modelos, "CAUDA_BYTES", janela):
            self.assertIsNone(modelos.modelo_codex(r"D:\x", raiz=self.raiz))

    # --- MAX_ROLLOUTS ---
    # O 41o arquivo mais recente nao e olhado. E a fronteira da constante, e sem
    # teste ela vira numero decorativo.
    def test_alem_de_max_rollouts_nao_e_olhado(self):
        for i in range(modelos.MAX_ROLLOUTS):
            self._rollout("19", f"rollout-enche-{i}.jsonl", r"D:\outro",
                          "gpt-5.6-terra", 2_000_000 + i)
        self._rollout("19", "rollout-velho.jsonl", r"D:\x",
                      "gpt-5.7-alpha", 1_000_000)
        self.assertIsNone(modelos.modelo_codex(r"D:\x", raiz=self.raiz))

    def test_o_40o_ainda_e_olhado(self):
        for i in range(modelos.MAX_ROLLOUTS - 1):
            self._rollout("19", f"rollout-enche-{i}.jsonl", r"D:\outro",
                          "gpt-5.6-terra", 2_000_000 + i)
        self._rollout("19", "rollout-velho.jsonl", r"D:\x",
                      "gpt-5.7-alpha", 1_000_000)
        self.assertEqual(modelos.modelo_codex(r"D:\x", raiz=self.raiz),
                         "GPT-5.7 Alpha")

    # Uma vez que um arquivo CASA o cwd, o modulo se compromete com ele: o
    # `return None` nao e `continue`. A sessao mais nova daquele diretorio e a
    # que esta na tela; cair para a anterior mostraria o modelo de uma conversa
    # que o usuario ja fechou. Escolha carregada, travada aqui.
    def test_casou_e_nao_tem_modelo_nao_cai_para_o_anterior(self):
        self._rollout("19", "rollout-velho.jsonl", r"D:\x",
                      "gpt-5.6-terra", 1_000_000)
        self._escrever("rollout-novo.jsonl", json.dumps(
            {"type": "session_meta", "payload": {"cwd": r"D:\x"}}), 2_000_000)
        self.assertIsNone(modelos.modelo_codex(r"D:\x", raiz=self.raiz))

    # --- C4: a varredura amortiza entre panes ---
    def test_varredura_acontece_uma_vez_para_varios_panes(self):
        self._rollout("19", "rollout-a.jsonl", r"D:\a", "gpt-5.6-terra", 3_000_000)
        self._rollout("19", "rollout-b.jsonl", r"D:\b", "gpt-5.7-alpha", 2_000_000)
        original = modelos.glob.glob
        with mock.patch.object(modelos.glob, "glob",
                               side_effect=original) as espiao, \
             mock.patch.object(modelos, "_agora", return_value=INSTANTE):
            for _ in range(4):
                modelos.modelo_codex(r"D:\a", raiz=self.raiz)
                modelos.modelo_codex(r"D:\b", raiz=self.raiz)
            self.assertEqual(espiao.call_count, 1)

    def test_cache_expira_depois_do_ttl(self):
        self._rollout("19", "rollout-a.jsonl", r"D:\a", "gpt-5.6-terra", 3_000_000)
        original = modelos.glob.glob
        with mock.patch.object(modelos.glob, "glob",
                               side_effect=original) as espiao:
            with mock.patch.object(modelos, "_agora", return_value=INSTANTE):
                modelos.modelo_codex(r"D:\a", raiz=self.raiz)
                modelos.modelo_codex(r"D:\a", raiz=self.raiz)
                self.assertEqual(espiao.call_count, 1)
            depois = INSTANTE + modelos.CACHE_TTL_S + 1
            with mock.patch.object(modelos, "_agora", return_value=depois):
                modelos.modelo_codex(r"D:\a", raiz=self.raiz)
                self.assertEqual(espiao.call_count, 2)

    # Sessao criada DEPOIS do TTL tem que aparecer. Sem isto o cache poderia
    # crescer sem ninguem notar que ele esconde agente novo.
    def test_sessao_nova_aparece_depois_do_ttl(self):
        self._rollout("19", "rollout-a.jsonl", r"D:\a", "gpt-5.6-terra", 3_000_000)
        with mock.patch.object(modelos, "_agora", return_value=INSTANTE):
            self.assertIsNone(modelos.modelo_codex(r"D:\b", raiz=self.raiz))
            self._rollout("19", "rollout-b.jsonl", r"D:\b",
                          "gpt-5.7-alpha", 4_000_000)
            self.assertIsNone(modelos.modelo_codex(r"D:\b", raiz=self.raiz))
        depois = INSTANTE + modelos.CACHE_TTL_S + 1
        with mock.patch.object(modelos, "_agora", return_value=depois):
            self.assertEqual(modelos.modelo_codex(r"D:\b", raiz=self.raiz),
                             "GPT-5.7 Alpha")

    # Duas raizes nao podem compartilhar cache: o painel consulta o Codex e o
    # Antigravity, e no teste cada caso tem a sua pasta temporaria.
    def test_cache_nao_mistura_raizes(self):
        self._rollout("19", "rollout-a.jsonl", r"D:\a", "gpt-5.6-terra", 3_000_000)
        outra = tempfile.TemporaryDirectory()
        self.addCleanup(outra.cleanup)
        self.assertEqual(modelos.modelo_codex(r"D:\a", raiz=self.raiz),
                         "GPT-5.6 Terra")
        self.assertIsNone(modelos.modelo_codex(r"D:\a", raiz=outra.name))

    # O Codex rotaciona sessao enquanto o painel varre: um arquivo pode sumir
    # entre o `glob` e o `stat`. Antes isso derrubava a sonda inteira para None,
    # porque o `sort(key=os.path.getmtime)` levantava OSError no meio.
    def test_arquivo_que_some_entre_o_glob_e_o_stat_so_pula_ele(self):
        sumido = self._rollout("19", "rollout-sumido.jsonl", r"D:\a",
                               "gpt-5.6-terra", 3_000_000)
        self._rollout("19", "rollout-b.jsonl", r"D:\b", "gpt-5.7-alpha", 2_000_000)
        real = os.path.getmtime

        def falha_no_sumido(p):
            if p == sumido:
                raise OSError(2, "sumiu")
            return real(p)

        with mock.patch.object(modelos.os.path, "getmtime",
                               side_effect=falha_no_sumido):
            self.assertEqual(modelos.modelo_codex(r"D:\b", raiz=self.raiz),
                             "GPT-5.7 Alpha")
            self.assertIsNone(modelos.modelo_codex(r"D:\a", raiz=self.raiz))


# Um blob de `executor_metadata` no MOLDE do real, byte a byte onde importa: a
# allowlist configurada pelo usuario vem ANTES do identificador do modelo; o
# caminho do projeto vem depois, com tamanho na frente e uma ETIQUETA de
# protocolo logo atras (`\xda`, medida nos 24 bancos desta maquina que trazem
# caminho); os diretorios internos do proprio Antigravity, que nunca sao o
# projeto, e uma copia do caminho em URI acompanham.
ALLOWLIST = (b"command(*) execute_url(*) read_url(*) mcp(*) escalate_admin(*) "
             b"command(powershell) command(claude) command(gemini) "
             b"command(gh) command(copilot) command(*) unsandboxed(*)")

SKILLS = (b"rzV\x00-C:\\Users\\x\\.gemini\\antigravity-cli\\skills"
          b"\x00\x1eC:\\Users\\x\\.gemini\\skills\x88\x01\x01")


def blob(modelo, cwd=None, ferramentas=b""):
    partes = [ALLOWLIST, b" ", ferramentas, b"\x00\x1a",
              modelo.encode(), b"\x00\x08"]
    if cwd:
        alvo = cwd.encode()
        partes += [SKILLS, b"Z", bytes([len(alvo) + 2]), b"\n",
                   bytes([len(alvo)]), alvo, b"\xda\x02\x00",
                   b"b", bytes([len(alvo) + 8]), b"file:///" + alvo, b"\xda"]
    return b"".join(partes)


class TestModeloAntigravity(_ComRaiz):
    """As datas saem de INSTANTE, nao do relogio: MAX_IDADE_S depende delas."""

    def setUp(self):
        super().setUp()
        p = mock.patch.object(modelos, "_agora", return_value=INSTANTE)
        p.start()
        self.addCleanup(p.stop)

    def _conversa(self, nome, dado, idade_h=0.0):
        """Um banco de conversa do Antigravity, com a tabela que ele usa."""
        pasta = os.path.join(self.raiz, "antigravity-cli", "conversations")
        os.makedirs(pasta, exist_ok=True)
        f = os.path.join(pasta, nome)
        con = sqlite3.connect(f)
        con.execute("create table executor_metadata (idx integer, data blob)")
        if dado is not None:
            con.execute("insert into executor_metadata values (?, ?)", (0, dado))
        con.commit()
        con.close()
        mtime = INSTANTE - idade_h * 3600
        os.utime(f, (mtime, mtime))
        return f

    def test_le_o_banco_mais_recente(self):
        self._conversa("velha.db", blob("gemini-3.6-flash-high"), idade_h=2)
        self._conversa("nova.db", blob("gemini-3.7-flash-high"), idade_h=1)
        self.assertEqual(modelos.modelo_antigravity(raiz=self.raiz),
                         "Gemini 3.7 Flash")

    # O Antigravity roda modelo de outro provedor, e a sonda tem que devolver o
    # que esta la — nao so o que comeca com "gemini".
    def test_conversa_rodando_claude(self):
        self._conversa("nova.db", blob("claude-opus-4-6-thinking"))
        self.assertEqual(modelos.modelo_antigravity(raiz=self.raiz),
                         "Claude Opus 4.6")

    def test_sem_banco_nenhum_e_none(self):
        self.assertIsNone(modelos.modelo_antigravity(raiz=self.raiz))

    def test_banco_sem_a_tabela_nao_levanta(self):
        pasta = os.path.join(self.raiz, "antigravity-cli", "conversations")
        os.makedirs(pasta)
        f = os.path.join(pasta, "vazia.db")
        con = sqlite3.connect(f)
        con.execute("create table outra (x integer)")
        con.commit()
        con.close()
        os.utime(f, (INSTANTE, INSTANTE))
        self.assertIsNone(modelos.modelo_antigravity(raiz=self.raiz))

    def test_arquivo_que_nao_e_sqlite_nao_levanta(self):
        pasta = os.path.join(self.raiz, "antigravity-cli", "conversations")
        os.makedirs(pasta)
        f = os.path.join(pasta, "falsa.db")
        with open(f, "w", encoding="utf-8") as fh:
            fh.write("isto nao e um banco")
        os.utime(f, (INSTANTE, INSTANTE))
        self.assertIsNone(modelos.modelo_antigravity(raiz=self.raiz))

    # A tabela existe mas o blob nao tem identificador de modelo nenhum: nao
    # pode devolver lixo nem levantar.
    def test_blob_sem_modelo_e_none(self):
        self._conversa("sem-modelo.db", b"nada de util aqui")
        self.assertIsNone(modelos.modelo_antigravity(raiz=self.raiz))

    def test_raiz_inexistente_e_none(self):
        self.assertIsNone(modelos.modelo_antigravity(
            raiz=os.path.join(self.raiz, "nao-existe")))

    # --- C3: o ULTIMO casamento, nunca o primeiro ---
    # `claude-flow`, `gemini-cli` e `gpt-researcher` sao nomes reais de MCP. Na
    # allowlist eles vem ANTES do modelo, e `search()` devolvia o nome da
    # ferramenta com cara de modelo.
    def test_nome_de_ferramenta_na_allowlist_nao_vira_modelo(self):
        for i, ferramenta in enumerate((b"mcp(claude-flow)",
                                        b"command(gemini-cli)",
                                        b"mcp(gpt-researcher)")):
            with self.subTest(ferramenta=ferramenta):
                modelos._esquecer()
                self._conversa(f"nova-{i}.db", blob("gemini-3.7-flash-high",
                                                    ferramentas=ferramenta))
                self.assertEqual(modelos.modelo_antigravity(raiz=self.raiz),
                                 "Gemini 3.7 Flash")

    # --- C5: teto de idade ---
    # Um Antigravity aberto agora ainda nao gravou turno nenhum. Sem teto, a
    # sonda entregava o modelo da conversa de ontem com cara de atual.
    def test_banco_velho_demais_e_none(self):
        self._conversa("ontem.db", blob("gemini-3.7-flash-high"),
                       idade_h=modelos.MAX_IDADE_S / 3600 + 1)
        self.assertIsNone(modelos.modelo_antigravity(raiz=self.raiz))

    def test_banco_dentro_do_teto_vale(self):
        self._conversa("hoje.db", blob("gemini-3.7-flash-high"),
                       idade_h=modelos.MAX_IDADE_S / 3600 - 1)
        self.assertEqual(modelos.modelo_antigravity(raiz=self.raiz),
                         "Gemini 3.7 Flash")

    # O teto vale SO na reserva. Quem poe o agente na lista do painel e o herdr
    # ve-lo vivo na tela: se o pane esta vivo em D:\workspace\gcloud e o banco
    # daquele projeto diz `claude-opus-4-6-thinking`, esse E o modelo
    # selecionado — a pessoa so nao mandou nada desde ontem. Descartar por idade
    # apagaria dado certo, e este e o caso REAL desta maquina.
    def test_banco_velho_que_casa_o_cwd_vale(self):
        self._conversa("ontem.db",
                       blob("claude-opus-4-6-thinking", cwd="D:/workspace/gcloud"),
                       idade_h=modelos.MAX_IDADE_S / 3600 + 16)
        self.assertEqual(
            modelos.modelo_antigravity(r"D:\workspace\gcloud", raiz=self.raiz),
            "Claude Opus 4.6")

    # E o buraco que o teto fechou continua fechado: banco velho que NAO casa
    # nao pode voltar pela reserva. E aqui que mora o caso ruim do C5 — um
    # Antigravity aberto agora no projeto X exibindo a conversa de ontem do Y.
    def test_banco_velho_que_nao_casa_nao_serve_de_reserva(self):
        self._conversa("ontem.db",
                       blob("gemini-3.7-flash-high", cwd="D:/workspace/sigad"),
                       idade_h=modelos.MAX_IDADE_S / 3600 + 1)
        self.assertIsNone(modelos.modelo_antigravity(r"D:\workspace\gcloud",
                                                     raiz=self.raiz))

    # Banco recente que nao casa continua servindo de reserva: sem `cwd` que
    # case, a conversa mais nova e a melhor aposta que existe.
    def test_banco_recente_que_nao_casa_serve_de_reserva(self):
        self._conversa("hoje.db",
                       blob("gemini-3.7-flash-high", cwd="D:/workspace/sigad"),
                       idade_h=modelos.MAX_IDADE_S / 3600 - 1)
        self.assertEqual(
            modelos.modelo_antigravity(r"D:\workspace\gcloud", raiz=self.raiz),
            "Gemini 3.7 Flash")

    # Os dois juntos, que e o estado real: o banco do projeto e velho e casa; o
    # de outro projeto e novo e nao casa. O casamento ganha da recencia.
    def test_casamento_velho_ganha_da_reserva_nova(self):
        self._conversa("outro-hoje.db",
                       blob("gemini-3.7-flash-high", cwd="D:/workspace/sigad"),
                       idade_h=0.5)
        self._conversa("gcloud-ontem.db",
                       blob("claude-opus-4-6-thinking", cwd="D:/workspace/gcloud"),
                       idade_h=modelos.MAX_IDADE_S / 3600 + 16)
        self.assertEqual(
            modelos.modelo_antigravity(r"D:\workspace\gcloud", raiz=self.raiz),
            "Claude Opus 4.6")

    # --- C6: banco com metadados vazios ---
    # 5 dos 33 bancos reais tem `executor_metadata` vazia — conversa aberta e
    # ainda sem turno. Pular e certo; o que era errado era o `[:3]` cru, que
    # entregava a conversa seguinte (de outro projeto) sem sinalizar nada.
    def test_banco_com_metadata_vazia_e_pulado(self):
        self._conversa("vazia.db", None, idade_h=0.5)
        self._conversa("cheia.db", blob("gemini-3.6-flash-high"), idade_h=1)
        self.assertEqual(modelos.modelo_antigravity(raiz=self.raiz),
                         "Gemini 3.6 Flash")

    # O `[:3]` cru nao existe mais: quem limita e o teto de idade. Aqui o banco
    # do projeto e o QUINTO mais recente, e todos os cinco estao no teto — com
    # o corte antigo o pane veria o modelo de outro projeto sem sinalizar nada.
    def test_casamento_nao_para_no_terceiro_banco(self):
        for i in range(4):
            self._conversa(f"outra-{i}.db",
                           blob("gemini-3.7-flash-high", cwd=f"D:/workspace/o{i}"),
                           idade_h=i)
        self._conversa("gcloud.db",
                       blob("claude-opus-4-6-thinking", cwd="D:/workspace/gcloud"),
                       idade_h=5)
        self.assertEqual(
            modelos.modelo_antigravity(r"D:\workspace\gcloud", raiz=self.raiz),
            "Claude Opus 4.6")

    # MAX_BANCOS e quem corta agora que o teto de idade so decide a reserva.
    # Sem teste ele viraria o numero decorativo que o `[:3]` era.
    def test_max_bancos_corta_a_varredura(self):
        velho = modelos.MAX_IDADE_S / 3600 + 1
        self._conversa("outro-0.db",
                       blob("gemini-3.7-flash-high", cwd="D:/workspace/a"),
                       idade_h=velho)
        self._conversa("outro-1.db",
                       blob("gemini-3.6-flash-high", cwd="D:/workspace/b"),
                       idade_h=velho + 1)
        self._conversa("gcloud.db",
                       blob("claude-opus-4-6-thinking", cwd="D:/workspace/gcloud"),
                       idade_h=velho + 2)
        with mock.patch.object(modelos, "MAX_BANCOS", 2):
            self.assertIsNone(modelos.modelo_antigravity(
                r"D:\workspace\gcloud", raiz=self.raiz))
        with mock.patch.object(modelos, "MAX_BANCOS", 3):
            self.assertEqual(modelos.modelo_antigravity(
                r"D:\workspace\gcloud", raiz=self.raiz), "Claude Opus 4.6")

    # E com `cwd`, o banco vazio nao pode roubar o casamento de quem tem dado.
    def test_banco_vazio_nao_atrapalha_o_casamento_por_cwd(self):
        self._conversa("vazia.db", None, idade_h=0.5)
        self._conversa("gcloud.db",
                       blob("gemini-3.6-flash-high", cwd="D:/workspace/gcloud"),
                       idade_h=1)
        self.assertEqual(
            modelos.modelo_antigravity(r"D:\workspace\gcloud", raiz=self.raiz),
            "Gemini 3.6 Flash")

    # --- C7: casamento por diretorio ---
    # A spec dizia que o Antigravity nao gravava o cwd. Grava: 24 dos 28 bancos
    # com metadados desta maquina citam o caminho do projeto no mesmo blob.
    def test_casa_o_banco_pelo_cwd(self):
        self._conversa("recente-outro.db",
                       blob("gemini-3.7-flash-high", cwd="D:/workspace/sigad"),
                       idade_h=0.5)
        self._conversa("gcloud.db",
                       blob("claude-opus-4-6-thinking", cwd="D:/workspace/gcloud"),
                       idade_h=2)
        self.assertEqual(
            modelos.modelo_antigravity(r"D:\workspace\gcloud", raiz=self.raiz),
            "Claude Opus 4.6")
        # E o outro pane, no mesmo ciclo, ve o SEU. Era exatamente isto que a
        # limitacao aceita pela spec impedia.
        self.assertEqual(
            modelos.modelo_antigravity(r"D:\workspace\sigad", raiz=self.raiz),
            "Gemini 3.7 Flash")

    # O blob grava com "/" e o herdr entrega com "\" — e a caixa diverge.
    def test_casa_ignorando_caixa_e_separador(self):
        self._conversa("gcloud.db",
                       blob("gemini-3.7-flash-high", cwd="D:/workspace/GCloud"))
        self.assertEqual(
            modelos.modelo_antigravity("d:\\WORKSPACE\\gcloud\\", raiz=self.raiz),
            "Gemini 3.7 Flash")

    # Sem `cwd`, o comportamento antigo: a conversa mais recente.
    def test_sem_cwd_devolve_a_mais_recente(self):
        self._conversa("nova.db",
                       blob("gemini-3.7-flash-high", cwd="D:/workspace/sigad"),
                       idade_h=0.5)
        self._conversa("velha.db",
                       blob("claude-opus-4-6-thinking", cwd="D:/workspace/gcloud"),
                       idade_h=2)
        self.assertEqual(modelos.modelo_antigravity(raiz=self.raiz),
                         "Gemini 3.7 Flash")

    # Sem `cwd` nao ha o que casar: a reserva ja esta decidida no primeiro banco
    # com modelo, e continuar abriria o historico inteiro por nada. E o caminho
    # mais comum da sonda, entao o custo dele merece prova.
    def test_sem_cwd_para_no_primeiro_banco_com_modelo(self):
        for i in range(5):
            self._conversa(f"c-{i}.db", blob("gemini-3.7-flash-high"), idade_h=i)
        original = modelos._metadados
        with mock.patch.object(modelos, "_metadados",
                               side_effect=original) as espiao:
            self.assertEqual(modelos.modelo_antigravity(raiz=self.raiz),
                             "Gemini 3.7 Flash")
            self.assertEqual(espiao.call_count, 1)

    # `cwd` que nao casa com banco nenhum cai para a mais recente — a reserva e
    # a resposta certa justamente quando nao ha como separar as conversas.
    def test_cwd_sem_casamento_cai_para_a_mais_recente(self):
        self._conversa("nova.db",
                       blob("gemini-3.7-flash-high", cwd="D:/workspace/sigad"),
                       idade_h=0.5)
        self.assertEqual(
            modelos.modelo_antigravity(r"D:\workspace\outro", raiz=self.raiz),
            "Gemini 3.7 Flash")

    # Blob sem caminho nenhum (o banco do agente de pesquisa, 4 dos 28 reais):
    # nao casa, e serve de reserva.
    def test_blob_sem_caminho_serve_de_reserva(self):
        self._conversa("pesquisa.db", blob("gemini-3.7-flash-tiered"))
        self.assertEqual(
            modelos.modelo_antigravity(r"D:\workspace\gcloud", raiz=self.raiz),
            "Gemini 3.7 Flash Tiered")

    # A guarda de fronteira: a pasta PAI nao pode casar com o blob da subpasta.
    # As duas existem nesta maquina, cada uma com conversa propria.
    def test_pasta_pai_nao_casa_com_a_subpasta(self):
        self._conversa("gcloud.db",
                       blob("claude-opus-4-6-thinking", cwd="D:/workspace/gcloud"),
                       idade_h=2)
        self._conversa("workspace.db",
                       blob("gemini-3.6-flash-high", cwd="D:/workspace"),
                       idade_h=3)
        self.assertEqual(
            modelos.modelo_antigravity(r"D:\workspace", raiz=self.raiz),
            "Gemini 3.6 Flash")

    # `cwd` vazio e `cwd` None sao a mesma pergunta: "nao sei onde este pane
    # esta". A resposta e a reserva, nunca um casamento com qualquer coisa.
    def test_cwd_vazio_cai_para_a_reserva(self):
        self._conversa("nova.db",
                       blob("gemini-3.7-flash-high", cwd="D:/workspace/sigad"))
        self.assertEqual(modelos.modelo_antigravity("", raiz=self.raiz),
                         "Gemini 3.7 Flash")
        self.assertEqual(modelos.modelo_antigravity(None, raiz=self.raiz),
                         "Gemini 3.7 Flash")


class TestBlobCitaPasta(unittest.TestCase):
    """A regra de fronteira do casamento por diretorio, sem passar pelo banco.

    Vale testar aqui e nao so pela sonda porque o caso perigoso e de UM byte: o
    que vem logo depois do caminho decide se ele acabou ali ou se a pasta e
    outra. Pela sonda, um erro desses se disfarcaria de "caiu na reserva".
    """

    def test_casa_o_caminho_solto(self):
        self.assertTrue(modelos._blob_cita_pasta(
            b"\n\x13D:/workspace/gcloud\xda\x02", r"D:\workspace\gcloud"))

    def test_casa_no_fim_do_blob(self):
        self.assertTrue(modelos._blob_cita_pasta(
            b"lixo\x00D:/workspace/gcloud", r"D:\workspace\gcloud"))

    def test_casa_ignorando_caixa_separador_e_barra_final(self):
        self.assertTrue(modelos._blob_cita_pasta(
            b"\n\x13D:/Workspace/GCloud\xda", "d:\\workspace\\gcloud\\"))

    # As duas pastas existem nesta maquina, cada uma com conversa propria.
    def test_pai_nao_casa_com_subpasta(self):
        self.assertFalse(modelos._blob_cita_pasta(
            b"\n\x13D:/workspace/gcloud\xda", r"D:\workspace"))

    # `D:/workspace` contra `D:/workspace_cd/sigad`: prefixo comum, pastas
    # diferentes. Sao os nomes reais desta maquina.
    def test_prefixo_comum_nao_casa(self):
        self.assertFalse(modelos._blob_cita_pasta(
            b"\n\x15D:/workspace_cd/sigad\xda", r"D:\workspace"))

    # Pasta com espaco no nome existe aqui ("D:/workspace intelliJ/sol"): sem o
    # espaco na lista de continuacao, "D:/workspace" casaria dentro dela.
    def test_espaco_no_nome_nao_deixa_o_pai_casar(self):
        self.assertFalse(modelos._blob_cita_pasta(
            b"\n\x19D:/workspace intelliJ/sol\xda", r"D:\workspace"))
        self.assertTrue(modelos._blob_cita_pasta(
            b"\n\x19D:/workspace intelliJ/sol\xda",
            r"D:\workspace intelliJ\sol"))

    # A copia em URI e o MESMO diretorio: casar por ela nao e engano. O que ela
    # nao pode e responder "sim" para outro caminho.
    def test_copia_em_uri_e_o_mesmo_diretorio(self):
        self.assertTrue(modelos._blob_cita_pasta(
            b"b\x1bfile:///D:/workspace/gcloud\xda", r"D:\workspace\gcloud"))
        self.assertFalse(modelos._blob_cita_pasta(
            b"b\x1bfile:///D:/workspace/gcloud\xda", r"D:\workspace\sigad"))

    # Os diretorios internos do Antigravity estao em todo blob e nunca sao o
    # projeto — a busca pelo cwd do pane simplesmente nao os encontra.
    def test_diretorio_interno_nao_responde_por_projeto(self):
        interno = (b"-C:\\Users\\x\\.gemini\\antigravity-cli\\skills"
                   b"\x00C:\\Users\\x\\.gemini\\skills\x88")
        self.assertFalse(modelos._blob_cita_pasta(interno, r"D:\workspace"))

    def test_cwd_vazio_nao_casa_com_nada(self):
        self.assertFalse(modelos._blob_cita_pasta(b"D:/workspace", ""))
        self.assertFalse(modelos._blob_cita_pasta(b"D:/workspace", None))

    def test_blob_sem_o_caminho_e_falso(self):
        self.assertFalse(modelos._blob_cita_pasta(
            b"\n\x13D:/workspace/gcloud\xda", r"D:\outro"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
