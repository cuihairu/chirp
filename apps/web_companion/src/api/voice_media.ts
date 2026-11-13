import { MsgID } from '@chirp/proto/gateway';
import {
  IceCandidateMessage,
  SdpAnswerMessage,
  SdpOfferMessage,
} from '@chirp/proto/voice';
import type { Store } from '../state/store';
import type { VoiceState } from '../state/voice_store';
import { setSpeaking } from '../state/voice_store';
import type { ChatConnection } from './chat_api';

/**
 * WebRTC media plane for the voice room (mesh): getUserMedia capture, one
 * RTCPeerConnection per remote participant, SDP/ICE signalling over the
 * voice socket's relay frames (4007-4009), AudioContext playback and
 * analyser-based speaking detection per stream.
 *
 * Negotiation follows the perfect-negotiation pattern — polite/impolite is
 * the lexicographic order of the two user ids, so a rejoin race cannot
 * deadlock both peers in signaling glare. The joiner dials everyone in the
 * roster snapshot; later arrivals dial in with their own offers. Speaking
 * indicators are purely client-measured (the proto SPEAKING_NOTIFY path has
 * no server producer yet).
 *
 * Platform access (getUserMedia / RTCPeerConnection / AudioContext /
 * AnalyserNode) is dependency-injected so jsdom tests drive the whole mesh
 * with fakes.
 */

/** Narrowed surface the media layer needs from the browser. */
export interface MediaPlatform {
  getUserMedia(constraints: MediaStreamConstraints): Promise<MediaStream>;
  createPeerConnection(config: RTCConfiguration): RTCPeerConnection;
  createAudioContext(): AudioContextLike;
  /** Level meter only — never wired to the speakers (used for the mic too). */
  attachMeter(
    ctx: AudioContextLike,
    stream: MediaStream,
    onLevel: (level: number) => void,
  ): LevelPump;
  /** Remote stream playback: source -> gain -> destination. */
  playStream(ctx: AudioContextLike, stream: MediaStream): Playback;
}

/** AnalyserNode chain ownership + start/stop pump. */
export interface LevelPump {
  start(): void;
  stop(): void;
}

export interface Playback {
  gain: GainNodeLike;
  stop(): void;
}

export interface AudioContextLike {
  createMediaStreamSource(stream: MediaStream): AudioNodeLike;
  createAnalyser(): AnalyserNodeLike;
  createGain(): GainNodeLike;
  destination: AudioNodeLike;
  close(): void;
}

export interface AnalyserNodeLike extends AudioNodeLike {
  getFloatTimeDomainData(buf: Float32Array): void;
  readonly fftSize: number;
}

export interface GainNodeLike extends AudioNodeLike {
  gain: { value: number };
}

export interface AudioNodeLike {
  connect(node: AudioNodeLike): void;
  disconnect(): void;
}

/** Browser platform; every consumer outside tests uses this one. */
export const browserPlatform: MediaPlatform = {
  getUserMedia: (c) => navigator.mediaDevices.getUserMedia(c),
  createPeerConnection: (config) => new RTCPeerConnection(config),
  createAudioContext: () => new AudioContext() as unknown as AudioContextLike,
  attachMeter: (ctx, stream, onLevel) => {
    const source = ctx.createMediaStreamSource(stream);
    const analyser = ctx.createAnalyser();
    const buf = new Float32Array(analyser.fftSize);
    source.connect(analyser);
    let timer: ReturnType<typeof setTimeout> | null = null;
    return {
      start(): void {
        if (timer !== null) return;
        const tick = (): void => {
          analyser.getFloatTimeDomainData(buf);
          let sum = 0;
          for (const v of buf) sum += v * v;
          onLevel(Math.sqrt(sum / buf.length));
          timer = setTimeout(tick, 50);
        };
        tick();
      },
      stop(): void {
        if (timer !== null) clearTimeout(timer);
        timer = null;
        source.disconnect();
        analyser.disconnect();
      },
    };
  },
  playStream: (ctx, stream) => {
    const source = ctx.createMediaStreamSource(stream);
    const gain = ctx.createGain();
    gain.gain.value = 1;
    source.connect(gain);
    gain.connect(ctx.destination);
    return {
      gain,
      stop(): void {
        source.disconnect();
        gain.disconnect();
      },
    };
  },
};

/** Normal browser names surfaced for the UI (NotAllowedError, NotFoundError...). */
export class MediaPermissionError extends Error {
  constructor(readonly cause: unknown) {
    super(cause instanceof DOMException ? cause.name : 'getUserMedia failed');
    this.name = 'MediaPermissionError';
  }
}

export interface VoiceMediaDeps {
  conn: ChatConnection;
  voice: Store<VoiceState>;
  platform?: MediaPlatform;
}

interface PeerEntry {
  pc: RTCPeerConnection;
  pump: LevelPump | null;
  playback: Playback | null;
  makingOffer: boolean;
  ignoreOffer: boolean;
  isSettingRemoteAnswerPending: boolean;
  polite: boolean;
}

/** Speaking flips above / below these RMS levels (hysteresis, no strobing). */
const SPEAK_ON_LEVEL = 0.02;
const SPEAK_OFF_LEVEL = 0.005;

export class VoiceMediaApi {
  private readonly conn: ChatConnection;
  private readonly voice: Store<VoiceState>;
  private readonly platform: MediaPlatform;
  private peers = new Map<string, PeerEntry>();
  private localStream: MediaStream | null = null;
  private localPump: LevelPump | null = null;
  private audio: AudioContextLike | null = null;
  private userId = '';
  private roomId = '';
  private iceServers: RTCIceServer[] = [];
  private deafened = false;
  private speaking = new Map<string, boolean>();
  private mediaError: string | null = null;
  private unsubs: Array<() => void> = [];

  constructor(deps: VoiceMediaDeps) {
    this.conn = deps.conn;
    this.voice = deps.voice;
    this.platform = deps.platform ?? browserPlatform;
  }

  /** Last media failure (permission denied, no device...), for the UI. */
  get lastError(): string | null {
    return this.mediaError;
  }

  /**
   * Starts capture and the mesh for the current room. The roster snapshot is
   * the set of peers to dial; peers that join later dial us with their own
   * offers (the joiner-initiates rule keeps glare to the rejoin races the
   * polite/impolite tiebreak already covers).
   */
  async start(
    userId: string,
    roomId: string,
    roster: readonly string[],
    iceServers: readonly RTCIceServer[] = [],
    deafened = false,
  ): Promise<void> {
    this.stop();
    this.userId = userId;
    this.roomId = roomId;
    this.iceServers = [...iceServers];
    this.deafened = deafened;
    this.mediaError = null;
    this.subscribeSignals();

    try {
      this.localStream = await this.platform.getUserMedia({ audio: true });
    } catch (e) {
      this.mediaError = new MediaPermissionError(e).message;
      throw new MediaPermissionError(e);
    }

    this.audio = this.platform.createAudioContext();
    this.localPump = this.platform.attachMeter(this.audio, this.localStream, (level) =>
      this.reportLevel(this.userId, level),
    );
    this.localPump.start();

    for (const peer of roster) {
      if (peer !== userId) await this.makeOffer(peer);
    }
  }

  /** Stops all peers, tracks and the audio graph. Safe to call twice. */
  stop(): void {
    const roomId = this.roomId;
    const userId = this.userId;
    for (const off of this.unsubs) off();
    this.unsubs = [];
    this.localPump?.stop();
    this.localPump = null;
    for (const entry of this.peers.values()) {
      this.closePeer(entry);
    }
    this.peers.clear();
    this.localStream?.getTracks().forEach((t) => t.stop());
    this.localStream = null;
    this.audio?.close();
    this.audio = null;
    this.roomId = '';
    if (userId) setSpeaking(this.voice, roomId, userId, false);
    this.userId = '';
  }

  /** Mute is local-only: the track keeps flowing, with silence. */
  setMuted(muted: boolean): void {
    this.localStream?.getAudioTracks().forEach((t) => {
      t.enabled = !muted;
    });
  }

  /** Deafen gates every remote stream's playback gain. */
  setDeafened(deafened: boolean): void {
    this.deafened = deafened;
    for (const entry of this.peers.values()) {
      if (entry.playback) entry.playback.gain.gain.value = deafened ? 0 : 1;
    }
  }

  /** Drops a departed peer (PARTICIPANT_LEFT for someone else). */
  dropPeer(peerId: string): void {
    const entry = this.peers.get(peerId);
    if (!entry) return;
    this.peers.delete(peerId);
    this.closePeer(entry);
    setSpeaking(this.voice, this.roomId, peerId, false);
  }

  private closePeer(entry: PeerEntry): void {
    entry.pump?.stop();
    entry.pump = null;
    entry.playback?.stop();
    entry.playback = null;
    try {
      entry.pc.close();
    } catch {
      // closing a half-open pc can throw; teardown must not.
    }
  }

  private reportLevel(peerId: string, level: number): void {
    const was = this.speaking.get(peerId) ?? false;
    const now = was ? level > SPEAK_OFF_LEVEL : level > SPEAK_ON_LEVEL;
    if (now === was) return;
    this.speaking.set(peerId, now);
    setSpeaking(this.voice, this.roomId, peerId, now);
  }

  private async makeOffer(peerId: string): Promise<void> {
    const entry = this.ensurePeer(peerId);
    entry.makingOffer = true;
    try {
      await entry.pc.setLocalDescription();
      this.sendSdpOffer(peerId, entry.pc.localDescription!);
    } finally {
      entry.makingOffer = false;
    }
  }

  private ensurePeer(peerId: string): PeerEntry {
    const existing = this.peers.get(peerId);
    if (existing) return existing;

    const pc = this.platform.createPeerConnection({
      iceServers: this.iceServers,
    });
    const entry: PeerEntry = {
      pc,
      pump: null,
      playback: null,
      makingOffer: false,
      ignoreOffer: false,
      isSettingRemoteAnswerPending: false,
      polite: this.userId < peerId,
    };
    this.peers.set(peerId, entry);

    if (this.localStream) {
      for (const track of this.localStream.getTracks()) {
        pc.addTrack(track, this.localStream);
      }
    }

    pc.onicecandidate = ({ candidate }): void => {
      if (candidate === null || !this.roomId) return;
      this.conn.send(
        MsgID.ICE_CANDIDATE_MSG,
        IceCandidateMessage.encode(
          IceCandidateMessage.create({
            roomId: this.roomId,
            fromUserId: this.userId,
            toUserId: peerId,
            candidate: {
              candidate: candidate.candidate,
              sdpMid: candidate.sdpMid ?? undefined,
              sdpMlineIndex: candidate.sdpMLineIndex ?? undefined,
            },
          }),
        ).finish(),
      );
    };

    pc.ontrack = (event): void => {
      const [stream] = event.streams;
      if (!stream || !this.audio) return;
      entry.pump?.stop();
      entry.playback?.stop();
      entry.pump = this.platform.attachMeter(this.audio, stream, (level) =>
        this.reportLevel(peerId, level),
      );
      entry.playback = this.platform.playStream(this.audio, stream);
      entry.playback.gain.gain.value = this.deafened ? 0 : 1;
      entry.pump.start();
    };

    pc.onconnectionstatechange = (): void => {
      if (
        pc.connectionState === 'failed' ||
        pc.connectionState === 'closed'
      ) {
        this.dropPeer(peerId);
      }
    };
    return entry;
  }

  private subscribeSignals(): void {
    this.unsubs = [
      this.conn.onNotify(MsgID.SDP_OFFER_MSG, (body) =>
        void this.onSdpOffer(body),
      ),
      this.conn.onNotify(MsgID.SDP_ANSWER_MSG, (body) =>
        void this.onSdpAnswer(body),
      ),
      this.conn.onNotify(MsgID.ICE_CANDIDATE_MSG, (body) =>
        void this.onIceCandidate(body),
      ),
    ];
  }

  private async onSdpOffer(body: Uint8Array): Promise<void> {
    let msg: SdpOfferMessage;
    try {
      msg = SdpOfferMessage.decode(body);
    } catch {
      return;
    }
    if (!this.roomId || msg.roomId !== this.roomId || msg.fromUserId === this.userId) return;

    const entry = this.ensurePeer(msg.fromUserId);
    const readyForOffer =
      !entry.makingOffer &&
      (entry.pc.signalingState === 'stable' || entry.isSettingRemoteAnswerPending);
    const offerCollision = !readyForOffer;
    entry.ignoreOffer = !entry.polite && offerCollision;
    if (entry.ignoreOffer) return;

    if (!readyForOffer) {
      try {
        await entry.pc.setLocalDescription({ type: 'rollback' });
      } catch {
        // Rollback can throw when no pending offer survived; the
        // setRemoteDescription below still recovers the session.
      }
    }
    try {
      await entry.pc.setRemoteDescription({ type: 'offer', sdp: msg.sdpOffer });
      await entry.pc.setLocalDescription();
      this.sendSdpAnswer(msg.fromUserId, entry.pc.localDescription!);
    } catch {
      // A malformed SDP kills this peer only; onconnectionstatechange drops it.
    }
  }

  private async onSdpAnswer(body: Uint8Array): Promise<void> {
    let msg: SdpAnswerMessage;
    try {
      msg = SdpAnswerMessage.decode(body);
    } catch {
      return;
    }
    if (!this.roomId || msg.roomId !== this.roomId || msg.fromUserId === this.userId) return;

    const entry = this.peers.get(msg.fromUserId);
    if (!entry) return;
    try {
      entry.isSettingRemoteAnswerPending = true;
      await entry.pc.setRemoteDescription({
        type: 'answer',
        sdp: msg.sdpAnswer,
      });
    } catch {
      // late or colliding answer: the perfect-negotiation tiebreak on the
      // next offer recovers the session
    } finally {
      entry.isSettingRemoteAnswerPending = false;
    }
  }

  private async onIceCandidate(body: Uint8Array): Promise<void> {
    let msg: IceCandidateMessage;
    try {
      msg = IceCandidateMessage.decode(body);
    } catch {
      return;
    }
    if (!this.roomId || msg.roomId !== this.roomId || msg.fromUserId === this.userId || !msg.candidate) return;

    const entry = this.peers.get(msg.fromUserId);
    if (!entry || entry.ignoreOffer) return;
    try {
      await entry.pc.addIceCandidate({
        candidate: msg.candidate.candidate,
        sdpMid: msg.candidate.sdpMid ?? undefined,
        sdpMLineIndex: msg.candidate.sdpMlineIndex ?? undefined,
      });
    } catch {
      // candidates that predate the remote description are buffered by the
      // implementation; a genuinely bad candidate must not kill the mesh
    }
  }

  private sendSdpOffer(peerId: string, description: RTCSessionDescription): void {
    this.conn.send(
      MsgID.SDP_OFFER_MSG,
      SdpOfferMessage.encode(
        SdpOfferMessage.create({
          roomId: this.roomId,
          fromUserId: this.userId,
          toUserId: peerId,
          sdpOffer: description.sdp,
        }),
      ).finish(),
    );
  }

  private sendSdpAnswer(peerId: string, description: RTCSessionDescription): void {
    this.conn.send(
      MsgID.SDP_ANSWER_MSG,
      SdpAnswerMessage.encode(
        SdpAnswerMessage.create({
          roomId: this.roomId,
          fromUserId: this.userId,
          toUserId: peerId,
          sdpAnswer: description.sdp,
        }),
      ).finish(),
    );
  }
}
