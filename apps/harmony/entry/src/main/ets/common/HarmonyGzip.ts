import { zlib } from '@ohos.zlib';
import { setGzipDecompressor } from '@chirp/app-protocol/gzip';

/** 与共享协议核 apps/shared/protocol/src/gzip.ts 同一解压上限。 */
const MAX_DECOMPRESSED_BYTES = 16 * 1024 * 1024;

/** 与共享协议核 GzipError 同语义：空输入/容器损坏/超限统一报错。 */
class HarmonyGzipError extends Error {}

/**
 * 鸿蒙平台 gzip 解压器（@ohos.zlib，API 12 Zip 低层流式接口）。
 *
 * ArkTS 运行时没有 DecompressionStream（web Compression Streams 标准 API），
 * 共享协议核的默认解压器在此不可用；共享侧留了 setGzipDecompressor 注入缝，
 * 本模块在 AppServices 装配时把平台实现装进去，GET_HISTORY 拆封钩子照常走
 * 共享 msg_map，协议面零分叉。
 *
 * zlib 语义：inflateInit2 的 windowBits 在 15 以上加 16 表示只解 gzip 容器
 * （31 = 16 + 15）；解压输出尺寸未知，按 16 MiB 上限一次性分配目标缓冲，
 * inflate 一趟不回到 STREAM_END（缓冲不足 = 超上限，或容器损坏/截断）即报
 * 错——与服务端 16 MiB 压缩上限对称。历史清单远小于该上限，单缓冲一趟即
 * 完成，无需流式续读循环。
 *
 * 符号核对（2026-10-15）：本文件用到的 12 个符号已逐条对照 OpenHarmony
 * `interface_sdk-js/api/@ohos.zlib.d.ts`——`createZipSync(): Zip`、
 * `Zip.inflateInit2(strm: ZStream, windowBits: int)`、`Zip.inflate(strm, flush:
 * CompressFlushMode)`、`Zip.inflateEnd(strm: ZStream)`（四者均 @since 12
 * dynamic，与本工程 compatibleSdkVersion 5.0.0(12) 对齐）、`ReturnStatus.OK=0`
 * / `STREAM_END=1`、`CompressFlushMode.FINISH=4`、ZStream 的 nextIn/nextOut
 * (`ArrayBuffer`)、availableIn/availableOut(`int`)、totalOut(`long`) 全部存在。
 * 其中 `totalOut` 在 d.ts 里是 `long`（官方 API 文档表格作 `number`），直接喂给
 * `ArrayBuffer.slice(begin: number)` 有类型不匹配风险，故显式 `Number()` 归一
 * ——`long` 解析为 number 或 bigint 两种口径下都成立。
 *
 * 边界：上述是**符号级静态核对**，仍非编译验证——环境内无 OHOS 工具链，
 * 工具链到位后的第一件事仍是编译本文件。
 */
export async function harmonyGzipDecompress(bytes: Uint8Array): Promise<Uint8Array> {
  if (bytes.length === 0) {
    throw new HarmonyGzipError('gzip: empty input');
  }
  const zip = zlib.createZipSync();
  const dest = new ArrayBuffer(MAX_DECOMPRESSED_BYTES);
  const strm: zlib.ZStream = {
    nextIn: bytes.buffer as ArrayBuffer,
    availableIn: bytes.byteLength,
    nextOut: dest,
    availableOut: MAX_DECOMPRESSED_BYTES,
  };
  const init = await zip.inflateInit2(strm, 31);
  if (init !== zlib.ReturnStatus.OK) {
    throw new HarmonyGzipError('gzip: init failed');
  }
  try {
    const status = await zip.inflate(strm, zlib.CompressFlushMode.FINISH);
    if (status !== zlib.ReturnStatus.STREAM_END) {
      throw new HarmonyGzipError('gzip: corrupt, truncated, or over the cap');
    }
    // totalOut = 实际解压字节数（d.ts 声明为 long）；Number() 归一到 slice 要
    // 求的 number 后拷出精确区间，及时释放 16 MiB 大缓冲。
    return new Uint8Array(dest.slice(0, Number(strm.totalOut ?? 0)));
  } finally {
    await zip.inflateEnd(strm);
  }
}

/** AppServices 装配时调用一次：把平台解压器装进共享协议核的注入缝。 */
export function installHarmonyGzipDecompressor(): void {
  setGzipDecompressor(harmonyGzipDecompress);
}
