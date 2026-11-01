<?php

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Models\BlocklistEntry;
use App\Models\Pc;
use App\Models\Rule;
use App\Models\SignatureVersion;
use App\Services\Audit;
use App\Services\BlocklistService;
use App\Services\CommandBus;
use App\Services\NetworkPolicyWriter;
use App\Services\SignatureCompiler;
use Illuminate\Http\Request;

/** Blocklist and signature editor with version history and hot reload to proxy, DNS and agents. */
class SignatureController extends Controller
{
    public function index(Request $request)
    {
        $cat = $request->string('category')->toString();

        return view('signatures.index', [
            'entries' => BlocklistEntry::query()->when($cat, fn ($q) => $q->where('category', $cat))
                ->when($request->string('q')->toString(), fn ($q, $s) => $q->where('value', 'like', "%$s%"))
                ->orderBy('category')->orderBy('value')->paginate(100)->withQueryString(),
            'counts' => BlocklistEntry::query()->selectRaw('category, count(*) as n')->groupBy('category')->pluck('n', 'category'),
            'rules' => Rule::query()->orderBy('kind')->orderBy('rule_id')->get(),
            'versions' => SignatureVersion::query()->latest('id')->limit(15)->get(['id', 'version', 'active', 'note', 'created_at', 'sha256']),
            'category' => $cat,
        ]);
    }

    public function addEntry(Request $request, BlocklistService $svc, NetworkPolicyWriter $net)
    {
        $d = $request->validate(['category' => 'required|in:'.implode(',', BlocklistEntry::CATEGORIES),
            'value' => 'required|string|max:255', 'note' => 'nullable|string|max:255']);
        $n = $svc->import($d['category'], $d['value'], 'manual', $request->user()->id);
        abort_if($n === 0, 422, 'Not a valid domain or host/path rule (or already listed).');
        Audit::log('blocklist.add', null, $d);
        $net->write();

        return back()->with('status', 'Entry added; DNS and proxy reload automatically.');
    }

    public function import(Request $request, BlocklistService $svc, NetworkPolicyWriter $net)
    {
        $d = $request->validate(['category' => 'required|in:'.implode(',', BlocklistEntry::CATEGORIES),
            'file' => 'required|file|max:4096', 'source' => 'nullable|string|max:24']);
        $n = $svc->import($d['category'], $request->file('file')->get(), 'import:'.($d['source'] ?? 'file'), $request->user()->id);
        Audit::log('blocklist.import', null, ['category' => $d['category'], 'added' => $n]);
        $net->write();

        return back()->with('status', "$n entries imported.");
    }

    public function toggleEntry(BlocklistEntry $entry, NetworkPolicyWriter $net)
    {
        $entry->update(['enabled' => ! $entry->enabled]);
        Audit::log('blocklist.toggle', $entry, ['value' => $entry->value, 'enabled' => $entry->enabled]);
        $net->write();

        return back();
    }

    public function deleteEntry(BlocklistEntry $entry, NetworkPolicyWriter $net)
    {
        Audit::log('blocklist.delete', $entry, $entry->only(['category', 'value']));
        $entry->delete();
        $net->write();

        return back();
    }

    public function saveRule(Request $request)
    {
        $d = $request->validate([
            'rule_id' => ['required', 'regex:/^[a-z0-9_.-]{3,64}$/'], 'kind' => 'required|in:process,extension,port,artifact,adapter,device',
            'severity' => 'required|in:info,low,medium,high,critical', 'description' => 'nullable|string|max:255',
            'definition' => 'required|json', 'enabled' => 'nullable|boolean',
            'test_actions' => 'nullable|array', 'test_actions.*' => 'in:log,warn,kill,alert,isolate',
        ]);
        $rule = Rule::query()->updateOrCreate(['rule_id' => $d['rule_id']], [
            'kind' => $d['kind'], 'severity' => $d['severity'], 'description' => $d['description'],
            'definition' => json_decode($d['definition'], true), 'enabled' => (bool) ($d['enabled'] ?? true),
            'actions' => ! empty($d['test_actions']) ? ['test' => array_values($d['test_actions'])] : null,
        ]);
        Audit::log('rule.save', $rule, ['rule_id' => $rule->rule_id]);

        return back()->with('status', 'Rule saved. Press "Publish" to push it to agents.');
    }

    public function toggleRule(Rule $rule)
    {
        $rule->update(['enabled' => ! $rule->enabled]);
        Audit::log('rule.toggle', $rule, ['enabled' => $rule->enabled]);

        return back();
    }

    /** Compile + sign a new bundle and tell every agent to refresh (hot reload). */
    public function publish(Request $request, SignatureCompiler $compiler, CommandBus $bus)
    {
        $v = $compiler->publish($request->user()->id, $request->string('note')->toString() ?: null);
        $n = $bus->sendMany(Pc::query()->active()->get(), 'policy_updated');
        Audit::log('signatures.publish', $v, ['version' => $v->version, 'agents' => $n]);

        return back()->with('status', "Published {$v->version}; $n agents notified.");
    }

    public function activate(SignatureVersion $version, CommandBus $bus)
    {
        SignatureVersion::query()->update(['active' => false]);
        $version->update(['active' => true]);
        $bus->sendMany(Pc::query()->active()->get(), 'policy_updated');
        Audit::log('signatures.rollback', $version, ['version' => $version->version]);

        return back()->with('status', "Rolled back to {$version->version}.");
    }
}
