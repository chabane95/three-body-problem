'use strict';

function readParameters(input) {
  const vector = value => Array.isArray(value) && value.length === 3 && value.every(Number.isFinite);
  const vectors = values => Array.isArray(values) && values.length === 3 && values.every(vector);
  if (!vector(input.masses) || input.masses.some(m => m <= 0)
      || !vectors(input.positions) || !vectors(input.velocities)) {
    throw new Error('Jeder Körper braucht eine positive Masse, drei Orts- und drei Geschwindigkeitskomponenten.');
  }
  const parameters = {
    G: input.G ?? 1, duration: input.duration, samples: input.samples ?? 721,
    rtol: input.rtol ?? 1e-8, atolPosition: input.atolPosition ?? 1e-10,
    atolVelocity: input.atolVelocity ?? 1e-10, minDistance: input.minDistance ?? 0,
    minStep: input.minStep ?? 0, maxSteps: input.maxSteps ?? 250000
  };
  for (const key of ['G', 'duration', 'rtol', 'atolPosition', 'atolVelocity']) {
    if (!Number.isFinite(parameters[key]) || parameters[key] <= 0) {
      throw new Error('Gravitationskonstante, Dauer und Fehlertoleranzen müssen endlich und positiv sein.');
    }
  }
  if (![parameters.minDistance, parameters.minStep].every(x => Number.isFinite(x) && x >= 0)) {
    throw new Error('Mindestabstand und Mindestschrittweite müssen endlich und mindestens null sein.');
  }
  if (!Number.isInteger(parameters.samples) || parameters.samples < 2 || parameters.samples > 10001) {
    throw new Error('Für die Darstellung sind 2 bis 10.001 Ausgabezeitpunkte möglich.');
  }
  if (!Number.isSafeInteger(parameters.maxSteps) || parameters.maxSteps < 1 || parameters.maxSteps > 0xffffffff) {
    throw new Error('Das Schrittbudget muss eine positive 32-Bit-Ganzzahl sein.');
  }
  return parameters;
}

function readFrame(t, heap) {
  const triple = (i) => [heap[i], heap[i + 1], heap[i + 2]];
  return {
    t, positions: [triple(0), triple(3), triple(6)],
    velocities: [triple(9), triple(12), triple(15)],
    energy: heap[18], momentum: triple(19), angularMomentum: heap[24],
    angularMomentumVector: triple(22), centerOfMass: triple(25), distances: triple(28)
  };
}

function describeFailure(message) {
  const messages = {
    'Coincident bodies: Newtonian gravity is singular':
      'Zwei Körper haben dieselbe Position. Die Newtonsche Punktmassengleichung ist dort singulär.',
    'Minimum separation reached during a close encounter':
      'Der eingestellte Mindestabstand wurde bei einer engen Begegnung erreicht.',
    'Integration step budget exhausted':
      'Das Schrittbudget ist erschöpft. Verkürze die Integrationsdauer oder prüfe die Fehlertoleranzen.',
    'Required step is below the minimum or floating-point resolution':
      'Die benötigte Schrittweite unterschreitet die numerische Auflösung oder die eingestellte Mindestschrittweite.',
    'Output times cannot be distinguished at this precision':
      'Die Ausgabezeitpunkte lassen sich mit dieser Zahlengenauigkeit nicht mehr unterscheiden.'
  };
  if (message.startsWith('Non-finite') || message.includes('floating-point range')) {
    return 'Die Zustandsgrößen überschreiten den darstellbaren Zahlenbereich. Prüfe Anfangswerte und Einheiten.';
  }
  return messages[message] || message;
}

async function loadModule(embedded) {
  let bytes = embedded;
  if (bytes == null) {
    const response = await fetch(new URL('solver.wasm', self.location.href));
    if (!response.ok) throw new Error('Die Datei solver.wasm konnte nicht geladen werden.');
    bytes = await response.arrayBuffer();
  }
  try {
    return await WebAssembly.compile(bytes);
  } catch {
    throw new Error('Der 3D-Solver konnte nicht geladen werden. Prüfe die WebAssembly-Datei und die Browserunterstützung.');
  }
}

async function calculate(input, sendProgress) {
  const p = readParameters(input);
  const module = await loadModule(input.wasm);
  const frames = [];
  let lastUpdate = -Infinity;
  let solver;
  const imports = {
    env: {
      fail(address) {
        const bytes = new Uint8Array(solver.memory.buffer, address);
        const message = new TextDecoder().decode(bytes.subarray(0, bytes.indexOf(0)));
        throw new Error(describeFailure(message));
      },
      sample(t, address) {
        const values = new Float64Array(solver.memory.buffer, address, 31);
        frames.push(readFrame(t, values));
        const now = performance.now();
        if (now - lastUpdate >= 100 || frames.length === p.samples) {
          sendProgress(t / p.duration);
          lastUpdate = now;
        }
      }
    }
  };
  solver = (await WebAssembly.instantiate(module, imports)).exports;
  solver._initialize();
  const bodies = new Float64Array(solver.memory.buffer, solver.input_buffer(), 21);
  for (let i = 0; i < 3; i++) {
    bodies.set([input.masses[i], ...input.positions[i], ...input.velocities[i]], i * 7);
  }
  solver.integrate(p.G, p.duration, p.samples - 1, p.rtol, p.atolPosition,
                   p.atolVelocity, p.minDistance, p.minStep, p.maxSteps);
  const [accepted, rejected, minStep, maxStep, minDistance, maxErrorRatio] =
    new Float64Array(solver.memory.buffer, solver.stats_buffer(), 6);
  return {
    scenario: 'custom', integrator: 'DOPRI5(4)', masses: input.masses, G: p.G,
    duration: p.duration, rtol: p.rtol, atolPosition: p.atolPosition, atolVelocity: p.atolVelocity,
    frames, stats: { accepted, rejected, minStep, maxStep, minDistance, maxErrorRatio }
  };
}

self.onmessage = async ({data}) => {
  if (!data || data.type !== 'simulate') return;
  try {
    const result = await calculate(data, fraction => self.postMessage({type: 'progress', fraction}));
    self.postMessage({type: 'result', data: result});
  } catch (error) {
    self.postMessage({type: 'error', message: error.message || String(error)});
  }
};
