# Microsoft Developer Studio Project File - Name="WxpNtfsLib" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) External Target" 0x0106

CFG=WxpNtfsLib - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "WxpNtfsLib.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "WxpNtfsLib.mak" CFG="WxpNtfsLib - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "WxpNtfsLib - Win32 Release" (based on "Win32 (x86) External Target")
!MESSAGE "WxpNtfsLib - Win32 Debug" (based on "Win32 (x86) External Target")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""

!IF  "$(CFG)" == "WxpNtfsLib - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Cmd_Line "NMAKE /f WxpNtfsLib.mak"
# PROP BASE Rebuild_Opt "/a"
# PROP BASE Target_File "WxpNtfsLib.exe"
# PROP BASE Bsc_Name "WxpNtfsLib.bsc"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Cmd_Line "..\..\bin\ddkbuild -WNETXP free ."
# PROP Rebuild_Opt "/a"
# PROP Target_File "WxpNtfsLib.exe"
# PROP Bsc_Name ""
# PROP Target_Dir ""

!ELSEIF  "$(CFG)" == "WxpNtfsLib - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Cmd_Line "NMAKE /f WxpNtfsLib.mak"
# PROP BASE Rebuild_Opt "/a"
# PROP BASE Target_File "WxpNtfsLib.exe"
# PROP BASE Bsc_Name "WxpNtfsLib.bsc"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Cmd_Line "..\..\bin\ddkbuild -WNETXP checked ."
# PROP Rebuild_Opt "-cZ"
# PROP Target_File "WxpNtfsLib.exe"
# PROP Bsc_Name "WxpNtfsLib.bsc"
# PROP Target_Dir ""

!ENDIF 

# Begin Target

# Name "WxpNtfsLib - Win32 Release"
# Name "WxpNtfsLib - Win32 Debug"

!IF  "$(CFG)" == "WxpNtfsLib - Win32 Release"

!ELSEIF  "$(CFG)" == "WxpNtfsLib - Win32 Debug"

!ENDIF 

# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=.\mcbsup.c
# End Source File
# Begin Source File

SOURCE=.\ntfs.c
# End Source File
# Begin Source File

SOURCE=.\WxpNtfsLib.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=.\index.h
# End Source File
# Begin Source File

SOURCE=.\lockorder.h
# End Source File
# Begin Source File

SOURCE=.\nodetype.h
# End Source File
# Begin Source File

SOURCE=.\ntfs.h
# End Source File
# Begin Source File

SOURCE=.\ntfsdata.h
# End Source File
# Begin Source File

SOURCE=.\ntfslog.h
# End Source File
# Begin Source File

SOURCE=.\ntfsproc.h
# End Source File
# Begin Source File

SOURCE=.\ntfsstru.h
# End Source File
# Begin Source File

SOURCE=.\WxpNtfsLib.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# Begin Source File

SOURCE=.\makefile
# End Source File
# Begin Source File

SOURCE=.\sources
# End Source File
# End Target
# End Project
