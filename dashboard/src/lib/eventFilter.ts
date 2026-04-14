import type { EventFilterChip, SimulationEvent } from '@chaos-lab/contracts'

export function eventMatchesFilter(
  ev: SimulationEvent,
  chip: EventFilterChip,
): boolean {
  if (chip === 'all') return true
  const t = ev.type
  switch (chip) {
    case 'retries':
      return t === 'retry'
    case 'ack_timeouts':
      return t === 'ack_timeout'
    case 'drops':
      return (
        t === 'duplicate_drop' ||
        t === 'expiry_drop' ||
        t === 'misroute_drop' ||
        t === 'send_fail'
      )
    case 'reordering':
      return t === 'reordering'
    case 'partitions':
      return t === 'partition'
    case 'responses':
      return t === 'response'
    default:
      return true
  }
}
