import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import type { EventFilterChip, ImpairmentProfile, RunState } from '@chaos-lab/contracts'
import { useCallback, useEffect, useState, startTransition } from 'react'
import { useChaosLabEventStream } from '../../hooks/useChaosLabEventStream'
import {
  apiGetPresets,
  apiGetRunSummary,
  apiGetScenario,
  apiGetTopology,
  apiPostRunPause,
  apiPostRunReplay,
  apiPostRunReset,
  apiPostRunStart,
  apiPostScenario,
  apiPostScenarioSave,
} from '../../services/chaosLabApi'
import { DEFAULT_IMPAIRMENTS } from '../../lib/defaultImpairments'
import { EventTimeline } from '../timeline/EventTimeline'
import { MetricsStrip } from '../metrics/MetricsStrip'
import { NetworkMap } from '../topology/NetworkMap'
import { ScenarioPanel } from '../scenario/ScenarioPanel'
import { TopBar } from './TopBar'

export function DashboardShell() {
  const qc = useQueryClient()

  const presetsQuery = useQuery({
    queryKey: ['presets'],
    queryFn: apiGetPresets,
  })

  const scenarioQuery = useQuery({
    queryKey: ['scenario'],
    queryFn: async () => {
      const r = await apiGetScenario()
      return r.scenario
    },
  })

  const summaryQuery = useQuery({
    queryKey: ['summary'],
    queryFn: apiGetRunSummary,
    refetchInterval: (q) =>
      q.state.data?.run_state === 'running' ? 1000 : false,
  })

  const [impairments, setImpairments] = useState<ImpairmentProfile>(DEFAULT_IMPAIRMENTS)
  const [scenarioName, setScenarioName] = useState('Custom scenario')
  const [scenarioId, setScenarioId] = useState('default')
  const [apiBannerDismissed, setApiBannerDismissed] = useState(false)
  const [eventFilter, setEventFilter] = useState<EventFilterChip>('all')

  const topologyQuery = useQuery({
    queryKey: ['topology', scenarioId],
    queryFn: apiGetTopology,
  })

  useEffect(() => {
    const s = scenarioQuery.data
    if (!s) return
    startTransition(() => {
      setScenarioName(s.name)
      setScenarioId(s.id)
      setImpairments({ ...DEFAULT_IMPAIRMENTS, ...s.base_impairments })
    })
  }, [scenarioQuery.data])

  const runState: RunState = summaryQuery.data?.run_state ?? 'idle'

  const { events, connected, error: wsError, clear: clearEvents } =
    useChaosLabEventStream(true, eventFilter)

  const invalidateAll = useCallback(() => {
    void qc.invalidateQueries({ queryKey: ['summary'] })
    void qc.invalidateQueries({ queryKey: ['scenario'] })
    void qc.invalidateQueries({ queryKey: ['topology'] })
  }, [qc])

  const mutationApply = useMutation({
    mutationFn: (body: Record<string, unknown>) => apiPostScenario(body),
    onSuccess: invalidateAll,
  })

  const mutationSave = useMutation({
    mutationFn: (body: Record<string, unknown>) => apiPostScenarioSave(body),
    onSuccess: invalidateAll,
  })

  const mutationStart = useMutation({
    mutationFn: apiPostRunStart,
    onSuccess: invalidateAll,
  })
  const mutationPause = useMutation({
    mutationFn: apiPostRunPause,
    onSuccess: invalidateAll,
  })
  const mutationReset = useMutation({
    mutationFn: apiPostRunReset,
    onSuccess: () => {
      clearEvents()
      invalidateAll()
    },
  })
  const mutationReplay = useMutation({
    mutationFn: apiPostRunReplay,
    onSuccess: () => {
      clearEvents()
      invalidateAll()
    },
  })

  const apiError =
    presetsQuery.error?.message ??
    scenarioQuery.error?.message ??
    summaryQuery.error?.message ??
    topologyQuery.error?.message ??
    null

  useEffect(() => {
    if (!apiError) {
      startTransition(() => setApiBannerDismissed(false))
    }
  }, [apiError])

  return (
    <div className="flex min-h-dvh flex-col">
      {apiError && !apiBannerDismissed && (
        <div
          className="flex items-start gap-3 border-b border-amber-200 bg-amber-50 px-4 py-2 text-sm text-amber-950"
          role="status"
        >
          <div className="min-w-0 flex-1">
            API: {apiError} — start the sidecar (
            <code className="rounded bg-amber-100 px-1">./sidecar/build/chaos_lab_sidecar</code>
            ) and run <code className="rounded bg-amber-100 px-1">npm run dev</code>.
          </div>
          <button
            type="button"
            onClick={() => setApiBannerDismissed(true)}
            className="shrink-0 rounded-md border border-amber-300 bg-amber-100 px-2 py-1 text-xs font-medium text-amber-950 hover:bg-amber-200"
          >
            Dismiss
          </button>
        </div>
      )}

      <TopBar
        runState={runState}
        scenarioName={scenarioName}
        onScenarioNameChange={setScenarioName}
        onStart={() => mutationStart.mutate()}
        onPause={() => mutationPause.mutate()}
        onReset={() => mutationReset.mutate()}
        onReplay={() => mutationReplay.mutate()}
        onExport={() => {
          void (async () => {
            try {
              const s = await apiGetRunSummary()
              const blob = new Blob([JSON.stringify(s, null, 2)], {
                type: 'application/json',
              })
              const a = document.createElement('a')
              a.href = URL.createObjectURL(blob)
              a.download = `chaos-lab-run-${scenarioId}.json`
              a.click()
              URL.revokeObjectURL(a.href)
            } catch {
              /* handled by query */
            }
          })()
        }}
        busy={
          mutationStart.isPending ||
          mutationPause.isPending ||
          mutationReset.isPending ||
          mutationReplay.isPending ||
          mutationApply.isPending ||
          mutationSave.isPending
        }
      />

      <div className="grid min-h-0 flex-1 grid-cols-1 gap-3 p-4 lg:grid-cols-[minmax(260px,320px)_minmax(0,1fr)_minmax(280px,360px)] lg:gap-4">
        <ScenarioPanel
          className="order-2 lg:order-1"
          presets={presetsQuery.data}
          presetsLoading={presetsQuery.isLoading}
          impairments={impairments}
          onImpairmentsChange={setImpairments}
          onApply={() =>
            mutationApply.mutate({
              scenario: {
                id: scenarioId,
                name: scenarioName,
                base_impairments: impairments,
              },
            })
          }
          onSave={() =>
            mutationSave.mutate({
              scenario: {
                id: scenarioId,
                name: scenarioName,
                base_impairments: impairments,
              },
            })
          }
          applyLoading={mutationApply.isPending}
          saveLoading={mutationSave.isPending}
          onSelectPreset={(preset) => {
            const next = { ...DEFAULT_IMPAIRMENTS, ...preset.impairments }
            setImpairments(next)
            setScenarioName(preset.name)
            setScenarioId(preset.id)
            mutationApply.mutate({
              scenario: {
                id: preset.id,
                name: preset.name,
                base_impairments: next,
              },
            })
          }}
        />
        <NetworkMap
          className="order-1 min-h-[280px] lg:order-2 lg:min-h-0"
          topology={topologyQuery.data}
          loading={topologyQuery.isLoading}
        />
        <EventTimeline
          className="order-3 min-h-[220px] lg:min-h-0"
          events={events}
          wsConnected={connected}
          wsError={wsError}
          filter={eventFilter}
          onFilterChange={setEventFilter}
        />
      </div>

      <MetricsStrip metrics={summaryQuery.data?.metrics} />
    </div>
  )
}
