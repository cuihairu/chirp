# Chirp API Notes

The canonical API overview is now [api/overview.md](./api/overview.md).

Use that page for:

- packet framing
- `chirp.gateway.Packet` envelope rules
- supported endpoint status
- current message ID mappings
- login and chat flows
- common error codes

The source of truth for schemas is still `proto/*.proto`.

Status reminder: the supported runtime path is `gateway + auth + chat`. Gateway does not yet forward chat, social, or voice business packets; current chat clients connect to the Chat service directly. Check [Capability Matrix](./CAPABILITY_MATRIX.md) before treating broader proto surfaces as stable.
