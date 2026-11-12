<?php

namespace App\Services;

use App\Models\BlocklistEntry;
use App\Models\Rule;
use App\Models\SignatureVersion;
use Symfony\Component\Yaml\Yaml;

/**
 * Builds the agent signature bundle from the `rules` table (seeded from data/signatures/*.yaml and
 * editable in the dashboard) and signs it with the CA key. Agents reject unsigned/altered bundles.
 */
class SignatureCompiler
{
    public const KINDS = ['processes' => 'process', 'extensions' => 'extension', 'ports' => 'port',
        'artifacts' => 'artifact', 'adapters' => 'adapter', 'devices' => 'device'];

    public function __construct(private readonly CertificateAuthority $ca) {}

    /** Imports data/signatures/*.yaml into the rules table (new rules only; edits are preserved). */
    public function importSeed(?string $dir = null): int
    {
        $dir ??= config('xephyguard.data_dir').'/signatures';
        $n = 0;
        foreach (self::KINDS as $file => $kind) {
            $path = "$dir/$file.yaml";
            if (! is_file($path)) {
                continue;
            }
            $doc = Yaml::parseFile($path) ?? [];
            foreach ($doc['rules'] ?? [] as $r) {
                if (empty($r['id'])) {
                    continue;
                }
                $rule = Rule::query()->where('rule_id', $r['id'])->first();
                if (! $rule) {
                    Rule::query()->create([
                        'rule_id' => $r['id'],
                        'kind' => $kind,
                        'severity' => $r['severity'] ?? 'medium',
                        'description' => $r['description'] ?? null,
                        'definition' => $r,
                        'enabled' => true,
                    ]);
                    $n++;
                } else {
                    $rule->update([
                        'description' => $r['description'] ?? $rule->description,
                        'definition' => $r,
                    ]);
                }
            }
            foreach (['probe', 'scan'] as $extra) {
                if (isset($doc[$extra])) {
                    \App\Models\Setting::put("signatures.$kind.$extra", $doc[$extra]);
                }
            }
        }

        return $n;
    }

    public function buildBundle(): array
    {
        $bundle = ['processes' => [], 'extensions' => [], 'ports' => ['rules' => []], 'artifacts' => ['rules' => []],
            'adapters' => [], 'devices' => [], 'domains' => ['ai' => [], 'ide' => []]];
        foreach (Rule::query()->where('enabled', true)->orderBy('rule_id')->get() as $rule) {
            $def = array_merge($rule->definition, ['id' => $rule->rule_id, 'severity' => $rule->severity]);
            if ($rule->description) {
                $def['description'] = $rule->description;
            }
            match ($rule->kind) {
                'process' => $bundle['processes'][] = $def,
                'extension' => $bundle['extensions'][] = $def,
                'port' => $bundle['ports']['rules'][] = $def,
                'artifact' => $bundle['artifacts']['rules'][] = $def,
                'adapter' => $bundle['adapters'][] = $def,
                'device' => $bundle['devices'][] = $def,
                default => null,
            };
        }
        foreach (BlocklistEntry::query()
            ->where('enabled', true)
            ->whereIn('category', ['ai', 'ide'])
            ->where('kind', 'domain')
            ->orderBy('value')
            ->get(['category', 'value']) as $entry) {
            $bundle['domains'][$entry->category][] = strtolower($entry->value);
        }
        $bundle['ports']['probe'] = \App\Models\Setting::get('signatures.port.probe', ['enabled' => true,
            'paths' => ['/v1/models', '/api/tags'], 'severity' => 'high', 'min_rss_mb' => 300]);
        $bundle['artifacts']['scan'] = \App\Models\Setting::get('signatures.artifact.scan', ['roots' => ['~'], 'max_depth' => 6]);

        return $bundle;
    }

    /** Compiles, signs and activates a new version if the rule set changed. */
    public function publish(?int $userId = null, ?string $note = null): SignatureVersion
    {
        $bundle = $this->buildBundle();
        $digest = hash('sha256', json_encode($bundle));
        $current = SignatureVersion::active();
        if ($current && $current->sha256 === $digest) {
            return $current;
        }
        $bundle['version'] = now()->format('Y.m.d-His').'-'.substr($digest, 0, 6);
        $json = json_encode($bundle, JSON_UNESCAPED_SLASHES);
        SignatureVersion::query()->update(['active' => false]);

        return SignatureVersion::create([
            'version' => $bundle['version'],
            'bundle' => $json,
            'signature' => $this->ca->signData($json),
            'sha256' => $digest,
            'created_by' => $userId,
            'note' => $note,
            'active' => true,
        ]);
    }
}
