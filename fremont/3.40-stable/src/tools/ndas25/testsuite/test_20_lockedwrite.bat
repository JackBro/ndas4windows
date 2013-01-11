@echo off
call config.bat

SET ITE=16
SET IOSIZE=256
SET SMALLIOSIZE=16

echo #### Lockwrite test. 
echo ## Warming up(IO timeout can happen..)
%CLI% LoginR %TARGET% 0x10001

echo ### mutex-write-lock mode. No yield, lock per write mode.
echo ## Starting 5 write instance. 
start lockedwrite %IOSIZE% %ITE% 10000 0x4
call sleep 1
start lockedwrite %IOSIZE% %ITE% 11000 0x4
call sleep 1
start lockedwrite %IOSIZE% %ITE% 12000 0x4
call sleep 1
start lockedwrite %IOSIZE% %ITE% 13000 0x4
call sleep 1
start lockedwrite %IOSIZE% %ITE% 14000 0x4

echo ### Check every write is completed successfully.
pause

