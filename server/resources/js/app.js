import Alpine from 'alpinejs';
import './echo';
import { ScreenViewer } from './screen/viewer.js';

window.ScreenViewer = ScreenViewer;

const csrf = () => document.querySelector('meta[name="csrf-token"]').content;

async function api(url, opts = {}) {
    const res = await fetch(url, {
        credentials: 'same-origin',
        ...opts,
        headers: { Accept: 'application/json', 'Content-Type': 'application/json', 'X-CSRF-TOKEN': csrf(), ...(opts.headers || {}) },
    });
    if (!res.ok) throw new Error((await res.json().catch(() => ({}))).message || res.statusText || `HTTP ${res.status}`);
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

window.fmtTime = function(val) {
    if (!val) return '';
    if (typeof val === 'string' && val.length === 8 && val[2] === ':' && val[5] === ':') return val;
    try {
        const d = new Date(val);
        if (!isNaN(d.getTime())) { const tz = document.querySelector('meta[name="xg-tz"]')?.content; return d.toLocaleTimeString('en-GB', tz ? { hour12: false, timeZone: tz } : { hour12: false }); }
    } catch (e) {}
    return (val || '').length > 19 ? val.substring(11, 19) : val;
};

/* Live lab view: seat-map grid, real-time tiles, detail drawer, drag-and-drop layout editor. */
Alpine.data('labView', (cfg) => ({
    labId: cfg.labId,
    cols: cfg.cols,
    rows: cfg.rows,
    pcs: Object.fromEntries(cfg.pcs.map((p) => [p.id, p])),
    seats: cfg.seats,
    alerts: cfg.alerts,
    fmtTime: window.fmtTime,
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
    screenMode: 'live',
    screenStreaming: false,
    screenHasFrame: false,
    screenStatus: '',
    screenAuditId: null,
    screenViewer: null,
    screenRenewTimer: null,
    screenReqSeq: 0,
    openSeq: 0,
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
    async tick() {
        // Mark tiles offline locally if no update arrived (the server also broadcasts this).
        const now = Date.now();
        for (const p of Object.values(this.pcs)) {
            if (p.last_seen && now - Date.parse(p.last_seen) > 20000 && p.status !== 'offline') p.status = 'offline';
        }
        if (this.drawer && this.drawer.pc?.id) {
            try {
                const refreshed = await api(`/pcs/${this.drawer.pc.id}`);
                if (this.drawer && this.drawer.pc?.id === refreshed.pc?.id) {
                    this.drawer = refreshed;
                }
            } catch (e) {}
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
        const prevPcId = this.drawer?.pc?.id;
        const wasStreaming = this.screenStreaming;
        const currentMode = this.screenMode || 'live';
        const isScreenTab = this.tab === 'screen';

        if (prevPcId !== id) {
            await this.stopScreen(false);
        }

        const currentOpenSeq = ++this.openSeq;
        try {
            const data = await api(`/pcs/${id}`);
            if (this.openSeq !== currentOpenSeq) return;
            this.drawer = data;

            // If the instructor was watching screen feeds or is on the screen tab, seamlessly stream the newly selected PC!
            if (this.drawer?.pc?.id && (wasStreaming || isScreenTab)) {
                await this.startScreen(currentMode);
            }
        } catch (e) {
            if (this.openSeq === currentOpenSeq) {
                Alpine.store('toasts').push({ title: `Failed to open PC #${id}: ${e.message}`, severity: 'alert' });
            }
        }
    },
    switchTab(t) {
        const prevTab = this.tab;
        this.tab = t;
        if (t === 'screen' && !this.screenStreaming && this.drawer?.pc?.id) {
            this.startScreen(this.screenMode || 'live');
        } else if (prevTab === 'screen' && t !== 'screen' && this.screenStreaming) {
            this.stopScreen();
        }
    },
    async closeDrawer() {
        await this.stopScreen();
        this.drawer = null;
    },
    async startScreen(mode = 'live') {
        const studentId = this.drawer?.student?.id;
        const pcId = this.drawer?.pc?.id;
        if (!studentId && !pcId) {
            this.screenStatus = 'No PC selected';
            return;
        }

        this.screenMode = mode;
        const currentReqSeq = ++this.screenReqSeq;
        const targetPcId = pcId;

        try {
            await this.stopScreen(false);
            if (this.screenReqSeq !== currentReqSeq) return;

            this.screenStreaming = true;
            this.screenHasFrame = false;
            this.screenStatus = `Connecting to ${this.drawer?.pc?.name || 'PC'} (${mode})…`;

            const tokenUrl = studentId ? `/api/screen/${studentId}/token` : `/api/screen/pc/${pcId}/token`;
            const data = await api(tokenUrl, {
                method: 'POST',
                body: JSON.stringify({ mode })
            });

            if (this.screenReqSeq !== currentReqSeq || this.drawer?.pc?.id !== targetPcId) {
                if (data.audit_id) {
                    api(`/api/screen/${data.audit_id}/stop`, { method: 'POST' }).catch(() => {});
                }
                return;
            }

            this.screenAuditId = data.audit_id;
            this.screenStatus = `Connecting to stream #${data.stream_id}…`;

            // Wait a tick to ensure canvas is in DOM
            await new Promise((r) => setTimeout(r, 20));

            const canvas = document.getElementById('xg-screen-canvas');
            if (canvas) {
                const ctx = (canvas && typeof canvas.getContext === 'function') ? canvas.getContext('2d') : null;
                if (ctx && canvas.width > 0 && canvas.height > 0) {
                    ctx.clearRect(0, 0, canvas.width, canvas.height);
                }

                let gwUrl = String(data.gateway_url || '').trim();
                const wsProto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
                if (!gwUrl) {
                    gwUrl = `${wsProto}//${window.location.host}/screen-gw`;
                } else if (gwUrl.startsWith('/')) {
                    gwUrl = `${wsProto}//${window.location.host}${gwUrl.replace(/\/$/, '')}`;
                } else {
                    try {
                        const parsed = new URL(gwUrl);
                        const isLocal = parsed.hostname === 'localhost' || parsed.hostname === '127.0.0.1';
                        if (window.location.protocol === 'https:' && parsed.protocol === 'ws:') {
                            parsed.protocol = 'wss:';
                            if (isLocal) {
                                parsed.hostname = window.location.hostname;
                                parsed.port = window.location.port;
                                parsed.pathname = '/screen-gw';
                            }
                        } else if (isLocal) {
                            parsed.hostname = window.location.hostname;
                        }
                        if (!parsed.port && window.location.port && parsed.pathname.startsWith('/screen-gw')) {
                            parsed.port = window.location.port;
                        }
                        gwUrl = parsed.toString().replace(/\/$/, '');
                    } catch (e) {}
                }

                const wsUrl = `${gwUrl}/view?token=${data.token}`;
                this.screenViewer = new ScreenViewer(canvas, {
                    onOpen: () => {
                        if (this.screenReqSeq === currentReqSeq) {
                            this.screenStatus = `Connected · ${this.drawer?.pc?.name || 'PC'} (${mode})`;
                        }
                    },
                    onControlMessage: (msg) => {
                        if (this.screenReqSeq !== currentReqSeq || !msg) return;
                        if (msg.type === 'status') {
                            if (msg.agent_connected) {
                                if (!this.screenHasFrame) {
                                    this.screenStatus = `Agent connected · waiting for frame…`;
                                }
                                this.screenViewer?.requestKeyframe(msg.stream_id || 0, 0);
                            } else {
                                this.screenHasFrame = false;
                                this.screenViewer?.tileCanvas?.clear();
                                this.screenStatus = `Waiting for agent screen stream (${this.drawer?.pc?.name || 'PC'})…`;
                            }
                        } else if (msg.type === 'error' && msg.message) {
                            this.screenStatus = `Stream error: ${msg.message}`;
                        }
                    },
                    onFrame: () => {
                        if (this.screenReqSeq === currentReqSeq) {
                            this.screenHasFrame = true;
                            this.screenStatus = `${mode === 'live' ? 'Live stream' : 'Snapshot'} · ${this.drawer?.pc?.name || 'PC'}`;
                        }
                    },
                    onError: (err) => {
                        if (this.screenReqSeq === currentReqSeq) {
                            this.screenStatus = 'Screen feed error';
                        }
                    },
                    onClose: () => {
                        if (this.screenReqSeq === currentReqSeq && this.screenStreaming) {
                            this.screenStatus = 'Stream disconnected';
                        }
                    }
                });
                this.screenViewer.connect(wsUrl);

                if (mode === 'live') {
                    this.screenRenewTimer = setInterval(async () => {
                        try {
                            if (this.screenAuditId && this.screenStreaming) {
                                await api(`/api/screen/${this.screenAuditId}/renew`, { method: 'POST' });
                            }
                        } catch (e) {
                            console.warn('Screen lease renewal failed', e);
                        }
                    }, 20000);
                }
            }
        } catch (e) {
            if (this.screenReqSeq === currentReqSeq) {
                this.screenStatus = `Error: ${e.message}`;
                this.screenStreaming = false;
                this.screenHasFrame = false;
            }
        }
    },
    async stopScreen(clearStatus = true) {
        if (this.screenRenewTimer) {
            clearInterval(this.screenRenewTimer);
            this.screenRenewTimer = null;
        }
        if (this.screenViewer) {
            this.screenViewer.disconnect();
            this.screenViewer = null;
        }
        if (this.screenAuditId) {
            const auditId = this.screenAuditId;
            this.screenAuditId = null;
            try {
                await api(`/api/screen/${auditId}/stop`, { method: 'POST' });
            } catch (e) {}
        }
        this.screenStreaming = false;
        this.screenHasFrame = false;
        if (clearStatus) {
            this.screenStatus = 'Stream stopped';
        }
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
