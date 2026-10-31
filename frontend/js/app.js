/* ==========================================================================
   BidMatrix — frontend logic
   Talks to the C++ backend (bidmatrix.exe) over its REST API:
     GET  /api/users            POST /api/users
     GET  /api/users/:id
     GET  /api/auctions         POST /api/auctions
     GET  /api/auctions/:id     POST /api/auctions/:id/bid
     GET  /api/closed           GET  /api/notifications
   ========================================================================== */
'use strict';

(() => {
  /* ---------------------------------------------------------------- config */
  // Served by bidmatrix.exe (any host, incl. an ngrok tunnel) -> same origin.
  // Opened straight from disk or from another dev server -> talk to localhost:8080.
  const API =
    location.protocol === 'file:' ||
    (/^(localhost|127\.0\.0\.1)$/.test(location.hostname) && location.port && location.port !== '8080')
      ? 'http://localhost:8080'
      : '';
  const IS_NGROK = /ngrok/i.test(location.hostname);
  const POLL_MS = 2500;

  /* --------------------------------------------------------------- helpers */
  const $  = (q, r = document) => r.querySelector(q);
  const $$ = (q, r = document) => Array.from(r.querySelectorAll(q));

  const esc = s =>
    String(s == null ? '' : s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  const safeUrl = u => (/^https?:\/\//i.test(String(u || '').trim()) ? String(u).trim() : '');
  const money = n =>
    '$' + Number(n || 0).toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 });
  const r2 = n => Math.round(n * 100) / 100;
  const pad = n => String(n).padStart(2, '0');
  const nowSec = () => Date.now() / 1000;

  function fmtRemaining(sec) {
    if (sec <= 0) return 'Closing…';
    const s = Math.floor(sec);
    const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), x = s % 60;
    return h > 0 ? `${pad(h)}:${pad(m)}:${pad(x)}` : `${pad(m)}:${pad(x)}`;
  }
  function timeAgo(ts) {
    const d = Math.max(0, Math.floor(nowSec() - ts));
    if (d < 60) return 'just now';
    if (d < 3600) return `${Math.floor(d / 60)}m ago`;
    if (d < 86400) return `${Math.floor(d / 3600)}h ago`;
    return `${Math.floor(d / 86400)}d ago`;
  }

  const store = {
    get(k) { try { return JSON.parse(localStorage.getItem(k)); } catch (e) { return null; } },
    set(k, v) { try { localStorage.setItem(k, JSON.stringify(v)); } catch (e) { /* storage unavailable */ } }
  };

  /* Current price / minimum bid follow the server rule exactly:
     a bid must beat the highest bid, or the starting price if there are no bids yet. */
  const curPrice = l => (l.currentHighestBid > 0 ? l.currentHighestBid : l.startingPrice);
  const minBid   = l => r2(curPrice(l) + 0.01);
  function niceStep(cur) {
    const raw = Math.max(cur * 0.03, 1);
    const mag = Math.pow(10, Math.floor(Math.log10(raw)));
    for (const m of [1, 2, 2.5, 5, 10]) if (m * mag >= raw) return r2(m * mag);
    return r2(10 * mag);
  }
  const nextBid = l => r2(curPrice(l) + niceStep(curPrice(l)));

  const CATS = [
    ['vehicles',     /\b(cars?|suv|sedan|bikes?|motorcycles?|scooter|truck|ev|tesla|bmw|honda|hyundai|tata|kawasaki|enfield|yamaha|ktm)\b/i],
    ['electronics',  /\b(laptops?|cameras?|dslr|phones?|iphone|tablet|console|tv|headphones?|gpu|rtx|gaming|speakers?|drone|lenses|lens)\b/i],
    ['art',          /\b(paintings?|oil|canvas|sculpture|prints?|artwork|art|artist)\b/i],
    ['collectibles', /\b(watch(es)?|chess|novel|books?|edition|antiques?|vintage|coins?|stamps?|collectibles?|signed)\b/i]
  ];
  function categoryOf(l) {
    for (const [name, re] of CATS) if (re.test(l.title || '')) return name;
    const all = (l.title || '') + ' ' + (l.description || '');
    for (const [name, re] of CATS) if (re.test(all)) return name;
    return 'other';
  }

  /* ----------------------------------------------------------------- state */
  const S = {
    users: [],
    userId: store.get('bm_user'),
    lots: [],
    closed: [],
    notifs: [],
    online: null,           // null = unknown yet, true / false afterwards
    filter: 'all',
    query: '',
    tab: 'live',
    modal: null,            // { type: 'lot', id } while the bid dialog is open
    lastSeenSig: null,
    knownLive: null,        // Set of live ids from the previous poll
    toasted: new Set()
  };
  const userName = () => {
    const u = S.users.find(x => x.id === S.userId);
    return u ? u.name : '';
  };

  /* ------------------------------------------------------------------- api */
  async function api(path, body) {
    const opts = { headers: {} };
    if (IS_NGROK) opts.headers['ngrok-skip-browser-warning'] = 'true';
    if (body !== undefined) {
      opts.method = 'POST';
      opts.headers['Content-Type'] = 'application/json';
      opts.body = JSON.stringify(body);
    }
    const res = await fetch(API + path, opts);
    let data = null;
    try { data = await res.json(); } catch (e) { /* non-JSON body */ }
    if (!res.ok) {
      const err = new Error((data && data.error) || `Request failed (HTTP ${res.status})`);
      err.http = true;
      throw err;
    }
    return data || {};
  }

  /* ----------------------------------------------------------------- toast */
  let toastTimer;
  function toast(msg) {
    const t = $('#toast');
    t.textContent = msg;
    t.classList.add('on');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => t.classList.remove('on'), 3200);
  }

  /* ----------------------------------------------------------------- theme */
    const savedTheme = store.get('bm_theme');
  if (savedTheme === 'dark') document.body.classList.add('dark-mode'); // light (white) by default, ignores OS dark setting
  $('#theme-toggle').addEventListener('click', () => {
    const dark = document.body.classList.toggle('dark-mode');
    store.set('bm_theme', dark ? 'dark' : 'light');
  });

  /* ------------------------------------------------------------------ tabs */
  function switchTab(id) {
    S.tab = id;
    $$('.nav-tab-btn').forEach(b => b.classList.toggle('active', b.dataset.tab === id));
    $$('.tab-section').forEach(s => s.classList.toggle('active', s.id === 'tab-' + id));
    if (id === 'closed') fetchClosed();
    if (id === 'dashboard') loadDashboard();
    if (id !== 'live') window.scrollTo({ top: 0, behavior: 'smooth' });
  }
  $$('.nav-tab-btn').forEach(b => b.addEventListener('click', () => switchTab(b.dataset.tab)));
  document.addEventListener('click', e => {
    const g = e.target.closest('[data-goto]');
    if (g) switchTab(g.dataset.goto);
  });

  /* ---------------------------------------------------------------- status */
  function setOnline(ok) {
    const changed = S.online !== ok;
    S.online = ok;
    const dot = $('#server-dot'), txt = $('#server-status-text');
    dot.className = 'pulse-dot' + (ok ? '' : ' offline');
    txt.textContent = ok
      ? `BidMatrix engine connected · ${API ? API.replace(/^https?:\/\//, '') : location.host}`
      : 'Backend offline — start bidmatrix.exe, then open http://localhost:8080';
    if (changed && ok === true && S.users.length === 0) loadUsers();
  }

  /* ----------------------------------------------------------------- users */
  async function loadUsers() {
    try {
      const d = await api('/api/users');
      S.users = d.users || [];
      // the server keeps everything in memory, so a saved id may be stale after a restart
      if (!S.users.some(u => u.id === S.userId)) S.userId = S.users.length ? S.users[0].id : null;
      renderUserSelect();
    } catch (e) { /* offline: tick() reports it */ }
  }
  function renderUserSelect() {
    const sel = $('#user-select');
    sel.innerHTML = S.users.length
      ? S.users.map(u => `<option value="${u.id}" ${u.id === S.userId ? 'selected' : ''}>${esc(u.name)}</option>`).join('')
      : '<option value="">No bidders</option>';
    const n = userName();
    $('#active-user-avatar').textContent = n ? n.charAt(0).toUpperCase() : '?';
    $('#dash-user-name').textContent = n || '…';
  }
  $('#user-select').addEventListener('change', e => {
    S.userId = parseInt(e.target.value, 10) || null;
    store.set('bm_user', S.userId);
    renderUserSelect();
    toast('Now bidding as ' + userName());
    renderLots(true);
    renderHero(true);
    if (S.tab === 'dashboard') loadDashboard();
  });

  $('#add-user-btn').addEventListener('click', () => {
    openModal(`
      <button class="modal-close" aria-label="Close">✕</button>
      <h3>Register a bidder</h3>
      <p style="color:var(--mute);font-size:13px;margin:4px 0 16px;">Creates a new user in the backend with an empty bid history.</p>
      <form id="user-form">
        <div class="form-group">
          <label for="nu-name">Name *</label>
          <input type="text" id="nu-name" maxlength="60" placeholder="e.g. Priya Sharma" required>
        </div>
        <div class="form-group">
          <label for="nu-email">Email <span style="color:var(--mute);font-weight:500;">(optional)</span></label>
          <input type="email" id="nu-email" placeholder="priya@example.com">
        </div>
        <div class="form-error" id="nu-error"></div>
        <button type="submit" class="btn primary" id="nu-submit" style="width:100%;padding:12px;margin-top:8px;">Create &amp; switch to this bidder</button>
      </form>`);
    setTimeout(() => { const el = $('#nu-name'); if (el) el.focus(); }, 60);
  });

  /* ---------------------------------------------------------------- dialog */
  const dlg = $('#dlg'), dc = $('#dc');
  function openModal(html, wide) {
    dlg.className = wide ? 'wide-modal' : '';
    dc.innerHTML = html;
    if (!dlg.open) dlg.showModal();
  }
  function closeModal() { if (dlg.open) dlg.close(); }
  dlg.addEventListener('click', e => {
    if (e.target === dlg || e.target.closest('.modal-close') || e.target.closest('[data-close]')) closeModal();
  });
  dlg.addEventListener('close', () => { S.modal = null; dc.innerHTML = ''; });

  dlg.addEventListener('submit', async e => {
    if (e.target.id === 'user-form') {
      e.preventDefault();
      const name = $('#nu-name').value.trim(), email = $('#nu-email').value.trim();
      const err = $('#nu-error'), btn = $('#nu-submit');
      if (!name) return;
      btn.disabled = true; err.textContent = '';
      try {
        const d = await api('/api/users', { name, email });
        S.userId = d.user.id;
        store.set('bm_user', S.userId);
        await loadUsers();
        closeModal();
        toast(`Welcome, ${name}! You are now bidding as ${name}.`);
        if (S.tab === 'dashboard') loadDashboard();
      } catch (ex) {
        err.textContent = ex.http ? ex.message : 'Could not reach the server.';
        btn.disabled = false;
      }
    } else if (e.target.id === 'bid-form') {
      e.preventDefault();
      submitModalBid();
    }
  });

  /* ============================================================ LIVE LOTS */
  let fetchingLots = false;
  async function fetchAuctions() {
    if (fetchingLots) return;
    fetchingLots = true;
    try {
      const d = await api('/api/auctions');
      const live = (d.auctions || []).filter(a => a.isActive);
      // announce lots that disappeared from the live list since the last poll
      if (S.knownLive) {
        live.forEach(a => S.knownLive.add(a.id));
        const liveIds = new Set(live.map(a => a.id));
        const gone = S.lots.filter(l => !liveIds.has(l.id));
        if (gone.length) {
          toast(gone.length === 1 ? `"${gone[0].title}" has closed — see Closed & Results` : `${gone.length} auctions have closed — see Closed & Results`);
          if (S.tab === 'closed') fetchClosed();
        }
      }
      S.knownLive = new Set(live.map(a => a.id));
      S.lots = live;
      setOnline(true);
    } catch (e) {
      setOnline(false);
    } finally {
      fetchingLots = false;
    }
    renderLots();
    renderHero();
  }

  function lotSig(l) {
    return [l.currentHighestBid, l.highestBidderName, l.totalBids, S.userId, l.endTime].join('|');
  }

  function lotCardHtml(l) {
    const cur = curPrice(l);
    const step = niceStep(cur);
    const mine = !!l.highestBidderName && l.highestBidderName === userName();
    const img = safeUrl(l.imageUrl);
    const parts = String(l.description || '').split('|').map(s => s.trim()).filter(Boolean);
    const metaChips = parts.length > 1
      ? parts.slice(0, 3).map(p => `<span>${esc(p)}</span>`).join(' • ')
      : '';
    const endAttr = Math.floor(l.endTime || 0);
    const rem = endAttr - nowSec();
    return `
      <article class="lot" data-id="${l.id}" data-sig="${esc(lotSig(l))}">
        <div class="lot-photo" data-act="detail" role="button" tabindex="0" aria-label="Open ${esc(l.title)}">
          ${img ? `<img data-img src="${esc(img)}" alt="${esc(l.title)}" loading="lazy">` : '<div class="no-img">No image</div>'}
          <span class="lot-badge live-pulse">Live Auction</span>
        </div>
        <div class="lot-info">
          <h3 class="lot-title" data-act="detail">${esc(l.title)}</h3>
          ${metaChips
            ? `<div class="lot-meta">${metaChips}</div>`
            : `<p class="lot-desc">${esc(l.description || '')}</p>`}
          <div class="lot-meta">
            <span class="lot-tag">${l.totalBids || 0} bid${l.totalBids === 1 ? '' : 's'}</span>
            <span>Starts ${money(l.startingPrice)}</span>
          </div>
          <div class="lot-pricing">
            <div>
              <span class="lot-pricing-label">Current Highest</span>
              <div class="lot-bid-val ${l.currentHighestBid > 0 ? 'highlight' : ''}">${money(cur)}</div>
              <div class="lot-bidder">
                ${l.highestBidderName
                  ? `👤 ${esc(l.highestBidderName)} ${mine ? '<b style="color:var(--emerald)">(You)</b>' : ''}`
                  : 'No bids yet'}
              </div>
            </div>
            <div style="text-align:right;">
              <span class="lot-pricing-label">Time Left</span>
              <div class="lot-timer" data-end="${endAttr}">${fmtRemaining(rem)}</div>
            </div>
          </div>
          <div class="lot-actions">
            <button class="btn primary" data-act="quick" data-step="${step}" ${rem <= 0 ? 'disabled' : ''}>Bid +${money(step)}</button>
            <button class="btn alt" data-act="custom" ${rem <= 0 ? 'disabled' : ''}>Custom bid</button>
          </div>
          <div style="display:flex;justify-content:center;margin-top:2px;">
            <span class="lot-detail-link" data-act="detail">Bid tree &amp; details ➔</span>
          </div>
        </div>
      </article>`;
  }

  function toNode(html) {
    const t = document.createElement('template');
    t.innerHTML = html.trim();
    return t.content.firstElementChild;
  }

  function notice(title, body, extra) {
    return `<div class="notice-card"><h4>${title}</h4><p>${body}</p>${extra || ''}</div>`;
  }

  function filteredLots() {
    const q = S.query.trim().toLowerCase();
    return S.lots.filter(l => {
      if (S.filter !== 'all' && categoryOf(l) !== S.filter) return false;
      if (q && !((l.title || '') + ' ' + (l.description || '')).toLowerCase().includes(q)) return false;
      return true;
    });
  }

  function renderLots(force) {
    const grid = $('#grid');
    $('#count-all').textContent = S.lots.length;
    const list = filteredLots();

    if (S.online === false && S.lots.length === 0) {
      grid.innerHTML = notice(
        'Can’t reach the BidMatrix engine',
        'Start <code>bidmatrix.exe</code> from the project folder and open <code>http://localhost:8080</code>. This page will reconnect automatically.');
      return;
    }
    if (S.online === null) return;               // still showing skeletons
    if (list.length === 0) {
      grid.innerHTML = S.lots.length === 0
        ? notice('No live auctions right now', 'Every auction has closed. Start a new one and it will appear here instantly.',
            '<button class="btn primary btn-sm" data-goto="create">Create an auction</button>')
        : notice('No lots match your filter', 'Try another category or clear the search box.',
            '<button class="btn alt btn-sm" id="clear-filters">Show all lots</button>');
      return;
    }

    // keyed reconcile: only touch cards whose data changed, so hover/focus/images survive polling
    const existing = new Map($$('.lot', grid).map(el => [Number(el.dataset.id), el]));
    $$(':scope > :not(.lot)', grid).forEach(el => el.remove());
    const wanted = new Set(list.map(l => l.id));
    existing.forEach((el, id) => { if (!wanted.has(id)) { el.remove(); existing.delete(id); } });

    list.forEach((l, i) => {
      let el = existing.get(l.id);
      const sig = lotSig(l);
      if (!el) {
        el = toNode(lotCardHtml(l));
      } else if (force || el.dataset.sig !== sig) {
        const bidChanged = el.dataset.sig && el.dataset.sig.split('|')[0] !== sig.split('|')[0];
        const fresh = toNode(lotCardHtml(l));
        el.replaceWith(fresh);
        el = fresh;
        if (bidChanged) { el.classList.add('flash'); setTimeout(() => el.classList.remove('flash'), 1200); }
      }
      if (grid.children[i] !== el) grid.insertBefore(el, grid.children[i] || null);
    });
    tickTimers();
  }

  /* hero preview = the busiest live lot */
  function renderHero(force) {
    const live = S.lots;
    $('#hero-live').textContent = `${live.length} live lot${live.length === 1 ? '' : 's'}`;
    const bids = live.reduce((n, l) => n + (l.totalBids || 0), 0);
    $('#hero-bids').textContent = `${bids} bid${bids === 1 ? '' : 's'} placed`;

    const card = $('#hero-card');
    const top = live.slice().sort((a, b) => (b.totalBids - a.totalBids) || (a.endTime - b.endTime))[0];
    if (!top) {
      if (force || card.dataset.id) {
        card.dataset.id = '';
        card.innerHTML = '<div class="hero-preview-img" style="display:grid;place-items:center;color:var(--mute);font-weight:600;">No live lots yet</div>';
      }
      return;
    }
    const sig = [top.id, top.currentHighestBid, top.totalBids].join('|');
    if (!force && card.dataset.sig === sig) return;
    card.dataset.id = top.id; card.dataset.sig = sig;
    const img = safeUrl(top.imageUrl);
    card.innerHTML = `
      ${img ? `<img data-img class="hero-preview-img" src="${esc(img)}" alt="${esc(top.title)}">` : '<div class="hero-preview-img no-img" style="display:grid;place-items:center;color:var(--mute)">No image</div>'}
      <div style="display:flex;justify-content:space-between;align-items:center;gap:10px;">
        <div style="min-width:0;">
          <h4 style="font-size:16px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">${esc(top.title)}</h4>
          <p style="font-size:12px;color:var(--mute);margin:2px 0 0;">${money(curPrice(top))} • ${top.totalBids || 0} bids • Most active lot</p>
        </div>
        <button class="btn primary btn-sm" data-hero-open="${top.id}">Bid</button>
      </div>`;
  }
  $('#hero-card').addEventListener('click', e => {
    const b = e.target.closest('[data-hero-open]');
    if (b) openLotModal(Number(b.dataset.heroOpen));
  });

  /* image fallback without inline handlers */
  document.addEventListener('error', e => {
    const img = e.target;
    if (!(img instanceof HTMLImageElement) || !img.hasAttribute('data-img')) return;
    const ph = document.createElement('div');
    ph.className = (img.className ? img.className + ' ' : '') + 'no-img';
    ph.textContent = 'No image';
    ph.style.display = 'grid'; ph.style.placeItems = 'center';
    img.replaceWith(ph);
  }, true);

  /* filters + search */
  $('#chips').addEventListener('click', e => {
    const b = e.target.closest('button[data-filter]');
    if (!b) return;
    S.filter = b.dataset.filter;
    $$('#chips button').forEach(x => x.setAttribute('aria-pressed', String(x === b)));
    renderLots(true);
  });
  $('#lot-search').addEventListener('input', e => { S.query = e.target.value; renderLots(true); });
  document.addEventListener('click', e => {
    if (e.target.id === 'clear-filters') {
      S.filter = 'all'; S.query = ''; $('#lot-search').value = '';
      $$('#chips button').forEach(x => x.setAttribute('aria-pressed', String(x.dataset.filter === 'all')));
      renderLots(true);
    }
  });

  /* card actions (event delegation) */
  $('#grid').addEventListener('click', e => {
    const el = e.target.closest('[data-act]');
    const card = e.target.closest('.lot');
    if (!el || !card) return;
    const id = Number(card.dataset.id);
    if (el.dataset.act === 'quick') quickBid(id, el);
    else if (el.dataset.act === 'custom') openLotModal(id, true);
    else if (el.dataset.act === 'detail') openLotModal(id);
  });
  $('#grid').addEventListener('keydown', e => {
    if ((e.key === 'Enter' || e.key === ' ') && e.target.classList.contains('lot-photo')) {
      e.preventDefault();
      openLotModal(Number(e.target.closest('.lot').dataset.id));
    }
  });

  /* ---------------------------------------------------------------- bidding */
  async function sendBid(id, amount) {
    try {
      await api(`/api/auctions/${id}/bid`, { userId: S.userId, amount });
      await Promise.all([fetchAuctions(), fetchNotifications()]);
      return { ok: true };
    } catch (e) {
      if (!e.http) setOnline(false);
      fetchAuctions();                                   // our view of the price was probably stale
      return { ok: false, error: e.http ? e.message : 'Could not reach the server.' };
    }
  }

  async function quickBid(id, btn) {
    const l = S.lots.find(x => x.id === id);
    if (!l) return;
    if (!S.userId) return toast('Pick or register a bidder first');
    if (l.highestBidderName && l.highestBidderName === userName())
      return toast('You already hold the highest bid on this lot');
    const amount = nextBid(l);
    btn.disabled = true;
    const r = await sendBid(id, amount);
    toast(r.ok ? `Bid of ${money(amount)} placed by ${userName()}` : r.error);
    if (!r.ok) btn.disabled = false;
  }

  /* ---------------------------------------------------------- lot modal */
  function ladderHtml(bids) {
    if (!bids || !bids.length) return '<div class="ladder-empty">No bids yet — be the first.</div>';
    // the API returns the Max-BST in-order, i.e. ascending by amount
    return bids.slice(-6).reverse().map((b, i) => `
      <div class="ladder-row ${i === 0 ? 'top' : ''}">
        <span>${i === 0 ? '👑 ' : ''}${esc(b.userName)}${b.userId === S.userId ? ' <small>(you)</small>' : ''}</span>
        <span>${money(b.amount)}</span>
      </div>`).join('');
  }

  function lotModalHtml(l) {
    const img = safeUrl(l.imageUrl);
    return `
      <button class="modal-close" aria-label="Close">✕</button>
      ${img ? `<img data-img class="modal-photo" src="${esc(img)}" alt="${esc(l.title)}">` : ''}
      <h3>${esc(l.title)}</h3>
      <p style="color:var(--mute);font-size:13px;margin:6px 0 14px;line-height:1.55;">${esc(l.description || '')}</p>

      <div class="modal-stats">
        <div>
          <div class="k">Current Highest</div>
          <div class="v" id="m-cur"></div>
          <div style="font-size:12px;color:var(--mute);" id="m-who"></div>
        </div>
        <div style="text-align:center;">
          <div class="k">Time Left</div>
          <div class="v lot-timer" id="m-timer" data-end="${Math.floor(l.endTime)}" style="font-size:18px;"></div>
        </div>
        <div style="text-align:right;">
          <div class="k">Minimum Next Bid</div>
          <div class="v green" id="m-min"></div>
        </div>
      </div>

      <h4 style="font-size:14px;margin-bottom:8px;">Top bids <span style="font-weight:500;color:var(--mute);font-size:12px;">(from the auction’s Max-BST)</span></h4>
      <div class="ladder" id="m-ladder"><div class="ladder-empty">Loading bids…</div></div>

      <form id="bid-form" novalidate>
        <div class="form-group">
          <label for="m-amt">Your bid ($) — bidding as <b>${esc(userName() || '—')}</b></label>
          <input type="number" id="m-amt" step="0.01" min="0" inputmode="decimal" required>
          <div class="quick-row" id="m-quick"></div>
          <div class="form-error" id="m-err"></div>
          <p style="font-size:12px;color:var(--mute);margin:6px 0 0;">
            Second-price rule: if you win, you pay the <b>second-highest</b> bid, not your own.
          </p>
        </div>
        <div style="display:flex;gap:10px;margin-top:14px;">
          <button type="button" class="btn alt" style="flex:1;" data-close>Cancel</button>
          <button type="submit" class="btn primary" style="flex:2;" id="m-submit">Place bid</button>
        </div>
      </form>`;
  }

  function paintLotModal(l, bids) {
    if (!S.modal) return;
    $('#m-cur').textContent = money(curPrice(l));
    $('#m-who').textContent = l.highestBidderName ? `by ${l.highestBidderName}` : 'No bids yet';
    $('#m-min').textContent = money(minBid(l));
    if (bids) $('#m-ladder').innerHTML = ladderHtml(bids);

    const input = $('#m-amt');
    input.min = minBid(l);
    if (!input.dataset.dirty) input.value = nextBid(l).toFixed(2);
    else if (parseFloat(input.value) < minBid(l)) input.value = minBid(l).toFixed(2);

    const step = niceStep(curPrice(l));
    $('#m-quick').innerHTML = [1, 2, 5].map(k =>
      `<button type="button" class="btn alt btn-sm" data-add="${r2(curPrice(l) + step * k)}">+${money(step * k)}</button>`).join('');

    const closed = !l.isActive || l.endTime - nowSec() <= 0;
    const sub = $('#m-submit');
    sub.disabled = closed;
    if (closed) $('#m-err').textContent = 'This auction has closed — see Closed & Results.';
  }

  async function openLotModal(id, focusInput) {
    const l = S.lots.find(x => x.id === id);
    if (!l) return toast('That auction is no longer live');
    S.modal = { type: 'lot', id };
    openModal(lotModalHtml(l), true);
    paintLotModal(l, null);
    tickTimers();
    await refreshLotModal();
    if (focusInput) { const i = $('#m-amt'); if (i) { i.focus(); i.select(); } }
  }

  async function refreshLotModal() {
    if (!S.modal || S.modal.type !== 'lot') return;
    const id = S.modal.id;
    try {
      const fresh = await api(`/api/auctions/${id}`);
      if (!S.modal || S.modal.id !== id) return;
      const l = S.lots.find(x => x.id === id);
      if (l) Object.assign(l, fresh);
      paintLotModal(fresh, fresh.bids || []);
    } catch (e) { /* keep what we have */ }
  }

  dlg.addEventListener('click', e => {
    const add = e.target.closest('[data-add]');
    if (add) { const i = $('#m-amt'); i.value = Number(add.dataset.add).toFixed(2); i.dataset.dirty = '1'; }
  });
  dlg.addEventListener('input', e => { if (e.target.id === 'm-amt') e.target.dataset.dirty = '1'; });

  async function submitModalBid() {
    if (!S.modal) return;
    const id = S.modal.id;
    const l = S.lots.find(x => x.id === id);
    const err = $('#m-err'), btn = $('#m-submit');
    const amount = r2(parseFloat($('#m-amt').value));
    err.textContent = '';
    if (!S.userId) { err.textContent = 'Pick or register a bidder first.'; return; }
    if (!isFinite(amount) || amount <= 0) { err.textContent = 'Enter a valid amount.'; return; }
    if (l && amount <= curPrice(l)) { err.textContent = `Bid must be higher than ${money(curPrice(l))}.`; return; }
    btn.disabled = true; btn.textContent = 'Placing bid…';
    const r = await sendBid(id, amount);
    if (r.ok) {
      closeModal();
      toast(`Bid of ${money(amount)} placed by ${userName()}`);
    } else {
      err.textContent = r.error;
      btn.disabled = false; btn.textContent = 'Place bid';
      refreshLotModal();
    }
  }

  /* ================================================================ CLOSED */
  async function fetchClosed() {
    const tbody = $('#closed-table-body');
    try {
      const d = await api('/api/closed');
      S.closed = (d.auctions || []).slice().sort((a, b) => (b.endTime || 0) - (a.endTime || 0));
    } catch (e) {
      tbody.innerHTML = `<tr class="empty-row"><td colspan="8">Can’t reach the backend. Start <code>bidmatrix.exe</code> and refresh.</td></tr>`;
      return;
    }
    if (!S.closed.length) {
      tbody.innerHTML = `<tr class="empty-row"><td colspan="8">No closed auctions yet. When a live timer runs out, its Vickrey settlement appears here automatically.</td></tr>`;
      return;
    }
    tbody.innerHTML = S.closed.map(a => {
      const sold = (a.totalBids || 0) > 0;
      const highest = a.currentHighestBid || 0;
      const settled = a.settledPrice || 0;
      const saved = Math.max(0, highest - settled);
      const img = safeUrl(a.imageUrl);
      return `
        <tr>
          <td>
            <div style="display:flex;align-items:center;gap:10px;">
              <span class="thumb">${img ? `<img data-img src="${esc(img)}" alt="" loading="lazy">` : ''}</span>
              <div>
                <strong>${esc(a.title)}</strong>
                <div class="row-sub">Auction #${a.id}</div>
              </div>
            </div>
          </td>
          <td>${sold && a.winnerName ? `<span class="winner-badge">🏆 ${esc(a.winnerName)}</span>` : '<span style="color:var(--mute);">No winner</span>'}</td>
          <td>${sold ? `<b>${money(highest)}</b>` : '—'}</td>
          <td>${sold ? `<b style="color:var(--emerald);">${money(settled)}</b>` : '—'}</td>
          <td>${!sold ? '—' : saved > 0 ? `<span class="savings-tag">Saved ${money(saved)}</span>` : '<span class="row-sub">Single bidder</span>'}</td>
          <td>${a.totalBids || 0}</td>
          <td>${sold ? '<span class="winner-badge" style="background:var(--soft);color:var(--ink);">Settled</span>' : '<span class="state-pill mute">No bids</span>'}</td>
          <td><button class="btn alt btn-sm" data-bst="${a.id}">Inspect BST</button></td>
        </tr>`;
    }).join('');
  }
  $('#refresh-closed').addEventListener('click', () => { fetchClosed(); toast('Results refreshed'); });
  $('#closed-table-body').addEventListener('click', e => {
    const b = e.target.closest('[data-bst]');
    if (b) openBstModal(Number(b.dataset.bst));
  });

  async function openBstModal(id) {
    try {
      const a = await api(`/api/auctions/${id}`);
      const bids = (a.bids || []).slice().reverse();            // ascending -> highest first
      const closed = !a.isActive;
      openModal(`
        <button class="modal-close" aria-label="Close">✕</button>
        <h3>Bid tree: ${esc(a.title)}</h3>
        <p style="color:var(--mute);font-size:13px;margin:4px 0 16px;">
          In-order traversal of the auction’s Max-BST, highest bid first.
          ${closed && a.totalBids ? `Winner pays <b>${money(a.settledPrice)}</b>.` : ''}
        </p>
        <div class="table-container">
          <div style="overflow-x:auto;">
          <table class="custom-table">
            <thead><tr><th>Rank</th><th>Bidder</th><th>Bid</th><th>Outcome</th></tr></thead>
            <tbody>
              ${bids.length ? bids.map((b, i) => `
                <tr>
                  <td>#${i + 1}</td>
                  <td><b>${esc(b.userName)}</b> <span class="row-sub">ID ${b.userId}</span></td>
                  <td><b>${money(b.amount)}</b></td>
                  <td>${i === 0
                      ? `<span class="winner-badge">${closed ? 'Winner (Max-BST)' : 'Leading'}</span>`
                      : i === 1
                        ? '<span style="color:var(--emerald);font-weight:700;">Vickrey price source</span>'
                        : '<span style="color:var(--mute);">Outbid</span>'}</td>
                </tr>`).join('') : '<tr class="empty-row"><td colspan="4">No bids were placed.</td></tr>'}
            </tbody>
          </table>
          </div>
        </div>`, true);
    } catch (e) {
      toast('Could not load the bid tree');
    }
  }

  /* ============================================================ DASHBOARD */
  let loadingDash = false;
  async function loadDashboard() {
    if (loadingDash) return;
    $('#dash-user-name').textContent = userName() || '…';
    const tbody = $('#dash-history-body');
    if (!S.userId) {
      tbody.innerHTML = '<tr class="empty-row"><td colspan="5">Pick or register a bidder to see their history.</td></tr>';
      return;
    }
    loadingDash = true;
    const uid = S.userId;
    try {
      const u = await api(`/api/users/${uid}`);
      const history = u.bidHistory || [];

      // one row per auction: the user's highest bid there
      const byAuction = {};
      history.forEach(b => {
        const cur = byAuction[b.auctionId];
        if (!cur) byAuction[b.auctionId] = { top: b, last: b.timestamp };
        else {
          if (b.amount > cur.top.amount) cur.top = b;
          cur.last = Math.max(cur.last, b.timestamp);
        }
      });
      const ids = Object.keys(byAuction);
      const details = await Promise.all(ids.map(id => api(`/api/auctions/${id}`).catch(() => null)));

      const rows = ids.map((id, i) => {
        const { top, last } = byAuction[id], d = details[i];
        let state = 'unknown', note = '';
        if (d && d.isActive) {
          const leader = (d.bids || []).slice(-1)[0];
          state = leader && leader.userId === uid ? 'leading' : 'outbid';
          note = state === 'leading' ? 'Highest bid right now' : `Current highest ${money(d.currentHighestBid)}`;
        } else if (d) {
          state = d.winnerId === uid ? 'won' : 'lost';
          note = state === 'won' ? `You pay ${money(d.settledPrice)} (second-highest bid)` : `Closed at ${money(d.settledPrice)}`;
        }
        return { id: Number(id), top, last, state, note, live: !!(d && d.isActive) };
      }).sort((a, b) => b.last - a.last);

      if (uid !== S.userId) return;                      // user switched while loading
      $('#stat-total-bids').textContent = u.totalBids != null ? u.totalBids : history.length;
      $('#stat-winning-bids').textContent = rows.filter(r => r.state === 'leading' || r.state === 'won').length;
      $('#stat-outbid-bids').textContent = rows.filter(r => r.state === 'outbid' || r.state === 'lost').length;

      if (!rows.length) {
        tbody.innerHTML = `<tr class="empty-row"><td colspan="5">${esc(userName())} hasn’t placed any bids yet. Head to Live Auctions to place the first one.</td></tr>`;
        return;
      }
      const pill = {
        leading: ['good', '🏆 Leading'], won: ['good', '🏆 Won'],
        outbid: ['bad', 'Outbid'], lost: ['mute', 'Lost'], unknown: ['mute', '—']
      };
      tbody.innerHTML = rows.map(r => `
        <tr>
          <td><strong>${esc(r.top.auctionTitle || 'Auction #' + r.id)}</strong><div class="row-sub">${esc(r.note)}</div></td>
          <td><b>${money(r.top.amount)}</b></td>
          <td style="color:var(--mute);font-size:12px;">${timeAgo(r.last)}</td>
          <td><span class="state-pill ${pill[r.state][0]}">${pill[r.state][1]}</span></td>
          <td>${r.live
              ? `<button class="btn alt btn-sm" data-open="${r.id}">${r.state === 'outbid' ? 'Raise bid' : 'View lot'}</button>`
              : '<button class="btn alt btn-sm" data-goto="closed">See result</button>'}</td>
        </tr>`).join('');
    } catch (e) {
      tbody.innerHTML = '<tr class="empty-row"><td colspan="5">Can’t reach the backend. Start <code>bidmatrix.exe</code> to see bid history.</td></tr>';
    } finally {
      loadingDash = false;
    }
  }
  $('#dash-history-body').addEventListener('click', e => {
    const b = e.target.closest('[data-open]');
    if (b) { switchTab('live'); openLotModal(Number(b.dataset.open)); }
  });

  /* ========================================================= NOTIFICATIONS */
  const notifSig = n => `${n.timestamp}|${n.message}`;
  async function fetchNotifications() {
    try {
      const d = await api('/api/notifications');
      S.notifs = (d.notifications || []).slice().sort((a, b) => b.timestamp - a.timestamp);
      renderNotifications();
    } catch (e) { /* offline: tick() reports it */ }
  }
  function unseenCount() {
    if (!S.notifs.length || S.lastSeenSig === null) return 0;
    const idx = S.notifs.findIndex(n => notifSig(n) === S.lastSeenSig);
    return idx === -1 ? S.notifs.length : idx;      // list is newest-first
  }
  function renderNotifications() {
    const open = $('#notification-dropdown').classList.contains('open');
    if (S.lastSeenSig === null && S.notifs.length) S.lastSeenSig = notifSig(S.notifs[0]);   // don't flag history on first load
    const fresh = unseenCount();

    // celebrate wins for the active bidder
    S.notifs.slice(0, fresh).forEach(n => {
      const k = notifSig(n);
      if (S.toasted.has(k)) return;
      S.toasted.add(k);
      const me = userName();
      if (me && n.message.startsWith('AUCTION CLOSED') && n.message.includes(`Winner: ${me} |`)) toast(`🏆 You won! ${n.message.split(' | ')[0].replace('AUCTION CLOSED: ', '')}`);
    });

    const badge = $('#bell-count');
    badge.hidden = open || fresh === 0;
    badge.textContent = fresh > 99 ? '99+' : fresh;
    $('#notif-total').textContent = `${S.notifs.length} event${S.notifs.length === 1 ? '' : 's'}`;

    $('#notif-list').innerHTML = S.notifs.length
      ? S.notifs.slice(0, 20).map((n, i) => `
          <div class="notif-item ${i < fresh ? 'fresh' : ''}">
            <div>${esc(n.message)}</div>
            <div class="notif-time">${timeAgo(n.timestamp)}</div>
          </div>`).join('')
      : '<div class="notif-empty">No notifications yet</div>';

    if (open && S.notifs.length) S.lastSeenSig = notifSig(S.notifs[0]);
  }
  $('#bell-btn').addEventListener('click', e => {
    e.stopPropagation();
    const dd = $('#notification-dropdown');
    const nowOpen = dd.classList.toggle('open');
    if (nowOpen && S.notifs.length) S.lastSeenSig = notifSig(S.notifs[0]);
    $('#bell-count').hidden = true;
  });
  document.addEventListener('click', e => {
    if (!e.target.closest('#notification-dropdown') && !e.target.closest('#bell-btn'))
      $('#notification-dropdown').classList.remove('open');
  });

  /* ============================================================== CREATE */
  $$('[data-img]').forEach(b => b.addEventListener('click', () => { $('#create-img').value = b.dataset.img; }));
  $('#create-auction-form').addEventListener('submit', async e => {
    e.preventDefault();
    const title = $('#create-title').value.trim();
    const description = $('#create-desc').value.trim();
    const imageUrl = $('#create-img').value.trim();
    const startingPrice = parseFloat($('#create-price').value);
    const durationSeconds = parseInt($('#create-duration').value, 10) || 300;
    if (!title || !description || !(startingPrice > 0)) return toast('Please fill in the title, description and a starting price');
    if (imageUrl && !safeUrl(imageUrl)) return toast('Image URL must start with http:// or https://');

    const btn = $('#create-submit');
    btn.disabled = true;
    try {
      await api('/api/auctions', { title, description, imageUrl, startingPrice, durationSeconds });
      toast(`“${title}” is now live!`);
      e.target.reset();
      switchTab('live');
      fetchAuctions();
      fetchNotifications();
    } catch (ex) {
      toast(ex.http ? ex.message : 'Could not reach the server.');
    } finally {
      btn.disabled = false;
    }
  });

  /* ============================================================== TIMERS */
  function tickTimers() {
    const t = nowSec();
    let expired = false;
    $$('.lot-timer[data-end]').forEach(el => {
      const rem = Number(el.dataset.end) - t;
      el.textContent = fmtRemaining(rem);
      el.classList.toggle('warn', rem > 30 && rem <= 300);
      el.classList.toggle('urgent', rem > 0 && rem <= 30);
      if (rem <= 0) {
        expired = true;
        const card = el.closest('.lot');
        if (card) $$('[data-act="quick"],[data-act="custom"]', card).forEach(b => { b.disabled = true; });
        const sub = $('#m-submit');
        if (sub && el.id === 'm-timer') sub.disabled = true;
      }
    });
    if (expired) fetchAuctions();                      // pick up the server-side close quickly
  }
  setInterval(tickTimers, 1000);

  /* =============================================================== POLLING */
  async function tick() {
    await Promise.all([fetchAuctions(), fetchNotifications()]);
    if (S.modal) refreshLotModal();
    if (S.tab === 'dashboard') loadDashboard();
    setTimeout(tick, document.hidden ? POLL_MS * 4 : POLL_MS);
  }

  /* ================================================================== INIT */
  (async function init() {
    await loadUsers();
    tick();
  })();
})();
