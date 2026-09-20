#!/usr/bin/env node
// Assert the on-screen controls in web/shell.html do not collide.
//
// The layout is a hand-written table of vmin offsets, and the failure it has
// already produced once is not a crash: DUCK addressed as `left: 42vmin` and
// USE as `right: 40vmin` are nowhere near each other on a desktop and exactly
// on top of each other on a 412 px phone. The button still works, it is just
// under another button, so the player presses one and gets the other — which
// reads as a game bug rather than a layout bug and is invisible to every other
// check in this repo.
//
//   node tools/mobile-layout-test.mjs           # the three viewports below
//   node tools/mobile-layout-test.mjs --all     # plus a sweep of odd sizes
//
// It needs no wasm: the control layer is built by shell.html's own inline
// script, which runs whether or not the game module ever loads. `?touch=1`
// forces the layer on, exactly as it does for a human testing on a desktop.
import { fileURLToPath, pathToFileURL } from 'node:url';
import { dirname, resolve } from 'node:path';
import { execFileSync } from 'node:child_process';

// Resolve playwright wherever it is. A local devDependency is the normal case;
// this sandbox and the CI image have it installed globally instead, and ESM
// does not consult NODE_PATH the way require() does, so a bare import fails
// there with ERR_MODULE_NOT_FOUND and nothing to say the package is in fact
// present. Ask npm for the global root and import by path as the fallback.
async function loadPlaywright() {
  try {
    return await import('playwright');
  } catch (e) {
    if (e.code !== 'ERR_MODULE_NOT_FOUND') throw e;
    const root = execFileSync('npm', ['root', '-g'], { encoding: 'utf8' }).trim();
    return await import(pathToFileURL(resolve(root, 'playwright', 'index.js')).href);
  }
}
// playwright is CommonJS. Imported by path its named exports are not always
// detected, so the whole module arrives under `default` instead and a bare
// destructure yields undefined rather than an error.
const pw = await loadPlaywright();
const chromium = pw.chromium ?? pw.default?.chromium;
if (!chromium) throw new Error('playwright resolved but exports no chromium');

const SHELL = resolve(dirname(fileURLToPath(import.meta.url)), '..', 'web', 'shell.html');

// The three the docs promise, in the order they are written down there.
const VIEWPORTS = [
  { name: 'portrait',  width: 390, height: 844 },
  { name: 'landscape', width: 844, height: 390 },
  { name: '360x640',   width: 360, height: 640 },
];

// Cheap phones are narrower and folded phones are squarer than anything above;
// both are where a vmin layout laid out from one edge folds in on itself.
const EXTRA = [
  { name: 'iphone-se',   width: 320, height: 568 },
  { name: 'pixel-7',     width: 412, height: 915 },
  { name: 'fold-closed', width: 280, height: 653 },
  { name: 'tablet',      width: 768, height: 1024 },
  { name: 'wide',        width: 1024, height: 600 },
];

// Overlap is the hard failure, but two controls that merely fail to intersect
// are still wrong: a shared edge means a thumb on the seam gets whichever the
// hit test happens to pick. The button table is authored in vmin and every
// deliberate neighbour gap in it is 1vmin, so that — not a pixel count someone
// picked — is the rule to assert. It is also where the AIM ring lives: 0.5vmin
// of box-shadow that no bounding box includes.
const MIN_GAP_VMIN = 1;
// Subpixel layout rounds; do not fail a pair that is a hundredth short.
const EPS = 0.25;

function gap(a, b) {
  // Separation along each axis; negative on both means they overlap.
  const dx = Math.max(a.x - (b.x + b.width), b.x - (a.x + a.width));
  const dy = Math.max(a.y - (b.y + b.height), b.y - (a.y + a.height));
  return Math.max(dx, dy);
}

async function measure(page, vp) {
  await page.setViewportSize({ width: vp.width, height: vp.height });
  await page.goto(pathToFileURL(SHELL).href + '?touch=1');
  // The gate covers the controls until ENTER; the game never loads here, so
  // dismiss it the way enter() does rather than waiting for a click to work.
  await page.evaluate(() => document.getElementById('gate').classList.add('hidden'));
  return page.evaluate(() => {
    const stage = document.getElementById('stage').getBoundingClientRect();
    const box = (el, name) => {
      const r = el.getBoundingClientRect();
      return { name, x: r.x, y: r.y, width: r.width, height: r.height };
    };
    const out = [box(document.getElementById('stick'), 'STICK')];
    for (const el of document.querySelectorAll('.tbtn')) out.push(box(el, el.textContent));
    return { stage: { x: stage.x, y: stage.y, width: stage.width, height: stage.height }, controls: out };
  });
}

function check(vp, { stage, controls }) {
  const fails = [];
  const vmin = Math.min(vp.width, vp.height) / 100;
  const minGap = MIN_GAP_VMIN * vmin - EPS;

  if (controls.length < 2) fails.push(`only ${controls.length} control(s) rendered — the touch layer did not come up`);

  for (const c of controls) {
    if (c.width < 1 || c.height < 1) fails.push(`${c.name} has no size (${c.width}x${c.height})`);
    // A control hanging off the stage is a control the thumb cannot reach, and
    // on a phone the bottom edge is where the browser chrome eats into it.
    if (c.x < stage.x - 0.5 || c.y < stage.y - 0.5 ||
        c.x + c.width > stage.x + stage.width + 0.5 ||
        c.y + c.height > stage.y + stage.height + 0.5) {
      fails.push(`${c.name} is outside the stage: ${c.x.toFixed(0)},${c.y.toFixed(0)} ` +
                 `${c.width.toFixed(0)}x${c.height.toFixed(0)} vs stage ` +
                 `${stage.width.toFixed(0)}x${stage.height.toFixed(0)}`);
    }
  }

  for (let i = 0; i < controls.length; i++) {
    for (let j = i + 1; j < controls.length; j++) {
      const g = gap(controls[i], controls[j]);
      if (g < 0) {
        fails.push(`${controls[i].name} overlaps ${controls[j].name} by ${(-g).toFixed(1)}px`);
      } else if (g < minGap) {
        fails.push(`${controls[i].name} and ${controls[j].name} are ${(g / vmin).toFixed(2)}vmin apart ` +
                   `(${g.toFixed(1)}px); the table's minimum is ${MIN_GAP_VMIN}vmin`);
      }
    }
  }
  return fails;
}

const viewports = process.argv.includes('--all') ? [...VIEWPORTS, ...EXTRA] : VIEWPORTS;
const browser = await chromium.launch();
const page = await browser.newPage();
let bad = 0;

for (const vp of viewports) {
  const measured = await measure(page, vp);
  const fails = check(vp, measured);
  const label = `${vp.name} (${vp.width}x${vp.height})`;
  if (fails.length) {
    bad++;
    console.log(`FAIL ${label}`);
    for (const f of fails) console.log(`       ${f}`);
  } else {
    console.log(`ok   ${label} — ${measured.controls.length} controls, no collisions`);
  }
}

await browser.close();
if (bad) {
  console.log(`\n${bad}/${viewports.length} viewport(s) failed`);
  process.exit(1);
}
console.log(`\nall ${viewports.length} viewport(s) clear`);
