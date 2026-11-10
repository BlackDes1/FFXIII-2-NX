@echo off
rem ffxiii2-nx (2026-11-08, k263): an AUDIT codegen of default.xex (+ z0070) WITH the register diet
rem (non_volatile_as_local, elide_lr_stores, non_argument_as_local), into out\audit\gen_diet (app\renut_manifest_diet.toml,
rem app\renut_config_diet.toml). app\generated (what the build uses) is not touched. Then the audits on it:
rem   tools\read_before_write.py out\audit\gen_diet --noreturn=82C15900
rem   tools\non_argument_audit.py out\audit\gen_diet --with-option --noreturn=82C15900
call "%~dp0pc_env.bat" || exit /b 1
set "ROOT=%~dp0.."
set "REXGLUE=%ROOT%\sdk\out\win-amd64\rexglue.exe"
if not exist "%REXGLUE%" (
  for /r "%ROOT%\out\host" %%F in (rexglue.exe) do if exist "%%F" set "REXGLUE=%%F"
)
if not exist "%REXGLUE%" ( echo rexglue.exe not found & exit /b 1 )
set REX_MAX_JUMP_TABLE_ENTRIES=1024
if not exist "%ROOT%\out\audit" mkdir "%ROOT%\out\audit"
pushd "%ROOT%\app"
echo == diet codegen default.xex + z0070
"%REXGLUE%" codegen renut_manifest_diet.toml --target z0070 --ignore-stamp > "%ROOT%\out\codegen_diet_z0070.log" 2>&1 || ( popd & echo codegen failed & exit /b 1 )
popd
echo == diet codegen done
