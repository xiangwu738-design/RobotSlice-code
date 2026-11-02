#!/bin/bash
# 拼某一版成图：bash tools/moon_sheet.sh v2
V="$1"
D="D:/RobotSlice/Saved/Screenshots/Moon/$V"
W="D:/RobotSlice/Inbox/_moon_nasa/work"
cd "$D"
py -3 -I /d/RobotSlice/tools/make_contact_sheet.py $W/${V}_sheet1.jpg 3 640 b=b_spawn_w.png c=c_north_road.png d=d_north_plaza.png e=e_south_tanks.png f=f_dome_in.png g=g_low_fly.png h=h_mid_fly.png i=i_earth.png j=j_massif.png
py -3 -I /d/RobotSlice/tools/make_contact_sheet.py $W/${V}_sheet2.jpg 3 640 k=k_crater.png l=l_rim_view.png m=m_ground_soil.png n=n_south_ridge.png o=o_west_hill.png p=p_mech_earth.png
