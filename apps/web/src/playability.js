// Catalog describes candidates, not a guarantee of successful media negotiation.
export function playability(resource, platform, branch = false) {
  const blocked = (reason) => ({ allowed: false, label: '不可播放', reason });
  if (resource.parental || branch) return blocked('目录或设备分组，请展开后选择视频通道');
  if (!platform || platform.enabled === false) return blocked('所属平台未启用');
  if (platform.state !== 'online') return blocked('所属平台未在线注册');
  if (resource.state === 'offline') return blocked('通道离线，请刷新目录或查询设备状态');
  const type = /^\d{20}$/.test(resource.gb_id || '') ? resource.gb_id.slice(10, 13) : '';
  const nonVideo = new Set([
    '111',
    '112',
    '113',
    '114',
    '115',
    '116',
    '117',
    '118',
    '130',
    '133',
    '134',
    '135',
    '136',
    '137',
    '200',
    '215',
    '216',
  ]);
  if (nonVideo.has(type)) return blocked('该编码表示主机、目录或非视频终端，请选择其视频通道');
  if (resource.state !== 'online' || !['131', '132'].includes(type))
    return {
      allowed: true,
      label: '尝试播放',
      reason: '状态或设备类型待验证，需通过点播协商和实际视频确认',
    };
  return {
    allowed: true,
    label: '点播',
    reason: '在线视频通道，可发起点播；实际播放以协商和视频解码结果为准',
  };
}
