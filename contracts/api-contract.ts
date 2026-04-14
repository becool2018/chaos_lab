/**
 * Chaos Lab — API contract (V1). Dashboard boundary; backend maps messageEngine types here.
 * @see messageEngine/src/core/ImpairmentConfig.hpp, DeliveryEvent.hpp, DeliveryStats.hpp
 */

// --- Shared ---

export type RunState = "idle" | "running" | "paused" | "completed" | "failed";

export type EventFilterChip =
  | "all"
  | "retries"
  | "ack_timeouts"
  | "drops"
  | "reordering"
  | "partitions"
  | "responses";

/**
 * Aligns with ImpairmentConfig: probabilities 0..1, jitter mean+variance, partition duration+gap.
 */
export interface ImpairmentProfile {
  enabled?: boolean;
  loss_probability: number;
  fixed_latency_ms: number;
  jitter_mean_ms: number;
  jitter_variance_ms: number;
  duplication_probability: number;
  reorder_enabled: boolean;
  reorder_window_size: number;
  partition_enabled: boolean;
  partition_duration_ms: number;
  partition_gap_ms: number;
  prng_seed?: number;
}

export interface LinkKey {
  from_node_id: string;
  to_node_id: string;
}

export type PerLinkImpairments = Partial<Record<string, ImpairmentProfile>>;

export interface ScenarioPayload {
  id: string;
  name: string;
  preset_id?: string;
  base_impairments: ImpairmentProfile;
  per_link?: PerLinkImpairments;
  advanced?: Record<string, unknown>;
}

export interface TopologyNode {
  id: string;
  label?: string;
}

export interface TopologyLink {
  from: string;
  to: string;
}

export interface TopologyResponse {
  nodes: TopologyNode[];
  links: TopologyLink[];
}

export interface MetricsSnapshot {
  messages_sent: number;
  messages_delivered: number;
  retries: number;
  ack_timeouts: number;
  duplicates_dropped: number;
  expiry_drops: number;
  average_latency_ms: number;
  in_flight_messages: number;
}

export interface RunSummaryExport {
  scenario: ScenarioPayload;
  run_state: RunState;
  started_at: string;
  ended_at?: string;
  metrics: MetricsSnapshot;
}

// --- REST ---

export interface PresetSummary {
  id: string;
  name: string;
  description: string;
  impairments: ImpairmentProfile;
}

export type GetPresetsResponse = PresetSummary[];

export type GetCurrentScenarioResponse = ScenarioPayload | null;

export interface PostCurrentScenarioRequest {
  scenario: ScenarioPayload;
}

export type PostCurrentScenarioResponse = ScenarioPayload;

export interface PostScenarioSaveRequest {
  scenario: ScenarioPayload;
}

export type PostScenarioSaveResponse = ScenarioPayload;

export interface PostRunBody {
  /** Optional seed override for replay */
  prng_seed?: number;
}

export interface PostRunResponse {
  run_state: RunState;
}

export type GetRunSummaryResponse = RunSummaryExport;

export type GetTopologyResponse = TopologyResponse;

/** GET /api/health — liveness and basic runtime state for operators. */
export interface HealthResponse {
  ok: boolean;
  version: string;
  sim_ready: boolean;
  run_state: RunState;
}

// --- WebSocket /api/events ---

export type SimulationEventType =
  | "message_sent"
  | "message_delivered"
  | "send_fail"
  | "retry"
  | "ack_timeout"
  | "duplicate_drop"
  | "expiry_drop"
  | "misroute_drop"
  | "reordering"
  | "partition"
  | "response"
  | "run_state"
  | "metric_tick";

export interface SimulationEventBase {
  id: string;
  ts: string;
  type: SimulationEventType;
  correlation_id?: string;
  source?: string;
  destination?: string;
  summary: string;
  detail?: Record<string, unknown>;
}

export interface RunStateEvent extends SimulationEventBase {
  type: "run_state";
  run_state: RunState;
}

export type SimulationEvent = SimulationEventBase | RunStateEvent;

export interface WsClientMessage {
  action: "subscribe" | "set_filter";
  filter?: EventFilterChip;
}

export interface WsServerMessage {
  event: SimulationEvent;
}
