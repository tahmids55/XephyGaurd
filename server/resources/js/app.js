import Alpine from 'alpinejs';
import './echo';

const csrf = () => document.querySelector('meta[name="csrf-token"]').content;

async function api(url, opts = {}) {
    const res = await fetch(url, {
        credentials: 'same-origin',
        ...opts,
        headers: { Accept: 'application/json', 'Content-Type': 'application/json', 'X-CSRF-TOKEN': csrf(), ...(opts.headers || {}) },
    });
    if (!res.ok) throw new Error((await res.json().catch(() => ({}))).message || res.statusText);
    return res.json();
}
window.xgApi = api;

/* Toast notifications (alerts arriving in real time). */
Alpine.store('toasts', {
    items: [],
    push(t) {
        const id = Math.random();
        this.items.push({ id, ...t });
        setTimeout(() => (this.items = this.items.filter((i) => i.id !== id)), 8000);
    },
});

/* Live lab view: seat-map grid, real-time tiles, detail drawer, drag-and-drop layout editor. */
Alpine.data('labView', (cfg) => ({
    labId: cfg.labId,
    cols: cfg.cols,
    rows: cfg.rows,
    pcs: Object.fromEntries(cfg.pcs.map((p) => [p.id, p])),
    seats: cfg.seats,
    alerts: cfg.alerts,
    placement: {},          // "x,y" -> pc id
    editing: false,
    dragging: null,
    drawer: null,
    tab: 'timeline',
    filter: '',
    procSearch: '',
    procFilter: 'all',
    procSort: 'ram',
    procSortDir: 'desc',
    init() {
        for (const s of this.seats) {
            const pc = Object.values(this.pcs).find((p) => p.seat_id === s.id);
            if (pc) this.placement[`${s.x},${s.y}`] = pc.id;
        }
        if (window.Echo) {
            window.Echo.private(`lab.${this.labId}`)
                .listen('.pc.updated', (t) => { this.pcs[t.id] = { ...(this.pcs[t.id] || {}), ...t }; })
                .listen('.alert.raised', (a) => {
                    this.alerts.unshift(a);
                    this.alerts = this.alerts.slice(0, 50);
                    Alpine.store('toasts').push({ title: `${a.pc}: ${a.title}`, severity: a.severity });
                    if (this.drawer && this.drawer.pc.id === a.pc_id) this.open(a.pc_id);
                })
                .listen('.session.changed', () => window.location.reload());
        }
        setInterval(() => this.tick(), 5000);
    },
    tick() {
        // Mark tiles offline locally if no update arrived (the server also broadcasts this).
        const now = Date.now();
        for (const p of Object.values(this.pcs)) {
            if (p.last_seen && now - Date.parse(p.last_seen) > 20000 && p.status !== 'offline') p.status = 'offline';
        }
    },
    cell(x, y) { const id = this.placement[`${x},${y}`]; return id ? this.pcs[id] : null; },
    get unplaced() {
        const placed = new Set(Object.values(this.placement));
        return Object.values(this.pcs).filter((p) => !placed.has(p.id));
    },
    get counts() {
        const c = { clean: 0, warning: 0, alert: 0, offline: 0 };
        for (const p of Object.values(this.pcs)) c[p.status] = (c[p.status] || 0) + 1;
        return c;
    },
    tileClass(p) {
        if (!p) return 'border-dashed border-slate-300 bg-slate-50';
        return {
            clean: 'border-emerald-400 bg-emerald-50',
            warning: 'border-amber-400 bg-amber-50',
            alert: 'border-red-500 bg-red-50 ring-2 ring-red-300',
            offline: 'border-slate-300 bg-slate-100 text-slate-400',
        }[p.status] || 'border-slate-300';
    },
    dragStart(id) { if (this.editing) this.dragging = id; },
    drop(x, y) {
        if (!this.editing || !this.dragging) return;
        for (const [k, v] of Object.entries(this.placement)) if (v === this.dragging) delete this.placement[k];
        const prev = this.placement[`${x},${y}`];
        this.placement[`${x},${y}`] = this.dragging;
        if (prev && prev !== this.dragging) { /* swapped-out PC returns to the tray */ }
        this.dragging = null;
    },
    unplace() {
        if (!this.dragging) return;
        for (const [k, v] of Object.entries(this.placement)) if (v === this.dragging) delete this.placement[k];
        this.dragging = null;
    },
    async saveLayout() {
        const tiles = Object.entries(this.placement).map(([k, id]) => {
            const [x, y] = k.split(',').map(Number);
            return { pc_id: id, x, y };
        });
        await api('/lab/layout', { method: 'POST', body: JSON.stringify({ cols: this.cols, rows: this.rows, tiles }) });
        this.editing = false;
        Alpine.store('toasts').push({ title: 'Seat map saved', severity: 'info' });
    },
    async open(id) {
        if (this.editing) return;
        this.drawer = await api(`/pcs/${id}`);
    },
    async message(ids) {
        const text = prompt(ids ? 'Message to this PC:' : 'Message to all PCs:');
        if (!text) return;
        const r = await api('/sessions/message', { method: 'POST', body: JSON.stringify({ text, pc_ids: ids || [] }) });
        Alpine.store('toasts').push({ title: `Message sent to ${r.sent} PC(s)`, severity: 'info' });
    },
    async isolate(pc) {
        const on = !pc.isolated;
        if (on && !confirm(`Cut network access for ${pc.name}? The agent stays connected.`)) return;
        await api(`/pcs/${pc.id}/isolate`, { method: 'POST', body: JSON.stringify({ isolated: on, reason: 'dashboard' }) });
        this.pcs[pc.id].isolated = on;
        if (this.drawer) this.drawer.pc.isolated = on;
    },
    async kill(pc, exe, pid = null) {
        if (!exe && !pid) {
            exe = prompt('Program to close (executable name, e.g. ollama):');
            if (!exe) return;
        }
        const payload = pid ? { pid: Number(pid), ...(exe ? { exe } : {}) } : { exe };
        await api(`/pcs/${pc.id}/kill`, { method: 'POST', body: JSON.stringify(payload) });
        Alpine.store('toasts').push({ title: `Close "${exe || ('PID ' + pid)}" sent to ${pc.name}`, severity: 'info' });
        setTimeout(() => { if (this.drawer && this.drawer.pc.id === pc.id) this.open(pc.id); }, 1500);
    },
    async rescan(pc, what = 'processes') {
        await api(`/pcs/${pc.id}/rescan`, { method: 'POST', body: JSON.stringify({ what }) });
        Alpine.store('toasts').push({ title: `Refreshing ${what} on ${pc.name}…`, severity: 'info' });
        setTimeout(() => { if (this.drawer && this.drawer.pc.id === pc.id) this.open(pc.id); }, 1500);
    },
    setProcSort(col) {
        if (this.procSort === col) {
            this.procSortDir = this.procSortDir === 'desc' ? 'asc' : 'desc';
        } else {
            this.procSort = col;
            this.procSortDir = 'desc';
        }
    },
    filteredProcesses() {
        const list = this.drawer?.running_processes || [];
        const q = (this.procSearch || '').trim().toLowerCase();
        const isSys = (u) => {
            const l = (u || '').toLowerCase();
            return !l || l === 'system' || l === 'local service' || l === 'network service' || l === 'root' || l.startsWith('dwm-') || l.startsWith('umfd-');
        };
        const filtered = list.filter((p) => {
            if (this.procFilter === 'flagged' && !p.rule_id) return false;
            if (this.procFilter === 'user' && isSys(p.user) && !p.rule_id) return false;
            if (!q) return true;
            return (p.exe || '').toLowerCase().includes(q) ||
                   (p.name || '').toLowerCase().includes(q) ||
                   (p.user || '').toLowerCase().includes(q) ||
                   (p.rule_id || '').toLowerCase().includes(q) ||
                   String(p.pid || '').includes(q);
        });
        const dir = this.procSortDir === 'asc' ? 1 : -1;
        const col = this.procSort || 'ram';
        return [...filtered].sort((a, b) => {
            let va = 0, vb = 0;
            if (col === 'cpu') {
                va = Number(a.cpu_pct || 0);
                vb = Number(b.cpu_pct || 0);
            } else if (col === 'net') {
                va = Number(a.net_kbps || 0) * 1000 + Number(a.net_conns || 0);
                vb = Number(b.net_kbps || 0) * 1000 + Number(b.net_conns || 0);
            } else {
                va = Number(a.rss_mb || 0);
                vb = Number(b.rss_mb || 0);
            }
            if (va !== vb) return (va - vb) * dir;
            const ra = Number(a.rss_mb || 0), rb = Number(b.rss_mb || 0);
            if (ra !== rb) return rb - ra;
            return Number(a.pid || 0) - Number(b.pid || 0);
        });
    },
    async ack(alert) {
        await api(`/alerts/${alert.id}/ack`, { method: 'POST', body: JSON.stringify({}) });
        alert.acknowledged_at = new Date().toISOString();
    },
    bars(series, key) {
        const max = Math.max(1, ...series.map((s) => s[key]));
        return series.map((s) => Math.round((s[key] / max) * 100));
    },
}));

Alpine.start();
window.Alpine = Alpine;
