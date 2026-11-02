@echo off
rem HX-07 network diagnostics. Put this file next to RobotSlice.exe and double-click it instead of RobotSlice.exe.
rem The game runs normally; every second it appends a report to RobotSlice\Saved\Logs\ (file name starts with the Chinese for "network diagnostics", .txt).
rem When the game closes, that folder opens. Send the newest .txt file back.
start "" /wait "%~dp0RobotSlice.exe" -MechNetDiag
explorer "%~dp0RobotSlice\Saved\Logs"
