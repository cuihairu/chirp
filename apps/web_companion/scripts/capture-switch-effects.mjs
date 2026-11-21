// 配置开关效果证据采集（web 侧）：把本应用的开关逐个截 off/on 图，并用
// ffmpeg 合成交替播放的 GIF。前置：desktop 的 stack-up.sh 全栈在线（redis
// 6390 / auth 5300 / chat 5310-5311 / gateway 5201 / party 7501 / voice 9001 /
// social 8001）。本脚本自己起一个 vite dev server（3001），把 /ws/* 代理重定向
// 到该栈的端口——vite.config.ts 里写死的 7001 是产品默认端口，与 dev 栈不同。
// 用法：node apps/web_companion/scripts/capture-switch-effects.mjs
// 产物落 docs/screenshots/switches/（与桌面端同一目录，`web-` 前缀区分）。
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createServer } from 'vite';
import { chromium } from 'playwright';

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const APP_DIR = path.resolve(SCRIPT_DIR, '..');
const REPO_ROOT = path.resolve(APP_DIR, '..', '..');
const OUT = path.join(REPO_ROOT, 'docs', 'screenshots', 'switches');
const PORT = 3001;
const BASE = `http://127.0.0.1:${PORT}/`;
fs.mkdirSync(OUT, { recursive: true });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// stack-up.sh 的端口表：/ws/chat 走 chat 平面 WS 边，其余各平面各自端口，
// /ws/device 与 gateway 同边（游戏在线状态走这条）。
const PROXY = {
  '/ws/chat': { target: 'ws://127.0.0.1:5311', ws: true },
  '/ws/social': { target: 'ws://127.0.0.1:8001', ws: true },
  '/ws/party': { target: 'ws://127.0.0.1:7501', ws: true },
  '/ws/voice': { target: 'ws://127.0.0.1:9001', ws: true },
  '/ws/device': { target: 'ws://127.0.0.1:5201', ws: true },
};

async function openApp(page) {
  for (let i = 0; i < 8; i++) {
    try { await page.goto(BASE, { waitUntil: 'domcontentloaded', timeout: 15000 }); } catch {}
    try { await page.getByRole('button', { name: '登录' }).waitFor({ timeout: 5000 }); return; } catch {}
    await sleep(900);
  }
  throw new Error('login page never rendered');
}

async function login(context) {
  const page = await context.newPage();
  await openApp(page);
  await page.getByLabel('用户 ID').fill('uia');
  await page.getByRole('button', { name: '登录' }).click();
  await page.getByRole('button', { name: '在线设备' }).waitFor({ timeout: 20000 });
  await sleep(1200);
  return page;
}

/**
 * MUI9 的 role="switch" 在内层 input 上，而 data-testid 落在 Switch 的根元素
 * （span）；input 用裸 `checked` 属性（不是 aria-checked）——断言取它，截图
 * 仍用根元素（整颗开关的轨道+滑块）。偏好持久化在服务端/本地：采集前先归零
 * （已是 on 就先点回 off），保证 off.png 真的是 off，on.png 真的是 on。
 */
async function captureSwitch(page, testid, name) {
  const box = page.getByTestId(testid);
  const input = box.locator('input[role="switch"]').first();
  await box.scrollIntoViewIfNeeded();
  await box.waitFor({ timeout: 8000 });
  if (await input.evaluate((el) => el.checked)) {
    await input.click();
    await sleep(1100);
  }
  // React 只同步 checked *property*（attribute 停在初次渲染），断言一律取 property。
  let checked = await input.evaluate((el) => el.checked);
  if (checked) throw new Error(`${name}: could not reset to off`);
  await box.screenshot({ path: path.join(OUT, `${name}-off.png`) });
  await input.click();
  await sleep(1300);
  checked = await input.evaluate((el) => el.checked);
  await box.screenshot({ path: path.join(OUT, `${name}-on.png`) });
  if (!checked) {
    throw new Error(`${name}: switch did not turn on`);
  }
  console.log(`${name}: off -> on`);
  await input.click();
  await sleep(900);
}

/** 桌面通知不是开关控件而是权限文案切换：off 显示「开启桌面通知」链接，
 * on 后该 testid 消失、换成「桌面通知已开启」——按文案变化断言。 */
async function captureNotify(page) {
  const link = page.getByTestId('notify-enable');
  await link.scrollIntoViewIfNeeded();
  await link.waitFor({ timeout: 8000 });
  const offText = (await link.textContent())?.trim() ?? '';
  await link.screenshot({ path: path.join(OUT, 'web-notify-off.png') });
  await link.click();
  await sleep(1300);
  const onText = await page.getByText('桌面通知已开启').first().textContent();
  if (offText === '' || onText !== '桌面通知已开启') {
    throw new Error(`web-notify: label did not flip (off="${offText}" on="${onText}")`);
  }
  await page.getByText('桌面通知已开启').first().screenshot({ path: path.join(OUT, 'web-notify-on.png') });
  console.log(`web-notify: "${offText}" -> "${onText}"`);
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

const vite = await createServer({
  root: APP_DIR,
  logLevel: 'error',
  resolve: {
    alias: {
      '@chirp/proto': path.resolve(APP_DIR, '../../proto/ts/proto'),
      '@chirp/app-protocol': path.resolve(APP_DIR, '../../sdks/ts/src'),
    },
  },
  server: { port: PORT, strictPort: true, proxy: PROXY },
});
await vite.listen();

const browser = await chromium.launch();
const ctx = await browser.newContext({ viewport: { width: 1280, height: 800 } });
// 无头 Chromium 把 Notification 权限硬判 denied，「开启桌面通知」点了没反应；
// 这里与单测同款打桩授一次权限——取证的是控件/文案的状态翻转，不是系统通知投递。
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
  await captureNotify(page);

  await page.getByRole('button', { name: '好友' }).click();
  await page.getByTestId('friend-online-notify-switch').waitFor({ timeout: 8000 });
  await sleep(500);
  await captureSwitch(page, 'friend-online-notify-switch', 'web-friend-online-notify');
  await captureSwitch(page, 'dm-restriction-switch', 'web-dm-restriction');
  await page.keyboard.press('Escape');
  await sleep(700);

  await page.getByRole('button', { name: '在线设备' }).click();
  await page.getByTestId('game-presence-toggle').waitFor({ timeout: 8000 });
  await sleep(500);
  const presence = page.getByTestId('game-presence-toggle').locator('input[role="switch"]').first();
  if (!(await presence.isDisabled())) {
    await captureSwitch(page, 'game-presence-toggle', 'web-game-presence');
  } else {
    console.log('web-game-presence: toggle disabled (presence plane unavailable) — skipped');
  }

  gif('web-notify');
  gif('web-friend-online-notify');
  gif('web-dm-restriction');
  const presenceGif = path.join(OUT, 'web-game-presence-off.png');
  if (fs.existsSync(presenceGif)) gif('web-game-presence');
} finally {
  await browser.close();
  await vite.close();
}
console.log('DONE');
