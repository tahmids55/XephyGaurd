<?php

namespace App\Services;

use App\Models\BlocklistEntry;
use App\Models\LabSession;
use App\Models\Pc;
use App\Models\Setting;

/**
 * Writes the network policy state document consumed by the DNS / nftables / proxy sidecars
 * (proxy/xg_netpolicy). Called whenever sessions, modes, overrides, isolation or blocklists change.
 */
class NetworkPolicyWriter
{
    public const DEFAULT_INSPECT_HOSTS = ['google.com', 'www.google.com', 'google.co.uk', 'google.ca', 'google.com.au',
        'google.co.in', 'google.de', 'google.fr', 'bing.com', 'www.bing.com', 'huggingface.co', 'duckduckgo.com',
        'search.brave.com'];

    public function __construct(private readonly PolicyBuilder $policies) {}

    public function build(): array
    {
        $groups = ['test' => [], 'block_ai' => [], 'isolated' => [], 'exempt' => []];
        $groupCats = ['test' => [], 'block_ai' => []];
        $udm14 = true;
        foreach (Pc::query()->active()->whereNotNull('ip')->get() as $pc) {
            if ($pc->isolated) {
                $groups['isolated'][] = $pc->ip;
                continue;
            }
            ['profile' => $profile, 'session' => $session, 'override' => $override] = $this->policies->effective($pc);
            $aiPolicy = PolicyBuilder::resolveAiPolicy($profile, $session, $override);
            $net = $profile->network ?? [];
            $cats = array_values($net['block_categories'] ?? []);
            if (in_array($aiPolicy, ['permitted_with_disclosure', 'open'], true)) {
                $cats = array_values(array_diff($cats, ['ai', 'ide']));
            }
            if (! $session) {
                continue;
            }
            if ($override && ! $cats) {
                $groups['exempt'][] = $pc->ip;          // accommodation: profile without blocking
                continue;
            }
            if (! $cats) {
                continue;
            }
            $group = ($net['strict_egress'] ?? false) || $profile->mode === 'test' ? 'test' : 'block_ai';
            $groups[$group][] = $pc->ip;
            $groupCats[$group] = array_values(array_unique(array_merge($groupCats[$group], $cats)));
            $udm14 = $udm14 && ($net['google_udm14'] ?? true);
        }

        $categories = [];
        $paths = [];
        $ips = [];
        foreach (BlocklistEntry::query()->where('enabled', true)->get(['category', 'value', 'kind']) as $e) {
            match ($e->kind) {
                'path' => $paths[$e->category][] = $e->value,
                'ip' => $ips[] = $e->value,
                default => $categories[$e->category][] = $e->value,
            };
        }
        $version = (int) Setting::get('net_state_version', 0) + 1;
        $logUrls = Setting::get('privacy.log_urls', ['enabled' => false, 'query' => false]);

        return [
            'version' => $version,
            'generated_at' => now()->toIso8601String(),
            'network_mode' => Setting::get('network_mode'),
            'ccc_ip' => Setting::get('ccc_ip'),
            'lab_subnet' => Setting::get('lab_subnet'),
            'lan_if' => config('xephyguard.lan_if'),
            'wan_if' => config('xephyguard.wan_if'),
            'proxy_port' => config('xephyguard.proxy_port'),
            'agent_port' => config('xephyguard.agent_port'),
            'groups' => array_map(fn ($l) => array_values(array_unique($l)), $groups),
            'categories' => $categories,
            'path_rules' => $paths,
            'group_categories' => $groupCats,
            'always_block' => ['doh'],
            'doh_ips' => array_values(array_unique($ips)),
            'google' => ['force_udm14' => $udm14, 'hide_selectors' => Setting::get('google_hide_selectors', [])],
            'inspect_hosts' => Setting::get('inspect_hosts', self::DEFAULT_INSPECT_HOSTS),
            'log_urls' => $logUrls,
            'events_url' => rtrim(config('xephyguard.internal_url'), '/').'/api/internal/net-events',
            'events_token' => config('xephyguard.internal_token'),
        ];
    }

    public function write(): array
    {
        $state = $this->build();
        $path = config('xephyguard.net_state_path');
        @mkdir(dirname($path), 0755, true);
        $tmp = $path.'.tmp';
        file_put_contents($tmp, json_encode($state, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES));
        rename($tmp, $path);
        Setting::put('net_state_version', $state['version']);

        return $state;
    }
}
