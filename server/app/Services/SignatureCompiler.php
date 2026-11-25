<?php

declare(strict_types=1);

namespace App\Services;

use App\Models\BlocklistEntry;
use App\Models\ResolvedPolicy;
use App\Models\Rule;
use App\Models\Setting;
use App\Models\SignatureVersion;
use Illuminate\Support\Facades\DB;
use RuntimeException;
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

    /**
     * Imports data/signatures/*.yaml into the rules table and returns the number of new rules.
     * Missing rules are inserted. Existing rules take the seed's definition and description only
     * while `edited` is false; a rule saved in the dashboard (edited) is never overwritten.
     */
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
                } elseif (! $rule->edited) {
                    $rule->update([
                        'description' => $r['description'] ?? $rule->description,
                        'definition' => $r,
                    ]);
                }
            }
            foreach (['probe', 'scan'] as $extra) {
                if (isset($doc[$extra])) {
                    Setting::put("signatures.$kind.$extra", $doc[$extra]);
                }
            }
        }
        // Browser AI site patterns and network hints (web.yaml) are lists, not rules: kept as one setting.
        if (is_file("$dir/web.yaml")) {
            $web = Yaml::parseFile("$dir/web.yaml") ?? [];
            unset($web['version']);
            Setting::put('signatures.web', $web);
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
        $bundle['ports']['probe'] = Setting::get('signatures.port.probe', ['enabled' => true,
            'paths' => ['/v1/models', '/api/tags'], 'severity' => 'high', 'min_rss_mb' => 300]);
        $bundle['artifacts']['scan'] = Setting::get('signatures.artifact.scan', ['roots' => ['~'], 'max_depth' => 6]);
        $web = Setting::get('signatures.web', []);
        if (is_array($web) && $web !== []) {
            $bundle['web'] = $web;
        }

        return $bundle;
    }

    /**
     * Refuses bundles that would blind every agent: an empty AI or IDE domain list makes the agent drop all
     * domain visibility, so such a bundle must never be signed or activated.
     *
     * @throws RuntimeException
     */
    public function assertPublishable(array $bundle): void
    {
        $missing = [];
        foreach (['ai', 'ide'] as $key) {
            $list = $bundle['domains'][$key] ?? null;
            if (! is_array($list) || $list === []) {
                $missing[] = "domains.$key";
            }
        }
        if ($missing) {
            throw new RuntimeException('Refusing to publish signatures: '.implode(' and ', $missing)
                .' is empty or missing. Import the blocklists first (php artisan xephyguard:bootstrap).');
        }
    }

    /**
     * Compiles, signs and activates a new version if the rule set changed.
     *
     * @throws RuntimeException when the compiled bundle fails validation (see assertPublishable)
     */
    public function publish(?int $userId = null, ?string $note = null): SignatureVersion
    {
        $bundle = $this->buildBundle();
        $this->assertPublishable($bundle);
        $digest = hash('sha256', json_encode($bundle, JSON_THROW_ON_ERROR));
        $current = SignatureVersion::active();
        if ($current && $current->sha256 === $digest) {
            return $current;
        }
        $bundle['version'] = now()->format('Y.m.d-His').'-'.substr($digest, 0, 6);
        $json = json_encode($bundle, JSON_UNESCAPED_SLASHES | JSON_THROW_ON_ERROR);
        // Sign before touching any row: a signing error leaves the current version active (I-SERVER-18).
        $signature = $this->ca->signData($json);

        return DB::transaction(function () use ($bundle, $json, $signature, $digest, $userId, $note) {
            self::lockVersions();
            // Re-check under the lock: a concurrent publish may have activated the same content already.
            $current = SignatureVersion::active();
            if ($current && $current->sha256 === $digest) {
                return $current;
            }
            SignatureVersion::query()->where('active', true)->update(['active' => false]);

            return SignatureVersion::create([
                'version' => $bundle['version'],
                'bundle' => $json,
                'signature' => $signature,
                'sha256' => $digest,
                'created_by' => $userId,
                'note' => $note,
                'active' => true,
            ]);
        });
    }

    /**
     * Makes an existing version the only active one (rollback). Refuses a version whose stored signature
     * does not verify against the current lab CA, since every agent would reject it.
     *
     * @throws RuntimeException
     */
    public function activate(SignatureVersion $version): void
    {
        if (! $this->ca->verifyData((string) $version->bundle, (string) $version->signature)) {
            throw new RuntimeException("Refusing to activate {$version->version}: its signature does not verify against the lab CA.");
        }
        DB::transaction(function () use ($version) {
            self::lockVersions();
            SignatureVersion::query()->whereKeyNot($version->getKey())->where('active', true)->update(['active' => false]);
            SignatureVersion::query()->whereKey($version->getKey())->update(['active' => true]);
            // Mass updates fire no model events, so invalidate the cached policies here.
            ResolvedPolicy::invalidateAll();
        });
        $version->refresh();
    }

    /** Serialises publish/activate: row locks on every version (and the gap after them on MySQL). */
    private static function lockVersions(): void
    {
        SignatureVersion::query()->lockForUpdate()->pluck('id');
    }
}
