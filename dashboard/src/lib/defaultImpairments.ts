import type { ImpairmentProfile } from '@chaos-lab/contracts'

export const DEFAULT_IMPAIRMENTS: ImpairmentProfile = {
  loss_probability: 0,
  fixed_latency_ms: 0,
  jitter_mean_ms: 0,
  jitter_variance_ms: 0,
  duplication_probability: 0,
  reorder_enabled: false,
  reorder_window_size: 0,
  partition_enabled: false,
  partition_duration_ms: 0,
  partition_gap_ms: 0,
}
