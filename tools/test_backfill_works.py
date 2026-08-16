#!/usr/bin/env python3
"""Testes da reconstrucao do livro-caixa a partir de transcripts.

A logica que estas provas seguram e a SEGMENTACAO. Ela decide onde um turno
comeca e termina, e um erro ali nao aparece como falha: aparece como um
livro-caixa inteiro com numeros plausiveis e errados.

O caso que mais engana: a maioria das linhas `user` de um transcript nao e o
usuario. Sao resultados de ferramenta voltando para o modelo. Medido num
transcript real de 7 turnos — 53 linhas `user` de ferramenta contra 7 humanas.
"""

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import backfill_works as bf                               # noqa: E402


def humano(txt, t):
    return {"type": "user", "timestamp": t, "origin": {"kind": "human"},
            "promptSource": "typed", "message": {"content": txt}}


def ferramenta(t):
    """Resultado de ferramenta: e uma linha `user`, mas NAO e o usuario."""
    return {"type": "user", "timestamp": t,
            "message": {"content": [{"type": "tool_result", "content": "ok"}]}}


def resposta(t, out=100, cache_read=0, cache_new=0, tools=(), texto=""):
    conteudo = [{"type": "text", "text": texto}] if texto else []
    conteudo += [{"type": "tool_use", "name": n, "id": "x", "input": {}} for n in tools]
    return {"type": "assistant", "timestamp": t, "cwd": "E:\\proj\\esp32-s3",
            "gitBranch": "main", "effort": "high",
            "message": {"model": "claude-opus-5", "content": conteudo,
                        "usage": {"output_tokens": out,
                                  "cache_read_input_tokens": cache_read,
                                  "cache_creation_input_tokens": cache_new}}}


class TestSegmentacao(unittest.TestCase):

    def escrever(self, linhas):
        fd, caminho = tempfile.mkstemp(suffix=".jsonl")
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            for d in linhas:
                f.write(json.dumps(d) + "\n")
        self.addCleanup(os.unlink, caminho)
        return caminho

    def test_resultado_de_ferramenta_nao_abre_turno_novo(self):
        # O erro que produziria um livro-caixa cheio de turnos de dois segundos.
        c = self.escrever([
            humano("faz isso", "2026-08-04T10:00:00Z"),
            resposta("2026-08-04T10:00:10Z", tools=["Bash"]),
            ferramenta("2026-08-04T10:00:11Z"),
            resposta("2026-08-04T10:00:20Z", tools=["Edit"]),
            ferramenta("2026-08-04T10:00:21Z"),
            resposta("2026-08-04T10:01:00Z"),
        ])
        regs = bf.reconstruir(c, "sid12345678")
        self.assertEqual(len(regs), 1)
        self.assertEqual(regs[0]["respostas"], 3)
        self.assertEqual(regs[0]["tools"], 2)
        self.assertAlmostEqual(regs[0]["seconds"], 60.0)

    def test_cada_mensagem_humana_abre_um_turno(self):
        c = self.escrever([
            humano("um", "2026-08-04T10:00:00Z"),
            resposta("2026-08-04T10:00:30Z"),
            humano("dois", "2026-08-04T10:05:00Z"),
            resposta("2026-08-04T10:06:00Z"),
        ])
        regs = bf.reconstruir(c, "sid12345678")
        self.assertEqual(len(regs), 2)
        self.assertAlmostEqual(regs[0]["seconds"], 30.0)
        self.assertAlmostEqual(regs[1]["seconds"], 60.0)

    def test_prompt_sem_resposta_nao_vira_registro(self):
        # O turno que esta acontecendo agora. Entra quando terminar.
        c = self.escrever([
            humano("um", "2026-08-04T10:00:00Z"),
            resposta("2026-08-04T10:00:30Z"),
            humano("dois", "2026-08-04T10:05:00Z"),
        ])
        self.assertEqual(len(bf.reconstruir(c, "sid12345678")), 1)

    def test_transcript_que_comeca_no_meio_de_um_turno(self):
        # Acontece depois de uma compactacao: as primeiras linhas sao respostas
        # sem o prompt que as originou. Nao da para atribuir a turno nenhum.
        c = self.escrever([
            resposta("2026-08-04T09:59:00Z"),
            humano("agora sim", "2026-08-04T10:00:00Z"),
            resposta("2026-08-04T10:00:30Z"),
        ])
        regs = bf.reconstruir(c, "sid12345678")
        self.assertEqual(len(regs), 1)
        self.assertEqual(regs[0]["respostas"], 1)

    def test_linha_corrompida_e_pulada(self):
        fd, caminho = tempfile.mkstemp(suffix=".jsonl")
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(json.dumps(humano("um", "2026-08-04T10:00:00Z")) + "\n")
            f.write('{"type": "assistant", "timesta\n')
            f.write(json.dumps(resposta("2026-08-04T10:00:30Z")) + "\n")
        self.addCleanup(os.unlink, caminho)
        regs = bf.reconstruir(caminho, "sid12345678")
        self.assertEqual(len(regs), 1)

    def test_arquivo_inexistente_devolve_vazio(self):
        self.assertEqual(bf.reconstruir("nao/existe/mesmo.jsonl", "sid"), [])


class TestNumeros(unittest.TestCase):

    def escrever(self, linhas):
        fd, caminho = tempfile.mkstemp(suffix=".jsonl")
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            for d in linhas:
                f.write(json.dumps(d) + "\n")
        self.addCleanup(os.unlink, caminho)
        return caminho

    def test_tokens_sao_somados_no_turno(self):
        c = self.escrever([
            humano("x", "2026-08-04T10:00:00Z"),
            resposta("2026-08-04T10:00:10Z", out=100, cache_read=1000, cache_new=50),
            resposta("2026-08-04T10:00:20Z", out=250, cache_read=2000, cache_new=10),
        ])
        r = bf.reconstruir(c, "sid12345678")[0]
        # Os tokens agora sao POR MODELO, e nao quatro contadores planos: um
        # turno pode misturar modelos, e o balde unico perdia isso.
        tok = r["tokens"]["claude-opus-5"]
        self.assertEqual(tok["output"], 350)
        self.assertEqual(tok["cache_read"], 3000)
        # A fixture usa o campo plano `cache_creation_input_tokens`, sem a
        # quebra por TTL — entao cai em 5m, o mais barato.
        self.assertEqual(tok["cache_5m"], 60)
        self.assertEqual(tok["cache_1h"], 0)
        self.assertEqual(r["model_id"], "claude-opus-5")

    def test_maior_vao_e_o_maior_intervalo_entre_respostas(self):
        # O numero que separa "tinha muito a fazer" de "ficou parado".
        c = self.escrever([
            humano("x", "2026-08-04T10:00:00Z"),
            resposta("2026-08-04T10:00:05Z"),
            resposta("2026-08-04T10:02:05Z"),      # vao de 2 min
            resposta("2026-08-04T10:02:15Z"),
        ])
        r = bf.reconstruir(c, "sid12345678")[0]
        self.assertAlmostEqual(r["gap_seconds"], 120.0)

    def test_o_que_o_transcript_nao_sabe_sai_nulo(self):
        # Custo, linhas, tempo de API e bloqueio so existem no caminho ao vivo.
        # Zero seria afirmar que nao houve.
        c = self.escrever([
            humano("x", "2026-08-04T10:00:00Z"),
            resposta("2026-08-04T10:00:10Z"),
        ])
        r = bf.reconstruir(c, "sid12345678")[0]
        for campo in ("cost_usd", "lines_added", "lines_removed",
                      "api_seconds", "blocked_seconds", "blocks"):
            self.assertIsNone(r[campo], campo)

    def test_registro_reconstruido_e_sempre_marcado(self):
        # Para nunca ser confundido com medicao direta, e para o script poder
        # rodar de novo sem duplicar.
        c = self.escrever([
            humano("x", "2026-08-04T10:00:00Z"),
            resposta("2026-08-04T10:00:10Z"),
        ])
        self.assertTrue(bf.reconstruir(c, "sid12345678")[0]["reconstruido"])

    def test_interrupcao_e_detectada(self):
        c = self.escrever([
            humano("x", "2026-08-04T10:00:00Z"),
            resposta("2026-08-04T10:00:10Z"),
            {"type": "user", "timestamp": "2026-08-04T10:00:20Z",
             "message": {"content": [
                 {"type": "text", "text": "[Request interrupted by user]"}]}},
        ])
        self.assertTrue(bf.reconstruir(c, "sid12345678")[0]["interrupted"])

    def test_identidade_sai_do_transcript(self):
        c = self.escrever([
            humano("x", "2026-08-04T10:00:00Z"),
            resposta("2026-08-04T10:00:10Z"),
        ])
        r = bf.reconstruir(c, "817d452c-2ff3-4844")[0]
        self.assertEqual(r["session_id"], "817d452c")   # os 8 de sempre
        self.assertEqual(r["repo"], "esp32-s3")
        self.assertEqual(r["branch"], "main")
        self.assertEqual(r["model"], "claude-opus-5")
        self.assertEqual(r["effort"], "high")


class TestSubagentes(unittest.TestCase):
    """O glob que ignorava 36% do acervo."""

    def test_rglob_alcanca_transcript_de_subagente(self):
        import shutil
        raiz = Path(tempfile.mkdtemp())
        try:
            (raiz / "proj").mkdir()
            (raiz / "proj" / "sessao" / "subagents").mkdir(parents=True)
            (raiz / "proj" / "principal.jsonl").write_text("", encoding="utf-8")
            (raiz / "proj" / "sessao" / "subagents" / "a.jsonl").write_text(
                "", encoding="utf-8")

            antigo = sorted(p.name for p in raiz.glob("*/*.jsonl"))
            novo = sorted(p.name for p in raiz.rglob("*.jsonl"))

            self.assertEqual(antigo, ["principal.jsonl"])
            self.assertEqual(novo, ["a.jsonl", "principal.jsonl"])
        finally:
            shutil.rmtree(raiz)


class TestModeloPrincipal(unittest.TestCase):

    def test_model_id_e_o_de_maior_saida_nao_o_ultimo(self):
        """Num turno misto o ultimo a responder pode ser o Haiku de uma tarefa
        de fundo, que nao descreve o turno."""
        w = {"sid": "s1", "started": 0.0, "ended": 10.0, "respostas": 2,
             "tools": [], "interrompido": False, "maior_vao": 0.0,
             "por_modelo": {"claude-opus-5": {"output": 5000},
                            "claude-haiku-4-5": {"output": 7}},
             "model": "claude-haiku-4-5", "effort": None,
             "repo": None, "branch": None}
        reg = bf.registro(w)
        self.assertEqual(reg["model_id"], "claude-opus-5")
        self.assertEqual(reg["tokens"]["claude-haiku-4-5"]["output"], 7)
        self.assertIs(reg["fast"], False)

    def test_subagente_e_somado_ao_turno_que_o_gerou(self):
        """O caso que o rglob sozinho NAO resolvia: um transcript de subagente
        nao tem linha humana, entao nunca abre turno por si."""
        import shutil
        raiz = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, raiz, ignore_errors=True)
        sessao = raiz / "sesid.jsonl"
        with open(sessao, "w", encoding="utf-8") as f:
            for d in (humano("x", "2026-08-04T10:00:00Z"),
                      resposta("2026-08-04T10:00:10Z", out=100)):
                f.write(json.dumps(d) + "\n")
        pasta = raiz / "sesid" / "subagents"
        pasta.mkdir(parents=True)
        delegado = resposta("2026-08-04T10:00:05Z", out=900)
        delegado["message"]["model"] = "claude-sonnet-5"
        with open(pasta / "agent-a.jsonl", "w", encoding="utf-8") as f:
            f.write(json.dumps(delegado) + "\n")

        r = bf.reconstruir(str(sessao), "sesid")[0]
        self.assertEqual(r["tokens"]["claude-opus-5"]["output"], 100)
        self.assertEqual(r["tokens"]["claude-sonnet-5"]["output"], 900)
        self.assertIs(r["delegado"], True)
        # O principal continua sendo quem produziu mais saida — aqui, o
        # subagente. O turno custou o que custou.
        self.assertEqual(r["model_id"], "claude-sonnet-5")

    def test_uma_cobranca_por_mensagem_e_nao_por_linha(self):
        """O defeito que inflava a conta em 2,1x, e que e anterior a este
        trabalho: o transcript grava uma linha por BLOCO DE CONTEUDO e todas
        repetem o `usage` da mensagem inteira."""
        linhas = [humano("x", "2026-08-04T10:00:00Z")]
        for _ in range(4):      # quatro blocos da MESMA resposta
            d = resposta("2026-08-04T10:00:10Z", out=100, cache_read=500)
            d["message"]["id"] = "msg_abc"
            d["requestId"] = "req_abc"
            linhas.append(d)
        c = self.escrever(linhas) if hasattr(self, "escrever") else None
        if c is None:
            fd, c = tempfile.mkstemp(suffix=".jsonl")
            with os.fdopen(fd, "w", encoding="utf-8") as f:
                for d in linhas:
                    f.write(json.dumps(d) + "\n")
            self.addCleanup(os.unlink, c)
        r = bf.reconstruir(c, "sid12345678")[0]
        tok = r["tokens"]["claude-opus-5"]
        self.assertEqual(tok["output"], 100)
        self.assertEqual(tok["cache_read"], 500)

    def test_turno_sem_modelo_nenhum(self):
        w = {"sid": "s1", "started": 0.0, "ended": 1.0, "respostas": 0,
             "tools": [], "interrompido": False, "maior_vao": 0.0,
             "por_modelo": {}, "model": None, "effort": None,
             "repo": None, "branch": None}
        reg = bf.registro(w)
        self.assertIsNone(reg["model_id"])
        self.assertEqual(reg["tokens"], {})


if __name__ == "__main__":
    unittest.main(verbosity=2)
