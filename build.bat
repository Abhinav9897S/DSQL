@echo off
rem Windows build without make. Needs g++ (MinGW-w64) on PATH.
rem   build.bat        builds minisql.exe
rem   build.bat test   builds and runs the tests

if not exist build mkdir build

if "%1"=="test" goto test

g++ -std=c++17 -O2 -Isrc -o minisql.exe src\main.cpp src\Cli.cpp src\ds\*.cpp src\util\*.cpp src\storage\*.cpp src\index\*.cpp src\sql\*.cpp src\server\*.cpp -lws2_32
if errorlevel 1 exit /b 1
echo Built minisql.exe - run it and the workbench opens in your browser.
exit /b 0

:test
g++ -std=c++17 -O2 -Isrc -o build\run_tests.exe tests\test_main.cpp src\ds\*.cpp src\util\*.cpp src\storage\*.cpp src\index\*.cpp src\sql\*.cpp
if errorlevel 1 exit /b 1
build\run_tests.exe
