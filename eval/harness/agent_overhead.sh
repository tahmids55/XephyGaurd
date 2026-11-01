#!/usr/bin/env bash
# Samples the running agent's CPU and RSS on a lab PC and compares with the acceptance targets
# (idle CPU < 1 %, RSS < 30 MB). Run on the PC:  sudo eval/harness/agent_overhead.sh 600
set -euo pipefail
DUR=${1:-300}
pid=$(pgrep -xo xephyguard-agent || pgrep -fo 'xephyguard-agent run')
tck=$(getconf CLK_TCK)
read -r u0 s0 < <(awk '{print $14, $15}' /proc/$pid/stat)
t0=$(date +%s.%N); max_rss=0; sum_rss=0; n=0
end=$(( $(date +%s) + DUR ))
while [ "$(date +%s)" -lt "$end" ]; do
  rss=$(awk '/VmRSS/{print $2}' /proc/$pid/status); n=$((n+1)); sum_rss=$((sum_rss+rss))
  [ "$rss" -gt "$max_rss" ] && max_rss=$rss
  sleep 5
done
read -r u1 s1 < <(awk '{print $14, $15}' /proc/$pid/stat)
t1=$(date +%s.%N)
cpu=$(echo "scale=3; 100 * (($u1 + $s1) - ($u0 + $s0)) / $tck / ($t1 - $t0)" | bc)
printf '{"seconds": %s, "cpu_pct": %s, "rss_mb_avg": %s, "rss_mb_max": %s, "targets": {"cpu_pct": 1, "rss_mb": 30}}\n' \
  "$DUR" "$cpu" "$(echo "scale=1; $sum_rss / $n / 1024" | bc)" "$(echo "scale=1; $max_rss / 1024" | bc)"
