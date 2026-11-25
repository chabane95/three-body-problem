const el = (id) => document.getElementById(id);
const orbitCanvas = el('orbits');
const chartCanvas = el('energy-plot');
const colors = ['#59d5d5', '#ffac72', '#b99aff'];
const format = (value, digits) => value.toFixed(digits).replace('-', '−').replace('.', ',');
const scientific = new Intl.NumberFormat('de-DE', { notation: 'scientific', maximumFractionDigits: 2 });
const cycleMilliseconds = 14000;
let run = null;
let frameIndex = 0;
let playbackTime = 0;
let previousTick = null;
let animation = 0;
let playing = false;
let loadVersion = 0;
let orbitLayout = null;
let chartLayout = null;

// Drawing only depends on samples produced by the C++ integration.
async function loadData(scenario) {
  const embedded = document.getElementById('data-' + scenario);
  if (embedded) return JSON.parse(embedded.textContent);
  const response = await fetch(`data/${scenario}.json`);
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  return response.json();
}

function prepareRun(data) {
  const frames = data.frames;
  if (!Array.isArray(frames) || frames.length < 2 || !(data.period > 0)) throw new Error('Ungültige Bahndaten');
  const initialEnergy = frames[0].energy;
  let maxX = 0, maxY = 0, maxDistance = 0, maxError = 0;
  const errors = frames.map((frame) => {
    for (const [x, y] of frame.positions) {
      maxX = Math.max(maxX, Math.abs(x));
      maxY = Math.max(maxY, Math.abs(y));
    }
    maxDistance = Math.max(maxDistance, ...frame.distances);
    const error = (frame.energy - initialEnergy) / Math.abs(initialEnergy);
    maxError = Math.max(maxError, Math.abs(error));
    return error;
  });
  return { ...data, maxX, maxY, maxDistance, maxError, errors };
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

function pathForBody(ctx, body, first, last, layout) {
  const { sx, sy } = layout;
  ctx.beginPath();
  for (let i = first; i <= last; i++) {
    const [x, y] = run.frames[i].positions[body];
    if (i === first) ctx.moveTo(sx(x), sy(y));
    else ctx.lineTo(sx(x), sy(y));
  }
  ctx.stroke();
}

// Keep full trajectories and grid lines in a bitmap. Playback only draws the short trails and markers.
function makeOrbitLayout() {
  const { width, height, ctx, ratio } = setupCanvas(orbitCanvas);
  if (width < 1 || height < 1) return null;
  const left = width < 450 ? 36 : 48, right = width - (width < 450 ? 16 : 28);
  const top = 24, bottom = height - 26;
  const scale = Math.min((right - left) / (2.30 * run.maxX), (bottom - top) / (2.34 * run.maxY));
  const ox = (left + right) / 2, oy = (top + bottom) / 2;
  const sx = (x) => ox + scale * x, sy = (y) => oy - scale * y;
  const step = niceStep((width < 450 ? 41 : 67) / scale);
  const tickLabel = (n) => Number(n.toFixed(3)).toLocaleString('de-DE');
  ctx.lineWidth = 1;
  const nx = Math.floor((right - ox) / (step * scale)), ny = Math.floor((oy - top) / (step * scale));
  for (let i = -nx; i <= nx; i++) {
    const x = sx(i * step);
    line(ctx, x, top, x, bottom, i === 0 ? '#486263' : '#2a4145');
    ctx.textAlign = 'center'; ctx.fillStyle = '#b1c5c4';
    ctx.fillText(tickLabel(i * step), x, bottom + 16);
  }
  for (let i = -ny; i <= ny; i++) {
    const y = sy(i * step);
    line(ctx, left, y, right, y, i === 0 ? '#486263' : '#2a4145');
    ctx.textAlign = 'right'; ctx.fillStyle = '#b1c5c4';
    ctx.fillText(tickLabel(i * step), left - 8, y + 3);
  }
  ctx.font = '11px Georgia, serif'; ctx.fillStyle = '#d3e2df'; ctx.textAlign = 'left';
  ctx.fillText('y / L₀', left, top - 6); ctx.textAlign = 'right';
  ctx.fillText('x / L₀', right, top - 6);
  const layout = { width, height, ratio, ctx, sx, sy };
  ctx.strokeStyle = '#8aa8a2'; ctx.lineWidth = 1.5;
  // In both presets the three bodies follow the same geometric curve during the period.
  pathForBody(ctx, 0, 0, run.frames.length - 1, layout);
  const background = document.createElement('canvas');
  background.width = orbitCanvas.width; background.height = orbitCanvas.height;
  background.getContext('2d').drawImage(orbitCanvas, 0, 0);
  return { ...layout, background };
}

function drawOrbit() {
  if (!orbitLayout) return;
  const { ctx, width, height, background, sx, sy } = orbitLayout;
  ctx.clearRect(0, 0, width, height); ctx.drawImage(background, 0, 0, width, height);
  const positions = run.frames[frameIndex].positions;
  if (el('trails').checked && frameIndex > 0) {
    const trailLength = Math.max(2, Math.round((run.frames.length - 1) * .105));
    const first = Math.max(0, frameIndex - trailLength);
    ctx.lineCap = 'round'; ctx.lineJoin = 'round';
    for (let body = 0; body < 3; body++) {
      ctx.strokeStyle = colors[body]; ctx.lineWidth = 2.8;
      for (let piece = 0; piece < 7; piece++) {
        const a = Math.max(first, frameIndex - Math.round(trailLength * (7 - piece) / 7));
        const b = Math.max(first, frameIndex - Math.round(trailLength * (6 - piece) / 7));
        ctx.globalAlpha = (piece + 1) / 7;
        if (b > a) pathForBody(ctx, body, a, b, orbitLayout);
      }
    }
    ctx.globalAlpha = 1;
  }
  for (let body = 0; body < positions.length; body++) {
    const [x, y] = positions[body], px = sx(x), py = sy(y);
    ctx.beginPath(); ctx.fillStyle = '#102126'; ctx.arc(px, py, 8, 0, 2 * Math.PI); ctx.fill();
    ctx.beginPath(); ctx.fillStyle = colors[body]; ctx.arc(px, py, 6, 0, 2 * Math.PI); ctx.fill();
    const tx = px + (px > width - 40 ? -13 : 12), ty = py + (py < 27 ? 16 : -10);
    ctx.font = 'bold 12px Consolas, monospace'; ctx.textAlign = 'center';
    ctx.lineWidth = 3; ctx.strokeStyle = '#14282c'; ctx.strokeText(String(body + 1), tx, ty);
    ctx.fillStyle = colors[body]; ctx.fillText(String(body + 1), tx, ty);
  }
}

function makeChartLayout() {
  const { ctx, width, height } = setupCanvas(chartCanvas);
  if (width < 1 || height < 1) return null;
  const left = 29, right = width - 5, top = 18, bottom = height - 18;
  const exponent = run.maxError > 0 ? Math.floor(Math.log10(run.maxError)) : 0;
  const factor = 10 ** exponent;
  const bound = Math.max(1, Math.ceil(run.maxError / factor)) * factor;
  const sx = (time) => left + (right - left) * time / run.period;
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
  ctx.textAlign = 'right'; ctx.fillText('T', right, height - 2);
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
  const cells = [document.createElement('td'), document.createElement('td')];
  tableRow.append(heading, ...cells); el('coordinates').append(tableRow); positionCells.push(cells);
}

function updateFrame() {
  const frame = run.frames[frameIndex];
  el('time').value = String(frameIndex);
  const fraction = format(frame.t / run.period, 3);
  el('time').setAttribute('aria-valuetext', `${fraction} Umlaufperioden`);
  el('clock').textContent = `t / T = ${fraction}`;
  el('energy').textContent = format(frame.energy, 7);
  el('error').textContent = run.errors[frameIndex] === 0 ? '0' : scientific.format(run.errors[frameIndex]);
  frame.distances.forEach((distance, i) => {
    distanceOutputs[i].textContent = format(distance, 4);
    distanceBars[i].style.transform = `scaleX(${distance / run.maxDistance})`;
    frame.positions[i].forEach((coordinate, j) => { positionCells[i][j].textContent = format(coordinate, 6); });
  });
  drawOrbit(); drawChart();
}

function resize() {
  if (!run) return;
  orbitLayout = makeOrbitLayout(); chartLayout = makeChartLayout();
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
  if (previousTick !== null) playbackTime = (playbackTime + (now - previousTick) * Number(el('speed').value) * run.period / cycleMilliseconds) % run.period;
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
    playbackTime = run.frames[frameIndex].t;
    animation = requestAnimationFrame(tick);
  }
}

async function setScenario({ autoPlay = false } = {}) {
  const scenario = el('scenario').value, version = ++loadVersion;
  setPlaying(false); run = null; frameIndex = 0;
  el('play').disabled = true; el('time').disabled = true;
  for (const id of ['clock', 'energy', 'error', 'period', 'steps']) el(id).textContent = '—';
  for (const value of [...distanceOutputs, ...positionCells.flat()]) value.textContent = '—';
  for (const bar of distanceBars) bar.style.transform = 'scaleX(0)';
  for (const canvas of [orbitCanvas, chartCanvas]) canvas.width = canvas.width;
  el('simulation').setAttribute('aria-busy', 'true'); el('plot-status').hidden = false;
  el('plot-status').textContent = 'Bahndaten werden geladen …';
  try {
    const data = await loadData(scenario);
    if (version !== loadVersion) return;
    run = prepareRun(data); el('time').max = String(run.frames.length - 1);
    el('period').textContent = format(run.period, 6);
    el('steps').textContent = run.stepsPerPeriod.toLocaleString('de-DE');
    resize(); updateFrame(); el('plot-status').hidden = true;
    el('play').disabled = false; el('time').disabled = false;
    if (autoPlay && !document.hidden) setPlaying(true);
  } catch (error) {
    if (version !== loadVersion) return;
    run = null;
    el('plot-status').textContent = location.protocol === 'file:' ? 'Öffne die Offline-Ausgabe oder starte die Projektseite über einen lokalen HTTP-Server.' : 'Die Bahndaten konnten nicht geladen werden. Bitte prüfe den Ordner data und lade die Seite erneut.';
    console.error('Bahndaten konnten nicht geladen werden.', error);
  } finally { if (version === loadVersion) el('simulation').setAttribute('aria-busy', 'false'); }
}

el('play').addEventListener('click', () => setPlaying(!playing));
el('trails').addEventListener('change', () => { if (run) drawOrbit(); });
el('scenario').addEventListener('change', () => setScenario({ autoPlay: playing }));
el('time').addEventListener('input', () => {
  if (!run) return;
  setPlaying(false); frameIndex = Number(el('time').value); updateFrame();
});
document.addEventListener('visibilitychange', () => { previousTick = null; });
el('details').addEventListener('toggle', resize);
new ResizeObserver(resize).observe(el('simulation'));
window.addEventListener('resize', resize);
setScenario({ autoPlay: !matchMedia('(prefers-reduced-motion: reduce)').matches });
