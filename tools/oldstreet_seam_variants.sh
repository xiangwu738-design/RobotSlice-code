#!/bin/bash
# 瞬移实测的几个对照组，每组单独拿锁。用法：bash tools/oldstreet_seam_variants.sh 组名...
cd /d/RobotSlice
for v in "$@"; do
  case "$v" in
    cut)    export OS_SEAM_CUT=1 OS_SEAM_EXEC="" ;;
    noblur) export OS_SEAM_CUT=0 OS_SEAM_EXEC="r.MotionBlurQuality 0" ;;
    cutnoblur) export OS_SEAM_CUT=1 OS_SEAM_EXEC="r.MotionBlurQuality 0" ;;
    nogi)   export OS_SEAM_CUT=0 OS_SEAM_EXEC="r.MotionBlurQuality 0;r.DynamicGlobalIlluminationMethod 0;r.ReflectionMethod 2" ;;
    clean)  export OS_SEAM_CUT=0 OS_SEAM_EXEC="r.MotionBlurQuality 0;showflag.Grain 0" ;;
    nofog)  export OS_SEAM_CUT=0 OS_SEAM_EXEC="r.MotionBlurQuality 0;showflag.Grain 0;r.VolumetricFog 0" ;;
    cutnofog) export OS_SEAM_CUT=1 OS_SEAM_EXEC="showflag.Grain 0;r.VolumetricFog 0" ;;
    cutclean) export OS_SEAM_CUT=1 OS_SEAM_EXEC="showflag.Grain 0" ;;
    fastgi) export OS_SEAM_CUT=1 OS_SEAM_EXEC="r.MotionBlurQuality 0;r.Lumen.ScreenProbeGather.Temporal.MaxFramesAccumulated 4;r.Lumen.ScreenProbeGather.RadianceCache.Clipmap.WorldExtent 6000" ;;
  esac
  export OS_SEAM_TAG=$v
  bash tools/oldstreet_ue_run.sh "老街瞬移实测 $v" bash tools/oldstreet_seam_run.sh 16.3 2>&1 | grep -a -E "退出码|截到|\[osseam\] 帧时间毫秒 (fwd_street|shade) k (2|16)" | cut -c1-230
  py -3 tools/oldstreet_seam_compare.py Saved/Screenshots/OldStreet/seam_$v "docs/老旧街道_20261102/瞬移实测_$v.jpg"
done
