<?php

declare(strict_types=1);

namespace App\Services\Ingest;

/**
 * Field allow-list per agent event type and for the heartbeat (S-28, DECISIONS #27, docs/PROTOCOL.md "Event types").
 * Fields that are not listed, or have the wrong type, are dropped and never stored; strings are cut to their limit.
 * Specs: int, num, bool, id (int or string ≤ 20), str:N, scalar:N, strs:N:M (≤ M strings of ≤ N chars),
 * map:M (≤ M scalar entries, keys ≤ 32 chars), map2:M (map whose values may also be one map), @name and @name[]:M
 * (nested object / list of ≤ M objects with schema NESTED[name]).
 */
final class EventSchema
{
    public const TYPES = [
        'heartbeat' => self::HEARTBEAT,
        'agent_started' => ['version' => 'str:32', 'previous_exit' => 'str:16', 'os' => 'str:128', 'hostname' => 'str:255'],
        'agent_stopping' => ['reason' => 'str:32', 'uptime_s' => 'int'],
        'process_start' => ['pid' => 'int', 'ppid' => 'int', 'exe' => 'str:256', 'rss_mb' => 'num', 'rule_id' => 'str:64'],
        'process_stop' => ['pid' => 'int', 'rule_id' => 'str:64'],
        'process_snapshot' => ['hash' => 'str:64', 'count' => 'int', 'total_running' => 'int', 'added' => 'strs:64:200',
            'removed' => 'strs:64:200', 'list' => '@proc[]:250'],
        'rule_hit' => ['rule_id' => 'str:64', 'severity' => 'str:16', 'category' => 'str:32', 'description' => 'str:512',
            'subject' => 'str:512', 'actions' => 'strs:16:8', 'result' => '@result', 'context' => '@context',
            'mode' => 'str:16', 'session_id' => 'id'],
        'listening_port' => ['port' => 'int', 'address' => 'str:64', 'process' => 'str:128', 'pid' => 'int',
            'rule_id' => 'str:64', 'probed' => 'bool'],
        'window_focus' => ['app' => 'str:64', 'title' => 'str:256', 'ai_site' => 'str:128'],
        'paste' => self::PASTE,
        'copy' => self::PASTE,
        'key_stats' => ['window_start' => 'int', 'window_s' => 'int', 'keys' => 'int', 'backspaces' => 'int',
            'backspace_ratio' => 'num', 'iki_mean' => 'num', 'iki_median' => 'num', 'iki_p90' => 'num', 'iki_std' => 'num',
            'bursts' => 'int', 'max_pause' => 'int', 'paste_chords' => 'int', 'source' => 'str:16'],
        'net_adapter' => ['change' => 'str:16', 'name' => 'str:128', 'description' => 'str:128', 'driver' => 'str:64',
            'mac' => 'str:32', 'addresses' => 'strs:64:32', 'up' => 'bool', 'wireless' => 'bool', 'virtual' => 'bool',
            'default_route' => 'bool', 'kind' => 'str:64', 'initial' => 'bool'],
        'device' => ['action' => 'str:16', 'subsystem' => 'str:32', 'class' => 'str:32', 'vendor' => 'str:16',
            'product' => 'str:16', 'model' => 'str:128', 'serial_hash' => 'str:64', 'rule_id' => 'str:64'],
        'extension_scan' => ['count' => 'int', 'hash' => 'str:64', 'ai' => '@ext[]:300', 'added' => 'strs:256:300',
            'removed' => 'strs:256:300', 'initial' => 'bool'],
        'artifact_scan' => ['count' => 'int', 'files_visited' => 'int', 'truncated' => 'bool', 'found' => '@artifact[]:500'],
        'file_activity' => ['dir' => 'str:256', 'saves' => 'int', 'created' => 'int', 'deleted' => 'int',
            'bytes_delta' => 'int', 'total_bytes' => 'int', 'exts' => 'strs:16:10', 'first_ts' => 'int'],
        'gpu_sample' => ['util_pct' => 'num', 'mem_used_mb' => 'num', 'mem_total_mb' => 'num', 'gpu' => 'str:128'],
        'tamper' => ['kind' => 'str:32', 'detail' => 'str:256', 'path' => 'str:512', 'expected' => 'str:128',
            'actual' => 'str:128', 'pinned' => 'bool', 'delta_ms' => 'int', 'at' => 'int', 'bundle_version' => 'str:64'],
        'capabilities' => ['caps' => 'map2:64'],
        'command_result' => ['command_id' => 'str:64', 'status' => 'str:16', 'detail' => 'map2:32'],
        'helper_status' => ['connected' => 'bool', 'pid' => 'int', 'session' => 'str:64', 'display' => 'str:64',
            'caps' => 'map2:64', 'helper_version' => 'int'],
    ];

    /** Every event may carry the login name (stored in events.user). */
    private const COMMON = ['user' => 'str:64'];

    public const HEARTBEAT = ['agent_version' => 'str:32', 'uptime_s' => 'int', 'agent_uptime_s' => 'int', 'mode' => 'str:16',
        'session_id' => 'id', 'policy_version' => 'id', 'signatures_version' => 'str:64', 'user' => 'str:64',
        'sessions' => 'strs:96:32', 'clock_offset_ms' => 'int', 'local_time_ms' => 'int', 'queue_depth' => 'int',
        'helpers' => 'int', 'realtime' => 'bool', 'suppressed_hits' => 'int', 'core_dropped' => 'int',
        'network_lock' => 'str:8', 'capabilities' => 'map2:64', 'usage' => '@usage'];

    private const PASTE = ['chars' => 'int', 'lines' => 'int', 'code' => 'bool', 'score' => 'num', 'src' => 'str:128',
        'dst' => 'str:128'];

    private const NESTED = [
        'proc' => ['pid' => 'int', 'ppid' => 'int', 'exe' => 'str:64', 'name' => 'str:64', 'user' => 'str:32',
            'rss_mb' => 'num', 'cpu_pct' => 'num', 'net_kbps' => 'num', 'net_conns' => 'int', 'start' => 'int',
            'rule_id' => 'str:64'],
        'result' => ['kill' => '@kill'],
        'kill' => ['pid' => 'int', 'killed' => 'bool', 'reason' => 'str:32'],
        // Union of the contexts the agent sends with a rule_hit (agent.cpp handle_rule_hit callers).
        'context' => ['exe' => 'scalar:256', 'pid' => 'int', 'user' => 'str:64', 'port' => 'int', 'adapter' => 'str:128',
            'driver' => 'str:64', 'initial' => 'bool', 'default_routes' => 'int', 'class' => 'str:32', 'vendor' => 'str:16',
            'model' => 'str:128', 'process' => 'str:256', 'domain' => 'str:255', 'remote_ip' => 'str:64',
            'remote_port' => 'int', 'sent_kb' => 'num', 'recv_kb' => 'num', 'signal' => 'str:32', 'ide' => 'str:32',
            'extension' => 'str:256', 'version' => 'str:64', 'activity' => 'str:64', 'path' => 'str:512',
            'size_mb' => 'num', 'rule_id' => 'str:64', 'app' => 'str:64', 'title' => 'str:256', 'ai_site' => 'str:128'],
        'ext' => ['ide' => 'str:32', 'id' => 'str:256', 'version' => 'str:64', 'user' => 'str:64', 'rule_id' => 'str:64',
            'description' => 'str:512', 'active' => 'bool', 'activity' => 'str:64'],
        'artifact' => ['path' => 'str:512', 'size_mb' => 'num', 'rule_id' => 'str:64', 'description' => 'str:512'],
        'usage' => ['rss_kb' => 'int', 'cpu_pct' => 'num', 'bytes_up' => 'int', 'screen' => 'map:8'],
    ];

    /** The allow-listed copy of one event's data. */
    public static function clean(string $type, array $data): array
    {
        return self::object($data, (self::TYPES[$type] ?? []) + self::COMMON);
    }

    public static function heartbeat(array $hb): array
    {
        return self::object($hb, self::HEARTBEAT);
    }

    private static function object(array $in, array $schema): array
    {
        $out = [];
        foreach ($schema as $key => $spec) {
            if (array_key_exists($key, $in) && ($v = self::value($in[$key], $spec)) !== null) {
                $out[$key] = $v;
            }
        }

        return $out;
    }

    private static function value(mixed $v, string $spec): mixed
    {
        $p = explode(':', $spec);
        if (str_starts_with($p[0], '@')) {
            $list = str_ends_with($p[0], '[]');
            $schema = self::NESTED[rtrim(substr($p[0], 1), '[]')];
            if (! is_array($v) || ($list && ! array_is_list($v))) {
                return null;
            }
            if (! $list) {
                return self::object($v, $schema);
            }

            return array_values(array_map(fn ($o) => self::object($o, $schema),
                array_filter(array_slice($v, 0, (int) $p[1]), 'is_array')));
        }

        return match ($p[0]) {
            'int' => is_int($v) ? $v : null,
            'num' => (is_int($v) || is_float($v)) && is_finite((float) $v) ? $v : null,
            'bool' => is_bool($v) ? $v : null,
            'id' => is_int($v) ? $v : (is_string($v) ? mb_substr($v, 0, 20) : null),
            'str' => is_string($v) ? mb_substr($v, 0, (int) $p[1]) : null,
            'scalar' => is_string($v) ? mb_substr($v, 0, (int) $p[1]) : (is_scalar($v) ? $v : null),
            'strs' => is_array($v) ? array_values(array_map(fn ($s) => mb_substr($s, 0, (int) $p[1]),
                array_filter(array_slice(array_values($v), 0, (int) $p[2]), 'is_string'))) : null,
            'map', 'map2' => is_array($v) ? self::map($v, (int) $p[1], $p[0] === 'map2') : null,
        };
    }

    private static function map(array $v, int $max, bool $nested): array
    {
        $out = [];
        foreach (array_slice($v, 0, $max, true) as $k => $x) {
            if (! is_string($k) || mb_strlen($k) > 32) {
                continue;
            }
            if (is_string($x)) {
                $out[$k] = mb_substr($x, 0, 128);
            } elseif (is_scalar($x)) {
                $out[$k] = $x;
            } elseif ($nested && is_array($x)) {
                $out[$k] = self::map($x, $max, false);
            }
        }

        return $out;
    }
}
