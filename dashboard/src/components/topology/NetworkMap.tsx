import type { TopologyLink, TopologyNode, TopologyResponse } from '@chaos-lab/contracts'

function nodeCenter(
  nodes: TopologyNode[],
  id: string,
  positions: { x: number; y: number }[],
): { x: number; y: number } | null {
  const i = nodes.findIndex((n) => n.id === id)
  if (i < 0) return null
  return positions[i] ?? null
}

function buildPositions(count: number, cx: number, cy: number, r: number) {
  if (count <= 0) return []
  return Array.from({ length: count }, (_, i) => {
    const a = (2 * Math.PI * i) / Math.max(count, 1) - Math.PI / 2
    return { x: cx + r * Math.cos(a), y: cy + r * Math.sin(a) }
  })
}

export function NetworkMap({
  className = '',
  topology,
  loading,
}: {
  className?: string
  topology?: TopologyResponse
  loading?: boolean
}) {
  const nodes = topology?.nodes ?? []
  const links = topology?.links ?? []

  const cx = 200
  const cy = 130
  const r = 72
  const positions = buildPositions(nodes.length, cx, cy, r)

  return (
    <section
      className={`flex flex-col rounded-[var(--radius-card)] bg-cl-panel p-4 shadow-sm ${className}`}
    >
      <h2 className="mb-3 text-lg font-semibold text-cl-text">Network map</h2>
      <div className="relative min-h-[220px] flex-1 overflow-hidden rounded-[var(--radius-control)] border border-cl-subtle/80 bg-gradient-to-b from-cl-brand-soft/25 to-cl-page">
        {loading && (
          <div className="absolute inset-0 z-10 flex items-center justify-center bg-cl-page/60 text-sm text-cl-text-secondary">
            Loading topology…
          </div>
        )}
        {!loading && nodes.length === 0 && (
          <div className="flex h-full min-h-[200px] items-center justify-center px-4 text-center text-sm text-cl-text-muted">
            No topology from the sidecar yet.
          </div>
        )}
        {!loading && nodes.length > 0 && (
          <svg
            className="h-full w-full min-h-[220px]"
            viewBox="0 0 400 260"
            role="img"
            aria-label="Simulation network topology"
          >
            <defs>
              <marker
                id="arrow"
                markerWidth="8"
                markerHeight="8"
                refX="6"
                refY="4"
                orient="auto"
              >
                <path d="M0,0 L8,4 L0,8 Z" fill="currentColor" className="text-cl-link-normal" />
              </marker>
            </defs>
            {links.map((link: TopologyLink, i: number) => {
              const from = nodeCenter(nodes, link.from, positions)
              const to = nodeCenter(nodes, link.to, positions)
              if (!from || !to) return null
              return (
                <line
                  key={`${link.from}-${link.to}-${i}`}
                  x1={from.x}
                  y1={from.y}
                  x2={to.x}
                  y2={to.y}
                  stroke="currentColor"
                  strokeWidth="2"
                  className="text-cl-link-normal/80"
                  markerEnd="url(#arrow)"
                />
              )
            })}
            {nodes.map((node: TopologyNode, i: number) => {
              const p = positions[i]
              if (!p) return null
              const label = node.label ?? node.id
              return (
                <g key={node.id}>
                  <circle
                    cx={p.x}
                    cy={p.y}
                    r="22"
                    className="fill-cl-panel stroke-cl-brand stroke-2"
                  />
                  <text
                    x={p.x}
                    y={p.y + 4}
                    textAnchor="middle"
                    className="fill-cl-text text-[11px] font-semibold"
                  >
                    {label.length > 14 ? `${label.slice(0, 12)}…` : label}
                  </text>
                </g>
              )
            })}
          </svg>
        )}
        <p className="border-t border-cl-subtle/60 px-3 py-2 text-center text-xs text-cl-text-muted">
          Topology from <code className="rounded bg-cl-page px-1">GET /api/topology</code>. Live
          delivery overlays can layer on this graph later.
        </p>
      </div>
    </section>
  )
}
