import type {
  GetPresetsResponse,
  GetTopologyResponse,
  HealthResponse,
  PostRunResponse,
  RunSummaryExport,
  ScenarioPayload,
} from '@chaos-lab/contracts'

const JSON_HEADERS = { 'Content-Type': 'application/json' } as const

async function fetchJson<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(path, {
    ...init,
    headers: { ...JSON_HEADERS, ...init?.headers },
  })
  const text = await res.text()
  if (!res.ok) {
    throw new Error(`${res.status} ${res.statusText}: ${text.slice(0, 200)}`)
  }
  return JSON.parse(text) as T
}

export interface ScenarioCurrentResponse {
  scenario: ScenarioPayload
}

export async function apiGetPresets(): Promise<GetPresetsResponse> {
  return fetchJson<GetPresetsResponse>('/api/presets')
}

export async function apiGetScenario(): Promise<ScenarioCurrentResponse> {
  return fetchJson<ScenarioCurrentResponse>('/api/scenario/current')
}

export async function apiPostScenario(body: Record<string, unknown>): Promise<ScenarioCurrentResponse> {
  return fetchJson<ScenarioCurrentResponse>('/api/scenario/current', {
    method: 'POST',
    body: JSON.stringify(body),
  })
}

export async function apiPostScenarioSave(body: Record<string, unknown>): Promise<ScenarioCurrentResponse> {
  return fetchJson<ScenarioCurrentResponse>('/api/scenario/save', {
    method: 'POST',
    body: JSON.stringify(body),
  })
}

export async function apiGetRunSummary(): Promise<RunSummaryExport> {
  return fetchJson<RunSummaryExport>('/api/run/summary')
}

export async function apiGetTopology(): Promise<GetTopologyResponse> {
  return fetchJson<GetTopologyResponse>('/api/topology')
}

export async function apiGetHealth(): Promise<HealthResponse> {
  return fetchJson<HealthResponse>('/api/health')
}

export async function apiPostRunStart(): Promise<PostRunResponse> {
  return fetchJson<PostRunResponse>('/api/run/start', { method: 'POST', body: '{}' })
}

export async function apiPostRunPause(): Promise<PostRunResponse> {
  return fetchJson<PostRunResponse>('/api/run/pause', { method: 'POST', body: '{}' })
}

export async function apiPostRunReset(): Promise<PostRunResponse> {
  return fetchJson<PostRunResponse>('/api/run/reset', { method: 'POST', body: '{}' })
}

export async function apiPostRunReplay(): Promise<PostRunResponse> {
  return fetchJson<PostRunResponse>('/api/run/replay', { method: 'POST', body: '{}' })
}

export function apiWebSocketEventsUrl(): string {
  const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
  return `${proto}//${window.location.host}/api/events`
}
