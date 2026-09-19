import type { ChannelRef } from './chat_api';

/**
 * Decision for firing a desktop notification for a live message: notify when
 * the tab is hidden, or the message landed in a channel other than the one
 * on screen. Pure so it is testable without a Notification global.
 */
export function shouldNotifyFor(
  tabHidden: boolean,
  openChannelKey: string | undefined,
  message: { channel: Pick<ChannelRef, 'key'> },
): boolean {
  return tabHidden || message.channel.key !== openChannelKey;
}
