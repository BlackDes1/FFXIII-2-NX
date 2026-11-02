@echo off
rem FFXIII-2 port: scripted New Game test (muted, minimized, ~12 min): title -> NORMAL MODE Yes -> new save -> opening ->
rem first battle -> fight. Fresh save folder; frame captures in out\pc\captures_newgame; log out\test_newgame.log.
rem Run after tools\win\build.ps1. Uses the shader cache in Documents\ffxiii2\cache.
cd /d "%~dp0..\.."
if not exist out mkdir out
set LOG=out\test_newgame.log
echo == %date% %time% start > "%LOG%"
set REX_CACHE_ROOT=%USERPROFILE%\Documents\ffxiii2\cache
if exist out\pc\user_newgame rmdir /s /q out\pc\user_newgame
mkdir out\pc\user_newgame
set REX_USER_DATA_ROOT=%CD%\out\pc\user_newgame
if exist out\pc\captures rmdir /s /q out\pc\captures
if exist out\pc\captures_newgame rmdir /s /q out\pc\captures_newgame
powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\boot_test.ps1 -Seconds 720 -Tail 3000 -Shots "100,200,300,420,440,460,490,520,550,580,610,640,670,700" -Pad "12:A,24:A,36:A,48:START,62:A,66:UP:0.4,72:A:0.4,84:A:0.3,92:A:0.3,100:A:0.3,110:A:0.3,120:A:0.3,470:DOWN:0.3,476:A:0.3,484:A,490:A,496:A,502:A,508:A,514:A,520:A,526:A,532:A,538:A,544:A,550:A,556:A,562:A,568:A,574:A,580:A,586:A,592:A,598:A,604:A,610:A,616:A,622:A,628:A,634:A,640:A,646:A,652:A,658:A,664:A,670:A,676:A,682:A,688:A,694:A,700:A" >> "%LOG%" 2>&1
if exist out\pc\captures ren out\pc\captures captures_newgame
echo DONE %time% >> "%LOG%"
