const state = {
  catalog: [],
  assets: new Map(),
  players: [],
  selected: null,
  backgroundPlayer: null,
  characterPlayer: null,
  paused: false,
  speed: 1,
};

const $ = (selector) => document.querySelector(selector);
const grid = $("#sprite-grid");
const template = $("#sprite-card-template");
const device = $("#device");

function formatBytes(value) {
  if (value >= 1024 * 1024) return `${(value / 1024 / 1024).toFixed(2)} MB`;
  return `${(value / 1024).toFixed(value >= 100 * 1024 ? 0 : 1)} KB`;
}

function parseClw(buffer) {
  const view = new DataView(buffer);
  if (buffer.byteLength < 20 || String.fromCharCode(...new Uint8Array(buffer, 0, 4)) !== "CLWD") {
    throw new Error("Arquivo CLW inválido");
  }
  const version = view.getUint16(4, true);
  if (version !== 1) throw new Error(`Versão CLW ${version} não suportada`);
  const frames = view.getUint16(6, true);
  const width = view.getUint16(8, true);
  const height = view.getUint16(10, true);
  const key = view.getUint16(12, true);
  const frameMs = view.getUint16(14, true);
  const scale = view.getUint16(16, true);
  const offsets = [];
  for (let i = 0; i <= frames; i += 1) offsets.push(view.getUint32(20 + i * 4, true));
  return { view, frames, width, height, key, frameMs, scale, offsets, dataOffset: 20 + (frames + 1) * 4, cache: new Map() };
}

function rgbaFrom565(value) {
  const r5 = (value >> 11) & 0x1f;
  const g6 = (value >> 5) & 0x3f;
  const b5 = value & 0x1f;
  return [(r5 * 255 / 31) | 0, (g6 * 255 / 63) | 0, (b5 * 255 / 31) | 0];
}

function decodedCanvas(sprite, frame) {
  if (sprite.cache.has(frame)) return sprite.cache.get(frame);
  const pixels = new Uint8ClampedArray(sprite.width * sprite.height * 4);
  let out = 0;
  for (let run = sprite.offsets[frame]; run < sprite.offsets[frame + 1]; run += 1) {
    const at = sprite.dataOffset + run * 4;
    const value = sprite.view.getUint16(at, true);
    const count = sprite.view.getUint16(at + 2, true);
    const [r, g, b] = value === sprite.key ? [0, 0, 0] : rgbaFrom565(value);
    const alpha = value === sprite.key ? 0 : 255;
    for (let i = 0; i < count; i += 1) {
      pixels[out++] = r; pixels[out++] = g; pixels[out++] = b; pixels[out++] = alpha;
    }
  }
  const canvas = document.createElement("canvas");
  canvas.width = sprite.width; canvas.height = sprite.height;
  canvas.getContext("2d").putImageData(new ImageData(pixels, sprite.width, sprite.height), 0, 0);
  sprite.cache.set(frame, canvas);
  if (sprite.cache.size > 12) sprite.cache.delete(sprite.cache.keys().next().value);
  return canvas;
}

function drawGridPlayer(player) {
  const { canvas, sprite, frame } = player;
  const ctx = canvas.getContext("2d");
  ctx.imageSmoothingEnabled = false;
  const background = player.meta.roles.includes("fundo");
  ctx.fillStyle = "#0f1320";
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  if (!background) {
    ctx.fillStyle = "#111a2b";
    ctx.fillRect(0, canvas.height - 18, canvas.width, 18);
    ctx.fillStyle = "#294a32";
    ctx.fillRect(0, canvas.height - 17, canvas.width, 2);
  }
  if (!sprite) return;
  const scale = background
    ? Math.min(canvas.width / sprite.width, canvas.height / sprite.height)
    : Math.min((canvas.width - 18) / sprite.width, (canvas.height - 24) / sprite.height);
  const w = Math.max(1, Math.floor(sprite.width * scale));
  const h = Math.max(1, Math.floor(sprite.height * scale));
  const y = background ? Math.floor((canvas.height - h) / 2) : canvas.height - 18 - h;
  ctx.drawImage(decodedCanvas(sprite, frame), Math.floor((canvas.width - w) / 2), y, w, h);
}

function drawDevice() {
  const ctx = device.getContext("2d");
  ctx.imageSmoothingEnabled = false;
  const background = state.backgroundPlayer;
  const character = state.characterPlayer;
  ctx.fillStyle = "#0f1320"; ctx.fillRect(0, 0, 480, 320);
  if (background?.sprite) {
    ctx.drawImage(decodedCanvas(background.sprite, background.frame), 0, 0, 480, 320);
  } else {
    ctx.fillStyle = "#25412d"; ctx.fillRect(0, 238, 480, 8);
    ctx.fillStyle = "#355e3b"; ctx.fillRect(0, 238, 480, 2);
  }
  ctx.fillStyle = "rgba(7, 11, 19, .78)"; ctx.fillRect(0, 0, 480, 45);
  ctx.fillStyle = "#8c96ad"; ctx.font = "11px monospace"; ctx.fillText("CLAWD / SPRITE PREVIEW", 16, 28);
  ctx.fillStyle = "#de886d"; ctx.fillText(state.selected?.name?.toUpperCase() || "—", 354, 28);
  if (character?.sprite) {
    const { sprite, frame, meta } = character;
    let x = 240 - meta.anchor_x * sprite.scale;
    let y = 238 - meta.anchor_base * sprite.scale;
    if (y < 46) y = 46;
    ctx.drawImage(decodedCanvas(sprite, frame), x, y, sprite.width * sprite.scale, sprite.height * sprite.scale);
  }
  if (!state.selected) return;
  const shown = state.selected.roles.includes("fundo") ? background : character;
  if (!shown?.sprite) return;
  ctx.fillStyle = "#8c96ad"; ctx.font = "12px monospace";
  ctx.fillText(`${shown.frame + 1}/${shown.sprite.frames} · ${shown.meta.fps.toFixed(1)} FPS · ${shown.meta.screen_width}×${shown.meta.screen_height}`, 16, 304);
}

function advance(player, now) {
  if (state.paused) return false;
  const interval = player.sprite.frameMs / state.speed;
  if (!player.last) player.last = now;
  if (now - player.last < interval) return false;
  player.last = now;
  player.frame = (player.frame + 1) % player.sprite.frames;
  return true;
}

function animate(now) {
  for (const player of state.players) {
    if (player.sprite && advance(player, now)) drawGridPlayer(player);
  }
  let stageChanged = false;
  if (state.backgroundPlayer && advance(state.backgroundPlayer, now)) stageChanged = true;
  if (state.characterPlayer && advance(state.characterPlayer, now)) stageChanged = true;
  if (stageChanged) drawDevice();
  requestAnimationFrame(animate);
}

async function loadAsset(meta) {
  if (state.assets.has(meta.name)) return state.assets.get(meta.name);
  const response = await fetch(meta.asset_url);
  if (!response.ok) throw new Error(`Falha ao carregar ${meta.name}`);
  const sprite = parseClw(await response.arrayBuffer());
  state.assets.set(meta.name, sprite);
  return sprite;
}

function renderSourceReview(meta) {
  const review = $("#source-review");
  const image = $("#source-image");
  const frameGrid = $("#source-frame-grid");
  const hint = $("#source-hint");
  if (!meta.source_url) {
    review.hidden = true;
    image.removeAttribute("src");
    frameGrid.replaceChildren();
    return;
  }

  review.hidden = false;
  $("#source-path").textContent = meta.source_file;
  $("#source-open").href = meta.source_url;
  image.src = meta.source_url;
  image.alt = `Fonte original de ${meta.label}`;
  frameGrid.replaceChildren();

  if (!meta.source_columns || !meta.source_rows) {
    frameGrid.hidden = true;
    hint.textContent = "Fonte vetorial original; abra o arquivo para inspecionar em tamanho integral.";
    return;
  }

  frameGrid.hidden = false;
  frameGrid.style.gridTemplateColumns = `repeat(${meta.source_columns}, 1fr)`;
  frameGrid.style.gridTemplateRows = `repeat(${meta.source_rows}, 1fr)`;
  const frameCount = meta.source_columns * meta.source_rows;
  for (let index = 0; index < frameCount; index += 1) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "source-frame";
    button.dataset.frame = String(index + 1);
    button.setAttribute("aria-label", `Selecionar quadro ${index + 1}`);
    const number = document.createElement("span");
    number.textContent = String(index + 1);
    button.append(number);
    button.addEventListener("click", () => {
      frameGrid.querySelectorAll(".source-frame.selected").forEach((frame) => frame.classList.remove("selected"));
      button.classList.add("selected");
      hint.textContent = `Quadro ${index + 1} selecionado — use este número ao apontar um defeito.`;
    });
    frameGrid.append(button);
  }
  hint.textContent = `Folha ${meta.source_columns}×${meta.source_rows}. Clique no quadro que deseja revisar.`;
}

function tag(text, extra = "") {
  const el = document.createElement("span");
  el.className = `tag ${extra}`.trim(); el.textContent = text;
  return el;
}

function buildCard(meta) {
  const card = template.content.firstElementChild.cloneNode(true);
  card.dataset.name = meta.name;
  card.dataset.testid = `sprite-card-${meta.name}`;
  card.setAttribute("aria-label", `Visualizar ${meta.label}`);
  card.querySelector("strong").textContent = meta.label;
  card.querySelector("em").textContent = meta.used ? "EM USO" : "ACERVO";
  card.querySelector(".card-meta").textContent = `${meta.frames}f · ${meta.fps.toFixed(1)}fps · ${meta.width}×${meta.height}`;
  const tags = card.querySelector(".card-tags");
  if (meta.used) tags.append(tag("firmware", "used"));
  for (const role of meta.roles.slice(0, 2)) tags.append(tag(role));
  grid.append(card);
  const player = { canvas: card.querySelector("canvas"), sprite: null, frame: 0, last: 0, meta, card };
  drawGridPlayer(player);
  card.addEventListener("click", () => selectSprite(meta, card));
  return player;
}

async function hydratePlayers(players, concurrency = 6) {
  let cursor = 0;
  async function worker() {
    while (cursor < players.length) {
      const player = players[cursor++];
      try {
        player.sprite = await loadAsset(player.meta);
        drawGridPlayer(player);
      } catch (error) {
        player.card.classList.add("preview-error");
        player.card.title = `Prévia indisponível: ${error.message}`;
        console.warn(`Falha na prévia de ${player.meta.name}`, error);
      }
    }
  }
  await Promise.all(Array.from({ length: Math.min(concurrency, players.length) }, worker));
}

async function selectSprite(meta, card) {
  state.selected = meta;
  document.querySelectorAll(".sprite-card.selected").forEach((el) => el.classList.remove("selected"));
  card.classList.add("selected");
  const sprite = await loadAsset(meta);
  const stagePlayer = { sprite, frame: 0, last: 0, meta };
  if (meta.roles.includes("fundo")) {
    state.backgroundPlayer = stagePlayer;
  } else {
    state.characterPlayer = stagePlayer;
    if (meta.level) await selectLevelBackground(meta.level);
    if ([...$("#overlay-select").options].some((option) => option.value === meta.name)) {
      $("#overlay-select").value = meta.name;
    }
  }
  $("#detail-role").textContent = meta.roles.join(" · ").toUpperCase();
  $("#detail-name").textContent = meta.label;
  $("#detail-status").textContent = meta.used ? "em uso" : "acervo";
  $("#detail-description").textContent = meta.description;
  renderSourceReview(meta);
  $("#metric-frames").textContent = `${meta.frames} / ${(meta.duration_ms / 1000).toFixed(1)}s`;
  $("#metric-fps").textContent = `${meta.fps.toFixed(1)} FPS`;
  $("#metric-native").textContent = `${meta.width} × ${meta.height}`;
  $("#metric-screen").textContent = `${meta.screen_width} × ${meta.screen_height} (${meta.scale}×)`;
  $("#metric-file").textContent = formatBytes(meta.file_bytes);
  $("#metric-compression").textContent = `${meta.compression.toFixed(1)}×`;
  $("#metric-bppf").textContent = meta.bytes_per_pixel_frame.toFixed(3);
  $("#metric-profile").textContent = meta.economy_profile
    ? `${meta.economy_profile.toUpperCase()} · ${meta.economy_compliant ? "APROVADO" : "REVISAR"}`
    : "Original";
  $("#metric-buffer").textContent = formatBytes(meta.buffer_bytes);
  $("#metric-cpu").textContent = `~${meta.page_cpu_pct.toFixed(0)}%`;
  drawDevice();
}

async function selectLevelBackground(level) {
  const meta = state.catalog.find((entry) =>
    entry.roles.includes("fundo")
    && Number.isInteger(entry.level_min)
    && entry.level_min <= level
    && level <= entry.level_max
  );
  if (!meta) return;
  state.backgroundPlayer = { sprite: await loadAsset(meta), frame: 0, last: 0, meta };
}

async function selectOverlay(name) {
  const meta = state.catalog.find((entry) => entry.name === name);
  if (!meta || meta.roles.includes("fundo")) return;
  state.characterPlayer = { sprite: await loadAsset(meta), frame: 0, last: 0, meta };
  if (meta.level) await selectLevelBackground(meta.level);
  drawDevice();
}

function applyFilters() {
  const query = $("#search").value.trim().toLocaleLowerCase("pt-BR");
  const role = $("#role-filter").value;
  let visible = 0;
  for (const player of state.players) {
    const meta = player.meta;
    const haystack = `${meta.name} ${meta.label} ${meta.description} ${meta.roles.join(" ")}`.toLocaleLowerCase("pt-BR");
    const matchesText = !query || haystack.includes(query);
    const matchesRole = role === "all" || (role === "used" ? meta.used : meta.roles.includes(role));
    player.card.hidden = !(matchesText && matchesRole);
    if (!player.card.hidden) visible += 1;
  }
  $("#visible-count").textContent = `${visible} resultado${visible === 1 ? "" : "s"}`;
  $("#empty-state").hidden = visible !== 0;
}

async function init() {
  $("#visible-count").textContent = "carregando catálogo…";
  const response = await fetch("/api/sprites");
  const data = await response.json();
  state.catalog = data.sprites.filter((sprite) => sprite.valid);
  $("#sprite-count").textContent = state.catalog.length;
  $("#total-size").textContent = formatBytes(state.catalog.reduce((sum, sprite) => sum + sprite.file_bytes, 0));
  // Primeiro publica todos os cards, depois hidrata os previews com concorrencia
  // limitada para a interface continuar responsiva mesmo com centenas de itens.
  state.players = state.catalog.map(buildCard);
  const overlaySelect = $("#overlay-select");
  const characters = state.catalog.filter((sprite) => !sprite.roles.includes("fundo") && !sprite.economy_profile);
  for (const meta of characters) {
    const option = document.createElement("option");
    option.value = meta.name;
    option.textContent = meta.label;
    overlaySelect.append(option);
  }
  const defaultBackground = state.catalog.find((sprite) => sprite.name === "background_cyber_rain");
  const defaultCharacter = state.catalog.find((sprite) => sprite.name === "idle") || characters[0];
  if (defaultBackground) {
    state.backgroundPlayer = { sprite: await loadAsset(defaultBackground), frame: 0, last: 0, meta: defaultBackground };
  }
  if (defaultCharacter) {
    overlaySelect.value = defaultCharacter.name;
    await selectOverlay(defaultCharacter.name);
  }
  applyFilters();
  if (state.players.length) {
    try {
      const initial = state.players.find((player) => player.meta.roles.includes("fundo")) || state.players[0];
      await selectSprite(initial.meta, initial.card);
    } catch (error) {
      $("#detail-name").textContent = "Prévia temporariamente indisponível";
      $("#detail-description").textContent = "O catálogo foi carregado. Selecione outro sprite ou recarregue a página.";
      console.warn("Falha ao abrir a primeira prévia", error);
    }
  }
  hydratePlayers(state.players).catch((error) => console.error(error));
  requestAnimationFrame(animate);
}

$("#search").addEventListener("input", applyFilters);
$("#role-filter").addEventListener("change", applyFilters);
$("#speed").addEventListener("change", (event) => { state.speed = Number(event.target.value); });
$("#pause").addEventListener("click", (event) => {
  state.paused = !state.paused;
  event.currentTarget.setAttribute("aria-pressed", String(state.paused));
  event.currentTarget.textContent = state.paused ? "Continuar" : "Pausar tudo";
});
$("#overlay-select").addEventListener("change", (event) => {
  selectOverlay(event.target.value).catch((error) => console.error(error));
});

init().catch((error) => {
  grid.innerHTML = `<p class="empty-state">Falha ao abrir o catálogo: ${error.message}</p>`;
  console.error(error);
});
