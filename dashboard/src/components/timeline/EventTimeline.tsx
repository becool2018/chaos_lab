import { useMemo } from 'react'
import type { EventFilterChip, SimulationEvent } from '@chaos-lab/contracts'
import { eventMatchesFilter } from '../../lib/eventFilter'

const FILTER_ROWS: { label: string; chip: EventFilterChip }[] = [
  { label: 'All', chip: 'all' },
  { label: 'Retries', chip: 'retries' },
  { label: 'ACK timeouts', chip: 'ack_timeouts' },
  { label: 'Drops', chip: 'drops' },
  { label: 'Reordering', chip: 'reordering' },
  { label: 'Partitions', chip: 'partitions' },
  { label: 'Responses', chip: 'responses' },
]

export function EventTimeline({
  className = '',
  events,
  wsConnected,
  wsError,
  filter,
  onFilterChange,
}: {
  className?: string
  events: SimulationEvent[]
  wsConnected: boolean
  wsError: string | null
  filter: EventFilterChip
  onFilterChange: (chip: EventFilterChip) => void
}) {
  const visible = useMemo(
    () => events.filter((ev) => eventMatchesFilter(ev, filter)),
    [events, filter],
  )

  return (
    <section
      className={`flex flex-col rounded-[var(--radius-card)] bg-cl-panel p-4 shadow-sm ${className}`}
    >
      <div className="mb-2 flex items-center justify-between gap-2">
        <h2 className="text-lg font-semibold text-cl-text">Event timeline</h2>
        <span
          className={`text-xs font-medium ${wsConnected ? 'text-cl-success' : 'text-cl-text-muted'}`}
        >
          {wsError ?? (wsConnected ? 'Live' : 'Connecting…')}
        </span>
      </div>
      <div className="mb-3 flex flex-wrap gap-2">
        {FILTER_ROWS.map(({ label, chip }) => {
          const active = filter === chip
          return (
            <button
              key={chip}
              type="button"
              onClick={() => onFilterChange(chip)}
              aria-pressed={active}
              className={
                active
                  ? 'rounded-full border border-cl-brand bg-cl-brand-soft px-3 py-1 text-xs font-medium text-cl-brand'
                  : 'rounded-full border border-cl-subtle bg-cl-page px-3 py-1 text-xs font-medium text-cl-text-secondary hover:border-cl-brand/40 hover:text-cl-text'
              }
            >
              {label}
            </button>
          )
        })}
      </div>
      <ul className="min-h-0 flex-1 space-y-2 overflow-auto">
        {visible.length === 0 && (
          <li className="rounded-[var(--radius-control)] bg-cl-page px-3 py-6 text-center text-sm text-cl-text-muted">
            {events.length === 0
              ? 'Choose a preset or apply impairments, start the run, and events will stream here.'
              : 'No events match this filter. Try another chip or clear filters.'}
          </li>
        )}
        {visible.map((ev) => (
          <li
            key={`${ev.id}-${ev.ts}`}
            className="cursor-pointer rounded-[var(--radius-control)] border border-transparent bg-cl-page px-3 py-2 text-sm hover:border-cl-subtle"
          >
            <div className="font-mono-technical text-xs text-cl-text-muted">{ev.ts}</div>
            <div className="font-medium text-cl-text">{ev.summary}</div>
            <div className="text-xs text-cl-text-secondary">
              {ev.type}
              {ev.source && ev.destination ? ` · ${ev.source} → ${ev.destination}` : ''}
            </div>
          </li>
        ))}
      </ul>
    </section>
  )
}
