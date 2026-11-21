// 配置开关效果证据采集：对桌面端顶栏三个开关各截 off→on 按钮级 PNG 对，
// 并用 ffmpeg 合成交替播放的 GIF。前置：stack-up.sh 全栈在线 + vite dev
// server（5180）在线。产物落 docs/screenshots/switches/。
// 用法：node apps/desktop/dev/capture-switch-effects.mjs
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { chromium } = require('/home/cui/workspaces/chirp/apps/web_companion/node_modules/playwright');

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '..', '..', '..');
const OUT = path.join(REPO, 'docs', 'screenshots', 'switches');
const URL = 'http://127.0.0.1:5180/';
fs.mkdirSync(OUT, { recursive: true });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function gotoApp(page) {
  for (let i = 0; i < 8; i++) {
    try { await page.goto(URL, { waitUntil: 'domcontentloaded', timeout: 15000 }); } catch {}
    try { await page.getByRole('button', { name: '登录' }).waitFor({ timeout: 5000 }); return; } catch {}
    await sleep(900);
  }
  throw new Error('login page never rendered');
}

async function login(context) {
  const page = await context.newPage();
  await gotoApp(page);
  await page.getByLabel('用户 ID').fill('uia');
  await page.getByRole('button', { name: '登录' }).click();
  await page.getByRole('heading', { name: 'Chirp' }).waitFor({ timeout: 20000 });
  await sleep(1500);
  return page;
}

async function captureToggle(page, testid, name) {
  const btn = page.getByTestId(testid);
  await btn.waitFor({ timeout: 8000 });
  await sleep(300);
  const offClass = await btn.getAttribute('class');
  await btn.screenshot({ path: path.join(OUT, `${name}-off.png`) });
  await btn.click();
  await sleep(1300);
  const onClass = await btn.getAttribute('class');
  await btn.screenshot({ path: path.join(OUT, `${name}-on.png`) });
  const stateOf = (c) => (c.includes('MuiButton-contained') ? 'contained' : 'outlined');
  if (stateOf(offClass) === stateOf(onClass)) {
    throw new Error(`${name}: variant did not flip (off=${stateOf(offClass)} on=${stateOf(onClass)})`);
  }
  console.log(`${name}: ${stateOf(offClass)} -> ${stateOf(onClass)}`);
  await btn.click();
  await sleep(900);
}

function gif(name) {
  const seq = path.join(OUT, `.seq-${name}`);
  fs.rmSync(seq, { recursive: true, force: true });
  fs.mkdirSync(seq);
  fs.copyFileSync(path.join(OUT, `${name}-off.png`), path.join(seq, '01.png'));
  fs.copyFileSync(path.join(OUT, `${name}-on.png`), path.join(seq, '02.png'));
  execFileSync('ffmpeg', [
    '-y', '-loglevel', 'error', '-framerate', '0.6', '-i', path.join(seq, '%02d.png'),
    '-filter_complex', 'split[a][b];[a]palettegen[p];[b][p]paletteuse',
    '-loop', '0', path.join(OUT, `${name}.gif`),
  ]);
  fs.rmSync(seq, { recursive: true, force: true });
  console.log(`gif ${name}.gif`);
}

const browser = await chromium.launch();
const ctx = await browser.newContext({ viewport: { width: 1280, height: 800 } });
// 无头 Chromium 把 Notification 权限硬判 denied，开关点不开；这里与单测同款
// 打桩授一次权限——GIF 取证的是按钮 UI 状态翻转，不是系统通知投递（桌面壳
// 里走 Tauri 插件真权限）。
await ctx.addInitScript(() => {
  class FakeNotification {
    constructor(title, opts) { this.title = title; Object.assign(this, opts || {}); }
    close() {}
  }
  FakeNotification.permission = 'default';
  FakeNotification.requestPermission = async () => {
    FakeNotification.permission = 'granted';
    return 'granted';
  };
  window.Notification = FakeNotification;
});
try {
  const page = await login(ctx);
  await captureToggle(page, 'friend-online-notify', 'friend-online-notify');
  await captureToggle(page, 'dm-restriction', 'dm-restriction');
  await captureToggle(page, 'desktop-notify', 'desktop-notify');
  gif('friend-online-notify');
  gif('dm-restriction');
  gif('desktop-notify');
} finally {
  await browser.close();
}
console.log('DONE');
