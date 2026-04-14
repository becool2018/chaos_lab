# Chaos Lab

[![CI](https://github.com/becool2018/chaos-lab/actions/workflows/ci.yml/badge.svg)](https://github.com/YOUR_GITHUB_USERNAME/chaos-lab/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](./LICENSE)

Chaos Lab is a dashboard + C++ sidecar that integrates with [`messageEngine`](https://github.com/becool2018/messageEngine) (`libmessageengine.a`) to run local simulation scenarios, apply impairments, and inspect transport behavior over REST/WebSockets.

> Replace `YOUR_GITHUB_USERNAME` in the CI badge URL with your own GitHub user/org after publishing.

## Prerequisites

- Node.js 20+ and npm
- C++17 toolchain (`clang++` or `g++`) and `make`
- `mbedtls` development libraries
  - macOS (Homebrew): `brew install mbedtls`
  - Ubuntu/Debian: `sudo apt-get install -y build-essential pkg-config libmbedtls-dev`
  - Fedora/RHEL: `sudo dnf install -y gcc-c++ make pkgconf-pkg-config mbedtls-devel`
- Optional: a local checkout of [`messageEngine`](https://github.com/becool2018/messageEngine) (only needed when refreshing vendored artifacts)

`chaos-lab` now supports a vendored engine snapshot at `third_party/messageengine` so sidecar builds can work even if your local [`messageEngine`](https://github.com/becool2018/messageEngine) checkout under `chaos-lab/messageEngine` is deleted.

## Linux Install (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install -y curl git build-essential pkg-config libmbedtls-dev
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt-get install -y nodejs
```

Verify toolchain:

```bash
node -v
npm -v
g++ --version
pkg-config --modversion mbedtls
```

## Linux Install (Fedora/RHEL)

```bash
sudo dnf install -y curl git gcc-c++ make pkgconf-pkg-config mbedtls-devel nodejs npm
```

Verify toolchain:

```bash
node -v
npm -v
g++ --version
pkg-config --modversion mbedtls
```

## Repository Layout

For live engine development, use either layout:

```text
parent/
  chaos-lab/
  messageEngine/
```

or:

```text
parent/
  chaos-lab/
    messageEngine/
```

If needed, set:

```bash
export MESSAGE_ENGINE=/absolute/path/to/messageEngine
```

## Build

From the `chaos-lab` root:

```bash
npm install
npm run sidecar:build
npm run build
```

What this does:
- `npm run sidecar:build`: resolves [`messageEngine`](https://github.com/becool2018/messageEngine), builds `libmessageengine.a`, then builds `sidecar/build/chaos_lab_sidecar`
- `npm run build`: builds the dashboard production bundle (`dashboard/dist`)

If no live [`messageEngine`](https://github.com/becool2018/messageEngine) checkout is found, `npm run sidecar:build` falls back to vendored artifacts in `third_party/messageengine`.

## Refresh vendored [messageEngine](https://github.com/becool2018/messageEngine) snapshot

Use this when you want to pull latest engine changes, then update the copy used by `chaos-lab`:

```bash
git -C messageEngine pull
npm run sidecar:vendor
```

## Test and Validate

From the `chaos-lab` root:

```bash
npm run typecheck
npm run lint
```

Optional engine test suite (from [`messageEngine`](https://github.com/becool2018/messageEngine) root):

```bash
make tests
make run_tests
```

Optional stress tests (long-running):

```bash
make run_stress_tests
```

## Run (Development)

Open two terminals from `chaos-lab` root.

Terminal 1 (sidecar):

```bash
npm run sidecar
```

Terminal 2 (dashboard):

```bash
npm run dev
```

Then open the dashboard URL printed by Vite (usually `http://localhost:5173`).

Default sidecar URL is `http://127.0.0.1:8787`. To point the dashboard somewhere else, set `VITE_SIDECAR_URL` in `dashboard/.env.local`.

## Run (Production-style Local)

```bash
npm run sidecar:build
./sidecar/build/chaos_lab_sidecar
```

In another terminal:

```bash
npm run build
```

Serve `dashboard/dist` with your preferred static file server and reverse-proxy `/api` + `/api/events` to the sidecar.

## Simple Usage Examples

### Example 1: Run locally with the dashboard

Use this when you want to click through presets and watch metrics update in the UI.

```bash
# Terminal 1
npm run sidecar

# Terminal 2
npm run dev
```

Then open `http://localhost:5173`, choose a preset (for example `Healthy`), click start/run, and watch run state + delivery metrics update.

### Example 2: Drive the sidecar API with curl

Use this when you want a quick headless smoke test without opening the dashboard.

```bash
# Terminal 1
npm run sidecar
```

In another terminal:

```bash
# Health
curl -s http://127.0.0.1:8787/api/health

# Apply a simple lossy scenario
curl -s -X POST http://127.0.0.1:8787/api/scenario/current \
  -H "Content-Type: application/json" \
  -d '{"loss_probability":0.20,"fixed_latency_ms":50,"jitter_mean_ms":0,"jitter_variance_ms":0,"duplication_probability":0,"reorder_enabled":false,"reorder_window_size":0,"partition_enabled":false,"partition_duration_ms":0,"partition_gap_ms":0}'

# Start the run
curl -s -X POST http://127.0.0.1:8787/api/run/start

# Read summary
curl -s http://127.0.0.1:8787/api/run/summary
```

## Troubleshooting

- Quick diagnostics (copy/paste)
  - Run these from `chaos-lab` root to print the most important checks:
    ```bash
    pwd
    bash scripts/resolve-message-engine.sh
    git -C "$(bash scripts/resolve-message-engine.sh)" status --short --branch
    ls -l "$(bash scripts/resolve-message-engine.sh)/build/libmessageengine.a"
    ls -l sidecar/build/chaos_lab_sidecar
    curl -fsS http://127.0.0.1:8787/health || echo "sidecar health check failed"
    ```

- [`messageEngine`](https://github.com/becool2018/messageEngine) not found
  - Run `bash scripts/resolve-message-engine.sh` to see the resolved path.
  - Ensure [`messageEngine`](https://github.com/becool2018/messageEngine) is either at `chaos-lab/messageEngine` or as a sibling `../messageEngine` if you want to rebuild from source.
  - Or rely on the vendored fallback at `third_party/messageengine`.
  - Or set `MESSAGE_ENGINE` explicitly:
    ```bash
    export MESSAGE_ENGINE=/absolute/path/to/messageEngine
    ```

- `fatal: not a git repository` inside [`messageEngine`](https://github.com/becool2018/messageEngine)
  - Verify you are in the actual engine clone:
    ```bash
    git -C /absolute/path/to/messageEngine rev-parse --is-inside-work-tree
    ```
  - If this fails, reclone [`messageEngine`](https://github.com/becool2018/messageEngine) and re-run `npm run sidecar:build`.

- `mbedtls` headers or libraries not found (for example `mbedtls/ssl.h` missing)
  - Install dependencies:
    ```bash
    # macOS
    brew install mbedtls pkg-config

    # Ubuntu/Debian
    sudo apt-get install -y pkg-config libmbedtls-dev

    # Fedora/RHEL
    sudo dnf install -y pkgconf-pkg-config mbedtls-devel
    ```
  - Re-run:
    ```bash
    npm run sidecar:build
    ```

- Sidecar link/build errors about `libmessageengine.a`
  - Rebuild the engine library directly:
    ```bash
    npm run sidecar:lib
    npm run sidecar:build
    ```
  - Confirm the archive exists at `messageEngine/build/libmessageengine.a` in your local [`messageEngine`](https://github.com/becool2018/messageEngine) checkout.

- Dashboard starts but API calls fail
  - Check sidecar is running on `http://127.0.0.1:8787`.
  - If using a different host/port, set `dashboard/.env.local`:
    ```bash
    VITE_SIDECAR_URL=http://your-sidecar-host:8787
    ```
  - Restart `npm run dev` after changing `.env.local`.

- Port already in use
  - Change sidecar port:
    ```bash
    CHAOS_LAB_PORT=8788 npm run sidecar
    ```
  - Point dashboard to the new port via `VITE_SIDECAR_URL`.

## Scripts

| Command | Description |
|---------|-------------|
| `npm run dev` | Run dashboard dev server |
| `npm run build` | Build dashboard production bundle |
| `npm run typecheck` | Run TypeScript checks (contracts + dashboard) |
| `npm run lint` | Run dashboard ESLint |
| `npm run sidecar:lib` | Build `libmessageengine.a` only |
| `npm run sidecar:vendor` | Refresh vendored engine headers + `libmessageengine.a` |
| `npm run sidecar:build` | Build `libmessageengine.a` and sidecar binary |
| `npm run sidecar` | Build and run sidecar |

## Docs

- `docs/INTEGRATION.md`: [`messageEngine`](https://github.com/becool2018/messageEngine) path/layout details
- `docs/PRODUCTION.md`: deployment and production hardening guidance
- `docs/REPO_BOUNDARIES.md`: ownership boundaries between repos
- `deploy/README.md`: reverse proxy examples
- `contracts/api-contract.ts`: API and WebSocket contract

## License

Chaos Lab is licensed under Apache 2.0 (`LICENSE`). [`messageEngine`](https://github.com/becool2018/messageEngine) is licensed separately in its own repository.
