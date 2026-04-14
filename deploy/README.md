# Deployment examples

- [`nginx.example.conf`](nginx.example.conf) — HTTPS, static `dashboard/dist`, reverse proxy to the sidecar for `/api` and WebSocket `/api/events`.

See [`../docs/PRODUCTION.md`](../docs/PRODUCTION.md) for environment variables (`CHAOS_LAB_CORS_ORIGIN`, `CHAOS_LAB_PORT`) and health checks.
