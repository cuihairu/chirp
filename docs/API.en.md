> 🌐 Language: [中文](./API.md) | **English** (this page)

# Chirp API

The authoritative API overview has moved to [api/overview.md](./api/overview.en.md).

Please see that page for:

- packet framing
- the `chirp.gateway.Packet` envelope rules
- the status of supported endpoints
- the current message ID reference
- the login and chat flows
- common error codes

The source of truth for message definitions (schema) remains `proto/*.proto`.

Status reminder: the supported runtime path is `gateway + auth + chat`. With `--chat_host` configured, the gateway forwards 2xxx (2001-2999) business packets through each client's ServiceBridge pipe to the corresponding chat instance, except that 2248 is split off to the search service (`--search_host`, default 5007) before the 2xxx wildcard; social (3xxx)/voice (4xxx) are not on the forwarding path. Chat clients can go through the gateway forwarding or connect directly to the Chat service (an optional path). Before treating a larger proto surface as a stable capability, check the [Capability Matrix](./CAPABILITY_MATRIX.en.md).
