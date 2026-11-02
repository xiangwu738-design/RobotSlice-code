"""11-02 石林联机主机卡顿：stat dumphitches 里一次 141 毫秒在 MediaPlayerFacade TickOutput（有视频播放器在跑），代码里没用视频。
找工程里所有视频相关资源（MediaPlayer / MediaSource / MediaTexture / MediaPlaylist），顺着引用往上找哪些地图用到（kind=py）。标签 [media]。"""
import unreal

log = lambda *a: unreal.log_warning('[media] ' + ' '.join(str(x) for x in a))
ar = unreal.AssetRegistryHelpers.get_asset_registry()
opts = unreal.AssetRegistryDependencyOptions(True, True, True, True, True)
classes = ['MediaPlayer', 'FileMediaSource', 'StreamMediaSource', 'PlatformMediaSource', 'MediaTexture', 'MediaPlaylist', 'MediaSoundComponent', 'ImgMediaSource']
found = []
for c in classes:
    try:
        flt = unreal.ARFilter(class_paths=[unreal.TopLevelAssetPath('/Script/MediaAssets', c)], recursive_classes=True)
        for a in ar.get_assets(flt):
            found.append(str(a.package_name))
    except Exception as e:
        log('查', c, '失败', e)
found = sorted(set(found))
log('视频相关资源', len(found), '个')
for p in found[:40]:
    seen = {p}
    frontier = [p]
    maps = set()
    for _ in range(6):
        nxt = []
        for q in frontier:
            for r in ar.get_referencers(q, opts) or []:
                r = str(r)
                if r in seen:
                    continue
                seen.add(r)
                if '/Maps/' in r or r.split('/')[-1].startswith('L_'):
                    maps.add(r)
                nxt.append(r)
        frontier = nxt[:200]
    log(' ', p, '→ 地图：', ', '.join(sorted(maps)) or '（没有地图引用）')
