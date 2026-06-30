#pragma once

// NetQualityMonitor — per-host network quality scoring table.
//
// Backs the network-aware remote-read priority: peers are scored by measured
// RTT (passive, sampled on real reads) and bandwidth (active probe), so that
// TIER2 (DataService::read_raw_compressed) can prefer better-connected replicas.
//
// Layering: pure network-layer data — keyed by host (CMString), no dependency on
// storage types. The consumer (DataService) reads score(host) and orders
// RemoteObjectInfo itself; this class does not know about replicas.
//
// Concurrency: one writer (the active-probe thread) + many readers (read-path
// score lookups under TIER2). shared_mutex fits the read-heavy profile.
//
// Cold start: every unknown host scores 0, so ranking falls back to the
// existing registration order — behavior is identical to before the feature.

#include <common/cpp/common_types.h>
#include <shared_mutex>
#include <chrono>

namespace fly {

class NetQualityMonitor {
public:
    static NetQualityMonitor& instance() {
        static NetQualityMonitor inst;
        return inst;
    }

    // Passive: record one RTT sample (ms) observed on a real remote read.
    void update_rtt(const CMString& host, double rtt_ms);
    // Active: record one bandwidth sample (Mbps) from a probe.
    void update_bandwidth(const CMString& host, double mbps);

    // Composite score for a host; higher is better. 0 for unknown or stale.
    double score(const CMString& host) const;

    // Test helper: shift a host's last-updated timestamp into the past by
    // `seconds` so ttl/expiry paths can be exercised.
    void age_entry_for_test(const CMString& host, int64_t seconds);

    void clear();

private:
    NetQualityMonitor() = default;

    struct PeerQuality {
        double rtt_ms_ = 0;          // EMA of RTT (ms)
        double bandwidth_mbps_ = 0;  // EMA of bandwidth (Mbps)
        bool has_rtt_ = false;
        bool has_bw_ = false;
        int64_t last_updated_sec_ = 0;
    };

    // EMA weight for a new sample: α on the new value, (1-α) on the old.
    static constexpr double kEmaAlpha = 0.3;

    mutable std::shared_mutex mtx_;
    CMUnorderedMap<CMString, PeerQuality> table_;
};

}  // namespace fly
