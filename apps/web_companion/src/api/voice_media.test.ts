import { describe, expect, it } from 'vitest';
import { MsgID } from '@chirp/proto/gateway';
import type {
  AudioContextLike,
  LevelPump,
  MediaPlatform,
  Playback,
} from './voice_media';
import { MediaPermissionError, VoiceMediaApi } from './voice_media';
import { createVoiceStore, type VoiceState } from '../state/voice_store';
import { FakeChatConnection } from '../state/test_helpers';

/**
 * The whole WebRTC mesh driven by fakes: two VoiceMediaApi instances over an
 * in-memory relay (a send on one side arrives as a notify on the other, same
 * body — exactly what the voice service's 4007-4009 relay does), a fake
 * RTCPeerConnection good enough for perfect negotiation, and canned analyser
 * levels for the speaking flips.
 */

class FakeTrack {
  kind = 'audio' as const;
  enabled = true;
  stopped = false;
  stop(): void {
    this.stopped = true;
  }
}

class FakeStream {
  readonly tracks: FakeTrack[];
  constructor(readonly label: string) {
    this.tracks = [new FakeTrack()];
  }
  getTracks(): FakeTrack[] {
    return this.tracks;
  }
  getAudioTracks(): FakeTrack[] {
    return this.tracks;
  }
}

/** Deterministic pc: offers/answers numbered, ICE fired on the microtask. */
class FakeRTCPeerConnection {
  signalingState: RTCSignalingState = 'stable';
  connectionState: RTCPeerConnectionState = 'new';
  localDescription: RTCSessionDescription | null = null;
  remoteDescription: RTCSessionDescription | null = null;
  onicecandidate: ((e: RTCPeerConnectionIceEvent) => void) | null = null;
  ontrack: ((e: RTCTrackEvent) => void) | null = null;
  onconnectionstatechange: (() => void) | null = null;
  readonly config: RTCConfiguration;
  private offerCount = 0;
  private trackDelivered = false;

  constructor(config: RTCConfiguration) {
    this.config = config;
  }

  addTrack(): void {}

  async setLocalDescription(desc?: RTCSessionDescriptionInit): Promise<void> {
    if (desc?.type === 'rollback') {
      this.signalingState = 'stable';
      this.localDescription = null;
      return;
    }
    const type = this.signalingState === 'stable' ? 'offer' : 'answer';
    if (type === 'offer') this.offerCount += 1;
    const sdp = `${type}-sdp-${this.offerCount}`;
    this.localDescription = { type, sdp } as RTCSessionDescription;
    this.signalingState = type === 'offer' ? 'have-local-offer' : 'stable';
    // Trickle ICE fires for offers and answers alike.
    queueMicrotask(() => {
      this.onicecandidate?.({
        candidate: {
          candidate: `host-candidate-${type}-${this.offerCount}`,
          sdpMid: '0',
          sdpMLineIndex: 0,
        },
      } as RTCPeerConnectionIceEvent);
    });
    if (type === 'answer' && this.remoteDescription) {
      this.fireConnected();
    }
  }

  async setRemoteDescription(desc: RTCSessionDescriptionInit): Promise<void> {
    if (this.remoteDescription && this.connectionState === 'connected') {
      throw new Error('answer arrived twice');
    }
    this.remoteDescription = desc as RTCSessionDescription;
    this.signalingState = desc.type === 'offer' ? 'have-remote-offer' : 'stable';
    if (desc.type === 'answer') {
      this.fireConnected();
    }
    // The remote side's mic arrives with their description (offer or answer
    // — real browsers fire ontrack once media is negotiated either way).
    if (!this.trackDelivered) {
      this.trackDelivered = true;
      const remote = new FakeStream('remote');
      this.ontrack?.({
        streams: [remote as unknown as MediaStream],
        track: remote.tracks[0],
      } as unknown as RTCTrackEvent);
    }
  }

  async addIceCandidate(): Promise<void> {}

  close(): void {
    this.connectionState = 'closed';
    this.onconnectionstatechange?.();
  }

  private fireConnected(): void {
    this.connectionState = 'connected';
    this.onconnectionstatechange?.();
  }
}

/** Records mic streams, meters, gains and pcs for the assertions. */
class FakeMediaPlatform implements MediaPlatform {
  readonly meters: Array<LevelPump & { emit(level: number): void; running: boolean }> = [];
  readonly playbacks: Playback[] = [];
  readonly micStreams: FakeStream[] = [];
  readonly pcs: FakeRTCPeerConnection[] = [];
  getUserMediaError: unknown = null;

  getUserMedia(): Promise<MediaStream> {
    if (this.getUserMediaError) return Promise.reject(this.getUserMediaError);
    const stream = new FakeStream('mic');
    this.micStreams.push(stream);
    return Promise.resolve(stream as unknown as MediaStream);
  }

  createPeerConnection(config: RTCConfiguration): RTCPeerConnection {
    const pc = new FakeRTCPeerConnection(config);
    this.pcs.push(pc);
    return pc as unknown as RTCPeerConnection;
  }

  createAudioContext(): AudioContextLike {
    const node = (): { connect(): void; disconnect(): void } => ({
      connect(): void {},
      disconnect(): void {},
    });
    return {
      createMediaStreamSource: node,
      createAnalyser: () => ({ ...node(), fftSize: 128, getFloatTimeDomainData(): void {} }),
      createGain: () => ({ ...node(), gain: { value: 1 } }),
      destination: node(),
      close(): void {},
    };
  }

  attachMeter(
    _ctx: AudioContextLike,
    _stream: MediaStream,
    onLevel: (level: number) => void,
  ): LevelPump & { emit(level: number): void; running: boolean } {
    const pump = {
      running: false,
      start(): void {
        this.running = true;
      },
      stop(): void {
        this.running = false;
      },
      emit(level: number): void {
        onLevel(level);
      },
    };
    this.meters.push(pump);
    return pump;
  }

  playStream(_ctx: AudioContextLike, _stream: MediaStream): Playback {
    void _ctx;
    void _stream;
    const playback: Playback = {
      gain: { gain: { value: 1 }, connect(): void {}, disconnect(): void {} },
      stop(): void {},
    };
    this.playbacks.push(playback);
    return playback;
  }
}

type SentFrame = { from: string; msgId: MsgID; body: Uint8Array };

const harness = () => {
  const platform = new FakeMediaPlatform();
  const mk = () => {
    const conn = new FakeChatConnection();
    const sent: SentFrame[] = [];
    const orig = conn.send.bind(conn);
    conn.send = (msgId, body): void => {
      orig(msgId, body);
      sent.push({ from: '', msgId, body });
    };
    return { conn, sent };
  };
  const { conn: connA, sent: sentRawA } = mk();
  const { conn: connB, sent: sentRawB } = mk();
  // In-memory relay: targeted delivery, same body — the service contract.
  connA.send = (msgId, body): void => {
    sentRawA.push({ from: 'alice', msgId, body });
    connB.emit(msgId, body);
  };
  connB.send = (msgId, body): void => {
    sentRawB.push({ from: 'bob', msgId, body });
    connA.emit(msgId, body);
  };
  const voiceA = createVoiceStore();
  const voiceB = createVoiceStore();
  const mediaA = new VoiceMediaApi({ conn: connA, voice: voiceA, platform });
  const mediaB = new VoiceMediaApi({ conn: connB, voice: voiceB, platform });
  return {
    platform,
    connA,
    connB,
    voiceA,
    voiceB,
    mediaA,
    mediaB,
    sentByA: sentRawA,
    sentByB: sentRawB,
  };
};

const applyRoom = (
  voice: { set(fn: (prev: VoiceState) => VoiceState): void },
  roomId: string,
  ids: string[],
): void => {
  voice.set(() => ({
    room: {
      roomId,
      roomName: 'r',
      maxParticipants: 0,
      participants: ids.map((userId) => ({
        userId,
        state: 1 as const,
        muted: false,
        deafened: false,
      })),
    },
    mediaError: null,
  }));
};

describe('VoiceMediaApi mesh', () => {
  it('the joiner dials the roster; both peers connect and exchange tracks', async () => {
    const h = harness();
    const iceServers: RTCIceServer[] = [
      { urls: 'turn:turn.example:3478', username: 'u', credential: 'c' },
    ];
    // Bob is in the room first, alone: no peers to dial.
    applyRoom(h.voiceB, 'r1', ['bob']);
    await h.mediaB.start('bob', 'r1', ['bob'], iceServers);
    expect(h.sentByB).toHaveLength(0);

    // Alice joins with the full roster: she dials bob.
    applyRoom(h.voiceA, 'r1', ['alice', 'bob']);
    await h.mediaA.start('alice', 'r1', ['alice', 'bob'], iceServers);

    const offer = h.sentByA.find((f) => f.msgId === MsgID.SDP_OFFER_MSG);
    const answer = h.sentByB.find((f) => f.msgId === MsgID.SDP_ANSWER_MSG);
    expect(offer).toBeDefined();
    expect(answer).toBeDefined();

    // Both sides of the session reached connected.
    expect(h.platform.pcs.filter((pc) => pc.connectionState === 'connected')).toHaveLength(2);
    // The TURN credentials reached both RTCPeerConnection configs.
    expect(h.platform.pcs.every((pc) => pc.config.iceServers?.length === 1)).toBe(true);
  });

  it('routes ICE candidates both ways through the relay', async () => {
    const h = harness();
    applyRoom(h.voiceB, 'r1', ['bob']);
    await h.mediaB.start('bob', 'r1', ['bob']);
    applyRoom(h.voiceA, 'r1', ['alice', 'bob']);
    await h.mediaA.start('alice', 'r1', ['alice', 'bob']);

    expect(
      h.sentByA.filter((f) => f.msgId === MsgID.ICE_CANDIDATE_MSG).length,
    ).toBeGreaterThan(0);
    // Bob's answerer pc emitted its candidate too (after the answer).
    expect(
      h.sentByB.filter((f) => f.msgId === MsgID.ICE_CANDIDATE_MSG).length,
    ).toBeGreaterThan(0);
  });

  it('flips the speaking flag with hysteresis and stops the meter on teardown', async () => {
    const h = harness();
    applyRoom(h.voiceB, 'r1', ['bob']);
    await h.mediaB.start('bob', 'r1', ['bob']);
    applyRoom(h.voiceA, 'r1', ['alice', 'bob']);
    await h.mediaA.start('alice', 'r1', ['alice', 'bob']);

    // meters order: [bobLocal, aliceLocal, bobRemote(alice), aliceRemote(bob)]
    // — index 2 is attached to mediaB (it reports into voiceB), so the meter
    // that flips bob's row in voiceA is index 3.
    const [, aliceLocal, , aliceRemote] = h.platform.meters;

    // Alice's own mic flips her row.
    aliceLocal.emit(0.05);
    expect(h.voiceA.get().room?.participants.find((p) => p.userId === 'alice')?.speaking).toBe(true);
    aliceLocal.emit(0.01); // between OFF and ON: hysteresis holds
    expect(h.voiceA.get().room?.participants.find((p) => p.userId === 'alice')?.speaking).toBe(true);
    aliceLocal.emit(0.001);
    expect(h.voiceA.get().room?.participants.find((p) => p.userId === 'alice')?.speaking).toBe(false);

    // Bob's remote stream flips his row on alice's side.
    aliceRemote.emit(0.05);
    expect(h.voiceA.get().room?.participants.find((p) => p.userId === 'bob')?.speaking).toBe(true);

    h.mediaA.stop();
    // Only alice's meters stop (her local pump and her remote meter of bob);
    // bob's local meter and bob's remote meter of alice keep running.
    expect(h.platform.meters[1].running).toBe(false);
    expect(h.platform.meters[3].running).toBe(false);
    expect(h.platform.meters[0].running).toBe(true);
    expect(h.platform.meters[2].running).toBe(true);
    // Alice's mic track is released, bob's is not.
    expect(h.platform.micStreams[1].tracks[0].stopped).toBe(true);
    expect(h.platform.micStreams[0].tracks[0].stopped).toBe(false);
  });

  it('surfaces the browser error name on a permission failure', async () => {
    const h = harness();
    h.platform.getUserMediaError = new DOMException(
      'Permission denied',
      'NotAllowedError',
    );
    await expect(h.mediaA.start('alice', 'r1', ['alice'])).rejects.toBeInstanceOf(
      MediaPermissionError,
    );
    expect(h.mediaA.lastError).toBe('NotAllowedError');
  });

  it('mute silences the mic track, deafen gates the playback gain', async () => {
    const h = harness();
    applyRoom(h.voiceB, 'r1', ['bob']);
    await h.mediaB.start('bob', 'r1', ['bob']);
    applyRoom(h.voiceA, 'r1', ['alice', 'bob']);
    await h.mediaA.start('alice', 'r1', ['alice', 'bob']);
    expect(h.platform.playbacks.length).toBeGreaterThan(0);

    h.mediaA.setMuted(true);
    expect(h.platform.micStreams[1].tracks[0].enabled).toBe(false);
    h.mediaA.setMuted(false);
    expect(h.platform.micStreams[1].tracks[0].enabled).toBe(true);

    // playbacks order: [bob's player (mediaB), alice's player (mediaA)] —
    // deafening alice gates only her own playback gain.
    h.mediaA.setDeafened(true);
    expect(h.platform.playbacks[1].gain.gain.value).toBe(0);
    expect(h.platform.playbacks[0].gain.gain.value).toBe(1);
    h.mediaA.setDeafened(false);
    expect(h.platform.playbacks[1].gain.gain.value).toBe(1);
  });

  it('a simultaneous double-dial resolves through the polite/impolite tiebreak', async () => {
    const h = harness();
    applyRoom(h.voiceA, 'r1', ['alice', 'bob']);
    applyRoom(h.voiceB, 'r1', ['alice', 'bob']);
    await Promise.all([
      h.mediaA.start('alice', 'r1', ['alice', 'bob']),
      h.mediaB.start('bob', 'r1', ['alice', 'bob']),
    ]);
    // Drain the microtask queue holding the queued ICE callbacks.
    await new Promise((r) => setTimeout(r, 0));

    // The glare must not deadlock: exactly one session survives.
    expect(h.platform.pcs.filter((pc) => pc.connectionState === 'connected')).toHaveLength(2);
  });

  it('dropPeer closes the session pc and clears the remote speaking flag', async () => {
    const h = harness();
    applyRoom(h.voiceB, 'r1', ['bob']);
    await h.mediaB.start('bob', 'r1', ['bob']);
    applyRoom(h.voiceA, 'r1', ['alice', 'bob']);
    await h.mediaA.start('alice', 'r1', ['alice', 'bob']);

    // alice's remote meter of bob (index 3 — see the meters-order note above).
    const aliceRemote = h.platform.meters[3];
    aliceRemote.emit(0.05);
    expect(h.voiceA.get().room?.participants.find((p) => p.userId === 'bob')?.speaking).toBe(true);

    const bobSession = h.platform.pcs.find((pc) => pc.connectionState === 'connected');
    h.mediaA.dropPeer('bob');
    expect(bobSession?.connectionState === 'closed' || bobSession === undefined).toBe(true);
    expect(h.voiceA.get().room?.participants.find((p) => p.userId === 'bob')?.speaking).toBe(false);
  });
});
