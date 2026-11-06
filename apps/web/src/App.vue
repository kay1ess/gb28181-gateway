<script setup>
import { computed, ref, watch } from 'vue';
import PlayerSlot from './components/PlayerSlot.vue';
import ResourceTree from './components/ResourceTree.vue';
import { terminal, useConsole } from './use_console.js';
import { modalFocus as vModalFocus } from './modal_focus.js';

const {
  settings,
  connectionOpen,
  connected,
  reachable,
  error,
  notice,
  busy,
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
  activeSessions,
  run,
  refresh,
  connect,
  disconnect,
  editPlatform,
  savePlatform,
  removePlatform,
  sync,
  filterResources,
  selectResource,
  device,
  start,
  sessionAction,
  forget,
  copy,
} = useConsole();

const names = {
  online: '在线',
  offline: '离线',
  unknown: '未知',
  running: '同步中',
  succeeded: '已完成',
  failed: '失败',
  cancelled: '已取消',
  negotiating: '信令协商中',
  waiting_media: '等待媒体',
  playing: '媒体就绪',
  stopping: '停止中',
  ended: '已结束',
};
const stateName = (value) => names[value] || value || '未知';
const formatTime = (value) =>
  value ? new Date(value).toLocaleString('zh-CN', { hour12: false }) : '—';
const platformName = (id) => platforms.value.find((p) => p.id === id)?.name || id;
const modeName = (value) =>
  ({ udp: 'UDP', tcp_passive: 'TCP 被动', tcp_active: 'TCP 主动' })[value] || value;
const detailFields = (value) =>
  Object.entries(value || {})
    .filter(([key]) => key !== 'resource_id')
    .map(([key, content]) => [
      {
        name: '名称',
        manufacturer: '厂商',
        model: '型号',
        firmware: '固件',
        state: '在线状态',
        status: '设备状态',
        queried_ms: '采集时间',
      }[key] || key,
      key === 'queried_ms'
        ? formatTime(content)
        : key === 'state'
          ? stateName(content)
          : content || '—',
    ]);

const previewOpen = ref(false);
const syncDialog = ref(null);
const syncPlatform = computed(() => syncDialog.value?.platform);
const detailsOpen = ref(false);
const closingPreview = ref(false);
const syncJob = computed(() => jobs.value.find((job) => job.id === syncDialog.value?.jobId));
watch(
  () => sessions.value.length,
  (count, previous) => {
    if (count > previous) previewOpen.value = true;
    if (!count) previewOpen.value = false;
  },
);
async function closeSession(session) {
  if (session.released || terminal(session.playback.state)) forget(session);
  else await sessionAction(session, 'close');
}
async function closePreview() {
  if (closingPreview.value) return;
  closingPreview.value = true;
  try {
    await Promise.all([...sessions.value].map(closeSession));
    if (!sessions.value.length) previewOpen.value = false;
  } finally {
    closingPreview.value = false;
  }
}
async function playResource(resource) {
  await start(resource);
  if (sessions.value.length) previewOpen.value = true;
}
async function showDetails(resource) {
  detailsOpen.value = true;
  await selectResource(resource);
}
async function showSync(platform) {
  syncDialog.value = { platform, jobId: null };
  const dialog = syncDialog.value;
  const jobId = await sync(platform);
  if (syncDialog.value === dialog) dialog.jobId = jobId;
}
function closeSync() {
  syncDialog.value = null;
}
</script>
<template>
  <div class="demo-shell">
    <main
      class="demo-main"
      :inert="connectionOpen || platformForm || syncPlatform || detailsOpen || previewOpen"
    >
      <header class="page-header">
        <div>
          <h1>下级与资源</h1>
        </div>
        <div class="header-actions">
          <span class="connection-label"
            ><i :class="['dot', { live: connected && reachable }]" />{{
              connected ? (reachable ? '服务已连接' : '连接异常，正在重试') : '尚未连接服务'
            }}</span
          >
          <button @click="connectionOpen = true">连接设置</button>
          <button
            :disabled="!connected || busy.has('refresh')"
            @click="run('refresh', () => refresh())"
          >
            ↻ 刷新
          </button>
          <button class="primary" :disabled="!connected" @click="editPlatform()">
            ＋ 添加下级
          </button>
        </div>
      </header>
      <div
        v-if="
          error && !connectionOpen && !platformForm && !syncPlatform && !detailsOpen && !previewOpen
        "
        class="alert error"
        role="alert"
      >
        <div>
          <strong>{{ error.message }}</strong
          ><small v-if="error.code">错误码 {{ error.code }} · {{ error.traceId }}</small>
        </div>
        <button aria-label="关闭错误提示" @click="error = null">×</button>
      </div>
      <div v-if="notice" class="toast" role="status">{{ notice }}</div>
      <div v-if="!connected" class="empty panel">
        <h2>连接上级平台</h2>
        <button @click="connectionOpen = true">设置连接</button>
      </div>
      <template v-else>
        <form class="demo-toolbar" @submit.prevent="filterResources()">
          <label class="search-field"
            >搜索资源<input v-model="filters.keyword" type="search" placeholder="名称或国标编码"
          /></label>
          <label
            >在线状态<select v-model="filters.state" @change="filterResources()">
              <option value="">全部状态</option>
              <option value="online">在线</option>
              <option value="offline">离线</option>
              <option value="unknown">未知</option>
            </select></label
          >
          <button>搜索</button>
          <button v-if="sessions.length" type="button" @click="previewOpen = true">
            打开预览（{{ sessions.length }}）
          </button>
        </form>
        <ResourceTree
          :platforms="platforms"
          :resources-by-platform="resourcesByPlatform"
          :expanded-platforms="expandedPlatforms"
          :expanded-nodes="expandedNodes"
          :loading-platforms="loadingPlatforms"
          :resource-errors="resourceErrors"
          :filters="filters"
          :selected-id="selected?.id"
          :busy="busy"
          :jobs="jobs"
          @toggle-platform="togglePlatform"
          @toggle-node="toggleNode"
          @reload="(platform) => loadPlatform(platform, true)"
          @select="showDetails"
          @play="playResource"
          @sync="showSync"
          @edit="editPlatform"
          @remove="removePlatform"
        />
      </template>
    </main>
    <div v-if="previewOpen" class="modal-backdrop" @click.self="closePreview">
      <section
        class="modal preview-modal"
        role="dialog"
        aria-modal="true"
        aria-labelledby="preview-title"
        v-modal-focus="closePreview"
      >
        <div class="modal-header">
          <div>
            <h2 id="preview-title">视频预览</h2>
          </div>
          <button aria-label="关闭播放窗口" :disabled="closingPreview" @click="closePreview">
            ×
          </button>
        </div>
        <div v-if="error" class="form-error" role="alert">
          {{ error.message }}<small>{{ error.code }} {{ error.traceId }}</small>
        </div>
        <div class="session-grid resource-previews">
          <article v-for="session in sessions" :key="session.lease.id" class="panel session-card">
            <div class="panel-heading">
              <div>
                <h2>{{ session.resource.name || session.resource.gb_id }}</h2>
                <p>
                  {{ platformName(session.resource.platform_id) }} ·
                  {{ modeName(session.playback.mode) }}
                </p>
              </div>
              <span :class="['badge', session.released ? 'ended' : session.playback.state]">{{
                session.released ? '本页已释放' : stateName(session.playback.state)
              }}</span>
            </div>
            <PlayerSlot
              :playback="session.playback"
              :resource="session.resource"
              :released="session.released"
              @error="(message) => (error = { message })"
            />
            <div class="session-body">
              <div
                v-if="session.problem || session.playback.error?.code"
                class="session-error"
                role="alert"
              >
                {{ session.problem || session.playback.error.message
                }}<small v-if="session.playback.error?.code"
                  >{{ session.playback.error.code }} · {{ session.playback.error.trace_id }}</small
                >
              </div>
              <details class="preview-diagnostics">
                <summary>播放诊断与地址</summary>
                <div
                  v-for="endpoint in session.playback.endpoints || []"
                  :key="endpoint.url"
                  class="endpoint"
                >
                  <div>
                    <span>{{ endpoint.protocol.toUpperCase() }} 地址</span
                    ><code>{{ endpoint.url }}</code>
                  </div>
                  <button @click="copy(endpoint.url)">复制</button>
                </div>
                <p v-if="!session.playback.endpoints?.length" class="hint">等待媒体就绪…</p>
                <dl class="session-meta">
                  <dt>播放 ID</dt>
                  <dd class="mono">{{ session.playback.id }}</dd>
                  <dt>SSRC</dt>
                  <dd>{{ session.playback.ssrc || '—' }}</dd>
                  <dt>无人拉流时断开</dt>
                  <dd>{{ session.playback.idle_timeout_seconds || 60 }} 秒</dd>
                </dl>
              </details>
              <div class="session-actions">
                <button
                  :disabled="busy.has('session:' + session.lease.id)"
                  @click="closeSession(session)"
                >
                  关闭预览
                </button>
              </div>
            </div>
          </article>
        </div>
        <div class="modal-actions"><button @click="previewOpen = false">继续选择设备</button></div>
      </section>
    </div>
    <div v-if="syncPlatform" class="modal-backdrop" @click.self="closeSync">
      <section
        class="modal wide"
        role="dialog"
        aria-modal="true"
        aria-labelledby="sync-title"
        v-modal-focus="closeSync"
      >
        <div class="modal-header">
          <div>
            <h2 id="sync-title">目录同步</h2>
            <p>{{ syncPlatform.name || syncPlatform.gb_id }}</p>
          </div>
          <button aria-label="关闭同步窗口" @click="closeSync">×</button>
        </div>
        <div v-if="error" class="form-error" role="alert">
          {{ error.message }}<small>{{ error.code }} {{ error.traceId }}</small>
        </div>
        <p v-if="!syncJob">
          {{ busy.has('sync:' + syncPlatform.id) ? '正在请求下级…' : '尚未取得同步任务' }}
        </p>
        <div v-else class="job-row">
          <span :class="['badge', syncJob.state]">{{ stateName(syncJob.state) }}</span
          ><span>{{ syncJob.received }} / {{ syncJob.expected }} 项</span
          ><progress
            v-if="syncJob.state === 'running' && syncJob.expected > 0"
            aria-label="目录同步进度"
            :value="syncJob.received"
            :max="syncJob.expected"
          />
          <span v-if="syncJob.error?.code" class="danger-text"
            >{{ syncJob.error.message }} · {{ syncJob.error.code
            }}<small>{{ syncJob.error.trace_id }}</small></span
          >
        </div>
        <div class="modal-actions">
          <button @click="closeSync">关闭</button>
        </div>
      </section>
    </div>
    <div v-if="detailsOpen" class="modal-backdrop" @click.self="detailsOpen = false">
      <section
        class="modal wide"
        role="dialog"
        aria-modal="true"
        aria-label="设备详情"
        v-modal-focus="() => (detailsOpen = false)"
      >
        <div class="modal-header">
          <h2>资源信息</h2>
          <button aria-label="关闭设备详情" @click="detailsOpen = false">×</button>
        </div>
        <div v-if="error" class="form-error" role="alert">
          {{ error.message }}<small>{{ error.code }} {{ error.traceId }}</small>
        </div>
        <div class="details">
          <div class="panel-heading">
            <h2>设备详情</h2>
            <span class="tag">{{ selected?.parental ? '目录分组' : '资源' }}</span>
          </div>
          <div v-if="!selected" class="empty">
            <span class="empty-icon">◎</span>
          </div>
          <template v-else
            ><div class="detail-body">
              <h3>{{ selected.name }}</h3>
              <dl>
                <dt>国标编码</dt>
                <dd class="mono">{{ selected.gb_id }}</dd>
                <dt>资源 ID</dt>
                <dd class="mono">{{ selected.id }}</dd>
                <dt>所属平台</dt>
                <dd>{{ platformName(selected.platform_id) }}</dd>
                <dt>父级编码</dt>
                <dd class="mono">{{ selected.parent_gb_id || '—' }}</dd>
                <dt>厂商 / 型号</dt>
                <dd>
                  {{ selected.manufacturer || '—' }} /
                  {{ selected.model || '—' }}
                </dd>
                <dt>地址 / 行政区</dt>
                <dd>
                  {{ selected.address || '—' }} /
                  {{ selected.civil_code || '—' }}
                </dd>
                <dt>目录更新时间</dt>
                <dd>{{ formatTime(selected.updated_ms) }}</dd>
              </dl>
              <div class="detail-block">
                <h3>设备信息</h3>
                <div class="button-group">
                  <button
                    :disabled="busy.has('device:info') || selected.parental"
                    @click="device('info', false)"
                  >
                    读取缓存</button
                  ><button
                    :disabled="busy.has('device:info') || selected.parental"
                    @click="device('info', true)"
                  >
                    查询下级
                  </button>
                </div>
                <dl v-if="info">
                  <template v-for="[label, value] in detailFields(info)" :key="label"
                    ><dt>{{ label }}</dt>
                    <dd>{{ value }}</dd></template
                  >
                </dl>
                <p v-else class="hint">暂无数据</p>
              </div>
              <div class="detail-block">
                <h3>设备状态</h3>
                <div class="button-group">
                  <button
                    :disabled="busy.has('device:status') || selected.parental"
                    @click="device('status', false)"
                  >
                    读取缓存</button
                  ><button
                    :disabled="busy.has('device:status') || selected.parental"
                    @click="device('status', true)"
                  >
                    查询下级
                  </button>
                </div>
                <dl v-if="deviceStatus">
                  <template v-for="[label, value] in detailFields(deviceStatus)" :key="label"
                    ><dt>{{ label }}</dt>
                    <dd>{{ value }}</dd></template
                  >
                </dl>
                <p v-else class="hint">暂无数据</p>
              </div>
            </div></template
          >
        </div>
      </section>
    </div>
    <div
      v-if="connectionOpen"
      class="modal-backdrop"
      @click.self="connected && (connectionOpen = false)"
    >
      <section
        class="modal"
        v-modal-focus="() => connected && !busy.has('connect') && (connectionOpen = false)"
        role="dialog"
        aria-modal="true"
        aria-labelledby="connection-title"
      >
        <div class="modal-header">
          <div>
            <h2 id="connection-title">连接上级平台</h2>
          </div>
          <button v-if="connected" aria-label="关闭连接设置" @click="connectionOpen = false">
            ×
          </button>
        </div>
        <form @submit.prevent="connect">
          <label
            >API 地址<input v-model="settings.base" required placeholder="/api" autocomplete="url"
          /></label>
          <label
            >API 令牌<input
              v-model="settings.token"
              type="password"
              required
              placeholder="输入配置中的 api_token"
              autocomplete="off"
          /></label>
          <p v-if="activeSessions.length" class="hint">请先关闭预览再切换连接。</p>
          <div v-if="error" class="form-error" role="alert">
            {{ error.message
            }}<small v-if="error.code">错误码 {{ error.code }} · {{ error.traceId }}</small>
          </div>
          <div class="modal-actions">
            <button
              v-if="connected"
              type="button"
              class="danger-text"
              :disabled="busy.has('disconnect')"
              @click="disconnect"
            >
              断开并释放本页点播</button
            ><button
              class="primary"
              :disabled="busy.has('connect') || (connected && activeSessions.length > 0)"
            >
              {{ busy.has('connect') ? '连接中…' : '连接服务' }}
            </button>
          </div>
        </form>
      </section>
    </div>

    <div
      v-if="platformForm"
      class="modal-backdrop"
      @click.self="!busy.has('platform-save') && (platformForm = null)"
    >
      <section
        class="modal wide"
        v-modal-focus="() => !busy.has('platform-save') && (platformForm = null)"
        role="dialog"
        aria-modal="true"
        aria-labelledby="platform-title"
      >
        <div class="modal-header">
          <div>
            <h2 id="platform-title">
              {{ editing ? '编辑下级平台' : '添加下级平台' }}
            </h2>
          </div>
          <button
            aria-label="关闭平台表单"
            :disabled="busy.has('platform-save')"
            @click="platformForm = null"
          >
            ×
          </button>
        </div>
        <form @submit.prevent="savePlatform">
          <div class="form-grid">
            <label
              >平台名称<input
                v-model="platformForm.name"
                required
                maxlength="128"
                placeholder="例如：园区监控平台" /></label
            ><label
              >国标编码<input
                v-model="platformForm.gb_id"
                required
                :readonly="editing"
                pattern="[0-9]{20}"
                maxlength="20"
                placeholder="20 位国标编码" /></label
            ><label
              >SIP 认证域<input
                v-model="platformForm.realm"
                required
                maxlength="64"
                placeholder="3402000000" /></label
            ><label
              >认证用户名<input
                v-model="platformForm.username"
                maxlength="64"
                placeholder="留空时使用国标编码"
                autocomplete="off" /></label
            ><label
              >认证密码<input
                v-model="platformForm.password"
                type="password"
                :required="!editing"
                maxlength="128"
                :placeholder="editing ? '留空保留原密码' : '与下级注册密码保持一致'"
                autocomplete="new-password" /></label
            ><label
              >默认收流方式<select
                v-model="platformForm.default_receive_mode"
                aria-label="默认收流方式"
              >
                <option value="udp">UDP</option>
                <option value="tcp_passive">TCP 被动（上级监听）</option>
                <option value="tcp_active">TCP 主动（上级连接）</option>
              </select></label
            ><label
              >心跳超时（秒）<input
                v-model.number="platformForm.heartbeat_timeout_seconds"
                type="number"
                min="1"
                max="86400"
                required /></label
            ><label class="checkbox-label"
              ><input v-model="platformForm.enabled" type="checkbox" />启用此下级平台</label
            >
          </div>
          <div v-if="error" class="form-error" role="alert">
            {{ error.message
            }}<small v-if="error.code">错误码 {{ error.code }} · {{ error.traceId }}</small>
          </div>
          <div class="modal-actions">
            <button
              type="button"
              :disabled="busy.has('platform-save')"
              @click="platformForm = null"
            >
              取消</button
            ><button class="primary" :disabled="busy.has('platform-save')">
              {{ busy.has('platform-save') ? '保存中…' : '保存平台' }}
            </button>
          </div>
        </form>
      </section>
    </div>
  </div>
</template>
