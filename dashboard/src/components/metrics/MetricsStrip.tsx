import type { MetricsSnapshot } from '@chaos-lab/contracts'

function fmt(n: number | undefined): string {
  if (n === undefined || Number.isNaN(n)) return '—'
  return String(n)
}

export function MetricsStrip({
  metrics,
}: {
  metrics?: MetricsSnapshot
}) {
  const rows: { label: string; value: string }[] = [
    { label: 'Sent', value: fmt(metrics?.messages_sent) },
    { label: 'Delivered', value: fmt(metrics?.messages_delivered) },
    { label: 'Retries', value: fmt(metrics?.retries) },
    { label: 'ACK timeouts', value: fmt(metrics?.ack_timeouts) },
    { label: 'Duplicates dropped', value: fmt(metrics?.duplicates_dropped) },
    { label: 'Expiry drops', value: fmt(metrics?.expiry_drops) },
    {
      label: 'Avg latency',
      value:
        metrics?.average_latency_ms !== undefined
          ? `${metrics.average_latency_ms} ms`
          : '—',
    },
    { label: 'In flight', value: fmt(metrics?.in_flight_messages) },
  ]

  return (
    <footer className="border-t border-cl-subtle bg-cl-panel px-4 py-3 shadow-[0_-4px_12px_rgba(21,32,51,0.06)]">
      <div className="mx-auto flex max-w-[1600px] flex-wrap gap-3 md:gap-4">
        {rows.map((m) => (
          <div
            key={m.label}
            className="min-w-[7rem] flex-1 rounded-[var(--radius-control)] bg-cl-page px-3 py-2"
          >
            <div className="font-mono-technical text-xl font-semibold text-cl-text">
              {m.value}
            </div>
            <div className="text-xs text-cl-text-muted">{m.label}</div>
          </div>
        ))}
      </div>
    </footer>
  )
}
