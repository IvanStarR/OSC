#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace secmem {

struct Hist {
  std::vector<double>   bounds;
  std::vector<uint64_t> buckets;
  uint64_t              count{0};
  double                sum{0.0};

  Hist();
  explicit Hist(std::vector<double> b);

  void observe_locked(double v);

  void snapshot_locked(std::vector<uint64_t>& out_buckets,
                       uint64_t& out_count,
                       double& out_sum) const;
};

class Metrics {
public:
  Metrics();

  void inc_op(const std::string& op);                 
  void inc_error(const std::string& type);            
  void observe_latency(const std::string& op, double seconds); 
  void set_secrets_gauge(uint64_t val);               
  std::string render_prometheus() const;            

private:
  Hist& hist_for(const std::string& op);
  const Hist& hist_for_c(const std::string& op) const;

  mutable std::mutex mu_;

  std::unordered_map<std::string, uint64_t> ops_;
  std::unordered_map<std::string, uint64_t> errors_;

  Hist hist_put_;
  Hist hist_get_;
  Hist hist_del_;

  uint64_t secrets_gauge_{0};
};

} // namespace secmem
