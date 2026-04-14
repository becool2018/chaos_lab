# messageEngine integration

The sidecar links statically against `libmessageengine.a`. Chaos Lab does not vendor engine source in this repo’s tracked files; keep a local clone of the messageEngine repository (your upstream) and point the build at it.

## Layout options

### A — Nested (convenient for one machine)

```
parent/
  chaos-lab/          ← this repo
    sidecar/
    dashboard/
    messageEngine/    ← clone here (gitignored in chaos-lab if you prefer not to commit it)
```

Default `make` in `sidecar/` uses `MESSAGE_ENGINE=../messageEngine` (relative to `sidecar/`), which resolves to `chaos-lab/messageEngine`.

### B — Sibling repositories

```
parent/
  chaos-lab/
  messageEngine/
```

Set the environment variable so the sidecar Makefile receives an absolute path:

```bash
export MESSAGE_ENGINE=/path/to/messageEngine
npm run sidecar:build
```

Or rely on `scripts/resolve-message-engine.sh`, which picks sibling `../messageEngine` when `chaos-lab/messageEngine` is missing.

## Sidecar environment (production)

| Variable | Default | Purpose |
|----------|---------|---------|
| `CHAOS_LAB_PORT` | `8787` | Listen port if no CLI argument |
| `CHAOS_LAB_CORS_ORIGIN` | `*` | CORS origin for browser clients; set to your dashboard URL in production |

See [PRODUCTION.md](./PRODUCTION.md) for TLS, health checks, and reverse-proxy layout.

For describing this project on a resume, see [PORTFOLIO.md](./PORTFOLIO.md).

## Build commands

From the **chaos-lab** root:

```bash
npm run sidecar:lib      # build libmessageengine.a only
npm run sidecar:build    # library + chaos_lab_sidecar
npm run sidecar          # build and run the sidecar binary
```

Equivalent manual steps:

```bash
./scripts/resolve-message-engine.sh   # prints resolved path; source or use scripts below
bash scripts/build-messageengine-lib.sh
bash scripts/build-sidecar.sh
```

## If `messageEngine` was previously committed inside chaos-lab

To stop tracking the nested copy while keeping files on disk until you migrate:

```bash
git rm -r --cached messageEngine
```

Then add `/messageEngine/` to chaos-lab `.gitignore` (already present) and clone the engine repo separately or keep an untracked nested clone for local builds.
