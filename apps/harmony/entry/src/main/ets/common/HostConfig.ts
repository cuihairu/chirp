import preferences from '@ohos.data.preferences';
import util from '@ohos.util';
import deviceInfo from '@ohos.deviceInfo';
import type common from '@ohos.app.ability.common';

/**
 * 平台持久化（@ohos.data.preferences）：服务器地址 + 每安装 device id。
 * 对齐 web 端 localStorage 的两个键——baseUrl 对应 HostPage 的服务器地址，
 * device_id 对应 createAuthStore 的 localStorage device id（语义不变：每
 * 安装稳定 id，避免重连互踢）。登录态不持久化（stores 为内存镜像，与
 * web 端刷新语义一致）。
 */
const PREFERENCES_NAME = 'chirp_host';
const KEY_BASE_URL = 'base_url';
const KEY_DEVICE_ID = 'device_id';

export interface HostConfigData {
  baseUrl: string;
  deviceId: string;
  deviceName: string;
}

let cached: HostConfigData | null = null;

/** 预载（EntryAbility onCreate 后的 prewarm 调用）；幂等。 */
export async function loadHostConfig(context: common.UIAbilityContext): Promise<HostConfigData> {
  if (cached) return cached;
  const store = await preferences.getPreferences(context, PREFERENCES_NAME);
  const baseUrl = (await store.get(KEY_BASE_URL, '')) as string;
  let deviceId = (await store.get(KEY_DEVICE_ID, '')) as string;
  if (!deviceId) {
    // 每安装生成一次并立即落盘（重连/顶号判定的稳定身份）。
    deviceId = util.generateRandomUUID(true);
    await store.put(KEY_DEVICE_ID, deviceId);
    await store.flush();
  }
  cached = { baseUrl, deviceId, deviceName: resolveDeviceName() };
  return cached;
}

/** HostPage 保存服务器地址；空串 = 未配置。 */
export async function saveBaseUrl(context: common.UIAbilityContext, baseUrl: string): Promise<void> {
  const store = await preferences.getPreferences(context, PREFERENCES_NAME);
  await store.put(KEY_BASE_URL, baseUrl.trim());
  await store.flush();
  if (cached) cached.baseUrl = baseUrl.trim();
}

export function cachedHostConfig(): HostConfigData | null {
  return cached;
}

/**
 * 设备展示名：web 端从 navigator.userAgent 归纳，鸿蒙用 productModel。
 * 注入 DeviceApi.registerSelf 的 device_name 展示面（非身份面——身份是
 * 落盘的 device_id）。
 */
function resolveDeviceName(): string {
  return `HarmonyOS ${deviceInfo.productModel}`;
}

/** 服务器地址合法性：必须是 ws:// 或 wss:// 基址（五平面在其上拼 /ws/*）。 */
export function isValidBaseUrl(baseUrl: string): boolean {
  return /^wss?:\/\/.+$/.test(baseUrl.trim());
}
