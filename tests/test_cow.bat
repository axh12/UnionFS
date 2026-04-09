@echo off
echo ========================================
echo Mini-UnionFS CoW Test
echo ========================================
echo.

echo Setting up test directories...
wsl mkdir -p lower upper mnt
echo.

echo Creating test file in lower directory...
wsl echo "Original content from lower dir" ^> lower/test.txt
echo.

echo Mounting Mini-UnionFS...
start /b wsl ./mini_unionfs lower upper mnt
timeout /t 3 /nobreak > nul
echo.

echo Test 1: Reading from mount point
wsl cat mnt/test.txt
echo.

echo Test 2: Modifying through mount point (CoW triggered)
wsl echo "Modified content added" ^>^> mnt/test.txt
echo.

echo Content at /mnt/test.txt after modification:
wsl cat mnt/test.txt
echo.

echo Test 3: Checking upper directory (should be modified)
wsl cat upper/test.txt
echo.

echo Test 4: Checking lower directory (should be original)
wsl cat lower/test.txt
echo.

echo Cleaning up...
wsl fusermount -u mnt
wsl rm -rf lower upper mnt
echo.

echo ========================================
echo CoW Test Complete!
echo ========================================
echo.
echo If you see:
echo - Modified content in mnt/test.txt: SUCCESS
echo - Modified content in upper/test.txt: CoW WORKED
echo - Original content in lower/test.txt: Read-only protection WORKED
echo.
pause