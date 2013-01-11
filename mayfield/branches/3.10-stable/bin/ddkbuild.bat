@echo OSR DDKBUILD.BAT V5.3 - OSR, Open Systems Resources, Inc.
@echo off
rem /////////////////////////////////////////////////////////////////////////////
rem //
rem //    This sofware is supplied for instructional purposes only.
rem //
rem //    OSR Open Systems Resources, Inc. (OSR) expressly disclaims any warranty
rem //    for this software.  THIS SOFTWARE IS PROVIDED  "AS IS" WITHOUT WARRANTY
rem //    OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING, WITHOUT LIMITATION,
rem //    THE IMPLIED WARRANTIES OF MECHANTABILITY OR FITNESS FOR A PARTICULAR
rem //    PURPOSE.  THE ENTIRE RISK ARISING FROM THE USE OF THIS SOFTWARE REMAINS
rem //    WITH YOU.  OSR's entire liability and your exclusive remedy shall not
rem //    exceed the price paid for this material.  In no event shall OSR or its
rem //    suppliers be liable for any damages whatsoever (including, without
rem //    limitation, damages for loss of business profit, business interruption,
rem //    loss of business information, or any other pecuniary loss) arising out
rem //    of the use or inability to use this software, even if OSR has been
rem //    advised of the possibility of such damages.  Because some states/
rem //    jurisdictions do not allow the exclusion or limitation of liability for
rem //    consequential or incidental damages, the above limitation may not apply
rem //    to you.
rem //
rem //    OSR Open Systems Resources, Inc.
rem //    105 Route 101A Suite 19
rem //    Amherst, NH 03031  (603) 595-6500 FAX: (603) 595-6503
rem //    email bugs to: bugs@osr.com
rem //
rem //
rem //    MODULE:
rem //
rem //        ddkbuild.bat 
rem //
rem //    ABSTRACT:
rem //
rem //      This file allows drivers to be build with visual studio and visual studio.net
rem //
rem //    AUTHOR(S):
rem //
rem //        OSR Open Systems Resources, Inc.
rem // 
rem //    REVISION:   V5.3
rem //
rem //      Fix a couple of bugs where the parameter was suppose to be "parameter"
rem //
rem //
rem //    REQUIREMENTS:  Environment variables that must be set.
rem //
rem //		BASEDIR - Automatically set up by NT4 DDK. (e.g. D:\NT4DDK )
rem //      W2KBASE - must be set up by user to point to W2K DDK  (e.g D:\Nt50DDK )
rem //      WXPBASE - must be set up by user to point to WXP DDK  (e.g D:\WINDDK\2600)
rem //      WNETBASE - must be set up by user to point to WNET DDK (e.g D:\WINDDK\3615) 
rem //
rem //
rem //    COMMAND FORMAT:
rem //
rem //		ddkbuild -PLATFORM BUILDTYPE DIRECTORY [FLAGS]
rem //
rem //              PLATFORM is either 
rem //                   WXP, WXP64, WXP2K - builds using WXP DDK
rem //                   W2K, W2K64,  - builds using W2k DDK
rem //                   WNET, WNET64, WNET2K, WNETXP, WNETXP64 - builds using WNET DDK
rem //                   NT4  - build using NT4 DDK (NT4 is the default)
rem //              BUILDTYPE - free, checked, chk or fre
rem //				DIRECTORY is the path to the directory to be build.  It can be "."
rem //      
rem //
rem //	  BROWSE FILES:
rem //	
rem //       This procedure supports the building of BROWSE files to be used by 
rem //       Visual Studio 6 and by Visual Studio.Net  However, the BSCfiles created
rem //       by bscmake for the 2 studios are not compatible. When this command procedure
rem //       runs, it selects the first bscmake.exe found in the path.   So, make
rem //       sure that the correct bscmake.exe is in the path....
rem //
rem //    COMPILERS:
rem //
rem //        If you are building NT4 or Windows 2000 drivers you should really
rem //        be using the VC 6 compiler.   If you are building a WXP driver,
rem //        you should be using the compiler that comes with the DDK.  This 
rem //        procedure should use the correct compiler.
rem //       
rem //    GENERAL COMMENTS:
rem //        This procedure is not written to be elegant!  It is written to work 
rem //        and to be easy to debug.   While we could have reused a bunch of
rem //        code, we decided not to.   
rem //
rem ///////////////////////////////////////////////////////////////////////////////

set scriptDebug=off
setlocal ENABLEEXTENSIONS

@echo %scriptDebug%

rem //
rem // Check for NT 4 Build
rem //
if /I %1 NEQ -NT4   goto NoNT4Base

@echo NT4 BUILD using NT4 DDK

shift

if "%BASEDIR%"=="" goto ErrNoBASEDIR

set path=%BASEDIR%\bin;%path%

set mode=
for %%f in (free FREE fre FRE) do if %%f == %1 set mode=free
for %%f in (checked CHECKED chk CHK) do if %%f == %1 set mode=checked
if "%mode%" =="" goto ErrBadMode

if "%2" == "" goto ErrNoDir

if not exist %2 goto ErrNoDir

pushd .
call %BASEDIR%\bin\setenv.bat %BASEDIR% %mode% "%MSDEVDIR%"
popd

@echo %scriptDebug%

goto RegularBuild

:NoNT4Base

rem //
rem // Check for WNET Windows 2000 Build using WNET DDK
rem //
if /I %1 NEQ -WNET2K goto NoWNET2KBase

@echo W2K BUILD using WNET DDK

set BASEDIR=%WNETBASE%

shift

if "%BASEDIR%"=="" goto ErrNoBASEDIR

set path=%BASEDIR%\bin;%path%

set mode=
for %%f in (free FREE fre FRE) do if %%f == %1 set mode=f
for %%f in (checked CHECKED chk CHK) do if %%f == %1 set mode=c
if "%mode%" =="" goto ErrBadMode

if "%2" == "" goto ErrNoDir

if not exist %2 goto ErrNoDir

pushd .
call %BASEDIR%\bin\setenv.bat %BASEDIR% W2K %mode% 
popd

@echo %scriptDebug%

goto RegularBuild

:NoWNET2KBase

rem //
rem // Check for WXP Build using WNET DDK
rem //
if /I %1 NEQ -WNETXP goto NoWNETXPBase

@echo WXP BUILD using WNET DDK

set BASEDIR=%WNETBASE%

shift

if "%BASEDIR%"=="" goto ErrNoBASEDIR

set path=%BASEDIR%\bin;%path%

set mode=
for %%f in (free FREE fre FRE) do if %%f == %1 set mode=fre
for %%f in (checked CHECKED chk CHK) do if %%f == %1 set mode=chk
if "%mode%" =="" goto ErrBadMode


if "%2" == "" goto ErrNoDir

if not exist %2 goto ErrNoDir

pushd .
call %BASEDIR%\bin\setenv.bat %BASEDIR% %mode% WXP 
popd

@echo %scriptDebug%

goto RegularBuild

:NoWNETXPBase

rem //
rem // Check for WXP 64 bit Build using WNET DDK
rem //
if /I %1 NEQ -WNETXP64 goto NoWNETXP64Base

@echo WXP 64 BIT BUILD using WNET DDK

set BASEDIR=%WNETBASE%

shift

if "%BASEDIR%"=="" goto ErrNoBASEDIR

set path=%BASEDIR%\bin;%path%

set mode=
for %%f in (free FREE fre FRE) do if %%f == %1 set mode=fre
for %%f in (checked CHECKED chk CHK) do if %%f == %1 set mode=chk
if "%mode%" =="" goto ErrBadMode

if "%2" == "" goto ErrNoDir

if not exist %2 goto ErrNoDir

pushd .
call %BASEDIR%\bin\setenv.bat %BASEDIR% %mode% 64 WXP 
popd

@echo %scriptDebug%

goto RegularBuild

:NoWNETXP64Base

rem //
rem // Check for WNET 64 bit Build using WNET DDK
rem //
if /I %1 NEQ -WNET64 goto NoWNET64Base

@echo WNET 64 BIT BUILD using WNET DDK

set BASEDIR=%WNETBASE%

shift

if "%BASEDIR%"=="" goto ErrNoBASEDIR

set path=%BASEDIR%\bin;%path%

set mode=
for %%f in (free FREE fre FRE) do if %%f == %1 set mode=fre
for %%f in (checked CHECKED chk CHK) do if %%f == %1 set mode=chk
if "%mode%" =="" goto ErrBadMode

if "%2" == "" goto ErrNoDir

if not exist %2 goto ErrNoDir

pushd .
call %BASEDIR%\bin\setenv.bat %BASEDIR% %mode% 64 WNET 
popd

@echo %scriptDebug%

goto RegularBuild

:NoWNET64Base

rem //
rem // Check for WNET 32 BIT BUILD using WNET DDK
rem //
if /I %1 NEQ -WNET goto NoWNETBase

@echo WNET 32 BIT BUILD using WNET DDK

set BASEDIR=%WNETBASE%

shift

if "%BASEDIR%"=="" goto ErrNoBASEDIR

set path=%BASEDIR%\bin;%path%

set mode=
for %%f in (free FREE fre FRE) do if %%f == %1 set mode=fre
for %%f in (checked CHECKED chk CHK) do if %%f == %1 set mode=chk
if "%mode%" =="" goto ErrBadMode

if "%2" == "" goto ErrNoDir

if not exist %2 goto ErrNoDir

pushd .
call %BASEDIR%\bin\setenv.bat %BASEDIR% %mode%
popd

@echo %scriptDebug%

goto RegularBuild

:NoWNETBase

rem //
rem // Check for WXP 64 BIT BUILD using WXP DDK
rem //
if /I %1 NEQ -WXP64 goto NoWxp64Base

@echo WXP 64 BIT BUILD using WXP DDK

set BASEDIR=%WXPBASE%

shift

if "%BASEDIR%"=="" goto ErrNoBASEDIR

set path=%BASEDIR%\bin;%path%

set
�               �!p                                                 �                              h 1	 ���h 1	 ���x 1	 ���x 1	 ���                       � 1	 ���� 1	 ����A             �����������������p������ ��6 ����1	 ���        9            �KoP    M�    XnP    �s#    XnP    �s#                        `              X1	 ���X1	 ���                                �1	 ����1	 ����1	 ����1	 ����1	 ����1	 ����1	 ����1	 ���       N               �f������        � 1	 ���                                     1	 ��� 1	 ���       81	 ���81	 ���                         ��������      �4�; ���        �1	 ����1	 ���                        �1	 ����1	 ���        �V
�                                            �#P    �a�     1	 ��� 1	 ���    ����                               81	 ���81	 ���                        y  y          
�               �!p                                                 �                              �1	 ����1	 ����1	 ����1	 ���                       1	 ���1	 ����A             �����������������p������ ��6 ���`1	 ���        :            �KoP    M�    XnP    �s#    XnP    �s#                        `              �1	 ����1	 ���                                �1	 ����1	 ���1	 ���1	 ���1	 ���1	 ���(1	 ���(1	 ���                      �f������         1	 ���                                    �1	 ����1	 ���       �1	 ����1	 ���                         ��������      �4�; ���        �1	 ����1	 ���                        1	 ���1	 ���        �V
�                                            �#P    �a�    p1	 ���p1	 ���    ����                               �1	 ����1	 ���                        y  y    1	 ���
�               �!p                                                 �                              H1	 ���H1	 ���X1	 ���X1	 ���                       �1	 ����1	 ����A             �����������������p������ ��6 ����1	 ���        ;            �KoP    M�    XnP    �s#    XnP    �s#                        `              81	 ���81	 ���                                h1	 ���h1	 ���x1	 ���x1	 ����1	 ����1	 ����1	 ����1	 ���       �              �f������        �1	 ���                                     	1	 ��� 	1	 ���       	1	 ���	1	 ���                         ��������      �4�; ���        `	1	 ���`	1	 ���                        �	1	 ����	1	 ���        �V
�                                            �#P    �a�    �	1	 ����	1	 ���    ����                               
1	 ���
1	 ���                        y  y  p1	 ���
�                $p       % p       ] p                         �                             �
1	 ����
1	 ����
1	 ����
1	 ��� 0                      �
1	 ����
1	 ����A             �����������������p������ ��6 ���@1	 ���        <            �KoP    M�    XnP    �s#    XnP    �s#                  0      `              �1	 ����1	 ���                                �1	 ����1	 ����1	 ����1	 ����1	 ����1	 ���1	 ���1	 ���       �              �f������         1	 ���                                    p1	 ���p1	 ���       �1	 ����1	 ���                         ��������      �4�; ���        �1	 ����1	 ���                        �1	 ����1	 ���        �V
�                                            �#P    �a�    P1	 ���P1	 ���    ����                               �1	 ����1	 ���                        y  y  �1	 ���
�               $p                                                 �                              (1	 ���(1	 ���81	 ���81	 ���                       `1	 ���`1	 ����A             �����������������p������ ��6 ����1	 ���        =            �KoP    M�    XnP    �j�    XnP    �j�                        `              1	 ���1	 ���                                H1	 ���H1	 ���X1	 ���X1	 ���h1	 ���h1	 ���x1	 ���x1	 ���       -               �f������        p1	 ���                                    �1	 ����1	 ���       �1	 ����1	 ���                         ��������      �4�; ���        @1	 ���@1	 ���                        h1	 ���h1	 ���        �V
�                                            �#P    �a�    �1	 ����1	 ���    ����                               �1	 ����1	 ���                        y  y  P
1	 ���
�               �!p                                                 �                              �1	 ����1	 ����1	 ����1	 ���                       �1	 ����1	 ����A             �����������������p������ ��6 ��� 1	 ���        3            �KoP    M�    XnP    �s#    XnP    �s#                        `              �1	 ����1	 ���                                �1	 ����1	 ����1	 ����1	 ����1	 ����1	 ����1	 ����1	 ���       o               �f������        �1	 ���                                    P1	 ���P1	 ���       h1	 ���h1	 ���                         ��������      �4�; ���        �1	 ����1	 ���                        �1	 ����1	 ���        �V
�                                            �#P    �a�    01	 ���01	 ���    ����                               h1	 ���h1	 ���                        y  y  �1	 ���
�           '$p   p       a p       � p       � p             �                             1	 ���1	 ���1	 ���1	 ��� P                      @1	 ���@1	 ����A             �����������������p������ ��6 ����1	 ���        4            �KoP    M�    XnP    ���    XnP    ���          0        P      `              �1	 ����1	 ���                                (1	 ���(1	 ���81	 ���81	 ���H1	 ���H1	 ���X1	 ���X1	 ���       �              �f������        P1	 ���                                    �1	 ����1	 ���       �1	 ����1	 ���                         ��������      �4�; ���         1	 ��� 1	 ���                        H1	 ���H1	 ���        �V
�                                            �#P    �a�    �1	 ����1	 ���    ����                               �1	 ����1	 ���                        y  y  01	 ���
�               �!p                                                 �                              x1	 ���x1	 ����1	 ����1	 ���                       �1	 ����1	 ����A             �����������������p������ ��6 ��� 1	 ���        1            �KoP    HCv&    � �O            �#P    �4!                        `              h1	 ���h1	 ���                                �1	 ����1	 ����1	 ����1	 ����1	 ����1	 ����1	 ����1	 ���                      �f������        �1	 ���                                    01	 ���01	 ���       H1	 ���H1	 ���                         ��������      �4�; ���        �1	 ����1	 ���                        �1	 ����1	 ���        �V
�                                            �#P    �a�    1	 ���1	 ���    ����                               H1	 ���H1	 ���                        y  y  �1	 ���
�               �#H                                                 �                              �1	 ����1	 ����1	 ����1	 ���                        1	 ��� 1	 ����A             �����������������p������ ��6 ���p1	 ���        q�            �KoP    HCv&    �z�O            �#P    ���                        `              �1	 ����1	 ���                                1	 ���1	 ���1	 ���1	 ���(1	 ���(1	 ���81	 ���81	 ���                      �f������        01	 ���                                    �1	 ����1	 ���       �1	 ����1	 ���                         ��������      �4�; ���         1	 ��� 1	 ���                        (1	 ���(1	 ���        Z
�                            �#H            �#P    ׸/6    �1	 ����1	 ���    ����                               �1	 ����1	 ���                      y  y  1	 ���
�               $p                                                 �                              X1	 ���X1	 ���h1	 ���h1	 ���               