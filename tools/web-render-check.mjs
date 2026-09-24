// Run against a locally served web build: GAME_URL=http://127.0.0.1:8105/dist/ node tools/web-render-check.mjs
// Use BROWSER_CHANNEL=chrome to exercise the desktop GPU/ANGLE path; software
// Chromium did not reproduce the dead-lamp reflection corruption.
import { createRequire } from 'node:module';
import { readFileSync, mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
import assert from 'node:assert/strict';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const browser = await chromium.launch({ channel: process.env.BROWSER_CHANNEL || undefined });
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 850 } });
  await page.goto(process.env.GAME_URL || 'http://127.0.0.1:8000/');
  const source = readFileSync(new URL('../src/shaders.cpp', import.meta.url), 'utf8');
  const visibility = source.slice(source.indexOf('int occAt('), source.indexOf('// the hunter is solid too:'));
  const results = await page.evaluate(visibility => {
    const canvas = document.createElement('canvas'); canvas.width = canvas.height = 1;
    const gl = canvas.getContext('webgl2'); if (!gl) throw Error('WebGL 2 unavailable');
    function shader(type, source) {
      const s = gl.createShader(type); gl.shaderSource(s, source); gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw Error(gl.getShaderInfoLog(s));
      return s;
    }
    const p = gl.createProgram();
    gl.attachShader(p, shader(gl.VERTEX_SHADER, '#version 300 es\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}'));
    gl.attachShader(p, shader(gl.FRAGMENT_SHADER, '#version 300 es\nprecision highp float; precision highp int; precision highp sampler2D;\nuniform float uOccN; uniform vec2 uOccOrigin; uniform sampler2D texture2; uniform vec2 a,b; uniform float spread; out vec4 color;\n' + visibility + '\nvoid main(){color=vec4(vec3(spread>0.0?panelVis(a,b,spread):lightVis(a,b)),1);}'));
    gl.linkProgram(p); if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw Error(gl.getProgramInfoLog(p));
    gl.useProgram(p); gl.uniform1f(gl.getUniformLocation(p,'uOccN'),6);
    gl.uniform2f(gl.getUniformLocation(p,'uOccOrigin'),-2,-2);
    const tex=gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D,tex);
    gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.NEAREST);
    const cases = [
      ['pillar across cells',[-1,1],[3,1],0,0,4,0],
      ['pillar in first cell',[0.2,1],[3,1],0,0,4,0],
      ['pillar in last cell',[-1,1],[1.8,1],0,0,4,0],
      ['pillar in same cell',[0.2,1],[1.8,1],0,0,4,0],
      ['cell corner stays open',[-1,0.2],[3,0.2],0,0,4,255],
      ['parallel beside pillar',[0.2,-1],[0.2,3],0,0,4,255],
      ['vertical crossing',[1,-1],[1,3],0,0,4,0],
      ['surface to exterior',[0.41,1],[-1,1],0,0,4,255],
      ['reverse crossing',[3,1],[-1,1],0,0,4,0],
      ['negative cell',[-3,-1],[1,-1],-1,-1,4,0],
      ['west wall',[-1,1],[1,1],0,0,2,0],
      ['north wall',[1,-1],[1,1],0,0,1,0],
      ['clear cell',[-1,1],[3,1],0,0,0,255],
      // Panel centres sit on cell corners. The walls of the cell at that
      // corner must not shadow the light into the cells beside it.
      ['corner start, diagonal cell',[0,0],[-1,-1],0,0,3,255],
      ['corner start, west neighbour',[0,0],[-1,1],0,0,3,255],
      ['corner start, north neighbour',[0,0],[1,-1],0,0,3,255],
      ['corner start, far wall blocks',[0,0],[3,1.5],1,0,2,0],
      // Near a panel two taps straddle the ray. A wall through the panel's
      // corner must not catch one of them: that halved the light inside 6 m.
      ['taps beside wall through corner',[0,0],[1,1.5],0,0,2,255,0.45],
      ['taps along wall through corner',[0,0],[0.2,1.8],0,0,2,255,0.45],
      ['taps still stopped by far wall',[0,0],[3,1.5],1,0,2,0,0.3],
    ];
    return cases.map(([name,a,b,x,z,code,expected,spread=0])=>{
      const data=new Uint8Array(6*6*4); data[((z+2)*6+x+2)*4]=code;
      gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA8,6,6,0,gl.RGBA,gl.UNSIGNED_BYTE,data);
      gl.uniform1f(gl.getUniformLocation(p,'spread'),spread);gl.uniform2fv(gl.getUniformLocation(p,'a'),a);gl.uniform2fv(gl.getUniformLocation(p,'b'),b);
      gl.drawArrays(gl.TRIANGLES,0,3);const pixel=new Uint8Array(4);
      gl.readPixels(0,0,1,1,gl.RGBA,gl.UNSIGNED_BYTE,pixel);
      return {name,actual:pixel[0],expected,error:gl.getError()};
    });
  }, visibility);
  for (const r of results) { assert.equal(r.error,0,r.name); assert.equal(r.actual,r.expected,r.name); }
  console.log(`PASS ${results.length} GPU pillar/wall visibility cases`);
  // Add capture-only environment knobs to the served shell, not the release.
  await page.route('**/index.html*',async route=>{
    const response=await route.fetch();
    const body=(await response.text()).replace(/level:\s*["']BACKROOMS_LEVEL["']/,
      "shot:'BACKROOMS_SHOT',clean:'BACKROOMS_CLEAN',time:'BACKROOMS_TIME',shotframe:'BACKROOMS_SHOTFRAME',level:'BACKROOMS_LEVEL'");
    await route.fulfill({response,body});
  });
  const errors=[];page.on('pageerror',e=>errors.push(String(e)));
  page.on('console',m=>{if(/SHADER:.*(failed|error)|^ERROR:/i.test(m.text()))errors.push(m.text());});
  const url=new URL('index.html',process.env.GAME_URL || 'http://127.0.0.1:8000/');
  url.search='seed=1337&noblackout=1&shot=probe.png&clean=1&time=4&shotframe=1000000';
  await page.goto(url.href);await page.waitForFunction(()=>!document.getElementById('start').disabled);
  await page.click('#start');await page.waitForTimeout(4000);
  const output=resolve(process.env.CAPTURE_DIR || 'shots/web-render');mkdirSync(output,{recursive:true});
  const png=await page.screenshot({path:resolve(output,'revolver.png')});
  const darkest=await page.evaluate(async base64=>{
    const img=new Image();img.src='data:image/png;base64,'+base64;await img.decode();
    const c=document.createElement('canvas');c.width=img.width;c.height=img.height;
    const ctx=c.getContext('2d');ctx.drawImage(img,0,0);
    // Interior barrel patch: no silhouette edges, gold trim or background.
    const d=ctx.getImageData(855,499,45,13).data;let low=255;
    for(let i=0;i<d.length;i+=4)low=Math.min(low,Math.max(d[i],d[i+1],d[i+2]));
    return low;
  },png.toString('base64'));
  assert.deepEqual(errors,[]);assert.ok(darkest>40,`black reflection shards: darkest barrel pixel ${darkest}`);
  console.log(`PASS desktop revolver capture (darkest barrel pixel ${darkest}); ${output}`);
} finally { await browser.close(); }
