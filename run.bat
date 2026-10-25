@echo off
REM %~dp0 is this script's own folder (with trailing backslash), so this
REM always finds trf_system.exe next to run.bat regardless of the current
REM directory the terminal happens to be sitting in when you launch it.
"%~dp0trf_system.exe"
