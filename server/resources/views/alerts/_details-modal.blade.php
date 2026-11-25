{{-- Alert details popup. Needs the xgAlertDetails() mixin (public/js/app.js) in the enclosing x-data. --}}
  <div class="overlay" x-show="alertModal" x-cloak x-transition.opacity @click.self="alertModal = false">
    <div class="modal" role="dialog" aria-label="Alert details" style="width:min(680px,100%);min-width:min(360px,100%);max-width:calc(100vw - 32px);min-height:240px;max-height:calc(100vh - 32px);display:flex;flex-direction:column;resize:both">
      <div class="modal-head" style="flex:none"><span class="h2">Alert details</span><button class="xbtn" @click="alertModal = false" aria-label="Close"><svg class="i" aria-hidden="true"><use href="#i-x"/></svg></button></div>
      <div class="modal-body stack" style="flex:1 1 auto;min-height:0;overflow:auto;padding-bottom:12px">
        <template x-if="alertDet"><div class="stack">
          <div style="display:flex;gap:8px;align-items:center;flex-wrap:wrap">
            <span class="sev" :class="alertDet.sev" x-text="alertDet.sev"></span>
            <span class="chip"><svg class="i i-sm" aria-hidden="true"><use :href="'#' + alertSrc(alertDet).icon"/></svg><span x-text="'Detected by: ' + alertSrc(alertDet).label"></span></span>
            <span class="chip" x-text="alertDet.acked ? ('Reviewed' + (alertDet.by ? ' by ' + alertDet.by : '')) : 'Needs review'"></span>
          </div>
          <div><div style="font-weight:600;font-size:15px" x-text="alertDet.title"></div><p class="muted" style="margin:4px 0 0" x-text="alertExplain(alertDet)"></p></div>
          <p class="small muted" style="margin:0" x-text="(window.xgSevMeaning[alertDet.sev] || '') + ' This is a flag for you to check, not proof.'"></p>

          <div class="label">Where and when</div>
          <dl class="prop" style="margin:0">
            <dt>PC</dt><dd x-text="alertDet.pc || '—'"></dd>
            <dt>Student</dt><dd x-text="alertDet.user || '—'"></dd>
            <dt>When</dt><dd x-text="alertWhen"></dd>
            <dt>Session</dt><dd x-text="alertDet.session ? alertDet.session.name + (alertDet.session.mode ? ' (' + alertDet.session.mode + ')' : '') : 'No session (always-on monitoring)'"></dd>
            <dt x-show="alertDet.os">Operating system</dt><dd x-show="alertDet.os" x-text="alertDet.os"></dd>
          </dl>

          <div class="muted small" x-show="alertLoading">Loading details…</div>
          <div class="small" style="color:var(--alert)" x-show="alertErr" x-text="alertErr"></div>

          <div x-show="alertFacts.length"><div class="label">What the PC reported</div>
            <dl class="prop" style="margin:0"><template x-for="f in alertFacts" :key="f.k + f.v"><div style="display:contents"><dt x-text="f.k"></dt><dd class="mono" style="word-break:break-all" x-text="f.v"></dd></div></template></dl>
          </div>

          <div x-show="alertActions.length"><div class="label">What XephyGuard did</div>
            <ul style="margin:4px 0 0;padding-left:18px"><template x-for="t in alertActions" :key="t"><li x-text="t"></li></template></ul>
          </div>

          <div x-show="alertDet.acked || (alertDet.reviews && alertDet.reviews.length)"><div class="label">Review</div>
            <template x-for="(r, i) in (alertDet.reviews || [])" :key="i"><div class="small"><b x-text="r.by || 'Staff'"></b> <span class="muted" x-text="r.at ? new Date(r.at).toLocaleString() : ''"></span><div x-show="r.note" x-text="r.note"></div></div></template>
          </div>

          <div x-show="alertDet.screenshot"><div class="label">Screenshot at alert time</div>
            <p class="small" style="margin:4px 0" x-text="alertShotText"></p>
            <template x-if="alertDet.screenshot && alertDet.screenshot.url">
              <div class="stack">
                <div><button class="btn" x-show="!alertShot" @click="alertShot = true">Show screenshot</button>
                  <span class="small muted" x-show="!alertShot">Opening it is recorded in the audit log.</span></div>
                <template x-if="alertShot"><a :href="alertDet.screenshot.url" target="_blank" rel="noopener"><img :src="alertDet.screenshot.url" alt="Screenshot taken by the PC when the alert was raised" style="max-width:100%;border:1px solid var(--border);border-radius:6px"></a></template>
                <div class="small muted mono" style="word-break:break-all" x-text="'sha256 ' + alertDet.screenshot.sha256"></div>
              </div>
            </template>
          </div>

          <details x-show="alertDet.has_snapshot" :open="alertSnapOpen" @toggle="alertSnapOpen = $event.target.open; if (alertSnapOpen) loadAlertSnap()"><summary class="small muted" style="cursor:pointer">What the CCC knew at alert time (saved copy; opening is logged)</summary>
            <div class="stack" style="margin-top:6px">
              <div class="muted small" x-show="alertSnapLoading">Loading saved context…</div>
              <div class="small" style="color:var(--alert)" x-show="alertSnapErr" x-text="alertSnapErr"></div>
              <template x-if="alertSnap"><div class="stack">
                <span class="chip" :style="alertSnap.intact ? '' : 'color:var(--alert)'" x-text="alertSnap.intact ? 'Unchanged since saved (hash checks out)' : 'Changed after saving: hash does not match, do not rely on it'"></span>
                <dl class="prop" style="margin:0">
                  <dt>Saved at</dt><dd x-text="alertSnap.captured_at ? new Date(alertSnap.captured_at).toLocaleString() : '—'"></dd>
                  <dt>Active app</dt><dd x-text="(alertSnap.pc && alertSnap.pc.active_app) || '—'"></dd>
                  <dt>PC mode</dt><dd x-text="(alertSnap.pc && alertSnap.pc.mode) || '—'"></dd>
                  <dt>Programs running</dt><dd x-text="alertSnap.processes ? alertSnap.processes.count : 'No process list yet'"></dd>
                </dl>
                <div class="small mono" style="word-break:break-word" x-show="alertSnap.processes && alertSnap.processes.names.length" x-text="alertSnap.processes && alertSnap.processes.names.join(', ')"></div>
                <div x-show="alertSnap.events.length"><div class="label">PC events in the 2 minutes before</div>
                  <ul style="margin:4px 0 0;padding-left:18px"><template x-for="(e, i) in alertSnap.events" :key="'e' + i"><li class="small"><span class="muted" x-text="e.at ? new Date(e.at).toLocaleTimeString() : ''"></span> <b x-text="e.type"></b> <span class="mono" style="word-break:break-all" x-text="e.summary"></span></li></template></ul>
                </div>
                <div x-show="alertSnap.net.length"><div class="label">Network in the 2 minutes before</div>
                  <ul style="margin:4px 0 0;padding-left:18px"><template x-for="(n, i) in alertSnap.net" :key="'n' + i"><li class="small"><span class="muted" x-text="n.at ? new Date(n.at).toLocaleTimeString() : ''"></span> <span class="mono" style="word-break:break-all" x-text="(n.host || '') + (n.path || '')"></span> <span x-text="[n.action, n.category].filter(Boolean).join(', ')"></span></li></template></ul>
                </div>
                <div class="small muted mono" style="word-break:break-all" x-text="'sha256 ' + alertSnap.sha256"></div>
              </div></template>
            </div>
          </details>

          <details><summary class="small muted" style="cursor:pointer">Technical details</summary>
            <dl class="prop" style="margin:6px 0 0">
              <dt>Rule</dt><dd class="mono" x-text="alertDet.rule || '—'"></dd>
              <dt>Category</dt><dd class="mono" x-text="alertDet.cat || '—'"></dd>
              <dt>Alert ID</dt><dd class="mono" x-text="alertDet.id"></dd>
              <dt x-show="alertDet.details && alertDet.details.mode">PC mode</dt><dd class="mono" x-show="alertDet.details && alertDet.details.mode" x-text="alertDet.details && alertDet.details.mode"></dd>
            </dl>
          </details>
        </div></template>
      </div>
      <div class="modal-foot" style="flex:none;border-top:1px solid var(--border)"><button class="btn" @click="alertModal = false">Close</button><button class="btn btn-solid" x-show="alertDet && !alertDet.acked" @click="ackAlertFromModal()">Mark reviewed</button></div>
    </div>
  </div>
