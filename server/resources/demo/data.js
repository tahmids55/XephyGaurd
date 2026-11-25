// Demo data for the interactive preview. In production this comes from the server (Reverb + API).
window.XG = (() => {
  const pc = (id, name, seat, o) => Object.assign({
    id, name, seat, host: 'LAB-' + name.toUpperCase().replace(/[^A-Z0-9]/g, ''), ip: '192.168.0.' + (100 + id),
    os: 'Windows 11 Pro', agent: '1.4.2', user: 'std_2107' + String(10 + id).padStart(2, '0'), app: 'Visual Studio Code',
    last: 'Typing', score: 100, status: 'active', isolated: false, kind: 'code', session: 'win32',
    caps: { snapshot: true, live: true, delta: true, multi: false, secure: true }, monitors: 1, ai: false
  }, o || {});

  const pcs = [
    pc(1, 'Rupom2', { x: 0, y: 0 }, { user: 'std_210701', app: 'Google Chrome', kind: 'chat', last: 'AI chat open: Grok (xAI)', score: 60, status: 'alert', ai: true, monitors: 2, caps: { snapshot: true, live: true, delta: true, multi: true, secure: true } }),
    pc(2, 'SIyam', { x: 1, y: 0 }, { app: 'CLion', last: 'main.cpp saved', score: 98 }),
    pc(3, 'Sazzad', { x: 2, y: 0 }, { app: 'Firefox', kind: 'browser', last: 'USB device attached', score: 88, status: 'warning' }),
    pc(4, 'PC-04', { x: 3, y: 0 }, { app: 'ollama.exe', kind: 'terminal', last: 'Local AI model running', score: 45, status: 'alert', isolated: true }),
    pc(5, 'PC-05', { x: 4, y: 0 }, { app: 'Sublime Text', score: 96, last: 'test.py saved' }),
    pc(6, 'PC-06', { x: 5, y: 0 }, { os: 'Ubuntu 24.04', app: 'Terminal', kind: 'terminal', score: 99, last: 'gcc build ok', session: 'x11', caps: { snapshot: true, live: true, delta: false, multi: false, secure: false } }),
    pc(7, 'PC-07', { x: 0, y: 1 }, { app: 'PyCharm', score: 100, last: 'Debug started' }),
    pc(8, 'PC-08', { x: 1, y: 1 }, { app: 'Chrome', kind: 'browser', score: 94, last: 'cppreference.com' }),
    pc(9, 'PC-09', { x: 2, y: 1 }, { app: 'VS Code', score: 82, status: 'warning', last: 'Large paste, 412 chars' }),
    pc(10, 'PC-10', { x: 3, y: 1 }, { app: 'CodeBlocks', score: 97, last: 'Build succeeded' }),
    pc(11, 'PC-11', { x: 4, y: 1 }, { app: 'Lock screen', kind: 'lock', status: 'idle', last: 'No one signed in', user: '-', score: '-' }),
    pc(12, 'PC-12', { x: 0, y: 2 }, { app: '-', status: 'offline', last: 'No heartbeat for 25 min', user: '-', score: '-', kind: 'lock' }),
    pc(13, 'PC-13', { x: 1, y: 2 }, { os: 'Ubuntu 24.04', app: 'Wayland session', kind: 'lock', score: 99, last: 'Screen view not supported', session: 'wayland', caps: { snapshot: false, live: false, delta: false, multi: false, secure: false } }),
    pc(14, 'PC-14', { x: 2, y: 2 }, { app: 'VS Code', score: 99, last: 'algorithm.cpp' }),
    pc(15, 'PC-15', { x: 3, y: 2 }, { app: 'Eclipse', score: 97, last: 'Java compiled' }),
    pc(16, 'Instructor laptop', null, { app: 'Terminal', kind: 'terminal', status: 'idle', user: 'assistant_01', score: '-', last: 'Spare machine' })
  ];

  // minutes ago, clock time
  const al = (id, pcId, sev, title, rule, ago, acked, note) => {
    const p = pcs.find(x => x.id === pcId); const mins = 12 * 60 + 8 - ago;
    return { id, pcId, pc: p.name, user: p.user, sev, title, rule, ago, at: String(Math.floor(mins / 60)).padStart(2, '0') + ':' + String(mins % 60).padStart(2, '0'), acked: !!acked, by: acked ? 'KUET admin' : '', note: note || '' };
  };
  const alerts = [
    al(24, 4, 'critical', 'Local AI model running: ollama.exe', 'proc.ollama', 6),
    al(23, 1, 'high', 'Grok (xAI) stream: chrome to cdn.grok.com', 'web.grok', 7),
    al(22, 1, 'high', 'Grok (xAI) stream: chrome to cdn.grok.com', 'web.grok', 9),
    al(21, 1, 'high', 'Grok (xAI) stream: chrome to cdn.grok.com', 'web.grok', 11),
    al(20, 3, 'low', 'External USB device attached: 0bda:0129', 'dev.usb_device', 14),
    al(19, 9, 'low', 'Large paste: 412 characters into VS Code', 'paste.large', 18),
    al(18, 12, 'high', 'Machine went silent: no heartbeat for 25 minutes', 'tamper.machine_silent', 25),
    al(17, 1, 'high', 'Session helper was terminated', 'tamper.helper_terminated', 31),
    al(16, 1, 'high', 'Session helper was terminated', 'tamper.helper_terminated', 33),
    al(15, 8, 'medium', 'Blocked AI site: chatgpt.com (3 attempts)', 'net.domain_block', 38, true, 'Spoke to the student.'),
    al(14, 2, 'medium', 'Search AI summary hidden on google.com', 'net.google_udm14', 44, true, 'Automatic filter, no action needed.'),
    al(13, 5, 'critical', 'Second network adapter plugged in: Realtek USB GbE', 'hw.adapter_added', 52, true, 'Removed an unauthorised USB dongle.'),
    al(12, 12, 'high', 'Machine went silent: agent ended from Task Manager', 'tamper.machine_silent', 61, true, 'Hardware freeze, machine rebooted.'),
    al(11, 7, 'low', 'Large paste: 540 characters into PyCharm', 'paste.large', 70, true, 'Starter code from the exam sheet.')
  ];

  // Detail data for the inspector, generated per PC.
  const detail = p => {
    const ai = p.ai || p.isolated;
    const proc = [
      { pid: 9820, name: 'Code.exe', title: 'main.cpp - Visual Studio Code', cpu: 6.2, mem: 480, net: 0.1 },
      { pid: 4412, name: p.session === 'win32' ? 'chrome.exe' : 'firefox', title: 'Browser', cpu: 3.4, mem: 910, net: 1.8 },
      { pid: 1024, name: p.session === 'win32' ? 'explorer.exe' : 'gnome-shell', title: 'Desktop shell', cpu: 0.8, mem: 120, net: 0 },
      { pid: 6184, name: 'xerophyte-agent', title: 'XephyGuard agent', cpu: 0.6, mem: 64, net: 0.2 }
    ];
    if (p.isolated) proc.unshift({ pid: 14208, name: 'ollama.exe', title: 'Ollama server', cpu: 38.5, mem: 5200, net: 0, threat: 'Local AI model' });
    return {
      events: [
        { time: '12:02', label: p.last, sev: p.status === 'alert' ? 'high' : p.status === 'warning' ? 'medium' : 'info' },
        { time: '11:58', label: 'Window changed: ' + p.app, sev: 'info' },
        { time: '11:41', label: 'Screen snapshot taken for review', sev: 'info' },
        { time: '11:30', label: 'Agent signed in and verified', sev: 'info' }
      ],
      processes: proc,
      aiTraffic: ai ? [
        { service: 'Grok (xAI)', host: 'cdn.grok.com', proc: 'chrome', count: 12, last: '12:01', mode: 'Seen' },
        { service: 'OpenAI', host: 'api.openai.com', proc: 'chrome', count: 4, last: '11:57', mode: 'Blocked' }
      ] : [],
      tabs: [
        { browser: 'Chrome', title: ai ? 'Grok' : 'cppreference.com', host: ai ? 'grok.com' : 'en.cppreference.com', ai: ai },
        { browser: 'Chrome', title: 'Exam sheet (PDF)', host: 'files.kuet.ac.bd', ai: false }
      ],
      extensions: ai ? [{ browser: 'Chrome', name: 'Sider: ChatGPT sidebar', id: 'difoiogjjojoaoomphldepapgpbgkhkb', flag: true }] : [],
      artifacts: p.isolated ? [{ kind: 'Folder', path: 'C:\\Users\\std\\.ollama\\models', note: 'Local model files' }] : [],
      adapters: [{ name: 'Ethernet', type: 'Wired', mac: '70:85:C2:55:1A:' + (10 + p.id), ip: p.ip, flag: false }],
      ports: [{ port: 11434, proto: 'TCP', proc: 'ollama.exe', flag: p.isolated }].filter(x => x.flag),
      devices: p.id === 3 ? [{ vidpid: '0bda:0129', name: 'Realtek USB reader', time: '11:54' }] : [],
      keyrate: Array.from({ length: 30 }, (_, i) => Math.max(0, Math.round(34 + 26 * Math.sin(i * (.4 + p.id * .03)) + (i * 7 % 11)))),
      pastes: p.status === 'warning' ? [{ time: '11:52', chars: 412, lines: 18, code: false, from: 'exam sheet', to: p.app }] : []
    };
  };

  return { pcs, alerts, detail, now: '12:08' };
})();
