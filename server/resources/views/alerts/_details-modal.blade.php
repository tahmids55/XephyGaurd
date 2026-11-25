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
