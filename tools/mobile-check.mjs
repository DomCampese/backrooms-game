// Layout checks for web/shell.html, at the sizes a phone actually has.
//
// The shell is the only part of the game a screenshot of the world will not
// show you, and its two failure modes are both silent: a control that has
// drifted on top of another one still works, it just does the neighbour's job;
// and a splash screen whose content is taller than the viewport does not
// scroll, it clips — and the first thing over the edge is ENTER, which reads as
// the page having failed to load rather than as having more to show.
//
//   node tools/mobile-check.mjs            # checks web/shell.html
//   node tools/mobile-check.mjs path.html  # or a built web/dist/index.html
//
// Needs playwright and a chromium. ESM import does not look at NODE_PATH, and
// in this sandbox playwright is installed globally rather than in the project,
// so fall back to the global root explicitly — otherwise the failure is a bare
// ERR_MODULE_NOT_FOUND that looks like the package is missing when it is not.
import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';
import { execSync } from 'node:child_process';

const pw = await import('playwright').catch(() => {
  const root = execSync('npm root -g', { encoding: 'utf8' }).trim();
  return import(pathToFileURL(resolve(root, 'playwright/index.js')).href);
});
// The global copy is CommonJS, so its exports arrive under `default`.
const { chromium } = pw.chromium ? pw : pw.default;

const file = process.argv[2] || 'web/shell.html';
const url = pathToFileURL(resolve(file)).href;

// Portrait and landscape of the same handsets, plus the desktop size every
// number in the HUD was authored against.
const SIZES = [
  { name: 'pixel portrait',   w: 412, h: 915 },
  { name: 'small portrait',   w: 360, h: 640 },
  { name: 'iphone portrait',  w: 390, h: 844 },
  { name: 'pixel landscape',  w: 915, h: 412 },
  { name: 'small landscape',  w: 640, h: 360 },
  { name: 'tablet',           w: 820, h: 1180 },
  { name: 'desktop',          w: 1440, h: 850 },
];

const fails = [];
const note = (size, msg) => fails.push(`${size.name} (${size.w}x${size.h}): ${msg}`);

const browser = await chromium.launch();

for (const size of SIZES) {
  const page = await browser.newPage({ viewport: { width: size.w, height: size.h } });
  // ?touch=1 forces the on-screen controls on, which is the only way to measure
  // them on a desktop browser.
  await page.goto(url + '?touch=1');
  await page.waitForTimeout(120);

  // ---- the controls must not overlap ------------------------------------
  const boxes = await page.$$eval('#touch .tbtn, #touch #stick', els =>
    els.map(e => {
      const r = e.getBoundingClientRect();
      return { t: e.id || e.textContent.trim(), x: r.left, y: r.top, w: r.width, h: r.height };
    }));
  if (boxes.length < 12) note(size, `expected 12 controls, measured ${boxes.length}`);
  for (let i = 0; i < boxes.length; i++)
    for (let j = i + 1; j < boxes.length; j++) {
      const a = boxes[i], b = boxes[j];
      if (a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h)
        note(size, `controls overlap: ${a.t} and ${b.t}`);
    }
  for (const b of boxes) {
    if (b.x < 0 || b.y < 0 || b.x + b.w > size.w + 0.5 || b.y + b.h > size.h + 0.5)
      note(size, `control ${b.t} is off screen`);
    // A thumb needs something to land on. 40 CSS px is the usual floor.
    if (Math.min(b.w, b.h) < 40 && b.t !== 'stick')
      note(size, `control ${b.t} is only ${Math.round(Math.min(b.w, b.h))} px`);
  }

  // ---- the splash must not clip, and ENTER must be reachable -------------
  const gate = await page.evaluate(() => {
    const g = document.getElementById('gate');
    const s = document.getElementById('start');
    const gr = g.getBoundingClientRect(), sr = s.getBoundingClientRect();
    return {
      scrolls: g.scrollHeight > g.clientHeight + 1,
      // Is the button inside the gate's visible box as the page first loads?
      startVisible: sr.top >= gr.top - 0.5 && sr.bottom <= gr.bottom + 0.5,
      startH: sr.height,
      hOverflow: document.documentElement.scrollWidth > window.innerWidth + 0.5,
      // Everything below the fold has to be reachable, which needs a scroller.
      overflowY: getComputedStyle(g).overflowY,
    };
  });
  if (gate.hOverflow) note(size, 'the page scrolls sideways');
  if (gate.startH < 40) note(size, `ENTER is only ${Math.round(gate.startH)} px tall`);
  if (!gate.startVisible)
    note(size, 'ENTER is not on screen when the splash loads');
  if (gate.scrolls && gate.overflowY !== 'auto' && gate.overflowY !== 'scroll')
    note(size, 'the splash overflows and cannot be scrolled');

  // ---- the canvas must never be stretched --------------------------------
  // raylib picks the backing-store size on its own and it is not the element's
  // box; stand in for it here and check fitCanvas keeps the aspect ratio. The
  // symptom of getting this wrong is a world with the wrong field of view, not
  // a layout that looks broken, so nothing else would catch it.
  for (const [bw, bh] of [[1440, 850], [size.w, size.h], [640, 360]]) {
    const fit = await page.evaluate(async ([bw, bh]) => {
      const c = document.getElementById('canvas'), st = document.getElementById('stage');
      c.width = bw; c.height = bh;
      await new Promise(r => setTimeout(r, 320));     // the shell's fit poll
      return { w: parseFloat(c.style.width), h: parseFloat(c.style.height),
               aw: st.clientWidth, ah: st.clientHeight };
    }, [bw, bh]);
    if (!fit.w || !fit.h) { note(size, `canvas ${bw}x${bh} was never sized`); continue; }
    const want = bw / bh, got = fit.w / fit.h;
    if (Math.abs(want - got) / want > 0.02)
      note(size, `canvas ${bw}x${bh} drawn at ${fit.w}x${fit.h}: aspect ${got.toFixed(3)} not ${want.toFixed(3)}`);
    if (fit.w > fit.aw + 0.5 || fit.h > fit.ah + 0.5)
      note(size, `canvas ${bw}x${bh} overflows the stage at ${fit.w}x${fit.h}`);
  }

  // ---- the title card's "tap anywhere" has to be true --------------------
  // Game::updateMenu starts a run on touch.startGesture. Every case below is a
  // gesture a player makes on a title screen, and the last two are the ones
  // that must NOT count: a button has its own job, and a look drag across the
  // card is not a request to begin.
  const gestures = await page.evaluate(async ([w, h]) => {
    // The gate is what the player dismisses with ENTER; the controls are under
    // it and get no events until it goes. Hide it the way enter() does.
    document.getElementById('gate').classList.add('hidden');
    const layer = document.getElementById('touch');
    const t = Module.__touch;
    let id = 1;
    // Dispatch at the element actually under the point and let it bubble: the
    // handler reads e.target to tell a button press from open screen, so firing
    // everything at the layer would make every gesture look like open screen —
    // which is exactly the false pass this check exists to avoid.
    const send = (type, x, y, pid) => {
      const el = document.elementFromPoint(x, y) || layer;
      el.dispatchEvent(new PointerEvent(type, {
        pointerId: pid, clientX: x, clientY: y, bubbles: true, cancelable: true }));
    };
    const gesture = (steps) => {
      t.startGesture = 0;
      const pid = id++;
      for (const [type, x, y] of steps) send(type, x, y, pid);
      return t.startGesture;
    };
    const mid = [w / 2, h * 0.32];              // open screen, clear of every control
    const btn = document.querySelector('#touch .tbtn').getBoundingClientRect();
    // The stick zone is the lower left; push well past STICK_PUSH.
    const sx = w * 0.18, sy = h * 0.78;
    return {
      midTap:   gesture([['pointerdown', ...mid], ['pointerup', ...mid]]),
      // a tap is allowed to wander a few pixels — a thumb is not a stylus
      sloppyTap: gesture([['pointerdown', ...mid],
                          ['pointermove', mid[0] + 6, mid[1] + 5],
                          ['pointerup', mid[0] + 6, mid[1] + 5]]),
      stickPush: gesture([['pointerdown', sx, sy], ['pointermove', sx + 90, sy - 90],
                          ['pointerup', sx + 90, sy - 90]]),
      stickTap:  gesture([['pointerdown', sx, sy], ['pointerup', sx, sy]]),
      button:    gesture([['pointerdown', btn.left + btn.width / 2, btn.top + btn.height / 2],
                          ['pointerup',   btn.left + btn.width / 2, btn.top + btn.height / 2]]),
      lookDrag:  gesture([['pointerdown', ...mid],
                          ['pointermove', mid[0] + 140, mid[1] + 40],
                          ['pointerup',   mid[0] + 140, mid[1] + 40]]),
      cancelled: gesture([['pointerdown', ...mid], ['pointercancel', ...mid]]),
    };
  }, [size.w, size.h]);
  for (const k of ['midTap', 'sloppyTap', 'stickPush', 'stickTap'])
    if (!gestures[k]) note(size, `${k} does not start the game`);
  for (const k of ['button', 'lookDrag', 'cancelled'])
    if (gestures[k]) note(size, `${k} starts the game and must not`);

  // ---- one drag has to turn you a useful amount ---------------------------
  // The shell reports a look drag in the same pixels a mouse delta is, and
  // Game::updateLook multiplies by 0.0030 rad/px. So how far one thumb drag
  // turns you is LOOK_GAIN * px * 0.0030, and a thumb — unlike a mouse — cannot
  // be lifted and replaced mid-gesture. At the original 1.35 a swipe clean
  // across a phone was a quarter turn and looking behind you took four of them.
  // Pin the product rather than the constant: either half can move.
  const RAD_PER_PX = 0.0030;
  const turn = await page.evaluate(async ([w, h]) => {
    const t = Module.__touch;
    const layer = document.getElementById('touch');
    const mid = [w / 2, h * 0.32];
    const send = (type, x, y) => {
      const el = document.elementFromPoint(x, y) || layer;
      el.dispatchEvent(new PointerEvent(type, {
        pointerId: 900, clientX: x, clientY: y, bubbles: true, cancelable: true }));
    };
    t.lookX = 0;
    send('pointerdown', mid[0], mid[1]);
    send('pointermove', mid[0] + 200, mid[1]);
    send('pointerup',   mid[0] + 200, mid[1]);
    return t.lookX;
  }, [size.w, size.h]);
  const deg = turn * RAD_PER_PX * 180 / Math.PI;
  if (!(deg > 60 && deg < 110))
    note(size, `a 200 px drag turns ${deg.toFixed(0)} deg, want 60-110`);

  await page.close();
}

// A touchscreen laptop reports maxTouchPoints in Chrome even though its
// primary pointer is a mouse or trackpad. Capability alone must not cover the
// desktop game with mobile controls; the primary-pointer media query decides.
const hybrid = await browser.newPage({ viewport: { width: 1440, height: 850 } });
await hybrid.addInitScript(() => {
  Object.defineProperty(Navigator.prototype, 'maxTouchPoints', {
    configurable: true,
    get: () => 10,
  });
});
await hybrid.goto(url);
const hybridTouch = await hybrid.evaluate(() => ({
  active: Module.__touch.active,
  visible: document.getElementById('touch').classList.contains('on'),
  coarse: matchMedia('(pointer: coarse)').matches,
  points: navigator.maxTouchPoints,
}));
if (hybridTouch.coarse)
  note({ name: 'hybrid desktop', w: 1440, h: 850 }, 'test browser unexpectedly has a coarse primary pointer');
if (hybridTouch.points !== 10)
  note({ name: 'hybrid desktop', w: 1440, h: 850 }, `could not simulate maxTouchPoints (got ${hybridTouch.points})`);
if (hybridTouch.active || hybridTouch.visible)
  note({ name: 'hybrid desktop', w: 1440, h: 850 }, 'touch capability enabled mobile controls with a fine primary pointer');
await hybrid.close();

// The other half of the classifier: an actual coarse-primary phone still gets
// controls automatically, without relying on the test-only query override.
const phoneContext = await browser.newContext({
  viewport: { width: 390, height: 844 },
  isMobile: true,
  hasTouch: true,
});
const phone = await phoneContext.newPage();
await phone.goto(url);
const phoneTouch = await phone.evaluate(() => ({
  active: Module.__touch.active,
  visible: document.getElementById('touch').classList.contains('on'),
  coarse: matchMedia('(pointer: coarse)').matches,
}));
if (!phoneTouch.coarse)
  note({ name: 'coarse phone', w: 390, h: 844 }, 'test browser did not expose a coarse primary pointer');
if (!phoneTouch.active || !phoneTouch.visible)
  note({ name: 'coarse phone', w: 390, h: 844 }, 'coarse primary pointer did not enable mobile controls');
await phoneContext.close();

await browser.close();

if (fails.length) {
  console.error('mobile-check FAILED:');
  for (const f of fails) console.error('  ' + f);
  process.exit(1);
}
console.log(`mobile-check OK - ${SIZES.length} viewports, controls and splash both clear`);
