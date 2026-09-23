// mermaid 11.x 的 dist 按 stylis 4.4 的导出面打包(import SCOPE/LAYER),
// 而 stylis 的这两个常量在 4.2 起的 index 导出面上已不可用;钉 4.1.4 时
// 两者皆缺。这里 re-export stylis 并补齐缺失常量(值取自 stylis 4.4.0
// 的 src/Enum.js:SCOPE = '@scope',LAYER = '@layer')。
export * from 'stylis'
export const SCOPE = '@scope'
export const LAYER = '@layer'
