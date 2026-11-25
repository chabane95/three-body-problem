const el = (id) => document.getElementById(id);
const orbitCanvas = el('orbits');
const chartCanvas = el('energy-plot');
const colors = ['#59d5d5', '#ffac72', '#b99aff'];
const format = (value, digits) => value.toFixed(digits).replace('-', '−').replace('.', ',');
const scientific = new Intl.NumberFormat('de-DE', { notation: 'scientific', maximumFractionDigits: 2 });
const compactNumber = new Intl.NumberFormat('de-DE', { maximumSignificantDigits: 4, useGrouping: false });
const cycleMilliseconds = 14000;
const defaultTolerances = { rtol: 1e-8, atolPosition: 1e-10, atolVelocity: 1e-10 };
let run = null;
let frameIndex = 0;
let playbackTime = 0;
let previousTick = null;
let animation = 0;
let playing = false;
let loadVersion = 0;
let orbitLayout = null;
let chartLayout = null;
let worker = null;
let workerUrl = null;
let displayedScenario = null;
let lastCustom = null;
let yaw = 0, pitch = 0, zoom = 1;
let drag = null;

// Drawing only depends on samples produced by the C++ integration.
async function loadData(scenario) {
  const embedded = el(scenario === 'flyby-preset' ? 'flyby-preset' : 'data-' + scenario);
  if (embedded) return JSON.parse(embedded.textContent);
  const response = await fetch(`data/${scenario}.json`);
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  return response.json();
}

function prepareRun(data) {
  const frames = data.frames;
  if (!Array.isArray(frames) || frames.length < 2) throw new Error('Keine vollständige Bahn empfangen.');
  const first = frames[0], duration = frames.at(-1).t;
  if (!Number.isFinite(duration) || duration <= 0) throw new Error('Ungültige Simulationsdauer.');
  // Near zero total energy, cancellation between kinetic and potential energy makes relative drift ill-conditioned.
  const kinetic = first.velocities.reduce((total, v, i) =>
    total + 0.5 * data.masses[i] * Math.hypot(...v) ** 2, 0);
  const energyScale = Math.abs(kinetic) + Math.abs(first.energy - kinetic);
  const relative = Math.abs(first.energy) > Math.sqrt(Number.EPSILON) * energyScale;
  const denominator = relative ? Math.abs(first.energy) : 1;
  const errors = frames.map(f => (f.energy - first.energy) / denominator);
  const maxError = errors.reduce((m, e) => Math.max(m, Math.abs(e)), 0);
  const maxDistance = frames.reduce((m, f) => Math.max(m, ...f.distances), 0);
  const low = [Infinity, Infinity, Infinity], high = [-Infinity, -Infinity, -Infinity];
  for (const f of frames) for (const p of f.positions) {
    for (let k = 0; k < 3; k++) {
      const c = p[k] ?? 0;
      low[k] = Math.min(low[k], c); high[k] = Math.max(high[k], c);
    }
  }
  const center = low.map((v, i) => 0.5 * v + 0.5 * high[i]);
  return { ...data, duration, maxDistance, maxError, errors, center, relative };
}

function setupCanvas(canvas) {
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  const ratio = Math.min(window.devicePixelRatio || 1, 3);
  canvas.width = Math.max(1, Math.round(width * ratio));
  canvas.height = Math.max(1, Math.round(height * ratio));
  const ctx = canvas.getContext('2d');
  ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  ctx.font = '11px Consolas, monospace';
  return { ctx, width, height, ratio };
}

function niceStep(target) {
  const power = 10 ** Math.floor(Math.log10(target));
  const factor = target / power;
  return (factor <= 1 ? 1 : factor <= 2 ? 2 : factor <= 2.5 ? 2.5 : factor <= 5 ? 5 : 10) * power;
}

function line(ctx, x1, y1, x2, y2, color) {
  ctx.beginPath(); ctx.strokeStyle = color;
  ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke();
}

function drawPath(ctx, points, first, last) {
  ctx.beginPath();
  ctx.moveTo(points[first][0], points[first][1]);
  for (let i = first + 1; i <= last; i++) ctx.lineTo(points[i][0], points[i][1]);
  ctx.stroke();
}

function makeOrbitLayout() {
  const { width, height, ctx } = setupCanvas(orbitCanvas);
  if (width < 1 || height < 1) return null;
  const cy = Math.cos(yaw), sy = Math.sin(yaw), cp = Math.cos(pitch), sp = Math.sin(pitch);
  function rotate(x, y, z) {
    const b = -sy * x + cy * y;
    return [cy * x + sy * y, cp * b + sp * z, -sp * b + cp * z];
  }
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  const paths = [[], [], []];
  const center = run.center;
  for (const frame of run.frames) frame.positions.forEach(([x, y, z = 0], i) => {
    const p = rotate(x - center[0], y - center[1], z - center[2]);
    minX = Math.min(minX, p[0]); minY = Math.min(minY, p[1]);
    maxX = Math.max(maxX, p[0]); maxY = Math.max(maxY, p[1]);
    paths[i].push(p);
  });
  const left = width < 450 ? 36 : 49, right = width - 22, top = 24, bottom = height - 25;
  const spanX = maxX - minX, spanY = maxY - minY;
  // A trajectory can project onto a straight line, or point end-on.
  let scale = Math.min(spanX > 0 ? (right - left) / spanX : Infinity,
                       spanY > 0 ? (bottom - top) / spanY : Infinity);
  if (!Number.isFinite(scale)) scale = 1;
  scale *= 0.84 * zoom;
  const midX = minX * 0.5 + maxX * 0.5, midY = minY * 0.5 + maxY * 0.5;
  const sx = x => (left + right) / 2 + (x - midX) * scale;
  const syScreen = y => (top + bottom) / 2 - (y - midY) * scale;
  for (const path of paths) for (const point of path) {
    point[0] = sx(point[0]); point[1] = syScreen(point[1]);
  }

  ctx.lineWidth = 1;
  if (yaw === 0 && pitch === 0) {
    const step = niceStep((width < 450 ? 51 : 83) / scale);
    const x0 = center[0] + midX, y0 = center[1] + midY;
    // Bound tick counts even when the coordinate origin is very far from the orbit.
    for (let axis = 0; axis < 2; axis++) {
      const horizontal = axis === 0;
      const middle = horizontal ? x0 : y0;
      const span = (horizontal ? right - left : bottom - top) / scale;
      const first = Math.ceil((middle - 0.5 * span) / step);
      const count = Math.min(32, Math.ceil(span / step) + 1);
      for (let i = 0; i < count; i++) {
        const value = (first + i) * step;
        const pos = horizontal ? sx(value - center[0]) : syScreen(value - center[1]);
        if (pos < (horizontal ? left : top) - 1 || pos > (horizontal ? right : bottom) + 1) continue;
        const color = value === 0 ? '#486263' : '#2a4145';
        if (horizontal) line(ctx, pos, top, pos, bottom, color);
        else line(ctx, left, pos, right, pos, color);
        ctx.textAlign = horizontal ? 'center' : 'right'; ctx.fillStyle = '#b1c5c4';
        ctx.fillText(shortNumber(value), horizontal ? pos : left - 5, horizontal ? bottom + 15 : pos + 4);
      }
    }
    ctx.fillStyle = '#d3e2df'; ctx.textAlign = 'left'; ctx.fillText('y / L₀', left, top - 7);
    ctx.textAlign = 'right'; ctx.fillText('x / L₀', right, top - 7);
  } else {
    // An orientation triad and a length bar have a direct meaning under orthographic projection.
    const ox = 47, oy = height - 47;
    const directions = [[1, 0, 0], [0, 1, 0], [0, 0, 1]];
    directions.forEach((unit, axis) => {
      const [x, y] = rotate(...unit);
      line(ctx, ox, oy, ox + 24 * x, oy - 24 * y, '#78918f');
      ctx.textAlign = 'center'; ctx.fillStyle = '#ccd9d5';
      ctx.fillText('xyz'[axis], ox + 32 * x, oy - 32 * y + 3);
    });
    const length = niceStep(36 / scale), x = width - 23, y = height - 20;
    line(ctx, x - length * scale, y, x, y, '#849a99');
    line(ctx, x, y - 3, x, y + 3, '#849a99');
    line(ctx, x - length * scale, y - 3, x - length * scale, y + 3, '#849a99');
    ctx.textAlign = 'right'; ctx.fillText(shortNumber(length) + ' L₀', x, y - 7);
  }

  if (el('trails').checked) {
    ctx.lineWidth = 1.3;
    if (run.period) {
      ctx.strokeStyle = '#849f99'; drawPath(ctx, paths[0], 0, paths[0].length - 1);
    } else {
      ctx.globalAlpha = 0.36;
      paths.forEach((path, i) => { ctx.strokeStyle = colors[i]; drawPath(ctx, path, 0, path.length - 1); });
      ctx.globalAlpha = 1;
    }
  }
  const background = document.createElement('canvas');
  background.width = orbitCanvas.width; background.height = orbitCanvas.height;
  background.getContext('2d').drawImage(orbitCanvas, 0, 0);
  return { width, height, ctx, paths, background };
}

function drawOrbit() {
  if (!orbitLayout) return;
  const { ctx, width, height, paths, background } = orbitLayout;
  ctx.clearRect(0, 0, width, height); ctx.drawImage(background, 0, 0, width, height);
  if (el('trails').checked && frameIndex > 0) {
    const length = Math.max(2, Math.round(0.10 * (run.frames.length - 1)));
    ctx.lineCap = 'round'; ctx.lineJoin = 'round'; ctx.lineWidth = 2.7;
    for (let body = 0; body < 3; body++) {
      ctx.strokeStyle = colors[body];
      for (let piece = 0; piece < 6; piece++) {
        const a = Math.max(0, frameIndex - Math.round(length * (6 - piece) / 6));
        const b = Math.max(0, frameIndex - Math.round(length * (5 - piece) / 6));
        ctx.globalAlpha = (piece + 1) / 6;
        if (b > a) drawPath(ctx, paths[body], a, b);
      }
    }
    ctx.globalAlpha = 1;
  }
  const backToFront = [0, 1, 2].sort((a, b) => paths[a][frameIndex][2] - paths[b][frameIndex][2]);
  for (const body of backToFront) {
    const [px, py] = paths[body][frameIndex];
    ctx.beginPath(); ctx.fillStyle = '#14252a'; ctx.arc(px, py, 8, 0, 2 * Math.PI); ctx.fill();
    ctx.beginPath(); ctx.fillStyle = colors[body]; ctx.arc(px, py, 5.6, 0, 2 * Math.PI); ctx.fill();
    const tx = px + (px > width - 37 ? -12 : 12), ty = py + (py < 24 ? 16 : -9);
    ctx.font = 'bold 12px Consolas, monospace'; ctx.textAlign = 'center';
    ctx.lineWidth = 3; ctx.strokeStyle = '#14252a'; ctx.strokeText(String(body + 1), tx, ty);
    ctx.fillStyle = colors[body]; ctx.fillText(String(body + 1), tx, ty);
  }
}

function shortNumber(value) {
  const n = Math.abs(value);
  return n > 0 && (n < .001 || n >= 1e5) ? scientific.format(value) : compactNumber.format(value);
}

function makeChartLayout() {
  const { ctx, width, height } = setupCanvas(chartCanvas);
  if (width < 1 || height < 1) return null;
  const left = 29, right = width - 5, top = 18, bottom = height - 18;
  const exponent = run.maxError > 0 ? Math.floor(Math.log10(run.maxError)) : 0;
  const factor = 10 ** exponent;
  const bound = Math.max(1, Math.ceil(run.maxError / factor)) * factor;
  const sx = (time) => left + (right - left) * time / run.duration;
  const sy = (error) => .5 * (top + bottom) - .5 * (bottom - top) * error / bound;
  const superscripts = '⁰¹²³⁴⁵⁶⁷⁸⁹';
  const powerText = String(exponent).replace('-', '⁻').replace(/\d/g, (d) => superscripts[Number(d)]);
  ctx.font = '10px Consolas, monospace'; ctx.fillStyle = '#53645d'; ctx.textAlign = 'left';
  ctx.fillText('×10' + powerText, 0, 9);
  for (const n of [-1, 0, 1]) {
    const y = sy(n * bound);
    line(ctx, left, y, right, y, n === 0 ? '#b0bcae' : '#d7ddd2');
    ctx.textAlign = 'right'; ctx.fillStyle = '#53645d';
    ctx.fillText(format(n * bound / factor, 0), left - 6, y + 3);
  }
  ctx.textAlign = 'left'; ctx.fillText('0', left, height - 2);
  ctx.textAlign = 'right'; ctx.fillText(run.period ? 'T' : shortNumber(run.duration) + ' t₀', right, height - 2);
  ctx.beginPath(); ctx.strokeStyle = '#417965'; ctx.lineWidth = 1.5;
  run.frames.forEach((frame, i) => {
    if (i === 0) ctx.moveTo(sx(frame.t), sy(run.errors[i]));
    else ctx.lineTo(sx(frame.t), sy(run.errors[i]));
  });
  ctx.stroke();
  const background = document.createElement('canvas');
  background.width = chartCanvas.width; background.height = chartCanvas.height;
  background.getContext('2d').drawImage(chartCanvas, 0, 0);
  return { ctx, width, height, background, sx, sy, top, bottom };
}

function drawChart() {
  if (!chartLayout) return;
  const { ctx, width, height, background, sx, sy, top, bottom } = chartLayout;
  const x = sx(run.frames[frameIndex].t), y = sy(run.errors[frameIndex]);
  ctx.clearRect(0, 0, width, height); ctx.drawImage(background, 0, 0, width, height);
  ctx.lineWidth = 1; line(ctx, x, top, x, bottom, '#82958a');
  ctx.beginPath(); ctx.fillStyle = '#184b37'; ctx.arc(x, y, 3.5, 0, 2 * Math.PI); ctx.fill();
}

const distanceOutputs = [], distanceBars = [], positionCells = [];
for (const label of ['1–2', '2–3', '3–1']) {
  const row = document.createElement('div'); row.className = 'distance-row';
  const pair = document.createElement('span'); pair.className = 'pair-label'; pair.textContent = label;
  const bar = document.createElement('span'); bar.className = 'bar'; bar.setAttribute('aria-hidden', 'true');
  const fill = document.createElement('i'); bar.append(fill);
  const value = document.createElement('span'); value.className = 'distance-value';
  row.append(pair, bar, value); el('distances').append(row);
  distanceOutputs.push(value); distanceBars.push(fill);
  const tableRow = document.createElement('tr'), heading = document.createElement('th');
  heading.scope = 'row'; heading.textContent = String(distanceOutputs.length);
  const cells = Array.from({length: 3}, () => document.createElement('td'));
  tableRow.append(heading, ...cells); el('coordinates').append(tableRow); positionCells.push(cells);
}

function updateFrame() {
  const frame = run.frames[frameIndex];
  el('time').value = String(frameIndex);
  const time = run.period ? format(frame.t / run.period, 3) : shortNumber(frame.t);
  el('time').setAttribute('aria-valuetext', time + (run.period ? ' Umlaufperioden' : ' Zeiteinheiten'));
  el('clock').textContent = (run.period ? 't / T = ' : 't / t₀ = ') + time;
  el('energy').textContent = shortNumber(frame.energy);
  el('error').textContent = run.errors[frameIndex] === 0 ? '0' : scientific.format(run.errors[frameIndex]);
  frame.distances.forEach((distance, i) => {
    distanceOutputs[i].textContent = shortNumber(distance);
    distanceBars[i].style.transform = `scaleX(${distance / run.maxDistance})`;
    positionCells[i].forEach((cell, j) => { cell.textContent = shortNumber(frame.positions[i][j] ?? 0); });
  });
  drawOrbit(); drawChart();
}

function resize() {
  if (!run) return;
  orbitLayout = makeOrbitLayout(); chartLayout = el('details').open ? makeChartLayout() : null;
  drawOrbit(); drawChart();
}

function indexAt(time) {
  let lo = 0, hi = run.frames.length - 1;
  while (lo < hi) {
    const mid = Math.ceil((lo + hi) / 2);
    if (run.frames[mid].t <= time) lo = mid;
    else hi = mid - 1;
  }
  return lo;
}

function tick(now) {
  if (!playing || !run) return;
  if (previousTick !== null) playbackTime += (now - previousTick) * Number(el('speed').value) * run.duration / cycleMilliseconds;
  if (playbackTime >= run.duration) {
    if (run.period) playbackTime %= run.duration;
    else { frameIndex = run.frames.length - 1; updateFrame(); setPlaying(false); return; }
  }
  previousTick = now;
  const next = indexAt(playbackTime);
  if (next !== frameIndex) { frameIndex = next; updateFrame(); }
  animation = requestAnimationFrame(tick);
}

function setPlaying(value) {
  cancelAnimationFrame(animation); playing = value; previousTick = null;
  el('play-label').textContent = playing ? 'Pausieren' : 'Abspielen';
  el('play-icon').textContent = playing ? 'Ⅱ' : '▶';
  if (playing) {
    if (frameIndex === run.frames.length - 1) { frameIndex = 0; updateFrame(); }
    playbackTime = run.frames[frameIndex].t;
    animation = requestAnimationFrame(tick);
  }
}

const massInputs = [], positionInputs = [], velocityInputs = [];
function makeField(parent, title, name, id, positive = false) {
  const label = document.createElement('label');
  const caption = document.createElement('span'); caption.textContent = title;
  const input = document.createElement('input');
  input.type = 'number'; input.step = 'any'; input.required = true; input.id = id;
  if (positive) input.min = '0';
  input.setAttribute('aria-label', name);
  label.append(caption, input); parent.append(label);
  return input;
}
for (let body = 0; body < 3; body++) {
  const group = document.createElement('fieldset'); group.className = 'body-input';
  const legend = document.createElement('legend'), dot = document.createElement('i');
  dot.className = 'dot body-' + (body + 1); legend.append(dot, 'Körper ' + (body + 1)); group.append(legend);
  const name = 'Körper ' + (body + 1) + ', ';
  const m = makeField(group, 'Masse / M₀', name + 'Masse', 'mass-' + (body + 1), true);
  m.parentElement.className = 'mass-field'; massInputs.push(m);
  for (const [key, title, list] of [
    ['r', 'Position / L₀', positionInputs], ['v', 'Geschwindigkeit / (L₀/t₀)', velocityInputs],
  ]) {
    const block = document.createElement('div'), heading = document.createElement('div');
    heading.className = 'vector-label'; heading.textContent = title;
    const vector = document.createElement('div'); vector.className = 'vector';
    list.push(['x', 'y', 'z'].map(axis => makeField(vector, axis, name + title + ', ' + axis,
      key + '-' + (body + 1) + '-' + axis)));
    block.append(heading, vector); group.append(block);
  }
  el('bodies').append(group);
}

function fillEditor(input) {
  massInputs.forEach((m, i) => {
    m.value = String(input.masses[i]);
    for (let k = 0; k < 3; k++) {
      positionInputs[i][k].value = String(input.positions[i][k] ?? 0);
      velocityInputs[i][k].value = String(input.velocities[i][k] ?? 0);
    }
  });
  el('duration').value = String(input.duration);
  el('rtol').value = String(input.rtol ?? defaultTolerances.rtol);
  el('atol-position').value = String(input.atolPosition ?? defaultTolerances.atolPosition);
  el('atol-velocity').value = String(input.atolVelocity ?? defaultTolerances.atolVelocity);
  el('min-distance').value = String(input.minDistance ?? 0);
  for (const field of el('initial-form').querySelectorAll('input')) field.setCustomValidity('');
}

function readEditor() {
  function number(input, positive = false) {
    const value = input.valueAsNumber;
    if (!Number.isFinite(value) || (positive && value <= 0)) {
      input.setCustomValidity(positive ? 'Bitte eine endliche Zahl größer als null eingeben.' : 'Bitte eine endliche Zahl eingeben.');
      input.reportValidity(); throw new Error(input.validationMessage);
    }
    return value;
  }
  const positions = positionInputs.map(row => row.map(input => number(input)));
  for (let i = 0; i < 3; i++) for (let j = i + 1; j < 3; j++) {
    if (positions[i].every((v, k) => v === positions[j][k])) {
      throw new Error('Körper ' + (i + 1) + ' und ' + (j + 1) + ' haben dieselbe Startposition.');
    }
  }
  return {
    masses: massInputs.map(input => number(input, true)), positions,
    velocities: velocityInputs.map(row => row.map(input => number(input))),
    duration: number(el('duration'), true), samples: 721, G: 1,
    rtol: number(el('rtol'), true), atolPosition: number(el('atol-position'), true),
    atolVelocity: number(el('atol-velocity'), true), minDistance: number(el('min-distance')),
  };
}

function showStatus(text = '', busy = false, isError = false) {
  const status = el('run-status');
  el('notice').textContent = text;
  status.hidden = !text; status.toggleAttribute('data-error', isError);
  el('progress').hidden = !busy; el('cancel').hidden = !busy;
  el('simulation').setAttribute('aria-busy', String(busy));
  el('calculate').disabled = busy;
  el('plot-status').hidden = Boolean(run);
  if (!run) el('plot-status').textContent = busy ? 'Bahn wird geladen …' : 'Keine berechnete Bahn.';
}

function stopWork() {
  loadVersion++;
  if (worker) { worker.terminate(); worker = null; }
  if (workerUrl) { URL.revokeObjectURL(workerUrl); workerUrl = null; }
}

function resetCamera(view = run?.period ? 'top' : 'space') {
  const wide = orbitCanvas.clientWidth > 1.6 * orbitCanvas.clientHeight;
  yaw = view === 'top' ? 0 : (wide ? 1.05 : 0.55);
  pitch = view === 'top' ? 0 : 0.65;
  zoom = 1; el('view').value = view;
  redrawCamera();
}
function redrawCamera() {
  if (!run) return;
  orbitLayout = makeOrbitLayout(); drawOrbit();
}
function changeZoom(factor) {
  zoom = Math.max(0.2, Math.min(20, zoom * factor)); redrawCamera();
}
function rotateCamera(horizontal, vertical) {
  yaw = (yaw + horizontal) % (2 * Math.PI);
  pitch = (pitch + vertical) % (2 * Math.PI);
  el('view').value = 'free'; redrawCamera();
}

function useRun(data, key, autoPlay) {
  const prepared = prepareRun(data);
  setPlaying(false); run = prepared; displayedScenario = key; frameIndex = 0;
  el('scenario').value = key; el('time').max = String(run.frames.length - 1);
  el('error-unit').textContent = run.relative ? 'ΔE / |E₀|' : 'ΔE';
  el('error-title').textContent = run.relative ? 'Energieabweichung ΔE / |E₀|' : 'ΔE in Einheiten von M₀ L₀²/t₀²';
  el('period').textContent = shortNumber(run.duration);
  el('time-label').textContent = run.period ? 'Periode T / t₀' : 'Laufdauer / t₀';
  el('steps').textContent = (run.stats?.accepted ?? run.steps).toLocaleString('de-DE');
  for (const [row, id, val] of [
    ['reject-row', 'rejected', run.stats?.rejected], ['minstep-row', 'min-step', run.stats?.minStep],
    ['minr-row', 'min-r', run.stats?.minDistance],
  ]) {
    el(row).hidden = !Number.isFinite(val);
    if (!el(row).hidden) el(id).textContent = shortNumber(val);
  }
  const ticks = document.querySelectorAll('.timeline-ticks span');
  ticks.forEach((tick, i) => { tick.textContent = run.period
    ? ['0', 'T / 4', 'T / 2', '3T / 4', 'T'][i] : shortNumber(run.duration * (i / 4)); });
  el('time').disabled = false; el('play').disabled = false;
  resetCamera(); chartLayout = el('details').open ? makeChartLayout() : null;
  updateFrame(); showStatus();
  if (autoPlay && !document.hidden) setPlaying(true);
}

function failRun(message) {
  stopWork(); el('scenario').value = displayedScenario ?? 'custom';
  showStatus(message, false, true);
  el('editor').open = true;
}

function startCalculation(input, key, autoPlay) {
  stopWork(); setPlaying(false);
  const version = loadVersion;
  el('progress').value = 0;
  showStatus('Bahn wird berechnet …', true);
  try {
    const embedded = el('solver-worker');
    if (embedded) workerUrl = URL.createObjectURL(new Blob([embedded.textContent], { type: 'text/javascript' }));
    worker = new Worker(workerUrl ?? new URL('src/solver-worker.js', document.baseURI));
    worker.onmessage = ({data}) => {
      if (loadVersion !== version) return;
      if (data.type === 'progress') { el('progress').value = data.fraction; return; }
      if (data.type === 'error') { failRun(data.message); return; }
      if (data.type === 'result') {
        stopWork();
        try {
          useRun({ ...data.data, masses: input.masses }, key, autoPlay);
          if (key === 'custom') {
            lastCustom = input;
            el('editor').open = false; el('play').focus({ preventScroll: true });
          }
        } catch (error) { failRun(error.message); }
      }
    };
    worker.onerror = () => {
      if (version === loadVersion) failRun('Der Rechenkern konnte nicht gestartet werden. Öffne die Offline-Ausgabe oder die Projektseite über einen lokalen HTTP-Server.');
    };
    const request = { ...input, type: 'simulate' };
    const wasm = el('solver-wasm');
    if (wasm) request.wasm = Uint8Array.from(atob(wasm.textContent.trim()), c => c.charCodeAt(0));
    worker.postMessage(request);
  } catch (error) { failRun('Rechnung nicht gestartet: ' + error.message); }
}

async function setScenario({ autoPlay = false } = {}) {
  const key = el('scenario').value;
  stopWork(); setPlaying(false);
  const version = loadVersion;
  if (key === 'custom') {
    showStatus(); el('editor').open = true;
    if (lastCustom) fillEditor(lastCustom);
    if (displayedScenario) el('scenario').value = displayedScenario;
    massInputs[0].focus(); return;
  }
  el('progress').removeAttribute('value');
  showStatus('Anfangsdaten werden geladen …', true);
  try {
    const data = await loadData(key === 'flyby' ? 'flyby-preset' : key);
    if (version !== loadVersion) return;
    if (key === 'flyby') { fillEditor(data); startCalculation(data, key, autoPlay); return; }
    const f = data.frames[0];
    fillEditor({ masses: data.masses, positions: f.positions, velocities: f.velocities, duration: data.period });
    useRun(data, key, autoPlay);
  } catch (error) {
    if (version === loadVersion) failRun('Die Beispieldaten konnten nicht geladen werden. Bitte öffne die Offline-Ausgabe oder die Projektseite über einen lokalen HTTP-Server.');
  }
}

el('play').addEventListener('click', () => setPlaying(!playing));
el('trails').addEventListener('change', redrawCamera);
el('scenario').addEventListener('change', () => setScenario({ autoPlay: playing }));
el('time').addEventListener('input', () => {
  if (!run) return;
  setPlaying(false); frameIndex = Number(el('time').value); updateFrame();
});
el('initial-form').addEventListener('submit', event => {
  event.preventDefault();
  try {
    const input = readEditor();
    startCalculation(input, 'custom', !matchMedia('(prefers-reduced-motion: reduce)').matches);
  } catch (error) { showStatus(error.message, false, true); }
});
el('initial-form').addEventListener('input', event => {
  event.target.setCustomValidity('');
  if (el('calculate').disabled) { stopWork(); el('scenario').value = displayedScenario ?? 'custom'; }
  showStatus();
});
el('cancel').addEventListener('click', () => {
  stopWork(); el('scenario').value = displayedScenario ?? 'custom';
  showStatus('Rechnung abgebrochen.'); if (!run) el('editor').open = true;
});
el('view').addEventListener('change', () => resetCamera(el('view').value));
el('zoom-out').addEventListener('click', () => changeZoom(1 / 1.25));
el('zoom-in').addEventListener('click', () => changeZoom(1.25));
el('reset-view').addEventListener('click', () => resetCamera());
orbitCanvas.addEventListener('pointerdown', event => {
  if (!run || drag || event.button !== 0) return;
  drag = { id: event.pointerId, x: event.clientX, y: event.clientY };
  orbitCanvas.setPointerCapture(event.pointerId);
});
orbitCanvas.addEventListener('pointermove', event => {
  if (drag?.id !== event.pointerId) return;
  const dx = event.clientX - drag.x, dy = event.clientY - drag.y;
  drag.x = event.clientX; drag.y = event.clientY;
  if (dx || dy) rotateCamera(-dx * .009, dy * .009);
});
orbitCanvas.addEventListener('lostpointercapture', () => { drag = null; });
orbitCanvas.addEventListener('keydown', event => {
  const angle = Math.PI / 24;
  switch (event.key) {
    case 'ArrowLeft': rotateCamera(angle, 0); break;
    case 'ArrowRight': rotateCamera(-angle, 0); break;
    case 'ArrowUp': rotateCamera(0, angle); break;
    case 'ArrowDown': rotateCamera(0, -angle); break;
    case '+': case '=': changeZoom(1.25); break;
    case '-': changeZoom(1 / 1.25); break;
    case '0': resetCamera(); break;
    default: return;
  }
  event.preventDefault();
});
document.addEventListener('visibilitychange', () => { previousTick = null; });
el('details').addEventListener('toggle', resize);
new ResizeObserver(resize).observe(el('simulation'));
window.addEventListener('resize', resize);
setScenario({ autoPlay: !matchMedia('(prefers-reduced-motion: reduce)').matches });
