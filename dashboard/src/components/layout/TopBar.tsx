import type { RunState } from '@chaos-lab/contracts'

const runStateLabel: Record<RunState, string> = {
  idle: 'Idle',
  running: 'Running',
  paused: 'Paused',
  completed: 'Completed',
  failed: 'Failed',
}

function badgeClass(runState: RunState): string {
  switch (runState) {
    case 'running':
      return 'bg-cl-brand-soft text-cl-brand'
    case 'paused':
      return 'bg-amber-100 text-amber-900'
    case 'failed':
      return 'bg-red-50 text-cl-danger'
    case 'completed':
      return 'bg-emerald-50 text-cl-success'
    default:
      return 'bg-cl-subtle text-cl-text-secondary'
  }
}

export interface TopBarProps {
  runState: RunState
  scenarioName: string
  onScenarioNameChange: (name: string) => void
  onStart: () => void
  onPause: () => void
  onReset: () => void
  onReplay: () => void
  onExport: () => void
  /** Disables run controls while a mutation is in flight */
  busy?: boolean
}

export function TopBar({
  runState,
  scenarioName,
  onScenarioNameChange,
  onStart,
  onPause,
  onReset,
  onReplay,
  onExport,
  busy = false,
}: TopBarProps) {
  const running = runState === 'running'

  return (
    <header className="flex flex-col gap-3 border-b border-cl-subtle bg-cl-panel px-4 py-3 shadow-sm md:flex-row md:items-center md:justify-between md:px-6">
      <div className="flex min-w-0 flex-1 flex-col gap-1">
        <div className="flex flex-wrap items-baseline gap-2">
          <h1 className="text-xl font-semibold tracking-tight text-cl-text">
            Chaos Lab
          </h1>
          <span className="text-sm text-cl-text-muted">
            Network reliability simulator
          </span>
        </div>
        <div className="flex flex-wrap items-center gap-2">
          <label className="sr-only" htmlFor="scenario-name">
            Scenario name
          </label>
          <input
            id="scenario-name"
            value={scenarioName}
            onChange={(e) => onScenarioNameChange(e.target.value)}
            className="min-w-0 max-w-md rounded-[var(--radius-control)] border border-cl-subtle bg-cl-page px-3 py-1.5 text-sm text-cl-text outline-none ring-cl-brand focus:ring-2"
          />
          <span
            className={`inline-flex items-center rounded-full px-2.5 py-0.5 text-xs font-medium ${badgeClass(runState)}`}
          >
            {runStateLabel[runState]}
          </span>
        </div>
      </div>

      <div className="flex flex-wrap gap-2">
        <button
          type="button"
          onClick={onStart}
          disabled={running || busy}
          className="rounded-[var(--radius-control)] bg-cl-brand px-4 py-2 text-sm font-semibold text-white shadow-sm hover:opacity-95 disabled:cursor-not-allowed disabled:opacity-50"
        >
          Start
        </button>
        <button
          type="button"
          onClick={onPause}
          disabled={!running || busy}
          className="rounded-[var(--radius-control)] border border-cl-subtle bg-cl-panel px-4 py-2 text-sm font-medium text-cl-text hover:bg-cl-subtle disabled:cursor-not-allowed disabled:opacity-50"
        >
          Pause
        </button>
        <button
          type="button"
          onClick={onReset}
          disabled={busy}
          className="rounded-[var(--radius-control)] border border-cl-subtle bg-cl-panel px-4 py-2 text-sm font-medium text-cl-text hover:bg-cl-subtle disabled:cursor-not-allowed disabled:opacity-50"
        >
          Reset
        </button>
        <button
          type="button"
          onClick={onReplay}
          disabled={busy}
          className="rounded-[var(--radius-control)] border border-cl-subtle bg-cl-panel px-4 py-2 text-sm font-medium text-cl-text hover:bg-cl-subtle disabled:cursor-not-allowed disabled:opacity-50"
        >
          Replay
        </button>
        <button
          type="button"
          onClick={onExport}
          disabled={busy}
          className="rounded-[var(--radius-control)] border border-cl-subtle bg-cl-panel px-4 py-2 text-sm font-medium text-cl-text hover:bg-cl-subtle disabled:cursor-not-allowed disabled:opacity-50"
        >
          Export
        </button>
      </div>
    </header>
  )
}
