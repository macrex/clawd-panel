# server/test_bloqueio.py
import unittest

import bloqueio

# Forma real do prompt de permissao do Claude Code: caixa desenhada, o comando
# em cima, a pergunta, as opcoes numeradas com o cursor na primeira, e o rodape
# que diz como confirmar.
CLAUDE = """\
╭──────────────────────────────────────────────────────╮
│ Bash command                                         │
│                                                      │
│   rm -rf build/                                      │
│   Remove the build directory                         │
│                                                      │
│ Do you want to proceed?                              │
│ ❯ 1. Yes                                             │
│   2. Yes, and don't ask again for rm commands        │
│   3. No, and tell Claude what to do differently      │
│                                                      │
╰──────────────────────────────────────────────────────╯
  Enter to confirm · Esc to cancel
"""


# Forma real do prompt de permissao do CODEX, capturada da tela em 14/08. Ela
# difere da do Claude Code em tres pontos que quebravam o parser: o cursor e `›`
# e nao `❯`, a pergunta nao mora colada na lista (entre as duas ficam o comando
# e o indicador de corte da tela), e o rotulo fala em "Codex" e nao em "Claude".
CODEX = """\
• PreToolUse hook (failed)
  error: hook exited with code 1


  Would you like to run the following command?

  Environment: local

  Reason: Voce permite remover do Registro as entradas de inicializacao do
  Logitech Download Assistant?

  $ $ErrorActionPreference='SilentlyContinue'
  $targets=@('Logitech Download Assistant')
  if($removed.Count){$removed | Format-List}else{'No matching Registry entry'}

  [… 37 lines] ctrl + a view all

› 1. Yes, proceed (y)
  2. No, and tell Codex what to do differently (esc)

  Press enter to confirm or esc to cancel
"""


# Forma real do seletor do AskUserQuestion, capturada da tela em 14/08. O que ela
# tem de diferente e o BLOCO DE TAREFAS que o Claude Code desenha ABAIXO do
# rodape: sao sete linhas depois do "Enter to select", e era isso que empurrava a
# marca para fora da janela medida a partir do fim da tela.
PERGUNTA_COM_TAREFAS = """\
  Nota do vault

  No /cpv, a parte "vault" deve criar uma nota nova de Evolucao a cada invocacao?

❯ 1. Uma nota por leva (recomendado)
     Se ja existe nota do mesmo tema, eu edito ela in-place; so crio nota nova
     quando o assunto muda.
  2. Uma nota por invocacao
     Toda vez que rodar /cpv nasce uma nota nova em Evolucoes/.
  3. Eu decido na hora
     O /cpv me mostra o diff resumido e pergunta.
  4. Type something.
  ─────────────────
  5. Chat about this

  Enter to select · up/down to navigate · Esc to cancel

  6 tasks (1 done, 1 in progress, 4 open)
    [x] Perguntas de esclarecimento
    [ ] Propor abordagens com trade-offs
    [ ] Apresentar design e obter aprovacao
    [ ] Escrever specs no vault Obsidian
    [ ] Auto-revisao das specs
    ... +1 completed
"""


class TesteFormulario(unittest.TestCase):

    def test_le_o_prompt_do_claude_code(self):
        f = bloqueio.parse_form(CLAUDE)
        self.assertIsNotNone(f)
        self.assertEqual(f["pergunta"], "Do you want to proceed?")
        self.assertEqual([o["n"] for o in f["opcoes"]], [1, 2, 3])
        self.assertEqual(f["opcoes"][0]["rotulo"], "Yes")
        self.assertEqual(f["opcoes"][1]["rotulo"],
                         "Yes, and don't ask again for rm commands")
        # O cursor e o INDICE na lista, nao o numero da opcao: e ele que diz
        # quantas setas faltam para chegar no alvo.
        self.assertEqual(f["cursor"], 0)

    def test_le_o_prompt_do_codex(self):
        # O cursor do Codex e `›`. Sem ele na expressao, a opcao 1 nao casava e
        # a lista era recusada por nao comecar no 1 — o painel caia na cauda da
        # tela e mostrava o comando em vez da pergunta.
        f = bloqueio.parse_form(CODEX)
        self.assertIsNotNone(f)
        self.assertEqual([o["n"] for o in f["opcoes"]], [1, 2])
        self.assertEqual(f["opcoes"][0]["rotulo"], "Yes, proceed (y)")
        self.assertEqual(f["cursor"], 0)

    def test_pergunta_do_codex_vem_do_reason(self):
        # No Codex a lista NAO fica colada na pergunta: entre as duas ficam o
        # comando e o "[… N lines]". Subir quatro linhas traria o script.
        p = bloqueio.parse_form(CODEX)["pergunta"]
        self.assertIn("Voce permite remover do Registro", p)
        self.assertNotIn("ErrorActionPreference", p)
        self.assertNotIn("view all", p)

    def test_opcao_de_texto_do_codex_e_marcada(self):
        # "tell Codex what to do differently" abre um campo de texto igual ao
        # "tell Claude": escolher nao resolve o bloqueio, so troca de pergunta.
        ops = bloqueio.parse_form(CODEX)["opcoes"]
        self.assertFalse(ops[0]["texto"])
        self.assertTrue(ops[1]["texto"])

    def test_cursor_na_terceira_opcao(self):
        texto = CLAUDE.replace("│ ❯ 1. Yes", "│   1. Yes")
        texto = texto.replace("│   3. No,", "│ ❯ 3. No,")
        self.assertEqual(bloqueio.parse_form(texto)["cursor"], 2)

    def test_sem_rodape_de_confirmacao_nao_e_formulario(self):
        # Uma lista numerada solta na tela nao e um seletor navegavel. Sem o
        # rodape, responder por setas mandaria Enter numa tela qualquer.
        sem = CLAUDE.replace("  Enter to confirm · Esc to cancel", "")
        self.assertIsNone(bloqueio.parse_form(sem))

    def test_rodape_antes_da_lista_nao_vale(self):
        # "enter to select" que o agente imprimiu ACIMA da lista nao prova nada:
        # o rodape de um seletor vem sempre depois dele.
        sem = CLAUDE.replace("  Enter to confirm · Esc to cancel", "")
        texto = "press enter to select something\n" + sem + "\n" * 8
        self.assertIsNone(bloqueio.parse_form(texto))

    def test_rodape_longe_demais_da_lista_nao_vale(self):
        # A janela e curta de proposito: um "enter to select" solto quinze linhas
        # abaixo de uma lista numerada do agente nao pode virar formulario.
        sem = CLAUDE.replace("  Enter to confirm · Esc to cancel", "")
        texto = sem + "\n" * 15 + "  Enter to confirm · Esc to cancel\n"
        self.assertIsNone(bloqueio.parse_form(texto))

    def test_tela_lida_no_meio_do_redesenho_e_relida(self):
        # Medido em 14/08: a fase de revisao do AskUserQuestion chegou ao painel
        # sem botao, e a tela lida terminava na ultima opcao — a lista ja
        # desenhada e o rodape ainda nao. Sem releitura, o cache serve a cauda
        # por RELER_S segundos e a tela que TEM opcoes fica sem botao.
        parcial = CLAUDE.replace("  Enter to confirm · Esc to cancel", "")
        telas = [parcial, CLAUDE]
        chamadas = []

        def ler(pane):
            chamadas.append(pane)
            return telas[min(len(chamadas) - 1, len(telas) - 1)]

        bloqueio.esquecer()
        b = bloqueio.atual([{"session_id": "a", "state": "blocked",
                             "pane_id": "w0:p1"}], ler=ler, agora=lambda: 100.0)
        self.assertEqual(len(chamadas), 2, "devia ter relido a tela parcial")
        self.assertEqual([o["n"] for o in b["opcoes"]], [1, 2, 3])

    def test_tela_sem_lista_nenhuma_nao_paga_releitura(self):
        # A releitura custa um subprocess e uma espera. Ela so se justifica
        # quando ha indicio de seletor na tela; texto corrido nao releva nada.
        chamadas = []

        def ler(pane):
            chamadas.append(pane)
            return "o agente escreveu isto e parou\nsem lista nenhuma\n"

        bloqueio.esquecer()
        b = bloqueio.atual([{"session_id": "a", "state": "blocked",
                             "pane_id": "w0:p1"}], ler=ler, agora=lambda: 100.0)
        self.assertEqual(len(chamadas), 1)
        self.assertEqual(b["opcoes"], [])

    def test_bloco_de_tarefas_abaixo_do_rodape_nao_esconde_a_lista(self):
        # O Claude Code desenha a lista de tarefas DEPOIS do rodape. Medindo a
        # janela do fim da tela, a marca ficava fora e a pergunta chegava ao
        # painel como texto sem botao — a tela inteira do brainstorm.
        f = bloqueio.parse_form(PERGUNTA_COM_TAREFAS)
        self.assertIsNotNone(f)
        self.assertEqual([o["n"] for o in f["opcoes"]], [1, 2, 3, 4, 5])
        self.assertEqual(f["opcoes"][0]["rotulo"],
                         "Uma nota por leva (recomendado)")
        self.assertEqual(f["cursor"], 0)
        self.assertIn("nota nova de Evolucao", f["pergunta"])

    def test_opcoes_de_texto_do_askuserquestion_sao_marcadas(self):
        # "Type something." e "Chat about this" abrem campo de texto: escolher
        # uma delas nao resolve o bloqueio, so troca de pergunta.
        ops = bloqueio.parse_form(PERGUNTA_COM_TAREFAS)["opcoes"]
        self.assertFalse(ops[0]["texto"])
        self.assertTrue(ops[3]["texto"])
        self.assertTrue(ops[4]["texto"])

    def test_lista_numerada_do_agente_acima_fica_de_fora(self):
        # O agente escreveu os proprios passos numerados antes de perguntar. Se
        # eles entrassem, o painel ofereceria botoes que nao selecionam nada.
        texto = ("  1. Primeiro eu leio o arquivo\n"
                 "  2. Depois eu apago a pasta\n"
                 "\n") + CLAUDE
        f = bloqueio.parse_form(texto)
        self.assertEqual(len(f["opcoes"]), 3)
        self.assertEqual(f["opcoes"][0]["rotulo"], "Yes")

    def test_regua_no_meio_da_lista_nao_quebra(self):
        texto = CLAUDE.replace(
            "│   2. Yes, and don't ask again for rm commands",
            "│ ──────────────────────────────────\n"
            "│   2. Yes, and don't ask again for rm commands")
        f = bloqueio.parse_form(texto)
        self.assertEqual([o["n"] for o in f["opcoes"]], [1, 2, 3])

    def test_uma_opcao_so_nao_e_escolha(self):
        texto = ("│ Do you want to proceed?\n"
                 "│ ❯ 1. Yes\n"
                 "  Enter to confirm\n")
        self.assertIsNone(bloqueio.parse_form(texto))

    def test_numeracao_que_nao_comeca_no_um_e_recusada(self):
        # Sem o 1 na tela nao da para saber se a lista foi cortada pelo scroll —
        # e contar setas a partir de uma lista incompleta erra o alvo.
        texto = ("│ Do you want to proceed?\n"
                 "│ ❯ 2. Yes\n"
                 "│   3. No\n"
                 "  Enter to confirm\n")
        self.assertIsNone(bloqueio.parse_form(texto))

    def test_a_pergunta_para_na_linha_em_branco(self):
        # Acima da pergunta vem o comando ou o diff. Sem esse corte, a "pergunta"
        # traria o comando junto e nao caberia na tela.
        f = bloqueio.parse_form(CLAUDE)
        self.assertNotIn("rm -rf", f["pergunta"])

    def test_pergunta_de_varias_linhas_vira_uma_so(self):
        texto = ("│ Claude quer editar o arquivo\n"
                 "│ src/main.cpp fora do diretorio do projeto\n"
                 "│ ❯ 1. Yes\n"
                 "│   2. No\n"
                 "  Enter to confirm\n")
        self.assertEqual(
            bloqueio.parse_form(texto)["pergunta"],
            "Claude quer editar o arquivo src/main.cpp fora do diretorio do projeto")

    def test_rotulo_longo_e_cortado(self):
        longo = ("Sim, e nao pergunte de novo para comandos de remocao de "
                 "arquivos neste diretorio")
        texto = ("│ Prosseguir?\n"
                 "│ ❯ 1. " + longo + "\n"
                 "│   2. No\n"
                 "  Enter to confirm\n")
        r = bloqueio.parse_form(texto)["opcoes"][0]["rotulo"]
        self.assertLessEqual(len(r.encode()), bloqueio.ROTULO_MAX_B + 3)
        self.assertTrue(r.endswith("..."))

    def test_cortar_nunca_parte_caractere_utf8(self):
        # `cortar` corta por BYTE porque o firmware guarda em buffer de tamanho
        # fixo. Sozinho ele ainda tem que nao partir um caractere no meio —
        # mesmo que hoje a transliteracao chegue antes e tire quase todos.
        r = bloqueio.cortar("aaaaç", 5)
        r.encode().decode()
        self.assertTrue(r.endswith("..."))

    def test_acento_vira_ascii(self):
        # A fonte do Arduino_GFX so tem ASCII: cada byte extra do UTF-8 vira um
        # glifo solto na tela. "mudanças" saía como "mudan|ºas". Ver a foto de
        # 11/08 — a tela mostrou a pergunta certa em caracteres errados.
        texto = ("│ As mudanças estão no diretório de trabalho?\n"
                 "│ ❯ 1. Não commitar ainda\n"
                 "│   2. Sim — com o túnel\n"
                 "  Enter to confirm\n")
        f = bloqueio.parse_form(texto)
        self.assertEqual(f["pergunta"],
                         "As mudancas estao no diretorio de trabalho?")
        self.assertEqual(f["opcoes"][0]["rotulo"], "Nao commitar ainda")
        # Travessao vira hifen: sem isto o NFKD o descarta e as palavras colam.
        self.assertEqual(f["opcoes"][1]["rotulo"], "Sim - com o tunel")
        for o in f["opcoes"]:
            o["rotulo"].encode("ascii")           # nao pode sobrar byte alto
        f["pergunta"].encode("ascii")

    def test_rotulo_para_na_coluna_vizinha(self):
        # A tela do agente tinha a lista a ESQUERDA e uma caixa de diff a
        # DIREITA, lado a lado. A regex captura ate o fim da linha, entao a
        # caixa do vizinho vinha colada no rotulo — visto na tela em 11/08:
        # "Commit local, sem push  │  camara/session.go   strings".
        texto = ("│ O que faço?\n"
                 "│ ❯ 1. Commit e push num commit     ┌────────────\n"
                 "│   2. Commits separados por        │\n"
                 "│   3. Commit local, sem push       │   camara/session.go\n"
                 "  Enter to confirm\n")
        ops = bloqueio.parse_form(texto)["opcoes"]
        self.assertEqual(ops[0]["rotulo"], "Commit e push num commit")
        self.assertEqual(ops[1]["rotulo"], "Commits separados por")
        self.assertEqual(ops[2]["rotulo"], "Commit local, sem push")

    def test_hifen_comum_no_rotulo_sobrevive(self):
        # O corte e nos caracteres de CAIXA, nao em tracinho: "nao-interativo"
        # e rotulo legitimo e nao pode ser decapitado.
        texto = ("│ Prosseguir?\n"
                 "│ ❯ 1. Rodar em modo nao-interativo\n"
                 "│   2. No\n"
                 "  Enter to confirm\n")
        self.assertEqual(bloqueio.parse_form(texto)["opcoes"][0]["rotulo"],
                         "Rodar em modo nao-interativo")

    def test_opcao_que_pede_texto_e_marcada(self):
        # Ela nao decide nada sozinha: escolher abre um campo. O painel precisa
        # saber para nao prometer que o toque resolve.
        texto = ("│ Prosseguir?\n"
                 "│ ❯ 1. Yes\n"
                 "│   2. No, and tell Claude what to do differently\n"
                 "  Enter to confirm\n")
        ops = bloqueio.parse_form(texto)["opcoes"]
        self.assertFalse(ops[0]["texto"])
        self.assertTrue(ops[1]["texto"])

    def test_entrada_podre_nunca_levanta(self):
        for ruim in ("", "   ", "\n\n\n", None, "Enter to confirm"):
            self.assertIsNone(bloqueio.parse_form(ruim), repr(ruim))

    def test_sem_cursor_na_tela_ainda_le_as_opcoes(self):
        # Devolver as opcoes serve para MOSTRAR; `cursor: None` e o que impede
        # de responder, porque sem ele nao da para contar setas.
        texto = ("│ Prosseguir?\n"
                 "│   1. Yes\n"
                 "│   2. No\n"
                 "  Enter to confirm\n")
        f = bloqueio.parse_form(texto)
        self.assertEqual(len(f["opcoes"]), 2)
        self.assertIsNone(f["cursor"])


class TesteCauda(unittest.TestCase):

    def test_cauda_da_tela_quando_nao_ha_formulario(self):
        # Sem formulario o painel ainda mostra ALGO — o fim da tela, que e onde
        # esta a pergunta em texto livre. Melhor do que uma tela vazia.
        texto = "linha velha\n" * 40 + "e ai, pode seguir?\n"
        c = bloqueio.cauda(texto, linhas=3)
        self.assertIn("pode seguir", c)
        self.assertLessEqual(len(c.encode()), bloqueio.PERGUNTA_MAX_B)

    def test_cauda_ignora_cromo_e_linhas_vazias(self):
        texto = "pergunta de verdade\n\n────────────\n  esc to cancel\n"
        self.assertEqual(bloqueio.cauda(texto, linhas=5), "pergunta de verdade")


def agente(sid, estado, pane="w0:p1"):
    return {"session_id": sid, "state": estado, "pane_id": pane}


class TesteAtual(unittest.TestCase):

    def setUp(self):
        bloqueio.esquecer()
        self.lidos = []
        self.tempo = 1000.0

    def ler(self, saida):
        def f(pane_id, linhas=50):
            self.lidos.append(pane_id)
            return saida
        return f

    def agora(self):
        return self.tempo

    def test_ninguem_bloqueado_nao_le_tela_nenhuma(self):
        # Ler custa ~20-40 ms de subprocess, e /status responde a cada 2 s. Sem
        # bloqueado nao ha o que ler, e a leitura tem que nem acontecer.
        r = bloqueio.atual([agente("aaa", "working"), agente("bbb", "idle")],
                           ler=self.ler("x"), agora=self.agora)
        self.assertIsNone(r)
        self.assertEqual(self.lidos, [])

    def test_bloqueado_com_formulario(self):
        r = bloqueio.atual([agente("aaa", "blocked")],
                           ler=self.ler(CLAUDE), agora=self.agora)
        self.assertEqual(r["agent_id"], "aaa")
        self.assertEqual(r["pane_id"], "w0:p1")
        self.assertEqual(r["pergunta"], "Do you want to proceed?")
        self.assertEqual(len(r["opcoes"]), 3)

    def test_bloqueado_sem_formulario_mostra_a_cauda_e_zero_opcoes(self):
        # Melhor o painel sem botao do que com botao que nao corresponde a
        # pergunta: o toque mandaria Enter numa tela que nao esperava Enter.
        r = bloqueio.atual([agente("aaa", "blocked")],
                           ler=self.ler("so um texto solto\n"), agora=self.agora)
        self.assertEqual(r["opcoes"], [])
        self.assertIn("texto solto", r["pergunta"])

    def test_leitura_falha_ainda_anuncia_o_bloqueio(self):
        # Sem conseguir ler, o painel ainda precisa saber que ha um bloqueio —
        # so nao tem o que perguntar. Sumir seria esconder o unico estado que
        # exige acao.
        r = bloqueio.atual([agente("aaa", "blocked")],
                           ler=self.ler(""), agora=self.agora)
        self.assertEqual(r["pergunta"], "")
        self.assertEqual(r["opcoes"], [])

    def test_nao_rele_dentro_da_janela(self):
        for _ in range(5):
            bloqueio.atual([agente("aaa", "blocked")],
                           ler=self.ler(CLAUDE), agora=self.agora)
        self.assertEqual(len(self.lidos), 1)

    def test_rele_passada_a_janela(self):
        bloqueio.atual([agente("aaa", "blocked")],
                       ler=self.ler(CLAUDE), agora=self.agora)
        self.tempo += bloqueio.RELER_S + 0.1
        bloqueio.atual([agente("aaa", "blocked")],
                       ler=self.ler(CLAUDE), agora=self.agora)
        self.assertEqual(len(self.lidos), 2)

    def test_pane_diferente_rele_na_hora(self):
        # Trocar de agente bloqueado nao pode esperar a janela: seria mostrar a
        # pergunta de um agente com o nome de outro.
        bloqueio.atual([agente("aaa", "blocked", "w0:p1")],
                       ler=self.ler(CLAUDE), agora=self.agora)
        bloqueio.atual([agente("bbb", "blocked", "w9:p9")],
                       ler=self.ler(CLAUDE), agora=self.agora)
        self.assertEqual(self.lidos, ["w0:p1", "w9:p9"])

    def test_seq_nao_anda_enquanto_a_pergunta_e_a_mesma(self):
        # O firmware desarma o botao quando `seq` muda. Um seq que andasse a
        # cada leitura desarmaria o botao debaixo do dedo do usuario.
        a = bloqueio.atual([agente("aaa", "blocked")],
                           ler=self.ler(CLAUDE), agora=self.agora)
        self.tempo += bloqueio.RELER_S + 0.1
        b = bloqueio.atual([agente("aaa", "blocked")],
                           ler=self.ler(CLAUDE), agora=self.agora)
        self.assertEqual(a["seq"], b["seq"])

    def test_seq_anda_quando_a_pergunta_muda(self):
        a = bloqueio.atual([agente("aaa", "blocked")],
                           ler=self.ler(CLAUDE), agora=self.agora)
        self.tempo += bloqueio.RELER_S + 0.1
        outra = CLAUDE.replace("Do you want to proceed?", "Rodar o build?")
        b = bloqueio.atual([agente("aaa", "blocked")],
                           ler=self.ler(outra), agora=self.agora)
        self.assertGreater(b["seq"], a["seq"])

    def test_desbloqueou_some_e_esquece(self):
        bloqueio.atual([agente("aaa", "blocked")],
                       ler=self.ler(CLAUDE), agora=self.agora)
        self.assertIsNone(bloqueio.atual([agente("aaa", "idle")],
                                         ler=self.ler(CLAUDE), agora=self.agora))
        # Bloqueou de novo com a MESMA pergunta: e outra pergunta do ponto de
        # vista de quem olha, entao o seq tem que andar — senao o painel
        # continuaria com o botao armado da rodada anterior.
        r = bloqueio.atual([agente("aaa", "blocked")],
                           ler=self.ler(CLAUDE), agora=self.agora)
        self.assertEqual(r["seq"], 2)


class TesteResponder(unittest.TestCase):

    def setUp(self):
        bloqueio.esquecer()
        self.teclas = []

    def enviar(self, ok=True):
        def f(pane_id, teclas):
            self.teclas.append(list(teclas))
            return ok
        return f

    def test_caminho_feliz_navega_e_confirma(self):
        # Cursor na opcao 1 (indice 0), alvo a opcao 3 (indice 2): duas setas
        # para baixo e so entao o Enter.
        telas = [CLAUDE, CLAUDE.replace("│ ❯ 1. Yes", "│   1. Yes")
                               .replace("│   3. No,", "│ ❯ 3. No,")]
        ok = bloqueio.responder(
            "w0:p1", 3, "No, and tell Claude what to do differently",
            ler=lambda p, linhas=50: telas.pop(0), teclas=self.enviar())
        self.assertTrue(ok)
        self.assertEqual(self.teclas, [["Down", "Down"], ["Enter"]])

    def test_opcao_ja_selecionada_so_confirma(self):
        ok = bloqueio.responder("w0:p1", 1, "Yes",
                                ler=lambda p, linhas=50: CLAUDE,
                                teclas=self.enviar())
        self.assertTrue(ok)
        self.assertEqual(self.teclas, [["Enter"]])

    def test_rotulo_divergente_nao_manda_nada(self):
        # A tela mudou entre o toque no painel e a chegada do POST. Sem esta
        # conferencia, um toque de dois segundos atras aprovaria outra coisa.
        ok = bloqueio.responder("w0:p1", 1, "Yes, and don't ask again",
                                ler=lambda p, linhas=50: CLAUDE,
                                teclas=self.enviar())
        self.assertFalse(ok)
        self.assertEqual(self.teclas, [])

    def test_numero_inexistente_nao_manda_nada(self):
        ok = bloqueio.responder("w0:p1", 9, "Yes",
                                ler=lambda p, linhas=50: CLAUDE,
                                teclas=self.enviar())
        self.assertFalse(ok)
        self.assertEqual(self.teclas, [])

    def test_sem_formulario_na_tela_nao_manda_nada(self):
        ok = bloqueio.responder("w0:p1", 1, "Yes",
                                ler=lambda p, linhas=50: "tela qualquer",
                                teclas=self.enviar())
        self.assertFalse(ok)
        self.assertEqual(self.teclas, [])

    def test_cursor_que_nao_andou_aborta_antes_do_enter(self):
        # As setas foram mandadas mas o cursor nao se moveu (agente ocupado,
        # tela redesenhada). Confirmar aqui aprovaria a opcao ERRADA — que e o
        # pior desfecho possivel desta funcao.
        ok = bloqueio.responder(
            "w0:p1", 3, "No, and tell Claude what to do differently",
            ler=lambda p, linhas=50: CLAUDE, teclas=self.enviar())
        self.assertFalse(ok)
        self.assertEqual(self.teclas, [["Down", "Down"]])

    def test_sem_cursor_na_tela_nao_da_para_navegar(self):
        sem = CLAUDE.replace("❯ 1.", "  1.")
        ok = bloqueio.responder("w0:p1", 1, "Yes",
                                ler=lambda p, linhas=50: sem,
                                teclas=self.enviar())
        self.assertFalse(ok)
        self.assertEqual(self.teclas, [])

    def test_falha_ao_mandar_tecla_para_o_fluxo(self):
        ok = bloqueio.responder(
            "w0:p1", 3, "No, and tell Claude what to do differently",
            ler=lambda p, linhas=50: CLAUDE, teclas=self.enviar(ok=False))
        self.assertFalse(ok)
        self.assertEqual(self.teclas, [["Down", "Down"]])


if __name__ == "__main__":
    unittest.main()
