import type { ImpairmentProfile, PresetSummary } from '@chaos-lab/contracts'

export interface ScenarioPanelProps {
  className?: string
  presets?: PresetSummary[]
  presetsLoading?: boolean
  impairments: ImpairmentProfile
  onImpairmentsChange: (next: ImpairmentProfile) => void
  onApply: () => void
  onSave: () => void
  onSelectPreset: (preset: PresetSummary) => void
  applyLoading?: boolean
  saveLoading?: boolean
}

export function ScenarioPanel({
  className = '',
  presets,
  presetsLoading,
  impairments,
  onImpairmentsChange,
  onApply,
  onSave,
  onSelectPreset,
  applyLoading,
  saveLoading,
}: ScenarioPanelProps) {
  const list = presets ?? []

  return (
    <aside
      className={`flex flex-col gap-4 rounded-[var(--radius-card)] bg-cl-panel p-4 shadow-sm ${className}`}
    >
      <h2 className="text-lg font-semibold text-cl-text">Presets</h2>
      {presetsLoading && (
        <p className="text-xs text-cl-text-muted">Loading presets…</p>
      )}
      <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 lg:grid-cols-1">
        {list.map((p) => (
          <button
            key={p.id}
            type="button"
            onClick={() => onSelectPreset(p)}
            className="flex flex-col items-start rounded-[var(--radius-control)] border border-cl-subtle bg-cl-page px-3 py-2 text-left transition hover:border-cl-brand/40 hover:bg-cl-brand-soft/50"
          >
            <span className="text-sm font-semibold text-cl-text">{p.name}</span>
            <span className="text-xs text-cl-text-muted">{p.description}</span>
          </button>
        ))}
      </div>

      <div>
        <h2 className="mb-3 text-lg font-semibold text-cl-text">Impairments</h2>
        <div className="flex flex-col gap-4">
          <SliderField
            label="Loss"
            unit="%"
            value={Math.round(impairments.loss_probability * 100)}
            onChange={(v) =>
              onImpairmentsChange({
                ...impairments,
                loss_probability: v / 100,
              })
            }
            helper="Packet loss randomly drops messages on this link."
          />
          <SliderField
            label="Latency"
            unit="ms"
            value={impairments.fixed_latency_ms}
            max={2000}
            onChange={(v) =>
              onImpairmentsChange({ ...impairments, fixed_latency_ms: v })
            }
            helper="Fixed delay before delivery."
          />
          <SliderField
            label="Jitter (mean)"
            unit="ms"
            value={impairments.jitter_mean_ms}
            max={500}
            onChange={(v) =>
              onImpairmentsChange({ ...impairments, jitter_mean_ms: v })
            }
            helper="Random extra delay around the mean."
          />
          <SliderField
            label="Duplicate"
            unit="%"
            value={Math.round(impairments.duplication_probability * 100)}
            onChange={(v) =>
              onImpairmentsChange({
                ...impairments,
                duplication_probability: v / 100,
              })
            }
            helper="Chance a send is duplicated."
          />
          <label className="flex items-center gap-2 text-sm text-cl-text">
            <input
              type="checkbox"
              className="rounded border-cl-subtle"
              checked={impairments.reorder_enabled}
              onChange={(e) =>
                onImpairmentsChange({
                  ...impairments,
                  reorder_enabled: e.target.checked,
                })
              }
            />
            Reordering
          </label>
          <SliderField
            label="Reorder window"
            unit="slots"
            value={impairments.reorder_window_size}
            max={32}
            onChange={(v) =>
              onImpairmentsChange({ ...impairments, reorder_window_size: v })
            }
            helper="How many messages can be held for out-of-order release."
            disabled={!impairments.reorder_enabled}
          />
          <div>
            <label className="flex items-center gap-2 text-sm text-cl-text">
              <input
                type="checkbox"
                className="rounded border-cl-subtle"
                checked={impairments.partition_enabled}
                onChange={(e) => {
                  const on = e.target.checked
                  const next: ImpairmentProfile = {
                    ...impairments,
                    partition_enabled: on,
                  }
                  // Engine requires partition_gap_ms > 0 when partitions are on (see ImpairmentEngine).
                  if (on && next.partition_gap_ms === 0) {
                    next.partition_gap_ms = 1
                  }
                  onImpairmentsChange(next)
                }}
              />
              Partition
            </label>
            {impairments.partition_enabled && (
              <p className="mt-1 pl-6 text-xs text-cl-text-muted">
                If gap was 0, it is set to 1 ms for the simulator. Tune gap and duration under
                Advanced.
              </p>
            )}
          </div>
        </div>
      </div>

      <details className="rounded-[var(--radius-control)] border border-dashed border-cl-subtle bg-cl-page/50 p-3 text-sm">
        <summary className="cursor-pointer font-medium text-cl-text-secondary">
          Advanced
        </summary>
        <div className="mt-3 flex flex-col gap-4">
          <SliderField
            label="Jitter variance"
            unit="ms"
            value={impairments.jitter_variance_ms}
            max={500}
            onChange={(v) =>
              onImpairmentsChange({ ...impairments, jitter_variance_ms: v })
            }
            helper="Spread of random jitter around the mean (0 = fixed jitter off)."
          />
          {impairments.partition_enabled && (
            <>
              <SliderField
                label="Partition gap"
                unit="ms"
                min={1}
                max={10000}
                value={Math.max(1, impairments.partition_gap_ms)}
                onChange={(v) =>
                  onImpairmentsChange({
                    ...impairments,
                    partition_gap_ms: Math.max(1, v),
                  })
                }
                helper="Time between partition windows (minimum 1 ms for the engine)."
              />
              <SliderField
                label="Partition duration"
                unit="ms"
                value={impairments.partition_duration_ms}
                max={120000}
                onChange={(v) =>
                  onImpairmentsChange({
                    ...impairments,
                    partition_duration_ms: v,
                  })
                }
                helper="How long each outage lasts once the gap elapses."
              />
            </>
          )}
        </div>
      </details>

      <div className="mt-auto flex flex-col gap-2 border-t border-cl-subtle pt-3">
        <button
          type="button"
          onClick={onApply}
          disabled={applyLoading}
          className="rounded-[var(--radius-control)] bg-cl-brand px-4 py-2 text-sm font-semibold text-white shadow-sm hover:opacity-95 disabled:cursor-not-allowed disabled:opacity-50"
        >
          {applyLoading ? 'Applying…' : 'Apply to engine'}
        </button>
        <button
          type="button"
          onClick={onSave}
          disabled={saveLoading}
          className="rounded-[var(--radius-control)] border border-cl-brand/30 py-2 text-sm font-medium text-cl-brand hover:bg-cl-brand-soft disabled:opacity-50"
        >
          {saveLoading ? 'Saving…' : 'Save as custom scenario'}
        </button>
      </div>
    </aside>
  )
}

function SliderField({
  label,
  unit,
  value,
  min = 0,
  max = 100,
  onChange,
  helper,
  disabled = false,
}: {
  label: string
  unit: string
  value: number
  min?: number
  max?: number
  onChange: (v: number) => void
  helper: string
  disabled?: boolean
}) {
  return (
    <div className={disabled ? 'opacity-50' : undefined}>
      <div className="mb-1 flex items-center justify-between gap-2">
        <span className="text-sm font-medium text-cl-text">{label}</span>
        <span className="font-mono-technical text-xs text-cl-text-secondary">
          {value} {unit}
        </span>
      </div>
      <input
        type="range"
        min={min}
        max={max}
        value={value}
        disabled={disabled}
        onChange={(e) => onChange(Number(e.target.value))}
        className="w-full accent-cl-brand disabled:cursor-not-allowed"
      />
      <p className="mt-1 text-xs text-cl-text-muted">{helper}</p>
    </div>
  )
}
