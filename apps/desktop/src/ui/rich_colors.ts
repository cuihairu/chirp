// P0 物品链接 / P1 成就分享共用的品质·稀有度色阶：白(普通)/绿(优秀)/蓝(精良)/
// 紫(史诗)/橙(传说)。白品质在浅色底上渲染为灰白（可读性适配）；枚举外未知值
// 回落普通（向前兼容，与 sender_kind 回落同口径）。与 web_companion 的
// components/rich_colors.ts 刻意保持同文（两 app 不共享 UI 代码，改一处须
// 同步另一处）。
export const QUALITY_COLORS: Record<number, string> = {
  0: '#9e9e9e',
  1: '#4caf50',
  2: '#2196f3',
  3: '#9c27b0',
  4: '#ff9800',
};

export const qualityColorOf = (quality: number): string =>
  QUALITY_COLORS[quality] ?? QUALITY_COLORS[0];

export const RARITY_COLORS: Record<number, string> = {
  0: '#9e9e9e',
  1: '#4caf50',
  2: '#2196f3',
  3: '#9c27b0',
  4: '#ff9800',
};

export const rarityColorOf = (rarity: number): string => RARITY_COLORS[rarity] ?? RARITY_COLORS[0];
