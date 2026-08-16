#!/usr/bin/env node
'use strict';
const fs = require('fs');
const { execSync } = require('child_process');

let raw = '';
try { raw = fs.readFileSync(0, 'utf8'); } catch (e) { raw = ''; }
let data = {};
try { data = JSON.parse(raw); } catch (e) { data = {}; }

const ws = data.workspace || {};
const cwd = ws.current_dir || data.cwd || process.cwd();
const model = (data.model && data.model.display_name) || 'Claude';
const effort = (data.effort && data.effort.level) || null;

function git(args, dir) {
  try {
    return execSync('git --no-optional-locks ' + args, { cwd: dir, stdio: ['ignore', 'pipe', 'ignore'] }).toString().trim() || '';
  } catch (e) { return ''; }
}
function basename(p) { return String(p).replace(/[\\/]+$/, '').split(/[\\/]/).pop() || String(p); }

function getRepo(dir) {
  if (ws.repo && ws.repo.name) return ws.repo.name;
  const top = git('rev-parse --show-toplevel', dir);
  if (top) return basename(top);
  return basename(ws.project_dir || dir);
}
function getBranch(dir) {
  const b = git('branch --show-current', dir);
  if (b) return b;
  return git('rev-parse --short HEAD', dir);
}

const repo = getRepo(cwd);
const branch = getBranch(cwd);

const cw = data.context_window || {};
const contextPct = (typeof cw.used_percentage === 'number') ? Math.round(cw.used_percentage) : null;

const cost = data.cost || {};
const rl = data.rate_limits || {};
const five = rl.five_hour;
const week = rl.seven_day;

function fmtHM(s) { if (s < 0) s = 0; const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60); return `${h}h${String(m).padStart(2, '0')}m`; }
function fmtDH(s) { if (s < 0) s = 0; const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600); return `${d}d${String(h).padStart(2, '0')}h`; }
const now = Math.floor(Date.now() / 1000);

let sessionPct = null, sessionTime = '';
if (five && typeof five.used_percentage === 'number') { sessionPct = Math.round(five.used_percentage); if (five.resets_at) sessionTime = fmtHM(five.resets_at - now); }
let weekPct = null, weekTime = '';
if (week && typeof week.used_percentage === 'number') { weekPct = Math.round(week.used_percentage); if (week.resets_at) weekTime = fmtDH(week.resets_at - now); }

const RESET = '\x1b[0m', DIM = '\x1b[90m', TEXT = '\x1b[37m', GREEN = '\x1b[32m', YELLOW = '\x1b[33m', RED = '\x1b[31m', CYAN = '\x1b[36m';
function pctColor(p) { if (p >= 80) return RED; if (p >= 50) return YELLOW; return GREEN; }
function metric(label, pct, time) { const h = `${pctColor(pct)}${label} ${pct}%${RESET}`; return time ? `${h} ${DIM}${time}${RESET}` : h; }

let out = `${DIM}❖${RESET} ${TEXT}${repo}${RESET}`;
if (branch) out += `  ${DIM}⎇${RESET} ${TEXT}${branch}${RESET}`;
out += ` ${DIM}|${RESET} ${TEXT}${model}${RESET}`;
if (effort) out += ` ${CYAN}⚡${effort}${RESET}`;
const seg = [];
if (contextPct !== null) seg.push(metric('Context', contextPct, ''));
if (sessionPct !== null) seg.push(metric('Session', sessionPct, sessionTime));
if (weekPct !== null) seg.push(metric('Week', weekPct, weekTime));
if (seg.length) out += ` ${DIM}|${RESET} ` + seg.join(` ${DIM}·${RESET} `);

process.stdout.write(out);

// --- Publisher: envia as métricas para a API local (fire-and-forget) ---
// Regra: isto NUNCA pode quebrar nem atrasar a statusline. O stdout já foi
// escrito acima; daqui pra baixo todo erro é engolido em silêncio.
(function publish() {
  try {
    const http = require('http');
    const body = JSON.stringify({
      session_id: data.session_id || 'unknown',
      pid: process.env.CLAUDE_PID || null,
      repo: repo || null,
      branch: branch || null,
      model: model,
      // O id da API, ao lado do nome de exibicao. Vem com sufixo de variante de
      // janela: "claude-opus-5[1m]". E o unico jeito de o caminho ao vivo
      // nomear o modelo do MESMO jeito que o transcript nomeia — sem ele os
      // dois caminhos do livro-caixa nao se encontram, e a quebra por modelo
      // sai com duas grafias para a mesma coisa.
      model_id: (data.model && data.model.id) || null,
      // Modo rapido DOBRA o preco do Opus (5/25 -> 10/50). Booleano puro no
      // payload. Sem este campo a tabela de preco erraria por 2x, em silencio,
      // em todo turno rodado assim.
      fast_mode: data.fast_mode === true,
      effort: effort,
      context_pct: contextPct,
      session_pct: sessionPct,
      session_resets_at: (five && five.resets_at) || null,
      week_pct: weekPct,
      week_resets_at: (week && week.resets_at) || null,
      // Contadores CUMULATIVOS da sessão, que já chegavam aqui e eram jogados
      // fora. `api_ms` é o que importa hoje: ele só anda quando há chamada de
      // API, então serve de prova de que o Claude está de fato trabalhando —
      // coisa que o percentual de contexto não conseguia dar, porque com janela
      // de 1M um ponto percentual são 10.000 tokens e um turno curto não o move.
      //
      // Os outros três não são usados ainda; vão junto porque são o mesmo envio
      // e evitam mexer neste arquivo de novo quando as métricas de trabalho
      // entrarem.
      // Também vai pelo hook. Redundância deliberada: estes três arquivos são
      // instalados à mão em ~/.claude e podem sair de sincronia, e a correção do
      // estado preso depende deste caminho existir.
      transcript_path: data.transcript_path || null,
      // A mesma chave de pareamento que o hook ja manda (ver claude_hook.cjs):
      // o herdr injeta esta variavel em todo pane que gerencia, e este script
      // roda dentro do pane igual ao hook. Mandar por aqui tambem, e nao so
      // no ciclo de vida, faz uma sessao parada parear bem mais cedo — a
      // statusline publica a cada 10s contra so um evento de hook.
      herdr_pane_id: process.env.HERDR_PANE_ID || null,
      api_ms: (cost && cost.total_api_duration_ms) || null,
      cost_usd: (cost && cost.total_cost_usd) || null,
      lines_added: (cost && cost.total_lines_added) || null,
      lines_removed: (cost && cost.total_lines_removed) || null
    });
    const req = http.request({
      host: '127.0.0.1',
      port: 8787,
      path: '/ingest',
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body) }
    });
    req.setTimeout(300, () => req.destroy());
    req.on('error', () => {});
    req.on('response', (res) => res.resume());
    req.end(body);
  } catch (e) { /* silêncio: a statusline já foi impressa */ }
})();
