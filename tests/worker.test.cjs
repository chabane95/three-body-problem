// Run the shipped browser worker and real WASM binary inside an isolated JS context.
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {execFileSync} = require('node:child_process');

const src = path.resolve(__dirname, '../src');
const workerCode = fs.readFileSync(path.join(src, 'solver-worker.js'), 'utf8');
const wasm = fs.readFileSync(path.join(src, 'solver.wasm'));

async function run(input) {
  const messages = [];
  const self = {postMessage: message => messages.push(message)};
  vm.runInNewContext(workerCode, {self, performance, TextDecoder, WebAssembly});
  await self.onmessage({data: {type: 'simulate', wasm, ...input}});
  return messages.at(-1);
}

test('WebAssembly advances all three axes on a tilted, translated Lagrange orbit', async () => {
  const scale = 1 / Math.sqrt(3);
  const period = 2 * Math.PI * scale;
  const positions = [], velocities = [];
  for (let i = 0; i < 3; ++i) {
    const angle = 2 * Math.PI * i / 3;
    positions.push([1 + scale * Math.cos(angle), -.2 + .6 * scale * Math.sin(angle),
                    .5 + .8 * scale * Math.sin(angle)]);
    velocities.push([.1 - Math.sin(angle), .6 * Math.cos(angle), -.05 + .8 * Math.cos(angle)]);
  }
  const result = await run({masses: [1,1,1], positions, velocities, duration: period,
                            samples: 24, rtol: 1e-10, atolPosition: 1e-12, atolVelocity: 1e-12});
  assert.equal(result.type, 'result', result.message);
  const {frames, stats} = result.data;
  assert.equal(frames.length, 24);
  for (const frame of frames) {
    for (let i = 0; i < 3; ++i) {
      const a = Math.sqrt(3) * frame.t + 2 * Math.PI * i / 3;
      const expected = [1 + .1 * frame.t + scale * Math.cos(a), -.2 + .6 * scale * Math.sin(a),
                        .5 - .05 * frame.t + .8 * scale * Math.sin(a)];
      const error = Math.hypot(...expected.map((x, j) => x - frame.positions[i][j]));
      assert.ok(error < 2e-8, `position error ${error}`);
    }
    assert.equal(frame.angularMomentumVector.length, 3);
    assert.equal(frame.centerOfMass.length, 3);
  }
  assert.ok(stats.accepted > frames.length && stats.maxErrorRatio <= 1 && stats.minStep > 0);
});

test('A grazing crossing between output frames arrives as a readable worker error', async () => {
  const result = await run({masses: [1, 2, 3], G: 1e-14,
                            positions: [[0,0,0], [-.065,.9999,0], [0,5,2]],
                            velocities: [[0,0,0],[1,0,0],[0,0,0]],
                            duration: .17, samples: 2, minDistance: 1});
  assert.equal(result.type, 'error');
  assert.match(result.message, /Mindestabstand/);
});

test('A spatial unequal-mass encounter agrees with the native C++ program', {
  skip: process.env.THREE_BODY_CLI ? false : 'Set THREE_BODY_CLI to the compiled executable'
}, async () => {
  const input = {
    masses: [1, .7, .18],
    positions: [[-.4, 0, 0], [.6, 0, 0], [-2.9, -2.4, 1.4]],
    velocities: [[0, -.54, 0], [0, .83, .13], [1.1, .4, -.27]],
    duration: 6, samples: 721
  };
  const args = ['--duration', String(input.duration), '--intervals', String(input.samples - 1)];
  for (let i = 0; i < 3; ++i) {
    args.push('--body', ...[input.masses[i], ...input.positions[i], ...input.velocities[i]].map(String));
  }
  const native = JSON.parse(execFileSync(path.resolve(process.env.THREE_BODY_CLI), args, {
    encoding: 'utf8', maxBuffer: 8 * 1024 * 1024
  }));
  const result = await run(input);
  assert.equal(result.type, 'result', result.message);
  assert.equal(result.data.frames.length, native.frames.length);
  for (let i = 0; i < native.frames.length; ++i) {
    const fromNative = native.frames[i];
    const fromBrowser = result.data.frames[i];
    assert.ok(Math.abs(fromNative.t - fromBrowser.t) < 2e-14);
    for (const key of ['positions', 'velocities']) {
      for (let b = 0; b < 3; ++b) {
        const difference = fromNative[key][b].map((v, k) => v - fromBrowser[key][b][k]);
        assert.ok(Math.hypot(...difference) < 2e-7, `${key} at t=${fromNative.t}`);
      }
    }
    assert.ok(Math.abs(fromNative.energy - fromBrowser.energy) < 1e-8);
  }
});
