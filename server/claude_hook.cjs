#!/usr/bin/env node
'use strict';
// Hook de ciclo de vida do Claude Code -> API local de métricas.
//
// Um único script serve todos os eventos: o Claude Code informa qual disparou
// no campo `hook_event_name` do stdin, então não há motivo para manter cinco
// arquivos quase idênticos.
//
// POR QUE ISTO EXISTE
// A statusline sozinha só produz um heartbeat, e heartbeat não distingue
// "bloqueado esperando você" de "fechado": os dois simplesmente calam. Os hooks
// dizem o estado de forma EXPLÍCITA, no instante em que ele muda.
//
// REGRA DE OURO: um hook roda de forma SÍNCRONA — o Claude Code espera por ele.
// Este script não pode travar nem demorar. Todo erro é engolido e a conexão tem
// prazo curto; se a API estiver fora, ele desiste em silêncio.

const EVENT_STATE = {
  SessionStart:      'idle',      // sessão nova, ninguém pediu nada ainda
  UserPromptSubmit:  'working',   // você mandou um prompt: Claude assumiu
  Stop:              'idle',      // turno acabou, a bola está com você
  Notification:      'blocked',   // pergunta / permissão / ociosidade: precisa de você
  SessionEnd:        'closed',    // encerrada de verdade — sai da lista na hora
};

let raw = '';
try { raw = require('fs').readFileSync(0, 'utf8'); } catch (e) { raw = ''; }
let data = {};
try { data = JSON.parse(raw); } catch (e) { data = {}; }

const event = data.hook_event_name || '';
const state = EVENT_STATE[event];

// Evento que não muda estado (PreToolUse e afins, caso alguém registre depois):
// sai sem falar com a API. Barato e silencioso.
if (!state) process.exit(0);

const body = JSON.stringify({
  session_id: data.session_id || process.env.CLAUDE_CODE_SESSION_ID || 'unknown',
  event: event,
  state: state,
  // `reason` só existe no SessionEnd ("clear" / "logout" / "prompt_input_exit"...).
  // `type` só existe no Notification ("permission_prompt", "agent_needs_input"...).
  // Guardar os dois permite explicar DEPOIS por que a sessão sumiu ou travou.
  reason: data.reason || null,
  kind: data.type || null,
  // O texto da notificacao. Registrado depois de um caso em que `kind` chegou
  // vazio e nao deu para saber QUAL notificacao havia marcado a sessao como
  // bloqueada — a causa teve que ser deduzida em vez de lida.
  message: typeof data.message === 'string' ? data.message.slice(0, 120) : null,
  cwd: data.cwd || null,
  // Onde a API pode ler o que realmente aconteceu quando um hook NÃO chega.
  // Apertar Esc aborta o turno sem disparar `Stop`, e sem isto a sessão fica
  // `working` para sempre — visto ao vivo: 644 s trabalhando com o transcript
  // parado há 675 s. Ver server/transcript.py.
  transcript_path: data.transcript_path || null,
  // O pane do herdr onde esta sessao vive. O herdr injeta esta variavel em todo
  // pane que gerencia, e este hook roda dentro do pane — entao a chave de
  // casamento entre os dois sistemas chega de graca. Vazio quando o Claude Code
  // foi aberto fora do herdr, e ai a sessao simplesmente nao tem par.
  herdr_pane_id: process.env.HERDR_PANE_ID || null,
  // Informativo apenas. Medido na prática: este PID às vezes aponta para um
  // processo filho transitório, então NUNCA é usado sozinho para remover uma
  // sessão — ver comentário em claude_metrics_api.py.
  pid: Number(process.env.CLAUDE_PID) || null,
});

try {
  const req = require('http').request({
    host: '127.0.0.1',
    port: 8787,
    path: '/event',
    method: 'POST',
    headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body) },
  });
  // 400ms: o SessionEnd precisa de um pouco mais de folga que a statusline,
  // porque é a última chance de avisar que esta sessão acabou.
  req.setTimeout(400, () => req.destroy());
  req.on('error', () => {});
  req.on('response', (res) => res.resume());
  req.end(body);
} catch (e) { /* silêncio: um hook nunca pode atrapalhar o Claude Code */ }
