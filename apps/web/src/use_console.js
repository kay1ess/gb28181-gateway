import { computed, onBeforeUnmount, onMounted, reactive, ref } from 'vue';
import { playability } from './playability.js';
import { ApiError, createApi } from './api.js';

const read = (key, fallback) => {
  try {
    return JSON.parse(sessionStorage.getItem(key)) ?? fallback;
  } catch {
    return fallback;
  }
};
const write = (key, value) => {
  try {
    sessionStorage.setItem(key, JSON.stringify(value));
  } catch {
    /* Storage may be disabled. */
  }
};
export const terminal = (state) => ['ended', 'failed'].includes(state);

export function useConsole() {
  const settings = reactive(read('gb.connection', { base: '/api', token: '' }));
  const connectionOpen = ref(!settings.token);
  const connected = ref(false);
  const reachable = ref(false);
  const view = ref('resources');
  const error = ref(null);
  const notice = ref('');
  const busy = reactive(new Set());
  const stats = ref({});
  const platforms = ref([]);
  const resourcesByPlatform = reactive({});
  const expandedPlatforms = reactive(new Set());
  const expandedNodes = reactive(new Set());
  const loadingPlatforms = reactive(new Set());
  const resourceErrors = reactive({});
  const selected = ref(null);
  const info = ref(null);
  const deviceStatus = ref(null);
  const jobs = ref([]);
  const sessions = ref([]);

  const filters = reactive({
    platform_id: '',
    keyword: '',
    state: '',
  });
  const platformForm = ref(null);
  const editing = ref(false);
  const operationId = ref('');
  const activeSessions = computed(() =>
    sessions.value.filter((s) => !terminal(s.playback.state) && !s.released),
  );
  let api;
  let timer;
  let eventSocket;
  let startEvents;
  let eventsReady = false;
  let catalogNeedsRefresh = false;
  let catalogSequence = 0;
  const catalogRequests = new Map();
  let polling = false;
  const resourceVersions = new Map();
  let generation = 0;

  function report(cause) {
    error.value = {
      message: cause.message,
      code: cause.code,
      traceId: cause.traceId,
    };
  }
  async function run(key, work) {
    if (busy.has(key)) return;
    busy.add(key);
    error.value = null;
    try {
      return await work();
    } catch (cause) {
      report(cause);
    } finally {
      busy.delete(key);
    }
  }
  function saveSessions() {
    write('gb.sessions', sessions.value);
  }
  function clearResources() {
    for (const key of Object.keys(resourcesByPlatform)) delete resourcesByPlatform[key];
    for (const key of Object.keys(resourceErrors)) delete resourceErrors[key];
    expandedPlatforms.clear();
    expandedNodes.clear();
    loadingPlatforms.clear();
    resourceVersions.clear();
  }
  async function loadPlatform(platform, force = false) {
    if (
      !connected.value ||
      loadingPlatforms.has(platform.id) ||
      (!force && resourcesByPlatform[platform.id])
    )
      return;
    const current = generation;
    const version = (resourceVersions.get(platform.id) || 0) + 1;
    resourceVersions.set(platform.id, version);
    loadingPlatforms.add(platform.id);
    delete resourceErrors[platform.id];
    try {
      const rows = [];
      const limit = 1000;
      for (let offset = 0; ; offset += limit) {
        const page = await api.resources({ platform_id: platform.id, offset, limit });
        if (current !== generation || resourceVersions.get(platform.id) !== version) return;
        rows.push(...page);
        if (rows.length > 100000) throw new Error('平台资源超过当前页面的 100000 项限制');
        if (page.length < limit) break;
      }
      resourcesByPlatform[platform.id] = rows;
    } catch (cause) {
      if (current === generation && resourceVersions.get(platform.id) === version)
        resourceErrors[platform.id] = cause.message;
    } finally {
      if (current === generation && resourceVersions.get(platform.id) === version)
        loadingPlatforms.delete(platform.id);
    }
  }
  async function togglePlatform(platform) {
    if (expandedPlatforms.has(platform.id)) {
      expandedPlatforms.delete(platform.id);
      return;
    }
    expandedPlatforms.add(platform.id);
    await loadPlatform(platform);
  }
  function toggleNode(id) {
    if (expandedNodes.has(id)) expandedNodes.delete(id);
    else expandedNodes.add(id);
  }
  async function loadResources() {
    await Promise.all(
      platforms.value
        .filter((platform) => expandedPlatforms.has(platform.id))
        .map((platform) => loadPlatform(platform, true)),
    );
  }
  async function filterResources() {
    if (!connected.value) return;
    if (filters.platform_id) {
      const platform = platforms.value.find((item) => item.id === filters.platform_id);
      if (platform) {
        expandedPlatforms.add(platform.id);
        await loadPlatform(platform);
      }
    }
  }
  async function refreshSession(session) {
    try {
      const playback = await api.playback(session.lease.playback_id);
      session.playback =
        !terminal(playback.state) && !playback.idle_timeout_seconds
          ? await api.playbackAddress(playback.id)
          : playback;
      session.problem = '';
    } catch (cause) {
      session.problem = cause.message;
      if ([1004, 1006].includes(cause.code)) {
        session.released = true;
      }
    }
  }
  function applyOperation(result) {
    const job = jobs.value.find((item) => item.id === result.id);
    if (!job || job.state !== 'running') return;
    if (
      result.state === 'running' &&
      (result.received < job.received || result.expected < job.expected)
    )
      return;
    Object.assign(job, result);
    if (job.state !== 'succeeded') return;

    const platformId = job.platform_id;
    delete resourcesByPlatform[platformId];
    resourceVersions.set(platformId, (resourceVersions.get(platformId) || 0) + 1);
    loadingPlatforms.delete(platformId);
    if (view.value === 'resources' && expandedPlatforms.has(platformId)) {
      const platform = platforms.value.find((item) => item.id === platformId);
      if (platform) void loadPlatform(platform, true);
    }
  }
  function closeEvents() {
    const socket = eventSocket;
    eventSocket = undefined;
    eventsReady = false;
    for (const pending of catalogRequests.values())
      pending.finish(new Error('连接已断开，目录同步是否受理未确认'));
    socket?.close();
  }
  function openEvents(base, token) {
    if (eventSocket) return;
    const url = new URL(base + '/events', window.location.href);
    url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:';
    const socket = new WebSocket(url);
    eventSocket = socket;
    socket.onopen = () => {
      if (eventSocket === socket) socket.send(JSON.stringify({ type: 'auth', token }));
    };
    socket.onmessage = ({ data }) => {
      if (eventSocket !== socket || typeof data !== 'string') return;
      let event;
      try {
        event = JSON.parse(data);
      } catch {
        return;
      }
      if (event?.type === 'auth_ok') {
        eventsReady = true;
        catalogNeedsRefresh = true;
      } else if (
        eventsReady &&
        (event?.type === 'catalog_accepted' || event?.type === 'catalog_error')
      ) {
        const pending = catalogRequests.get(event.request_id);
        if (event.type === 'catalog_error')
          pending?.finish(new ApiError(event.error.message, event.error.code, event.request_id));
        else if (event.operation?.id) pending?.finish(null, event.operation);
      } else if (eventsReady && event?.type === 'operation_changed' && event.operation) {
        applyOperation(event.operation);
      }
    };
    socket.onclose = () => {
      if (eventSocket !== socket) return;
      closeEvents();
      catalogNeedsRefresh = true;
    };
  }
  async function refresh(quiet = false) {
    if (!connected.value || polling) return;
    polling = true;
    const current = generation;
    try {
      startEvents?.();
      const [health, items] = await Promise.all([api.health(), api.platforms()]);
      if (current !== generation) return;
      stats.value = health;
      platforms.value = items;
      reachable.value = true;
      if (view.value === 'resources' && !quiet) await loadResources();
      if (!eventsReady || catalogNeedsRefresh) {
        catalogNeedsRefresh = false;
        const results = await Promise.allSettled(
          jobs.value
            .filter((job) => job.state === 'running')
            .map(async (job) => {
              const result = await api.operation(job.id);
              if (current === generation) applyOperation(result);
            }),
        );
        if (current === generation && results.some((result) => result.status === 'rejected'))
          catalogNeedsRefresh = true;
      }
    } catch (cause) {
      if (current !== generation) return;
      reachable.value = false;
      if (!quiet) report(cause);
    } finally {
      if (current === generation) {
        // A catalog/health error must not prevent playback status updates.
        await Promise.all(activeSessions.value.map(refreshSession));
        saveSessions();
      }
      polling = false;
    }
  }
  async function connect() {
    await run('connect', async () => {
      if (activeSessions.value.length && connected.value)
        throw new Error('请先释放本页点播，再切换连接');
      const base = settings.base.trim().replace(/\/+$/, '');
      if (!(base.startsWith('/') && !base.startsWith('//')) && !/^https?:\/\//.test(base))
        throw new Error('API 地址应为 /api 或完整的 http(s) 地址');
      if (!settings.token.trim()) throw new Error('请输入 API 令牌');
      const token = settings.token.trim();
      const next = createApi({ base, token });
      try {
        const health = await next.health();
        const items = await next.platforms();
        closeEvents();
        api?.abort();
        generation++;
        api = next;
        settings.base = base;
        settings.token = token;
        stats.value = health;
        platforms.value = items;
        sessions.value =
          read('gb.connection', {}).base === base &&
          read('gb.connection', {}).token === settings.token
            ? read('gb.sessions', []).filter((s) => s.lease?.playback_id && !s.released)
            : [];
        jobs.value = [];
        clearResources();
        selected.value = null;
        info.value = null;
        deviceStatus.value = null;
        write('gb.connection', { ...settings });
        saveSessions();
        connected.value = true;
        reachable.value = true;
        connectionOpen.value = false;

        clearInterval(timer);
        startEvents = () => {
          try {
            openEvents(base, token);
          } catch {
            // HTTP polling covers deployments without WebSocket support.
          }
        };
        startEvents();
        timer = setInterval(() => refresh(true), 3000);
        if (view.value === 'resources') await filterResources();
      } catch (cause) {
        next.abort();
        throw cause;
      }
    });
  }
  async function disconnect() {
    await run('disconnect', async () => {
      await Promise.allSettled(activeSessions.value.map((s) => api.release(s.lease.id)));

      generation++;
      closeEvents();
      startEvents = undefined;
      clearInterval(timer);
      api?.abort();
      connected.value = false;
      reachable.value = false;
      sessions.value = [];
      platforms.value = [];
      clearResources();
      selected.value = null;
      jobs.value = [];
      stats.value = {};
      settings.token = '';
      write('gb.connection', { base: settings.base, token: '' });
      saveSessions();
      connectionOpen.value = true;
    });
  }
  async function openView(next) {
    view.value = next;
    if (next === 'resources') await filterResources();
  }
  async function editPlatform(platform) {
    if (!platform) {
      editing.value = false;
      platformForm.value = {
        gb_id: '',
        name: '',
        realm: '3402000000',
        username: '',
        password: '',
        enabled: true,
        heartbeat_timeout_seconds: 180,
        default_receive_mode: 'udp',
      };
      return;
    }
    await run(`edit:${platform.id}`, async () => {
      const fresh = await api.platform(platform.id);
      editing.value = true;
      platformForm.value = {
        ...fresh,
        default_receive_mode: fresh.default_receive_mode || 'udp',
        password: '',
      };
    });
  }
  async function savePlatform() {
    await run('platform-save', async () => {
      const {
        id,
        gb_id,
        name,
        realm,
        username,
        password,
        enabled,
        heartbeat_timeout_seconds,
        default_receive_mode,
      } = platformForm.value;
      const value = {
        gb_id,
        name,
        realm,
        username,
        enabled,
        heartbeat_timeout_seconds,
        default_receive_mode,
      };
      if (editing.value) value.id = id;
      if (password) value.password = password;
      if (!editing.value && !password) throw new Error('新增下级必须填写认证密码');
      await api.savePlatform(value, editing.value);
      platformForm.value = null;

      await refresh();
    });
  }
  async function removePlatform(platform) {
    if (!window.confirm(`删除下级“${platform.name || platform.id}”？其资源目录和缓存也会被删除。`))
      return;
    await run(`delete:${platform.id}`, async () => {
      await api.removePlatform(platform.id);
      delete resourcesByPlatform[platform.id];
      expandedPlatforms.delete(platform.id);
      resourceVersions.set(platform.id, (resourceVersions.get(platform.id) || 0) + 1);
      loadingPlatforms.delete(platform.id);
      if (filters.platform_id === platform.id) filters.platform_id = '';
      jobs.value = jobs.value.filter((job) => job.platform_id !== platform.id);
      if (selected.value?.platform_id === platform.id) selected.value = null;

      await refresh();
      if (view.value === 'resources') await loadResources();
    });
  }
  async function sync(platform) {
    return run(`sync:${platform.id}`, async () => {
      const current = generation;
      const socket = eventSocket;
      if (!eventsReady || socket?.readyState !== WebSocket.OPEN)
        throw new Error('事件连接尚未就绪，请稍后再同步目录');
      const requestId = `catalog_${Date.now()}_${++catalogSequence}`;
      const job = await new Promise((resolve, reject) => {
        const finish = (error, operation) => {
          clearTimeout(timeout);
          catalogRequests.delete(requestId);
          if (error) reject(error);
          else resolve(operation);
        };
        const timeout = setTimeout(
          () => finish(new Error('等待目录同步受理超时，任务可能已经提交')),
          15000,
        );
        catalogRequests.set(requestId, { finish });
        try {
          socket.send(
            JSON.stringify({
              type: 'catalog_sync',
              request_id: requestId,
              platform_id: platform.id,
            }),
          );
        } catch (error) {
          finish(error);
        }
      });
      if (current !== generation) return;
      jobs.value.unshift(job);
      jobs.value = jobs.value.slice(0, 20);
      // An event can arrive before the WebSocket acknowledges the task.
      catalogNeedsRefresh = true;
      return job.id;
    });
  }
  async function lookupOperation() {
    if (!operationId.value.trim()) return;
    await run('operation', async () => {
      const job = await api.operation(operationId.value.trim());
      jobs.value = [job, ...jobs.value.filter((j) => j.id !== job.id)].slice(0, 20);
    });
  }
  async function selectResource(resource) {
    selected.value = resource;
    info.value = null;
    deviceStatus.value = null;
    await run(`resource:${resource.id}`, async () => {
      const detail = await api.resource(resource.id);
      if (selected.value?.id === resource.id) selected.value = detail;
    });
  }
  async function device(kind, live) {
    const resource = selected.value;
    if (!resource) return;
    await run(`device:${kind}`, async () => {
      const result = await api[kind](resource.id, live);
      if (selected.value?.id !== resource.id) return;
      if (kind === 'info') info.value = result;
      else deviceStatus.value = result;
    });
  }
  async function start(resource) {
    const platform = platforms.value.find((item) => item.id === resource.platform_id);
    const siblings = resourcesByPlatform[resource.platform_id] || [];
    const branch = siblings.some(
      (item) => item.id !== resource.id && item.parent_gb_id === resource.gb_id,
    );
    const capability = playability(resource, platform, branch);
    if (!capability.allowed || busy.has('play:' + resource.id)) return;
    view.value = 'resources';
    if (activeSessions.value.some((session) => session.resource.id === resource.id)) return;
    await run(`play:${resource.id}`, async () => {
      const lease = await api.start(resource.platform_id, resource.gb_id);
      let playback;
      try {
        playback = await api.playbackAddress(lease.playback_id);
      } catch (cause) {
        await api.release(lease.id).catch(() => {});
        throw cause;
      }
      const session = {
        lease,
        resource: { ...resource },
        playback,
        released: false,
        problem: '',
      };
      sessions.value.unshift(session);
      saveSessions();
      view.value = 'resources';

      await refreshSession(session);
      saveSessions();
    });
  }
  async function sessionAction(session, action) {
    if (action === 'force' && !window.confirm('强制停止会结束该播放的所有观看者，确认继续？'))
      return;
    await run(`session:${session.lease.id}`, async () => {
      if (action === 'close') {
        await api.release(session.lease.id);
        session.released = true;
        forget(session);
        await refresh();
        return;
      }
      if (action === 'release') {
        await api.release(session.lease.id);
        session.released = true;
      } else if (action === 'force') {
        await api.forceStop(session.playback.id);
        for (const row of sessions.value.filter((s) => s.playback.id === session.playback.id))
          row.released = true;
      }
      session.playback = await api.playback(session.lease.playback_id);
      saveSessions();

      await refresh();
    });
  }
  function forget(session) {
    sessions.value = sessions.value.filter((s) => s !== session);
    saveSessions();
  }
  async function copy(text) {
    await run('copy', async () => {
      await navigator.clipboard.writeText(text);
      notice.value = '播放地址已复制';
      setTimeout(() => {
        notice.value = '';
      }, 2500);
    });
  }
  onMounted(() => {
    if (settings.token) connect();
  });
  // Closing the player disconnects its reader. The server reaps idle streams.
  onBeforeUnmount(() => {
    clearInterval(timer);
    closeEvents();
    api?.abort();
  });
  return {
    settings,
    connectionOpen,
    connected,
    reachable,
    view,
    error,
    notice,
    busy,
    stats,
    platforms,
    resourcesByPlatform,
    expandedPlatforms,
    expandedNodes,
    loadingPlatforms,
    resourceErrors,
    togglePlatform,
    toggleNode,
    loadPlatform,
    selected,
    info,
    deviceStatus,
    jobs,
    sessions,
    filters,
    platformForm,
    editing,
    operationId,
    activeSessions,
    run,
    refresh,
    connect,
    disconnect,
    openView,
    editPlatform,
    savePlatform,
    removePlatform,
    sync,
    lookupOperation,
    filterResources,
    selectResource,
    device,
    start,
    sessionAction,
    forget,
    copy,
  };
}
