import { defineComponent, h, ref, shallowRef, computed, markRaw, toRaw, mergeProps, watch, onMounted, onBeforeUnmount, onActivated, onDeactivated } from 'vue';
import { loadPlayerRuntime } from '../types/runtime.mjs';

const forwardedEvents = ['loadstart', 'ready', 'play', 'pause', 'waiting', 'playing', 'autoplayblocked', 'ended',
    'mediainfo', 'durationchange', 'cacheduration', 'volumechange', 'ratechange', 'stop'];
const size = value => typeof value === 'number' ? `${value}px` : value;
const playing = state => ['playing', 'waiting'].includes(state?.status);

export const SmsPlayer = defineComponent({
    name: 'SmsPlayer',
    inheritAttrs: false,
    props: {
        src: { type: String, default: '' },
        autoplay: { type: Boolean, default: false },
        controls: { type: Boolean, default: true },
        loop: { type: Boolean, default: false },
        muted: { type: Boolean, default: false },
        volume: { type: Number, default: 1, validator: value => Number.isFinite(value) && value >= 0 && value <= 1 },
        playbackRate: { type: Number, default: 1, validator: value => Number.isFinite(value) && value > 0 },
        width: { type: [Number, String], default: '100%' },
        height: { type: [Number, String], default: 360 },
        assetsBase: { type: String, default: '/smsplayer/' },
        nonce: { type: String, default: undefined },
        options: { type: Object, default: () => ({}) },
        runtime: { type: Object, default: null },
        uiFactory: { type: Function, default: undefined },
        progressInterval: { type: Number, default: 100, validator: value => Number.isFinite(value) && value >= 0 }
    },
    emits: ['created', 'destroy', 'error', 'statechange', 'timeupdate', 'update:src', 'update:volume', 'update:muted', 'update:playbackRate', ...forwardedEvents],
    setup(props, { emit, expose, attrs, slots }) {
        const container = ref(null), state = shallowRef(null), failure = shallowRef(null), loading = ref(false);
        let player = null, runtime = null, binding = null, listeners = [], mounted = false, inactive = false;
        let generation = 0, uiGeneration = 0, initializing = null, needsInit = false, ready = false, source = props.src, loadedSource = '';
        let shouldPlay = props.autoplay, loadRequested = true, resumeOnActivate = false, preferredVolume = props.volume, preferredRate = props.playbackRate;
        let stateTimer = null, pendingState = null, publishedAt = 0;
        let configuredOptions = { ...props.options };

        const reportError = (error, phase) => {
            const value = { message: String(error?.message || error?.error?.message || error || 'Playback failed'), error, phase };
            failure.value = value; emit('error', value);
        };
        const clearProgress = () => { clearTimeout(stateTimer); stateTimer = null; pendingState = null; };
        const commit = next => {
            clearProgress();
            const previous = state.value;
            state.value = next; publishedAt = performance.now(); emit('statechange', next);
            if (!previous || previous.currentTime !== next.currentTime) emit('timeupdate', next.currentTime);
        };
        const publish = next => {
            const previous = state.value;
            const urgent = !previous || ['status', 'source', 'generation', 'volume', 'playbackRate', 'mediaInfo', 'error'].some(key => previous[key] !== next[key]) ||
                ['audio', 'seek', 'snapshot'].some(key => previous.capabilities?.[key] !== next.capabilities?.[key]);
            const remaining = props.progressInterval - (performance.now() - publishedAt);
            if (urgent || remaining <= 0) commit(next);
            else {
                pendingState = next;
                if (stateTimer === null) stateTimer = setTimeout(() => { if (mounted && pendingState) commit(pendingState); }, remaining);
            }
        };
        const command = (name, ...args) => {
            try { return player?.[name](...args); }
            catch (error) { reportError(error, 'command'); }
        };
        const play = () => { shouldPlay = true; if (ready && !inactive) command('play'); };
        const pause = () => { shouldPlay = false; command('pause'); };
        const seek = seconds => { if (Number.isFinite(seconds)) command('seek', seconds); };
        const setVolume = value => {
            if (!Number.isFinite(value)) return;
            preferredVolume = Math.max(0, Math.min(1, value));
            if (player && player.state.volume !== (props.muted ? 0 : preferredVolume)) player.volume = props.muted ? 0 : preferredVolume;
        };
        const setPlaybackRate = value => {
            if (!Number.isFinite(value) || value <= 0) return;
            preferredRate = value;
            if (player && player.state.playbackRate !== value) player.playbackRate = value;
        };
        const applySource = (force = false) => {
            if (!player || inactive || !loadRequested) return;
            const value = String(source || '').trim();
            if (!force && value === loadedSource) return;
            failure.value = null; ready = false;
            if (!value) { loadedSource = ''; command('stop'); return; }
            try {
                const url = new URL(value, container.value.ownerDocument.baseURI);
                if (!['http:', 'https:'].includes(url.protocol)) throw new Error('视频地址必须使用 HTTP 或 HTTPS');
                loadedSource = value; player.load(url.href);
            } catch (error) { loadedSource = ''; command('stop'); reportError(error, 'load'); }
        };
        const load = value => {
            source = value || ''; loadRequested = true; shouldPlay = props.autoplay || shouldPlay;
            emit('update:src', source); applySource(true);
        };
        const reload = () => { loadRequested = true; if (player) applySource(true); else initialize(true); };
        const stop = () => { shouldPlay = false; loadRequested = false; loadedSource = ''; ready = false; command('stop'); };

        const dispose = () => {
            uiGeneration++; clearProgress();
            const previousBinding = binding; binding = null;
            try { previousBinding?.destroy(); } catch (error) { reportError(error, 'ui'); }
            listeners.forEach(remove => remove()); listeners = [];
            const previous = player; player = null; ready = false; loadedSource = '';
            if (previous) {
                try { previous.destroy(); } finally { emit('destroy'); }
            }
        };
        const syncUI = async () => {
            const token = ++uiGeneration, instance = player;
            const previousBinding = binding; binding = null;
            try { previousBinding?.destroy(); } catch (error) { reportError(error, 'ui'); }
            if (!instance || !props.controls) return;
            try {
                let nextRuntime = runtime;
                if (!nextRuntime.ui) {
                    if (props.runtime) throw new Error('runtime.ui is required when controls are enabled');
                    nextRuntime = await loadPlayerRuntime({ assetsBase: props.assetsBase, controls: true, nonce: props.nonce });
                }
                if (!mounted || token !== uiGeneration || instance !== player || !props.controls) return;
                runtime = nextRuntime;
                binding = runtime.ui.mountUI(instance, props.uiFactory || runtime.ui.createUI);
                if (failure.value?.phase === 'ui') failure.value = null;
            } catch (error) { if (mounted && token === uiGeneration && instance === player) reportError(error, 'ui'); }
        };
        const connect = instance => {
            const listen = (type, handler) => {
                const listener = event => { if (instance === player) handler(event); };
                instance.addEventListener(type, listener);
                listeners.push(() => instance.removeEventListener(type, listener));
            };
            listen('statechange', event => publish(event.detail));
            listen('error', event => reportError(event, 'playback'));
            for (const type of forwardedEvents) listen(type, event => {
                if (type === 'ready') ready = true;
                if (type === 'play') shouldPlay = true;
                if (type === 'pause') shouldPlay = false;
                if (type === 'volumechange') {
                    if (!props.muted || event.detail > 0) { preferredVolume = event.detail; emit('update:volume', event.detail); }
                    emit('update:muted', event.detail === 0);
                }
                if (type === 'ratechange') { preferredRate = event.detail; emit('update:playbackRate', event.detail); }
                if (type === 'stop') { ready = false; shouldPlay = false; loadRequested = false; }
                emit(type, event.detail);
                if (type === 'ready' && shouldPlay && !inactive) command('play');
                if (type === 'ended' && props.loop) {
                    const token = generation;
                    Promise.resolve().then(() => {
                        if (mounted && !inactive && token === generation && instance === player && player.state.status === 'ended') {
                            shouldPlay = true; applySource(true);
                        }
                    });
                }
            });
            commit(instance.state);
        };
        const initialize = (force = false) => {
            if (!mounted) return Promise.resolve();
            if (inactive) { if (force) needsInit = true; return Promise.resolve(); }
            if (initializing && !force) return initializing;
            const token = ++generation;
            dispose(); state.value = null; failure.value = null; loading.value = true;
            const task = (async () => {
                try {
                    const loaded = props.runtime || await loadPlayerRuntime({ assetsBase: props.assetsBase, controls: props.controls, nonce: props.nonce });
                    if (!mounted || inactive || token !== generation) return;
                    runtime = { core: toRaw(loaded.core), ui: loaded.ui ? toRaw(loaded.ui) : null };
                    const instance = markRaw(runtime.core.createPlayerInstance({ ...props.options, container: container.value, autoPlay: false }));
                    player = instance;
                    instance.volume = props.muted ? 0 : preferredVolume; instance.playbackRate = preferredRate;
                    connect(instance); await syncUI();
                    if (!mounted || token !== generation || instance !== player) return;
                    emit('created', instance); applySource();
                } catch (error) {
                    if (mounted && token === generation) { dispose(); reportError(error, 'runtime'); }
                } finally { if (token === generation) loading.value = false; }
            })();
            initializing = task;
            task.finally(() => { if (token === generation) initializing = null; });
            return task;
        };

        watch(() => props.src, value => {
            if (value !== source) { source = value; loadRequested = true; shouldPlay = props.autoplay || shouldPlay; }
            applySource();
        });
        watch(() => props.autoplay, value => { if (value) play(); else pause(); });
        watch(() => props.volume, setVolume);
        watch(() => props.muted, value => {
            if (player) player.volume = value ? 0 : preferredVolume;
        });
        watch(() => props.playbackRate, setPlaybackRate);
        watch(() => [props.controls, props.uiFactory], () => { syncUI(); });
        // Player options are flat. Equal inline objects must not reload a stream
        // whenever the parent renders a progress update.
        watch(() => ({ ...props.options }), next => {
            const keys = Object.keys(next);
            if (keys.length === Object.keys(configuredOptions).length && keys.every(key => Object.is(next[key], configuredOptions[key]))) return;
            configuredOptions = next; initialize(true);
        });
        watch(() => [props.assetsBase, props.runtime, props.nonce], () => { initialize(true); });
        watch(() => props.progressInterval, () => { if (pendingState) commit(pendingState); });
        onMounted(() => { mounted = true; initialize(); });
        onBeforeUnmount(() => { mounted = false; generation++; dispose(); });
        onDeactivated(() => {
            inactive = true; resumeOnActivate = playing(player?.state) || (shouldPlay && !ready);
            if (resumeOnActivate) command('pause');
        });
        onActivated(() => {
            inactive = false;
            if (resumeOnActivate) shouldPlay = true;
            if (needsInit) { needsInit = false; initialize(true); }
            else if (!player) initialize();
            else { applySource(); if (resumeOnActivate) play(); }
            resumeOnActivate = false;
        });

        const api = { play, pause, seek, load, reload, stop, setVolume, setPlaybackRate,
            snapshot: () => command('snapshot'), debug: () => command('debug'),
            getPlayer: () => player, getState: () => player?.state || null,
            state: computed(() => state.value), error: computed(() => failure.value) };
        expose(api);
        return () => {
            const context = { ...api, state: state.value, error: failure.value, player };
            const overlay = slots.default?.(context);
            const errorContent = failure.value && (slots.error?.(context) || ((failure.value.phase !== 'playback' || !props.controls) && [
                h('span', failure.value.message), h('button', { type: 'button', onClick: reload, style: { pointerEvents: 'auto', marginLeft: '12px' } }, '重试')
            ]));
            return h('div', mergeProps({ class: 'sms-vue-player', role: 'region', 'aria-label': '视频播放器', style: { position: 'relative', width: size(props.width), minWidth: 0 } }, attrs), [
                h('div', { ref: container, class: 'sms-vue-player__media', style: { position: 'relative', width: '100%', height: size(props.height), background: '#11151c', overflow: 'hidden' } }),
                loading.value ? h('div', { role: 'status', class: 'sms-vue-player__loading', style: { position: 'absolute', inset: 0, display: 'grid', placeItems: 'center', color: '#fff', pointerEvents: 'none' } }, slots.loading?.(context) || '播放器加载中…') : null,
                errorContent ? h('div', { role: 'alert', class: 'sms-vue-player__error', style: { position: 'absolute', inset: 0, display: 'flex', alignItems: 'center', justifyContent: 'center', color: '#fff', background: '#111d', padding: '20px' } }, errorContent) : null,
                overlay ? h('div', { class: 'sms-vue-player__overlay', style: { position: 'absolute', inset: 0, pointerEvents: 'none' } }, overlay) : null
            ]);
        };
    }
});

SmsPlayer.install = app => { app.component(SmsPlayer.name, SmsPlayer); };
export { loadPlayerRuntime };
export default SmsPlayer;
