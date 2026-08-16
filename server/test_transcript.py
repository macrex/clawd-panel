#!/usr/bin/env python3
"""Testes da deteccao de turno interrompido.

Todos os transcripts sao SINTETICOS, montados aqui. Ler os transcripts reais da
maquina faria o teste passar ou falhar conforme o que o usuario tivesse feito
naquele dia.
"""

import datetime
import json
import os
import shutil
import tempfile
import unittest

import transcript


def escrever(linhas):
    """Grava um transcript sintetico e devolve o caminho."""
    fd, caminho = tempfile.mkstemp(suffix=".jsonl")
    with os.fdopen(fd, "w", encoding="utf-8") as fh:
        for l in linhas:
            fh.write(l + "\n")
    return caminho


# Linhas no formato real do Claude Code, reduzidas ao que a deteccao olha.
MARCA = ('{"parentUuid":"a","type":"user","message":{"role":"user","content":'
         '[{"type":"text","text":"[Request interrupted by user]"}]}}')
ASSIST = ('{"parentUuid":"b","type":"assistant","message":{"role":"assistant",'
          '"content":[{"type":"text","text":"ok"}]}}')
USUARIO = ('{"parentUuid":"c","type":"user","message":{"role":"user",'
           '"content":"faz isso"}}')
SNAPSHOT = '{"type":"file-history-snapshot","messageId":"x"}'
PROMPT = '{"type":"last-prompt","prompt":"faz isso"}'


class TesteInterrompido(unittest.TestCase):

    def tearDown(self):
        for c in getattr(self, "_arquivos", []):
            try:
                os.unlink(c)
            except OSError:
                pass

    def arquivo(self, linhas):
        c = escrever(linhas)
        self._arquivos = getattr(self, "_arquivos", []) + [c]
        return c

    # ---- o caso que motivou tudo ----

    def test_marca_sem_assistant_depois_e_interrompido(self):
        # A sessao `seeu`: Esc no meio do turno, nenhum Stop, nada depois.
        c = self.arquivo([USUARIO, ASSIST, MARCA])
        self.assertTrue(transcript.interrompido(c))

    def test_marca_seguida_so_de_ruido_ainda_e_interrompido(self):
        # Depois da marca vem snapshot e last-prompt, que nao sao resposta.
        c = self.arquivo([USUARIO, ASSIST, MARCA, SNAPSHOT, PROMPT])
        self.assertTrue(transcript.interrompido(c))

    # ---- o que impede o falso positivo ----

    def test_marca_com_assistant_depois_nao_e_interrompido(self):
        # Voce interrompeu e mandou outra coisa; o Claude respondeu.
        c = self.arquivo([USUARIO, MARCA, USUARIO, ASSIST])
        self.assertFalse(transcript.interrompido(c))

    def test_varias_marcas_vale_a_ULTIMA(self):
        # Uma sessao saudavel acumula marcas. Medido: um transcript de 5912
        # linhas tinha quatro. Se a presenca da marca decidisse, toda sessao
        # com historico seria declarada interrompida.
        c = self.arquivo([MARCA, ASSIST, MARCA, ASSIST, MARCA, ASSIST])
        self.assertFalse(transcript.interrompido(c))

    def test_varias_marcas_e_a_ultima_sem_resposta(self):
        c = self.arquivo([MARCA, ASSIST, MARCA, ASSIST, MARCA])
        self.assertTrue(transcript.interrompido(c))

    def test_sem_marca_nenhuma(self):
        c = self.arquivo([USUARIO, ASSIST, USUARIO, ASSIST])
        self.assertFalse(transcript.interrompido(c))

    # ---- degradacao: na duvida, NAO mexe no estado ----
    # O hook e a fonte da verdade. Esta funcao so tem permissao para contradize-lo
    # quando tem prova; sem arquivo nao ha prova.

    def test_arquivo_inexistente(self):
        self.assertFalse(transcript.interrompido(r"C:\nao\existe\nada.jsonl"))

    def test_caminho_vazio_ou_nulo(self):
        self.assertFalse(transcript.interrompido(None))
        self.assertFalse(transcript.interrompido(""))

    def test_arquivo_vazio(self):
        c = self.arquivo([])
        self.assertFalse(transcript.interrompido(c))

    def test_caminho_e_um_diretorio(self):
        self.assertFalse(transcript.interrompido(tempfile.gettempdir()))

    # ---- a cauda ----

    def test_le_so_a_cauda_e_a_linha_partida_nao_atrapalha(self):
        # Enche o arquivo para a marca cair fora da cauda. Marca fora da cauda
        # significa muita coisa depois dela — ou seja, retomou.
        enchimento = [ASSIST] * 3000
        c = self.arquivo([MARCA] + enchimento)
        self.assertFalse(transcript.interrompido(c, cauda=4096))

    def test_marca_dentro_da_cauda_de_arquivo_grande(self):
        c = self.arquivo([ASSIST] * 3000 + [MARCA])
        self.assertTrue(transcript.interrompido(c, cauda=4096))

    def test_arquivo_menor_que_a_cauda(self):
        c = self.arquivo([MARCA])
        self.assertTrue(transcript.interrompido(c, cauda=1024 * 1024))

    # ---- ruido que nao pode confundir ----

    def test_marca_citada_no_meio_de_uma_frase_nao_conta(self):
        # Conversar SOBRE a interrupcao nao e interromper. Sem este cuidado, esta
        # propria sessao — que passou horas escrevendo a frase da marca em
        # arquivos — se declararia interrompida. Medido no transcript real: a
        # busca solta acha 17 linhas, 3 sao reais, e a ultima solta esta 4.600
        # linhas depois da ultima real.
        #
        # Sem `assistant` depois DE PROPOSITO: e o unico jeito de provar que o
        # filtro da marca funciona sozinho, sem se apoiar na segunda regra.
        citando = ('{"type":"user","message":{"role":"user","content":'
                   '[{"type":"text","text":"por que o [Request interrupted by user] '
                   'nao dispara Stop?"}]}}')
        c = self.arquivo([ASSIST, citando])
        self.assertFalse(transcript.interrompido(c))

    def test_marca_dentro_de_conteudo_de_ferramenta_nao_conta(self):
        # Um arquivo que CONTEM a marca, passado a uma ferramenta, chega ao
        # transcript com as aspas escapadas. E o caso deste proprio teste.
        ferramenta = ('{"type":"assistant","message":{"role":"assistant","content":'
                      '[{"type":"tool_use","name":"Write","input":{"content":'
                      '"{\\"type\\":\\"text\\",\\"text\\":\\"[Request interrupted by user]\\"}"'
                      '}}]}}')
        c = self.arquivo([USUARIO, ferramenta])
        self.assertFalse(transcript.interrompido(c))


def linha_resposta(ts, modelo, entrada=0, saida=0, c5m=0, c1h=0, cread=0,
                   com_iterations=True):
    """Uma linha `assistant` no formato real, reduzida ao que `uso` olha."""
    u = {
        "input_tokens": entrada,
        "output_tokens": saida,
        "cache_read_input_tokens": cread,
        "cache_creation_input_tokens": c5m + c1h,
        "cache_creation": {"ephemeral_5m_input_tokens": c5m,
                           "ephemeral_1h_input_tokens": c1h},
    }
    if com_iterations:
        # O formato real REPETE os totais aqui dentro. Somar este array dobra
        # a conta — e este campo existe no teste exatamente para provar que
        # `uso` nao o soma.
        u["iterations"] = [dict(u)]
    return json.dumps({"type": "assistant", "timestamp": ts,
                       "message": {"model": modelo, "usage": u}},
                      separators=(",", ":"))


class TestUso(unittest.TestCase):
    # 2026-08-04T19:00:00Z e as horas seguintes, em epoch.
    T0 = 1785870000.0

    def carimbo(self, offset):
        d = datetime.datetime.fromtimestamp(self.T0 + offset,
                                            datetime.timezone.utc)
        return d.strftime("%Y-%m-%dT%H:%M:%S.000Z")

    def test_soma_por_modelo(self):
        c = escrever([
            linha_resposta(self.carimbo(10), "claude-opus-5", saida=100),
            linha_resposta(self.carimbo(20), "claude-opus-5", saida=50),
            linha_resposta(self.carimbo(30), "claude-haiku-4-5", saida=7),
        ])
        r = transcript.uso(c, self.T0, self.T0 + 100)
        os.unlink(c)
        self.assertEqual(r["claude-opus-5"]["output"], 150)
        self.assertEqual(r["claude-haiku-4-5"]["output"], 7)

    def test_nao_soma_o_array_iterations(self):
        """O bug que dobraria a conta inteira, em silencio."""
        c = escrever([
            linha_resposta(self.carimbo(10), "claude-opus-5", saida=100),
        ])
        r = transcript.uso(c, self.T0, self.T0 + 100)
        os.unlink(c)
        self.assertEqual(r["claude-opus-5"]["output"], 100)

    def test_separa_cache_por_ttl(self):
        c = escrever([
            linha_resposta(self.carimbo(10), "claude-opus-5", c5m=11, c1h=22),
        ])
        r = transcript.uso(c, self.T0, self.T0 + 100)
        os.unlink(c)
        self.assertEqual(r["claude-opus-5"]["cache_5m"], 11)
        self.assertEqual(r["claude-opus-5"]["cache_1h"], 22)

    def test_sem_cache_creation_cai_no_campo_plano(self):
        """Transcript antigo, sem a quebra por TTL."""
        linha = json.dumps({
            "type": "assistant", "timestamp": self.carimbo(10),
            "message": {"model": "claude-opus-5",
                        "usage": {"cache_creation_input_tokens": 500}}},
            separators=(",", ":"))
        c = escrever([linha])
        r = transcript.uso(c, self.T0, self.T0 + 100)
        os.unlink(c)
        self.assertEqual(r["claude-opus-5"]["cache_5m"], 500)
        self.assertEqual(r["claude-opus-5"]["cache_1h"], 0)

    def test_fatia_pelo_carimbo(self):
        c = escrever([
            linha_resposta(self.carimbo(-100), "claude-opus-5", saida=1),
            linha_resposta(self.carimbo(50), "claude-opus-5", saida=10),
            linha_resposta(self.carimbo(999), "claude-opus-5", saida=1),
        ])
        r = transcript.uso(c, self.T0, self.T0 + 100)
        os.unlink(c)
        self.assertEqual(r["claude-opus-5"]["output"], 10)

    def test_pula_o_modelo_synthetic(self):
        linha = json.dumps({
            "type": "assistant", "timestamp": self.carimbo(10),
            "message": {"model": "<synthetic>", "usage": {}}},
            separators=(",", ":"))
        c = escrever([linha])
        r = transcript.uso(c, self.T0, self.T0 + 100)
        os.unlink(c)
        self.assertEqual(r, {})

    def test_ignora_linha_que_nao_e_assistant(self):
        c = escrever([
            json.dumps({"type": "user", "timestamp": self.carimbo(10),
                        "message": {"content": "oi"}},
                       separators=(",", ":")),
            linha_resposta(self.carimbo(11), "claude-opus-5", saida=3),
        ])
        r = transcript.uso(c, self.T0, self.T0 + 100)
        os.unlink(c)
        self.assertEqual(list(r), ["claude-opus-5"])

    def test_linha_truncada_nao_derruba_o_arquivo(self):
        c = escrever([
            '{"type":"assistant","timest',
            linha_resposta(self.carimbo(10), "claude-opus-5", saida=5),
        ])
        r = transcript.uso(c, self.T0, self.T0 + 100)
        os.unlink(c)
        self.assertEqual(r["claude-opus-5"]["output"], 5)

    def test_uma_cobranca_por_mensagem_e_nao_por_linha(self):
        """A armadilha mais cara do formato.

        O transcript grava UMA LINHA POR BLOCO DE CONTEUDO — texto, cada
        tool_use — e todas repetem o `usage` da mensagem inteira. Medido numa
        sessao real: 346 linhas `assistant` para 161 mensagens, inflando a
        conta em 2,1x. Foi assim que o custo do painel ficaria com o dobro do
        valor sem nada denunciar.
        """
        linhas = []
        for _ in range(4):      # quatro blocos da MESMA resposta
            d = json.loads(linha_resposta(self.carimbo(10), "claude-opus-5",
                                          saida=100, cread=500))
            d["message"]["id"] = "msg_abc"
            d["requestId"] = "req_abc"
            linhas.append(json.dumps(d, separators=(",", ":")))
        c = escrever(linhas)
        r = transcript.uso(c, self.T0, self.T0 + 100)
        os.unlink(c)
        self.assertEqual(r["claude-opus-5"]["output"], 100)
        self.assertEqual(r["claude-opus-5"]["cache_read"], 500)

    def test_mensagens_diferentes_somam(self):
        linhas = []
        for i, ident in enumerate(("msg_a", "msg_b")):
            d = json.loads(linha_resposta(self.carimbo(10 + i), "claude-opus-5",
                                          saida=100))
            d["message"]["id"] = ident
            linhas.append(json.dumps(d, separators=(",", ":")))
        c = escrever(linhas)
        r = transcript.uso(c, self.T0, self.T0 + 100)
        os.unlink(c)
        self.assertEqual(r["claude-opus-5"]["output"], 200)

    def test_sem_id_nenhum_conta_a_linha(self):
        """Subcontar por medo trocaria um erro conhecido por outro."""
        c = escrever([
            linha_resposta(self.carimbo(10), "claude-opus-5", saida=100),
            linha_resposta(self.carimbo(11), "claude-opus-5", saida=100),
        ])
        r = transcript.uso(c, self.T0, self.T0 + 100)
        os.unlink(c)
        self.assertEqual(r["claude-opus-5"]["output"], 200)

    def test_arquivo_inexistente_devolve_vazio(self):
        self.assertEqual(transcript.uso("/nao/existe.jsonl", 0, 9e9), {})
        self.assertEqual(transcript.uso(None, 0, 9e9), {})


class TestSubagentes(unittest.TestCase):
    """O trabalho delegado mora noutro arquivo, e custa dinheiro igual.

    Medido no acervo real: 166 transcripts de subagente, e TODO o Sonnet 5
    esta neles. Ler so o arquivo da sessao subconta o turno que delegou.
    """

    T0 = 1785870000.0

    def carimbo(self, offset):
        d = datetime.datetime.fromtimestamp(self.T0 + offset,
                                            datetime.timezone.utc)
        return d.strftime("%Y-%m-%dT%H:%M:%S.000Z")

    def montar(self, principais, delegados):
        """Uma sessao com <sessao>.jsonl e <sessao>/subagents/*.jsonl."""
        raiz = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, raiz, ignore_errors=True)
        sessao = os.path.join(raiz, "sesid.jsonl")
        with open(sessao, "w", encoding="utf-8") as fh:
            fh.write("\n".join(principais) + "\n")
        if delegados is not None:
            pasta = os.path.join(raiz, "sesid", "subagents")
            os.makedirs(pasta)
            with open(os.path.join(pasta, "agent-a.jsonl"), "w",
                      encoding="utf-8") as fh:
                fh.write("\n".join(delegados) + "\n")
        return sessao

    def test_soma_o_subagente_ao_turno(self):
        c = self.montar(
            [linha_resposta(self.carimbo(10), "claude-opus-5", saida=100)],
            [linha_resposta(self.carimbo(15), "claude-sonnet-5", saida=900)])
        r = transcript.uso(c, self.T0, self.T0 + 100)
        self.assertEqual(r["claude-opus-5"]["output"], 100)
        self.assertEqual(r["claude-sonnet-5"]["output"], 900)

    def test_mesmo_modelo_soma_nos_dois_arquivos(self):
        c = self.montar(
            [linha_resposta(self.carimbo(10), "claude-opus-5", saida=100)],
            [linha_resposta(self.carimbo(15), "claude-opus-5", saida=25)])
        r = transcript.uso(c, self.T0, self.T0 + 100)
        self.assertEqual(r["claude-opus-5"]["output"], 125)

    def test_subagente_fora_da_janela_nao_entra(self):
        """Um subagente de OUTRO turno da mesma sessao."""
        c = self.montar(
            [linha_resposta(self.carimbo(10), "claude-opus-5", saida=100)],
            [linha_resposta(self.carimbo(9999), "claude-sonnet-5", saida=900)])
        r = transcript.uso(c, self.T0, self.T0 + 100)
        self.assertNotIn("claude-sonnet-5", r)

    def test_sessao_que_nunca_delegou(self):
        c = self.montar(
            [linha_resposta(self.carimbo(10), "claude-opus-5", saida=100)],
            None)
        r = transcript.uso(c, self.T0, self.T0 + 100)
        self.assertEqual(r["claude-opus-5"]["output"], 100)

    def test_da_para_desligar(self):
        c = self.montar(
            [linha_resposta(self.carimbo(10), "claude-opus-5", saida=100)],
            [linha_resposta(self.carimbo(15), "claude-sonnet-5", saida=900)])
        r = transcript.uso(c, self.T0, self.T0 + 100, com_subagentes=False)
        self.assertNotIn("claude-sonnet-5", r)

    def test_subagentes_de_caminho_invalido(self):
        self.assertEqual(transcript.subagentes(None), [])
        self.assertEqual(transcript.subagentes("/nao/existe.jsonl"), [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
