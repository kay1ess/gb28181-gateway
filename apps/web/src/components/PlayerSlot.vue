<script setup>
import { computed, ref, shallowRef, watch } from 'vue';
import { SmsPlayer } from 'smsplayer/vue';
import { playerSource } from '../player_source.js';

const props = defineProps({ playback: Object, resource: Object, released: Boolean });
const emit = defineEmits(['error']);
const player = shallowRef(null);
const status = ref('loading');
const failure = ref('');
const assetsBase = `${import.meta.env.BASE_URL}smsplayer/`;
const active = computed(() => !props.released && props.playback?.state === 'playing');
const source = computed(() =>
  active.value ? playerSource(props.playback, import.meta.env.BASE_URL) : '',
);
const options = Object.freeze({
  cacheduration: 3,
  wasmSIMD: true,
  hevcRenderer: 'canvas2d',
  loglevel: 'error',
});
watch(source, () => {
  failure.value = '';
  status.value = 'loading';
});
function onError(error) {
  failure.value = error.message;
  emit('error', `${props.resource?.name || '摄像头'}：${error.message}`);
}
function retry() {
  failure.value = '';
  player.value?.reload();
}
</script>

<template>
  <section class="camera-player" aria-label="摄像头播放器" :data-player-state="status">
    <SmsPlayer
      v-if="source"
      ref="player"
      :src="source"
      :height="'100%'"
      :assets-base="assetsBase"
      :options="options"
      autoplay
      muted
      controls
      @statechange="(state) => (status = state.status)"
      @error="onError"
    >
      <template #error="{ error }"
        ><span>{{ error.message }}</span></template
      >
    </SmsPlayer>
    <div v-else class="player-slot">
      <div class="player-mark" aria-hidden="true">▷</div>
      <strong>{{
        released
          ? '本页观看已结束'
          : active
            ? '缺少浏览器播放地址'
            : playback?.state === 'failed'
              ? '点播失败'
              : playback?.state === 'ended'
                ? '播放已结束'
                : '等待媒体就绪'
      }}</strong>
      <span v-if="active">请使用支持 HTTP-FLV 输出的上级服务</span>
    </div>
    <div v-if="source && (failure || status === 'blocked')" class="player-feedback">
      <button v-if="failure" @click="retry">重试播放</button>
      <button v-else @click="player?.play()">点击播放</button>
    </div>
  </section>
</template>

<style scoped>
.camera-player {
  aspect-ratio: 16 / 9;
  min-height: 180px;
  position: relative;
  background: #11151c;
}
.camera-player :deep(.sms-vue-player) {
  height: 100%;
  position: absolute !important;
  inset: 0;
}
.camera-player :deep(.sms-vue-player__media) {
  height: 100% !important;
}
.player-feedback {
  position: absolute;
  top: 12px;
  left: 12px;
  z-index: 5;
}
.player-feedback button {
  background: #eaf2e8;
  color: #183c2c;
}
</style>
