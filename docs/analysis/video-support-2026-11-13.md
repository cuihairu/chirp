> 🌐 Language: **English** (this page) | [中文](../zh/analysis/video-support-2026-11-13.md)

# Video support research: routes, reuse, gaps and sizing

> Researched 2026-11-13/14 as the video-track kickoff ticket (`chirp-voice-video` B part). This is a decision document: every claim about Chirp itself is anchored to source or test evidence; claims about external projects were checked against their repositories on 2026-11-14 and their LICENSE files remain the authority.

## TL;DR

- **Recommendation: extend the current P2P mesh (route A) for video.** The voice signaling face relays opaque SDP bodies; video m-lines and mid-session renegotiation flow through it unchanged. For the room sizes a game party actually has (2–5), mesh video is the only route that adds video without adding a stateful media service.
- **Route B (an SFU) is a separate program, not a retrofit.** It is justified by concrete triggers (rooms beyond ~6, one-way broadcast, recording), and it comes in two shapes with very different reuse of this repo (see [SFU routes](#route-b-sfu)).
- **Skeleton landed (experimental) and the probe passes:** a real-browser video probe over the unchanged voice face (`apps/web_companion/scripts/voice_media_e2e.mjs`, `E2E_VIDEO=1`) plus a protocol placeholder in `proto/voice.proto`. Run 2026-11-14: both clients, SDP settled, a live remote video track on each side, `framesDecoded` +40 over a 2 s window at 640×480, host-candidate pair. No video feature is shipped; nothing in this document should be read as "video is supported".
- Sizing summary: **S** mesh video probe/product toggle (days), **M** four-platform video client surface (weeks), **L** mediasoup-based SFU service integrated with chirp (a dedicated quarter), **XL** distributed SFU fleet with recording/transcoding (out of scope until a game needs it).

## What the voice face is today (facts)

The voice service is a **pure SDP/ICE signaling relay**. Media never touches it:

| Fact | Anchor |
| --- | --- |
| ICE/offer/answer relays are targeted 1:1 forwards via `to_user_id`, gated by room membership + bound identity (`RelaySenderAllowed`) | `services/voice/src/voice_dispatch.cc` (`RelaySenderAllowed`, the 4007/4008/4009 handlers) |
| Every member peers with every other member (full mesh); the server has no RTP/RTCP port and parses no SDP | same file; no media dependency in `services/voice/CMakeLists.txt` |
| TURN/STUN handed out per join as coturn REST short-term credentials | `MakeTurnUsername`/`MakeTurnCredential`/`AppendIceServers` in `voice_dispatch.cc`; deployment runbook in [guide/deployment](../guide/deployment.md) |
| `media_tier`/`rtcp_feedback_ms` are **policy fields only** (≤12 people → FULL_MIX/1000ms, >12 → FORWARD_BYPASS/2000ms); no server-side mixing or forwarding exists behind them | `MediaTierForRoom`/`RtcpFeedbackMsForTier` in `voice_dispatch.cc`; honest split recorded in `todo.md` (VOICE-RT entry) |
| Web client runs the real media loop and is proven by a real-browser E2E: fake-mic chromium × 2 through the real UI, SDP settles, remote audio track live, inbound bytes grow; both host-candidate and coturn relay forms verified (`relay->relay` nominated pair) | `apps/web_companion/scripts/voice_media_e2e.mjs` |
| SDKs (core C++, Unity/C#) wrap the signaling only (request construction + notify callbacks); no capture/encode/playback | `sdks/core`, `sdks/csharp` (VOICE-RT ③, accounting in `todo.md`) |

The load-bearing consequence for video: **the relay does not inspect SDP**, so "video support" on the server is either *zero work* (mesh) or *a different service* (SFU). There is no middle state.

## Route A: video over the existing mesh

What changes technically, per concern the ticket names:

- **Negotiation.** Video tracks ride the same SDP offer/answer relay (messages 4008/4009). Adding a track after join is a renegotiation — another offer/answer pair through the same two messages; the service already relays bodies it never parses. The probe skeleton proves this end-to-end (`E2E_VIDEO=1`).
- **Bandwidth adaptation.** In a P2P pair, Chrome's transport-wide congestion control and the receiver's feedback work as usual; each sender automatically degrades its single spatial layer. What does *not* exist is server-side layer switching — that is inherent to mesh, not a missing feature.
- **Keyframes.** PLI/FIR RTCP requests flow peer-to-peer; a new mesh member joining mid-session gets keyframes from existing senders without the server doing anything.
- **Simulcast.** In a mesh there is nobody to choose layers per receiver, so simulcast is not a "gap" here — it is meaningless (it would double uplink cost). Honest framing: simulcast is an SFU capability, and wanting it is a signal to move to route B.
- **The real ceiling.** Uplink is O(N−1) per peer (every member encodes and uploads to every other member) plus per-peer encode CPU. Practical comfort zone is small rooms — which is exactly the game-party case. Large rooms break mesh on client bandwidth long before any server limit.

## Route B: SFU

An SFU receives one upstream per client and forwards (selected) RTP layers to the others. It is a new stateful media service with its own scaling model (one SFU per room or shard; rooms don't span nodes cheaply). Two shapes:

### B1 — LiveKit as a sidecar

- Apache-2.0, Go, self-described "Scalable, distributed WebRTC SFU" on Pion (checked 2026-11-14).
- Brings **its own client SDKs and its own WebSocket signaling**, and authenticates clients with JWT access tokens.
- Reuse assessment: **chirp's voice signaling face is not reused for media rooms** — it would be reduced to a token issuer (chirp_auth already owns an HS256 secret LiveKit tokens could share the signing shape with) plus a roster mirror. The room/membership/TURN work in `chirp_voice` stays useful for the voice face, but video rooms would live in a parallel stack with their own lifecycle, deployment, and failure modes.
- Buys the most for the least code, at the cost of a second real-time system to operate.

### B2 — self-built SFU on mediasoup

- ISC license; a C++ media worker (libuv) with Node.js and Rust server APIs; explicitly signaling-agnostic (checked 2026-11-14).
- Reuse assessment: **the voice face is reusable in shape** — rooms, targeted relay discipline, coturn REST credential minting, the tier policy fields could finally govern a real bypass. But chirp then owns the hard parts: RTP routing, simulcast layer selection, keyframe pacing, bandwidth management, worker lifecycle. This is a media-team project, not an incremental feature.

### What an SFU buys (either shape)

One upstream per client (the mesh O(N−1) ceiling disappears), real server-side tiering (the `media_tier` field stops being a hint), simulcast with per-receiver layer choice, recording, and spectator-scale rooms.

## Game-scenario fit

| Scenario | Route | Why |
| --- | --- | --- |
| Party voice + occasional video, 2–5 people | **A (mesh)** | No new service; uplink fine at these sizes; already E2E-proven plumbing |
| Squad up to ~8 with video | A borderline | 7 upstreams per peer at 8 members; workable only at low resolution/bitrate |
| Spectator / streaming / tournament overlay | B (L+) | One sender, many receivers — the definition of an SFU use case |
| Large-room voice (>12, the FORWARD_BYPASS tier) | B (L+) | The tier field already promises a bypass that does not exist; build or remove the promise |
| MMO proximity voice | out of scope | Spatial audio + many tiny rooms is a different architecture problem; mesh breaks on uplink, SFU breaks on room churn — needs its own research |

## Sizing

- **S — mesh video, minimal (days).** Landed as skeleton (probe, below). Productizing means: a video toggle in the web dialog, track-level mute handling, and the E2E extended to assert video frames — all client-side; zero server change.
- **M — four-platform video client surface (weeks).** Capture/playback/device management per platform (web done in S; core C++ and Unity/C# need a media abstraction each — this is where the honest "SDK signaling wrapper" limitation bites), renegotiation UX, per-platform tests.
- **L — mediasoup SFU integrated with chirp (a dedicated quarter).** New service binary + worker lifecycle, chirp_voice bridge (rooms/creds/roster), real tier implementation, ops (metrics, TURN for the SFU, capacity). Only with a concrete game need.
- **XL — distributed SFU fleet (recording, transcoding, multi-node rooms).** Not scoped; revisit only after L exists and a scenario demands it.

## Skeleton landed (experimental, this batch)

1. `apps/web_companion/scripts/voice_media_e2e.mjs` gains `E2E_VIDEO=1`: both clients additionally capture a fake camera and add the video track to the same pc; the test asserts the remote video track is live and video frames actually decode (`framesDecoded` growth, positive `frameWidth`). This is a probe of the route, not a feature — the UI has no video toggle and nothing ships to users.
2. `proto/voice.proto` carries a placeholder comment sketching the eventual video capability fields (marked experimental, no fields reserved yet). Adding real fields is deliberately deferred until a client actually consumes them.

## Sources

- LiveKit repository: license Apache-2.0, Go SFU on Pion, JWT access tokens, own SDKs/signaling (fetched 2026-11-14).
- mediasoup repository: license ISC, C++ worker + Node/Rust APIs, signaling-agnostic SFU library (fetched 2026-11-14).
- In-repo evidence: `services/voice/src/voice_dispatch.cc`, `apps/web_companion/scripts/voice_media_e2e.mjs`, `docs/guide/deployment.md`, `todo.md` (VOICE-RT honest-split entry).
