import type { EventFilterChip, SimulationEvent, SimulationEventType } from '@chaos-lab/contracts'
import { useCallback, useEffect, useRef, useState } from 'react'
import { apiWebSocketEventsUrl } from '../services/chaosLabApi'

const MAX_EVENTS = 400

interface WsEnvelope {
  event?: {
    id?: string
    ts?: string
    type?: string
    summary?: string
    source?: string
    destination?: string
    detail?: Record<string, unknown>
  }
}

function normalizeEvent(raw: WsEnvelope['event']): SimulationEvent | null {
  if (!raw?.type && raw?.id === undefined && raw?.summary === undefined) {
    return null
  }
  const t = (raw.type ?? 'message_sent') as SimulationEventType
  const ev: SimulationEvent = {
    id: String(raw.id ?? ''),
    ts: String(raw.ts ?? ''),
    type: t,
    summary: raw.summary ?? '',
    source: raw.source,
    destination: raw.destination,
    detail: raw.detail,
  }
  return ev
}

function sendSetFilter(ws: WebSocket, filter: EventFilterChip) {
  if (ws.readyState !== WebSocket.OPEN) return
  ws.send(JSON.stringify({ action: 'set_filter', filter }))
}

export function useChaosLabEventStream(enabled: boolean, filter: EventFilterChip) {
  const [events, setEvents] = useState<SimulationEvent[]>([])
  const [socketConnected, setSocketConnected] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const wsRef = useRef<WebSocket | null>(null)
  const filterRef = useRef<EventFilterChip>(filter)

  useEffect(() => {
    filterRef.current = filter
  }, [filter])

  const clear = useCallback(() => {
    setEvents([])
  }, [])

  useEffect(() => {
    if (!enabled) {
      wsRef.current?.close()
      wsRef.current = null
      return
    }

    const url = apiWebSocketEventsUrl()
    let ws: WebSocket
    try {
      ws = new WebSocket(url)
    } catch (e) {
      const msg = e instanceof Error ? e.message : 'WebSocket failed'
      queueMicrotask(() => setError(msg))
      return
    }

    wsRef.current = ws
    queueMicrotask(() => setError(null))

    ws.onopen = () => {
      setSocketConnected(true)
      sendSetFilter(ws, filterRef.current)
    }
    ws.onclose = () => {
      setSocketConnected(false)
    }
    ws.onerror = () => {
      setError('WebSocket error')
    }
    ws.onmessage = (ev) => {
      try {
        const data = JSON.parse(ev.data as string) as WsEnvelope
        const normalized = normalizeEvent(data.event)
        if (!normalized) return
        setEvents((prev) => {
          const next = [normalized, ...prev]
          return next.length > MAX_EVENTS ? next.slice(0, MAX_EVENTS) : next
        })
      } catch {
        // ignore malformed frames
      }
    }

    return () => {
      ws.close()
      wsRef.current = null
      queueMicrotask(() => setSocketConnected(false))
    }
  }, [enabled])

  useEffect(() => {
    const ws = wsRef.current
    if (!ws || !enabled) return
    sendSetFilter(ws, filter)
  }, [filter, enabled])

  return {
    events,
    connected: enabled && socketConnected,
    error,
    clear,
  }
}
