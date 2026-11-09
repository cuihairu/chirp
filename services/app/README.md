# services/app — App plane

`services/app` is a **container directory, not a process**: like
`services/game/` and `services/shared/`, each child owns its own CMake
subdirectory and its own `src/main.cc` entrypoint. There is deliberately
no `services/app/src/main.cc` — the app plane has three independent
processes, and a shared assembly point would only pretend otherwise.

| Directory | Binary | Entrypoint | Role |
|---|---|---|---|
| `auth/` | `chirp_app_auth` | `src/main.cc` (basic fallback) / `src/main_enhanced.cc` (full, needs MySQL + libsodium) | Companion login: register/login/session issue, JWT signing, brute-force lockout |
| `notification/` | `chirp_app_notification` | `src/main.cc` | Device registry + APNs/FCM push routing for companion clients |
| `sdk_gateway/` | `chirp_app_sdk_gateway` | `src/main.cc` | Companion-app edge: auth + chat + search + notification aggregation behind one TCP/WS port |

Shared code between the three already lives in `libs/` (`chirp_network`
carries the session registry, auth client, redis session manager and the
notification RPC client), which is why `sdk_gateway/src/` is only
`main.cc`. Cross-plane process wiring is documented where it is deployed:
`docker-compose.cluster.yml` and `deploy/k8s/*-deployment.yaml`.
