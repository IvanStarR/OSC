#include "secmem/metrics.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace secmem {

// ---------------------
// Hist implementation
// ---------------------

static std::vector<double> default_bounds_seconds() {
  // Экспоненциальная сетка для латентности (в секундах): ~0.5ms … 10s
  return {
      0.0005, 0.001, 0.002, 0.005,
      0.010,  0.020, 0.050,
      0.100,  0.200, 0.500,
      1.0,    2.0,   5.0, 10.0
  };
}

Hist::Hist() : Hist(default_bounds_seconds()) {}

Hist::Hist(std::vector<double> b)
    : bounds(std::move(b)),
      buckets(bounds.size() + 1, 0),
      count(0),
      sum(0.0) {
  // Гарантируем упорядоченность границ.
  std::sort(bounds.begin(), bounds.end());
}

// Найти индекс бакета для значения v.
static inline std::size_t bucket_index(const std::vector<double>& bounds, double v) {
  // Первый бакет, чья граница >= v.
  auto it = std::lower_bound(bounds.begin(), bounds.end(), v);
  if (it == bounds.end()) {
    return bounds.size(); // +Inf бакет
  }
  return static_cast<std::size_t>(std::distance(bounds.begin(), it));
}

void Hist::observe_locked(double v) {
  const std::size_t idx = bucket_index(bounds, v);
  buckets[idx] += 1;
  count += 1;
  sum += v;
}

void Hist::snapshot_locked(std::vector<uint64_t>& out_buckets,
                           uint64_t& out_count,
                           double& out_sum) const {
  out_buckets = buckets;  // копия raw-бакетов
  out_count   = count;
  out_sum     = sum;
}

// ---------------------
// Metrics implementation
// ---------------------

Metrics::Metrics()
    : hist_put_(default_bounds_seconds()),
      hist_get_(default_bounds_seconds()),
      hist_del_(default_bounds_seconds()) {}

void Metrics::inc_op(const std::string& op) {
  std::lock_guard<std::mutex> lk(mu_);
  ops_[op] += 1;
}

void Metrics::inc_error(const std::string& type) {
  std::lock_guard<std::mutex> lk(mu_);
  errors_[type] += 1;
}

void Metrics::observe_latency(const std::string& op, double seconds) {
  std::lock_guard<std::mutex> lk(mu_);
  hist_for(op).observe_locked(seconds);
}

void Metrics::set_secrets_gauge(uint64_t val) {
  std::lock_guard<std::mutex> lk(mu_);
  secrets_gauge_ = val;
}

Hist& Metrics::hist_for(const std::string& op) {
  if (op == "put" || op == "PUT" || op == "Put") return hist_put_;
  if (op == "get" || op == "GET" || op == "Get") return hist_get_;
  if (op == "del" || op == "DEL" || op == "Del" || op == "delete") return hist_del_;
  // По умолчанию — в "get".
  return hist_get_;
}

const Hist& Metrics::hist_for_c(const std::string& op) const {
  if (op == "put" || op == "PUT" || op == "Put") return hist_put_;
  if (op == "get" || op == "GET" || op == "Get") return hist_get_;
  if (op == "del" || op == "DEL" || op == "Del" || op == "delete") return hist_del_;
  return hist_get_;
}

static void render_counter_block(std::ostringstream& os,
                                 const std::string& metric_name,
                                 const std::string& help,
                                 const std::string& label_key,
                                 const std::unordered_map<std::string, uint64_t>& kv) {
  os << "# HELP " << metric_name << " " << help << "\n";
  os << "# TYPE " << metric_name << " counter\n";
  for (const auto& [label, val] : kv) {
    os << metric_name << "{" << label_key << "=\"" << label << "\"} " << val << "\n";
  }
}

static void render_gauge(std::ostringstream& os,
                         const std::string& metric_name,
                         const std::string& help,
                         uint64_t val) {
  os << "# HELP " << metric_name << " " << help << "\n";
  os << "# TYPE " << metric_name << " gauge\n";
  os << metric_name << " " << val << "\n";
}

static void render_hist(std::ostringstream& os,
                        const std::string& metric_base, // напр., secmem_latency_seconds
                        const std::string& op_label,
                        const Hist& h) {
  // Преобразуем raw бакеты в cumulative, как требует Prometheus.
  std::vector<uint64_t> raw; uint64_t cnt = 0; double sum = 0.0;
  h.snapshot_locked(raw, cnt, sum);

  std::vector<uint64_t> cum(raw.size(), 0);
  uint64_t running = 0;
  for (std::size_t i = 0; i < raw.size(); ++i) {
    running += raw[i];
    cum[i] = running;
  }

  // buckets
  os << "# TYPE " << metric_base << "_bucket histogram\n";
  for (std::size_t i = 0; i < h.bounds.size(); ++i) {
    os << metric_base << "_bucket{op=\"" << op_label << "\",le=\""
       << std::setprecision(6) << std::fixed << h.bounds[i]
       << "\"} " << cum[i] << "\n";
  }
  // +Inf bucket — последний элемент cum
  os << metric_base << "_bucket{op=\"" << op_label << "\",le=\"+Inf\"} "
     << cum.back() << "\n";

  // sum & count
  os << metric_base << "_sum{op=\"" << op_label << "\"} "
     << std::setprecision(9) << std::fixed << sum << "\n";
  os << metric_base << "_count{op=\"" << op_label << "\"} " << cnt << "\n";
}

std::string Metrics::render_prometheus() const {
  std::lock_guard<std::mutex> lk(mu_);
  std::ostringstream os;

  // Счётчики операций
  render_counter_block(os, "secmem_ops_total", "Total operations", "op", ops_);

  // Счётчики ошибок
  render_counter_block(os, "secmem_errors_total", "Total errors", "type", errors_);

  // Gauge
  render_gauge(os, "secmem_secrets_gauge", "Current number of secrets", secrets_gauge_);

  // Гистограммы латентности по операциям
  os << "# HELP secmem_latency_seconds Request latency in seconds\n";
  render_hist(os, "secmem_latency_seconds", "put", hist_put_);
  render_hist(os, "secmem_latency_seconds", "get", hist_get_);
  render_hist(os, "secmem_latency_seconds", "del", hist_del_);

  return os.str();
}

} // namespace secmem
