import Echo from 'laravel-echo';
import Pusher from 'pusher-js';

window.Pusher = Pusher;

// The dashboard talks only to the CCC. Reverb is reached through nginx on the same origin
// (path /app), so one build works whatever IP the CCC has.
const meta = (n) => document.querySelector(`meta[name="${n}"]`)?.content;
const secure = window.location.protocol === 'https:';
window.Echo = new Echo({
    broadcaster: 'reverb',
    key: meta('reverb-key'),
    wsHost: window.location.hostname,
    wsPort: Number(window.location.port || (secure ? 443 : 80)),
    wssPort: Number(window.location.port || 443),
    forceTLS: secure,
    enabledTransports: ['ws', 'wss'],
    authEndpoint: '/broadcasting/auth',
    auth: { headers: { 'X-CSRF-TOKEN': meta('csrf-token') } },
});
