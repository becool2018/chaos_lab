# Repository boundaries

**Chaos Lab** (this repository) owns:

- `contracts/` — TypeScript API contract shared with the dashboard
- `dashboard/` — Vite + React UI
- `sidecar/` — HTTP/WebSocket adapter binary (links against messageEngine **only** as a library)

**messageEngine** is a **separate project**. Do not add Chaos Lab–specific source, docs, or build logic inside a `messageEngine` checkout. Track engine work in the messageEngine repository only.

Integration is limited to:

- Building `libmessageengine.a` from a path given by `MESSAGE_ENGINE` (see [INTEGRATION.md](./INTEGRATION.md))
- Comments in `contracts/` that reference engine headers for traceability (no code coupling)
