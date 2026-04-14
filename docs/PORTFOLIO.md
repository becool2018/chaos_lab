# Portfolio & interviews

Use this page to **describe Chaos Lab in interviews** and to **copy lines onto your resume**. Chaos Lab exists to **demonstrate that [messageEngine](https://github.com/) is real software**: a deterministic messaging / delivery stack with impairments, not a mock.

## One-sentence pitch

> “I built a React operator console and a small C++ sidecar that links my messageEngine static library—LOCAL_SIM plus DeliveryEngine—so you can drive scenarios, start runs, and watch delivery events and metrics over HTTP and WebSockets.”

## 30-second demo script

1. Run the sidecar and `npm run dev`, open the dashboard.
2. Pick a preset (e.g. **Lossy**) or adjust impairments → **Apply to engine** (re-inits the sim from the engine).
3. **Start** the run; point at the **metrics strip** (sent/delivered/retries) and **event timeline** (live WebSocket stream).
4. Say: *“All of that behavior is coming from messageEngine through the sidecar—the UI is just observability.”*

## Resume bullets (copy/adapt)

- Built **Chaos Lab**: TypeScript/React dashboard + **C++ HTTP/WebSocket sidecar** statically linked to **messageEngine** (`libmessageengine.a`), driving **LOCAL_SIM** and **DeliveryEngine** with configurable impairments.
- Defined a **typed REST/WebSocket contract** (`contracts/api-contract.ts`) between UI and sidecar; implemented **TanStack Query** polling, **live event stream**, filters, and **production-oriented** hooks (health, CORS, run summary with ISO timestamps).
- Documented **multi-repo layout** (engine vs app), **nginx** deployment example, and **CI** for the TypeScript stack.

## What not to over-claim

- Chaos Lab is a **focused demo / operator shell**, not a multi-tenant SaaS unless you build that separately.
- **messageEngine** stays its **own repository**; this repo only **integrates** it (see [REPO_BOUNDARIES.md](./REPO_BOUNDARIES.md)).

## Licensing

**Chaos Lab** and **messageEngine** are both **Apache License 2.0**—this repo has a root [`LICENSE`](../LICENSE); the engine repo already ships its own Apache 2.0 `LICENSE` (no extra license work needed there).

## GitHub polish checklist

- [ ] Replace **`YOUR_GITHUB_USERNAME`** in the README badge with your real username or org.
- [ ] Push both repos (or pin **messageEngine** version in your README / integration doc).
- [ ] Optional: add a **15–60s screen recording** or GIF in the README showing Apply → Start → metrics/events.
