export class ApiError extends Error {
  constructor(message, code, traceId = '', status = 0) {
    super(message);
    this.code = code;
    this.traceId = traceId;
    this.status = status;
  }
}

// Same-origin /api is the default. A custom origin needs its own CORS policy.
export function createApi({ token, base = '/api', fetcher = fetch }) {
  const root = base.replace(/\/+$/, '');
  const controllers = new Set();
  async function request(path, method = 'GET', body, { keepalive = false } = {}) {
    const controller = new AbortController();
    controllers.add(controller);
    const timer = setTimeout(() => controller.abort(), 15000);
    try {
      const response = await fetcher(`${root}${path}`, {
        method,
        headers: {
          Authorization: `Bearer ${token}`,
          ...(body === undefined ? {} : { 'Content-Type': 'application/json' }),
        },
        body: body === undefined ? undefined : JSON.stringify(body),
        signal: controller.signal,
        keepalive,
      });
      let envelope;
      try {
        envelope = await response.json();
      } catch {
        throw new ApiError('服务响应不是有效 JSON，请检查 API 代理地址', 0, '', response.status);
      }
      if (!response.ok || envelope.code !== 0) {
        throw new ApiError(
          envelope.message || '请求失败',
          envelope.code,
          envelope.trace_id,
          response.status,
        );
      }
      return envelope.data;
    } catch (error) {
      if (error instanceof ApiError) throw error;
      throw new ApiError(
        error.name === 'AbortError' ? '请求超时或已取消' : '连接失败，请检查上级服务和 API 代理',
        0,
      );
    } finally {
      clearTimeout(timer);
      controllers.delete(controller);
    }
  }
  const id = encodeURIComponent;
  return {
    abort: () => {
      for (const controller of controllers) controller.abort();
    },
    health: () => request('/health'),
    platforms: () => request('/platforms'),
    platform: (value) => request(`/platforms/${id(value)}`),
    savePlatform: (value, editing = false) =>
      request(
        editing ? `/platforms/${id(value.id)}` : '/platforms',
        editing ? 'PATCH' : 'POST',
        value,
      ),
    removePlatform: (value) => request(`/platforms/${id(value)}`, 'DELETE'),
    operation: (value) => request(`/operations/${id(value)}`),
    resources: (filters) =>
      request(
        `/resources?${new URLSearchParams(Object.entries(filters).filter(([, value]) => value !== ''))}`,
      ),
    resource: (value) => request(`/resources/${id(value)}`),
    info: (value, refresh = false) =>
      request(
        `/resources/${id(value)}/${refresh ? 'info-refresh' : 'info'}`,
        refresh ? 'POST' : 'GET',
      ),
    status: (value, refresh = false) =>
      request(
        `/resources/${id(value)}/${refresh ? 'status-refresh' : 'status'}`,
        refresh ? 'POST' : 'GET',
      ),
    start: (resourceId, mode) =>
      request('/playbacks', 'POST', {
        resource_id: resourceId,
        ...(mode === undefined ? {} : { mode }),
      }),
    playback: (value) => request(`/playbacks/${id(value)}`),
    forceStop: (value) => request(`/playbacks/${id(value)}/stop`, 'POST'),
    renew: (value) => request(`/playback-leases/${id(value)}/renew`, 'POST'),
    release: (value, options) =>
      request(`/playback-leases/${id(value)}`, 'DELETE', undefined, options),
  };
}
