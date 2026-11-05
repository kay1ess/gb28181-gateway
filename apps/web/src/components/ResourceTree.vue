<script setup>
import { computed } from 'vue';
import { playability } from '../playability.js';
import { buildTree, visibleRows } from '../resource_tree.js';

const props = defineProps({
  platforms: Array,
  resourcesByPlatform: Object,
  expandedPlatforms: Set,
  expandedNodes: Set,
  loadingPlatforms: Set,
  resourceErrors: Object,
  filters: Object,
  selectedId: String,
  busy: Set,
  jobs: Array,
});
defineEmits([
  'toggle-platform',
  'toggle-node',
  'select',
  'play',
  'reload',
  'sync',
  'edit',
  'remove',
]);
const stateName = (value) =>
  ({ online: '在线', offline: '离线', unknown: '未知' })[value] || '未知';
const groups = computed(() =>
  props.platforms
    .filter((platform) => !props.filters.platform_id || platform.id === props.filters.platform_id)
    .map((platform) => {
      const resources = props.resourcesByPlatform[platform.id];
      return {
        platform,
        resources,
        rows: resources
          ? visibleRows(buildTree(platform, resources), props.expandedNodes, props.filters).map(
              (row) => ({ ...row, capability: playability(row.resource, platform, row.branch) }),
            )
          : [],
      };
    }),
);
</script>

<template>
  <section class="panel resource-tree-panel">
    <div class="panel-heading">
      <div>
        <h2>下级列表</h2>
      </div>
      <span class="tag">{{ groups.length }} 个平台</span>
    </div>
    <div role="tree" aria-label="平台资源树" class="resource-tree">
      <div
        v-for="group in groups"
        :key="group.platform.id"
        class="platform-branch"
        role="treeitem"
        :aria-expanded="expandedPlatforms.has(group.platform.id)"
        aria-level="1"
        :aria-label="group.platform.name || group.platform.id"
      >
        <div class="platform-tree-row">
          <button
            class="tree-toggle"
            :aria-label="`${expandedPlatforms.has(group.platform.id) ? '收起' : '展开'}平台 ${group.platform.name || group.platform.id}`"
            :aria-expanded="expandedPlatforms.has(group.platform.id)"
            @click="$emit('toggle-platform', group.platform)"
          ></button>
          <button class="tree-title" @click="$emit('toggle-platform', group.platform)">
            <span class="entity-icon" aria-hidden="true">
              <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor">
                <rect x="2" y="2" width="12" height="12" />
                <path d="M6 2v12m4-12v12M2 6h12m-12 4h12" />
              </svg> </span
            ><span
              ><strong>{{ group.platform.name || group.platform.id }}</strong
              ><small class="mono">{{ group.platform.gb_id }}</small></span
            >
          </button>
          <span :class="['badge', group.platform.state]"
            ><i class="dot" />{{ stateName(group.platform.state) }}</span
          >
          <span class="tree-count">{{
            loadingPlatforms.has(group.platform.id)
              ? '加载中…'
              : group.resources
                ? `${group.resources.length} 项`
                : '未展开'
          }}</span>
          <span class="platform-address"
            >{{
              group.platform.remote_address
                ? group.platform.remote_address + ':' + group.platform.remote_port
                : '等待注册'
            }}<small v-if="group.platform.enabled === false">已禁用</small></span
          >
          <div class="row-actions platform-actions">
            <button
              class="text-button"
              :disabled="
                group.platform.state !== 'online' ||
                busy.has('sync:' + group.platform.id) ||
                jobs?.some(
                  (job) => job.platform_id === group.platform.id && job.state === 'running',
                )
              "
              @click="$emit('sync', group.platform)"
            >
              同步目录</button
            ><button
              class="text-button"
              :disabled="busy.has('edit:' + group.platform.id)"
              @click="$emit('edit', group.platform)"
            >
              编辑</button
            ><button
              class="text-button danger-text"
              :disabled="busy.has('delete:' + group.platform.id)"
              @click="$emit('remove', group.platform)"
            >
              删除
            </button>
          </div>
          <button
            class="text-button"
            :disabled="loadingPlatforms.has(group.platform.id)"
            :aria-label="`刷新资源 ${group.platform.name || group.platform.id}`"
            @click="$emit('reload', group.platform)"
          >
            ↻
          </button>
        </div>
        <div v-if="expandedPlatforms.has(group.platform.id)" role="group">
          <div
            v-if="resourceErrors[group.platform.id]"
            class="tree-message danger-text"
            role="alert"
          >
            {{ resourceErrors[group.platform.id] }}
            <button class="text-button" @click="$emit('reload', group.platform)">重试</button>
          </div>
          <div
            v-else-if="loadingPlatforms.has(group.platform.id) && !group.resources"
            class="tree-message"
            role="status"
          >
            正在读取平台资源…
          </div>
          <template v-else>
            <div
              v-for="row in group.rows"
              :key="row.id"
              role="treeitem"
              :aria-label="row.resource.name || row.resource.gb_id"
              :aria-level="row.depth + 2"
              :aria-expanded="row.branch ? row.open : undefined"
              :aria-selected="selectedId === row.id"
              :class="['resource-tree-row', { selected: selectedId === row.id }]"
              :style="{ '--tree-depth': row.depth }"
            >
              <button
                v-if="row.branch"
                class="tree-toggle"
                :aria-label="`${row.open ? '收起' : '展开'}目录 ${row.resource.name || row.resource.gb_id}`"
                :aria-expanded="row.open"
                @click="$emit('toggle-node', row.id)"
              ></button
              ><span v-else class="tree-leaf-mark" aria-hidden="true">└</span>
              <button
                class="tree-title"
                :title="row.capability.reason"
                @click="
                  row.branch
                    ? $emit('toggle-node', row.id)
                    : row.capability.allowed
                      ? $emit('play', row.resource)
                      : $emit('select', row.resource)
                "
              >
                <span class="tree-resource-icon">{{ row.branch ? '▤' : '◉' }}</span
                ><span
                  ><strong>{{ row.resource.name || '未命名设备' }}</strong
                  ><small class="mono">{{ row.resource.gb_id }}</small></span
                >
              </button>
              <span :class="['badge', row.resource.state]"
                ><i class="dot" />{{ stateName(row.resource.state) }}</span
              >
              <span class="capability-label" :title="row.capability.reason">{{
                row.capability.allowed
                  ? row.capability.label === '点播'
                    ? '视频通道'
                    : '待验证'
                  : row.capability.reason
              }}</span>
              <div class="row-actions">
                <button class="text-button" @click="$emit('select', row.resource)">详情</button
                ><button
                  class="text-button"
                  :disabled="!row.capability.allowed || busy.has(`play:${row.id}`)"
                  :title="row.capability.reason"
                  @click="$emit('play', row.resource)"
                >
                  {{ row.capability.allowed ? row.capability.label : '不可播放' }}
                </button>
              </div>
            </div>
            <div
              v-if="!group.rows.length && !loadingPlatforms.has(group.platform.id)"
              class="tree-message"
            >
              {{ group.resources?.length ? '无匹配资源' : '暂无资源' }}
            </div>
          </template>
        </div>
      </div>
    </div>
    <div v-if="!groups.length" class="empty">
      <h3>没有下级平台</h3>
    </div>
  </section>
</template>
