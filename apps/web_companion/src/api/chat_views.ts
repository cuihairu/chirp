/**
 * Chat 视图映射层：把 wire 形态（proto 消息 / metadata / 反应）翻成 UI
 * store 的视图对象。纯函数、无框架、无副作用；连接与 store 编排留在
 * chat_api.ts，这里只做解析。
 */
import {
  AchievementMetadata,
  ChatMessage,
  CoordMetadata,
  GuildRecruitMetadata,
  ItemMetadata,
  MessageReaction,
  MsgType,
  PartyRecruitMetadata,
  Priority,
  SkillMetadata,
  TradeMetadata,
} from '@chirp/proto/chat';
import {
  conversationOf,
  type AchievementView,
  type ChatMessageView,
  type CoordView,
  type GuildRecruitView,
  type ItemView,
  type MessageReactionView,
  type PartyRecruitView,
  type SkillView,
  type TradeView,
} from '../state/models';

export interface ChannelRef {
  key: string;
  kind: 'private' | 'group';
  channelId: string;
  peerId: string;
}

export const decodeText = (bytes: Uint8Array): string => new TextDecoder().decode(bytes);

/**
 * 系统级消息面（P0 消息展示）：走马灯 = PRIORITY_URGENT 且 ttl_seconds > 0
 * （屏幕顶部滚动，新的替换旧的）；系统弹窗 = PRIORITY_HIGH（维护通知、全服
 * 奖励）。其余优先级按普通气泡渲染，不产生系统面事件。
 */
export interface SystemSurface {
  type: 'marquee' | 'announce';
  content: string;
  /** 走马灯展示时长（秒）；announce 无时长语义，恒 0。 */
  ttlSeconds: number;
  messageId: string;
}

export const systemSurfaceOf = (msg: ChatMessage): SystemSurface | null => {
  if (msg.priority === Priority.PRIORITY_HIGH) {
    return { type: 'announce', content: decodeText(msg.content), ttlSeconds: 0, messageId: msg.messageId };
  }
  if (msg.priority === Priority.PRIORITY_URGENT && msg.ttlSeconds > 0) {
    return {
      type: 'marquee',
      content: decodeText(msg.content),
      ttlSeconds: msg.ttlSeconds,
      messageId: msg.messageId,
    };
  }
  return null;
};

// 坐标卡片解析：非 COORD_LINK 或 metadata 解析不出 / 缺 map_id 一律 undefined
// （气泡当普通文本渲染），与服务端 ValidateCoordMetadata 的拒绝口径一致。
const coordViewOf = (msg: ChatMessage): CoordView | undefined => {
  if (msg.msgType !== MsgType.COORD_LINK || msg.metadata.length === 0) return undefined;
  let coord: CoordMetadata;
  try {
    coord = CoordMetadata.decode(msg.metadata);
  } catch {
    return undefined;
  }
  if (!coord.mapId) return undefined;
  return { mapId: coord.mapId, x: coord.x, y: coord.y, z: coord.z, label: coord.label };
};

// 物品卡片解析：非 ITEM_LINK 或 metadata 解析不出 / 缺 item_id 一律 undefined
// （气泡当普通文本渲染），与服务端 ValidateItemMetadata 的拒绝口径一致。
const itemViewOf = (msg: ChatMessage): ItemView | undefined => {
  if (msg.msgType !== MsgType.ITEM_LINK || msg.metadata.length === 0) return undefined;
  let item: ItemMetadata;
  try {
    item = ItemMetadata.decode(msg.metadata);
  } catch {
    return undefined;
  }
  if (!item.itemId) return undefined;
  return {
    itemId: item.itemId,
    itemName: item.itemName,
    quality: item.quality,
    iconUrl: item.iconUrl,
    count: item.count,
    attrs: item.attrs,
  };
};

// 成就卡片解析：非 ACHIEVEMENT 或 metadata 解析不出 / 缺 achievement_id 一律
// undefined（气泡当普通文本渲染），与服务端 ValidateAchievementMetadata 的拒绝口径一致。
const achievementViewOf = (msg: ChatMessage): AchievementView | undefined => {
  if (msg.msgType !== MsgType.ACHIEVEMENT || msg.metadata.length === 0) return undefined;
  let achievement: AchievementMetadata;
  try {
    achievement = AchievementMetadata.decode(msg.metadata);
  } catch {
    return undefined;
  }
  if (!achievement.achievementId) return undefined;
  return {
    achievementId: achievement.achievementId,
    achievementName: achievement.achievementName,
    description: achievement.description,
    iconUrl: achievement.iconUrl,
    rarity: achievement.rarity,
  };
};

// 交易状态卡片（P1 TRADE_STATUS）：解析口径与服务端校验门一致——非
// TRADE_STATUS 或 metadata 空/解析不出/缺 trade_id 的不当卡片。
const tradeViewOf = (msg: ChatMessage): TradeView | undefined => {
  if (msg.msgType !== MsgType.TRADE_STATUS || msg.metadata.length === 0) return undefined;
  let trade: TradeMetadata;
  try {
    trade = TradeMetadata.decode(msg.metadata);
  } catch {
    return undefined;
  }
  if (!trade.tradeId) return undefined;
  return {
    tradeId: trade.tradeId,
    status: trade.status,
    amount: trade.amount,
    itemName: trade.itemName,
    itemCount: trade.itemCount,
  };
};

// 招募卡片解析（P1 GUILD_RECRUIT/PARTY_RECRUIT）：解析口径与服务端校验门
// 一致——非对应类型或 metadata 空/解析不出/缺 id 的不当卡片。
const guildRecruitViewOf = (msg: ChatMessage): GuildRecruitView | undefined => {
  if (msg.msgType !== MsgType.GUILD_RECRUIT || msg.metadata.length === 0) return undefined;
  let guild: GuildRecruitMetadata;
  try {
    guild = GuildRecruitMetadata.decode(msg.metadata);
  } catch {
    return undefined;
  }
  if (!guild.guildId) return undefined;
  return {
    guildId: guild.guildId,
    guildName: guild.guildName,
    level: guild.level,
    memberCount: guild.memberCount,
    maxMembers: guild.maxMembers,
    description: guild.description,
  };
};

const partyRecruitViewOf = (msg: ChatMessage): PartyRecruitView | undefined => {
  if (msg.msgType !== MsgType.PARTY_RECRUIT || msg.metadata.length === 0) return undefined;
  let party: PartyRecruitMetadata;
  try {
    party = PartyRecruitMetadata.decode(msg.metadata);
  } catch {
    return undefined;
  }
  if (!party.partyId) return undefined;
  return {
    partyId: party.partyId,
    dungeonName: party.dungeonName,
    needed: party.needed,
    current: party.current,
    leaderName: party.leaderName,
  };
};

// 技能卡片解析（P1 SKILL_LINK）：解析口径与服务端校验门一致——非
// SKILL_LINK 或 metadata 空/解析不出/缺 skill_id 的不当卡片。
const skillViewOf = (msg: ChatMessage): SkillView | undefined => {
  if (msg.msgType !== MsgType.SKILL_LINK || msg.metadata.length === 0) return undefined;
  let skill: SkillMetadata;
  try {
    skill = SkillMetadata.decode(msg.metadata);
  } catch {
    return undefined;
  }
  if (!skill.skillId) return undefined;
  return {
    skillId: skill.skillId,
    skillName: skill.skillName,
    level: skill.level,
    iconUrl: skill.iconUrl,
    description: skill.description,
  };
};

export const toView = (msg: ChatMessage, channel: ChannelRef): ChatMessageView => ({
  messageId: msg.messageId,
  senderId: msg.senderId,
  channelKey: channel.key,
  channelType: msg.channelType,
  channelId: msg.channelId,
  content: decodeText(msg.content),
  timestamp: msg.timestamp,
  pending: false,
  replyToMessageId: msg.replyToMessageId || undefined,
  coord: coordViewOf(msg),
  item: itemViewOf(msg),
  achievement: achievementViewOf(msg),
  skill: skillViewOf(msg),
  trade: tradeViewOf(msg),
  guildRecruit: guildRecruitViewOf(msg),
  partyRecruit: partyRecruitViewOf(msg),
  priority: msg.priority,
  ttlSeconds: msg.ttlSeconds,
  senderKind: msg.senderKind,
});

export const toReactionView = (reaction: MessageReaction, selfId: string): MessageReactionView => ({
  emoji: reaction.emoji,
  count: reaction.count,
  mine: reaction.reactedByMe || reaction.userIds.includes(selfId),
});

/** ChannelRef for a stored conversation key; private peers resolve via privatePeerId. */
export const channelRefOf = (key: string, selfId: string): ChannelRef => {
  const { kind, channelId } = conversationOf(key);
  return {
    key,
    kind,
    channelId,
    peerId: kind === 'group' ? channelId : privatePeerId(channelId, selfId),
  };
};

/** Private peer id from a sorted-pair channel id ("a|b" → the other side). */
export const privatePeerId = (channelId: string, selfId: string): string =>
  channelId.split('|').find((id) => id !== selfId) ?? channelId;
