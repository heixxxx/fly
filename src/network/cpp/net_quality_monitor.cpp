#include <network/cpp/net_quality_monitor.h>
#include <algorithm>
#include <mutex>

namespace fly {

namespace {

// Scoring weights and expiry. Compile-time constants: network layer has no
// Config dependency by design, and these values rarely need tuning.
constexpr double kRttWeight = 10.0;     // RTT dominates — latency gates reads
constexpr double kBwWeight = 1.0;       // bandwidth is a secondary tiebreaker
constexpr int64_t kTtlSec = 300;        // entries older than this score 0

constexpr double kMinRtt = 1.0;         // floor to avoid divide blow-up

int64_t now_sec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

void NetQualityMonitor::update_rtt(const CMString& host, double rtt_ms) {
    if (rtt_ms <= 0) return;
    std::unique_lock<std::shared_mutex> lk(mtx_);
    auto& q = table_[host];
    q.rtt_ms_ = q.has_rtt_ ? (kEmaAlpha * rtt_ms + (1.0 - kEmaAlpha) * q.rtt_ms_)
                           : rtt_ms;
    q.has_rtt_ = true;
    q.last_updated_sec_ = now_sec();
}

void NetQualityMonitor::update_bandwidth(const CMString& host, double mbps) {
    if (mbps <= 0) return;
    std::unique_lock<std::shared_mutex> lk(mtx_);
    auto& q = table_[host];
    q.bandwidth_mbps_ =
        q.has_bw_ ? (kEmaAlpha * mbps + (1.0 - kEmaAlpha) * q.bandwidth_mbps_)
                  : mbps;
    q.has_bw_ = true;
    q.last_updated_sec_ = now_sec();
}

double NetQualityMonitor::score(const CMString& host) const {
    std::shared_lock<std::shared_mutex> lk(mtx_);
    auto it = table_.find(host);
    if (it == table_.end()) return 0.0;
    const auto& q = it->second;
    if (q.last_updated_sec_ != 0 && now_sec() - q.last_updated_sec_ > kTtlSec) {
        return 0.0;  // stale → treat as unknown
    }
    double s = 0.0;
    if (q.has_rtt_) s += kRttWeight / std::max(q.rtt_ms_, kMinRtt);
    if (q.has_bw_) s += kBwWeight * q.bandwidth_mbps_;
    return s;
}

void NetQualityMonitor::age_entry_for_test(const CMString& host, int64_t seconds) {
    std::unique_lock<std::shared_mutex> lk(mtx_);
    auto it = table_.find(host);
    if (it == table_.end()) return;
    it->second.last_updated_sec_ = now_sec() - seconds;
}

void NetQualityMonitor::clear() {
    std::unique_lock<std::shared_mutex> lk(mtx_);
    table_.clear();
}

}  // namespace fly
