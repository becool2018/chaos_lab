# Production deployment

Chaos Lab ships as a **static dashboard** (any static host or CDN) plus the **sidecar** binary (or container) that serves `/api/*` and WebSocket `/api/events`.

## Sidecar environment

| Variable | Default | Purpose |
|----------|---------|---------|
| `CHAOS_LAB_PORT` | `8787` | Listen port (overridden by first CLI arg: `./chaos_lab_sidecar 9000`) |
| `CHAOS_LAB_CORS_ORIGIN` | `*` | `Access-Control-Allow-Origin`. Set to your dashboard origin (e.g. `https://chaos-lab.example.com`) in production. Never use `*` with credentialed requests. |

## Health checks

- `GET /api/health` — JSON: `{ "ok", "version", "sim_ready", "run_state" }`
- `GET /health` — same payload (for load balancers that strip path prefixes)

Point orchestration at `/api/health` for readiness.

## Dashboard build

```bash
npm run build
```

Serve `dashboard/dist/` behind HTTPS. The UI uses **relative** `/api` URLs in the browser, so the recommended pattern is a **reverse proxy** on one host:

- `/` → static files from `dist/`
- `/api` → sidecar upstream (HTTP + WebSocket upgrade)

For local dev, `npm run dev` proxies to `VITE_SIDECAR_URL` (see `dashboard/.env.example`).

## TLS and WebSockets

Terminate TLS at the proxy (nginx, Caddy, cloud LB). Ensure WebSocket pass-through to the sidecar for `/api/events`.

## Run summary (`GET /api/run/summary`)

Responses include the full current **`scenario.base_impairments`**, wall-clock **`started_at`** (ISO 8601 UTC), and optional **`ended_at`** when the run is not `running` (e.g. after **Pause**). Timestamps reset on **Reset**.

## messageEngine

Build the sidecar against a separate [messageEngine](./INTEGRATION.md) checkout; do not ship modified engine sources from this repo.
