import { ErrorCode } from '@chirp/proto/common';
import { describe, expect, it } from 'vitest';
import { errorText, RequestError } from './errors';

describe('error copy', () => {
  // TARGET_OFFLINE deliberately reads as a queued delivery, not a failure:
  // the server has already put the message into the recipient's offline
  // queue and will push it when they reconnect.
  const EXPECTED: Array<[ErrorCode, string]> = [
    [ErrorCode.OK, '成功'],
    [ErrorCode.INTERNAL_ERROR, '服务器内部错误'],
    [ErrorCode.INVALID_PARAM, '参数错误'],
    [ErrorCode.AUTH_FAILED, '认证失败'],
    [ErrorCode.SESSION_EXPIRED, '登录已过期,请重新登录'],
    [ErrorCode.USER_NOT_FOUND, '用户不存在'],
    [ErrorCode.TARGET_OFFLINE, '对方离线,消息将在其上线后送达'],
    [ErrorCode.SERVER_UNAVAILABLE, '服务暂不可用,请稍后再试'],
    [ErrorCode.RATE_LIMITED, '操作过于频繁,请稍后再试'],
  ];

  it.each(EXPECTED)('maps ErrorCode %s to copy', (code, text) => {
    expect(errorText(code)).toBe(text);
  });

  it('falls back for unknown codes', () => {
    expect(errorText(99 as ErrorCode)).toMatch(/未知错误\(99\)/);
  });
});

describe('RequestError', () => {
  it('defaults its message from the kind', () => {
    expect(new RequestError('timeout').message).toBe('请求超时');
    expect(new RequestError('closed').message).toBe('连接已断开');
    expect(new RequestError('kicked').message).toContain('其他设备');
  });

  it('uses error copy for server errors and carries the code', () => {
    const err = new RequestError('server', ErrorCode.RATE_LIMITED);
    expect(err.kind).toBe('server');
    expect(err.code).toBe(ErrorCode.RATE_LIMITED);
    expect(err.message).toBe(errorText(ErrorCode.RATE_LIMITED));
  });

  it('falls back to the internal-error copy when a server error has no code', () => {
    expect(new RequestError('server').message).toBe(errorText(ErrorCode.INTERNAL_ERROR));
  });

  it('keeps an explicit message when given one', () => {
    expect(new RequestError('closed', undefined, 'socket gone').message).toBe('socket gone');
  });
});
