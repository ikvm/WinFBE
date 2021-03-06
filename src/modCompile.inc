'    WinFBE - Programmer's Code Editor for the FreeBASIC Compiler
'    Copyright (C) 2016-2018 Paul Squires, PlanetSquires Software
'
'    This program is free software: you can redistribute it and/or modify
'    it under the terms of the GNU General Public License as published by
'    the Free Software Foundation, either version 3 of the License, or
'    (at your option) any later version.
'
'    This program is distributed in the hope that it will be useful,
'    but WITHOUT any WARRANTY; without even the implied warranty of
'    MERCHANTABILITY or FITNESS for A PARTICULAR PURPOSE.  See the
'    GNU General Public License for more details.


''
''  Handle information related to Compile process 
''
Type COMPILE_TYPE
   MainFilename    As WString * MAX_PATH   ' main source file (full path and file.ext)
   MainName        As WString * MAX_PATH   ' main source file (Name only, no extension)
   MainFolder      As WString * MAX_PATH   ' main source folder 
   ResourceFile    As WString * MAX_PATH   ' full path and file.ext to resource file (if applicable) 
   OutputFilename  As WString * MAX_PATH   ' resulting exe/dll/lib name 
   CompilerPath    As WString * MAX_PATH   ' full path and file.ext to fbc.exe
   ObjFolder       As WString * MAX_PATH   ' *.o for all modules (set depending on 32/64 bit) (full path)
   ObjFolderShort  As WString * MAX_PATH   ' ".\.wfbe\"
   ObjID           As WString * MAX_PATH   ' "32" or "64" appended to object name
   CompileFlags    As WString * 2048
   RunAfterCompile As BOOLEAN
   StartTime       As Double
   EndTime         As Double
   CompileID       as long                 ' This is the wID. Needed in case frmOutput listview later clicked on.
End Type
Dim Shared gCompile As COMPILE_TYPE


''
''  Set the statusbar text and icon for a good/bad compile
'------------------------------------------------------------------------------------------------------------------------
function SetCompileStatusBarMessage( byref wszText as const wstring, byval hIconCompile as HICON ) as LRESULT
   dim as hwnd hStatusBar = GetDlgItem( HWND_FRMMAIN, IDC_FRMMAIN_STATUSBAR )
   StatusBar_SetIcon(hStatusBar, 4, hIconCompile)
   StatusBar_SetText(hStatusBar, 4, wszText)
   function = 0
end function


''
''  Main module that handles the entire compile process
''
'------------------------------------------------------------------------------------------------------------------------
Function code_Compile( ByVal wID As Long ) As BOOLEAN

   Dim pDoc As clsDocument Ptr
   
   Dim wszTemp      As WString * MAX_PATH 
   Dim wszFileExe   As WString * MAX_PATH
   dim wszCompile   as CWSTR
   dim wszCommand   as CWSTR 
   dim wszParams    as CWSTR
   dim sConsoleText as string 

   Dim As CWSTR wexecmd, wDQ, wsLogSt, wst
         
   Dim As BOOLEAN fCompile32, fCompile64
   
   dim as long idx = gApp.GetActiveProjectIndex()
   dim as long idxBuild, f, i, n, nCount
   
   wDQ = wchr(34) ' unicode double quotes
   
   ' Can only continue to compile if the primary source code file
   ' is not dirty and needs to be saved.
   if wID <> IDM_QUICKRUN THEN
      If gConfig.CompileAutosave Then
         If OnCommand_FileSaveAll(HWND_FRMMAIN) Then 
            Return True  ' user cancelled save
         end if   
      End If
   END IF
   
   ' Get the array index for the current selected build (project or document)
   idxBuild = ComboBox_GetCurSel(HWND_FRMMAIN_COMBOBUILDS)
   if idxBuild = -1 then idxBuild = 0
   
   ' Perform some pre-compile checks to see if we should continue.
   gConfig.FBWINCompiler32 = ProcessFromCurdrive(gConfig.FBWINCompiler32)
   gConfig.FBWINCompiler64 = ProcessFromCurdrive(gConfig.FBWINCompiler64)
   gConfig.AfxPath         = ProcessFromCurdrive(gConfig.AfxPath)
   if gConfig.Builds(idxBuild).Is32bit then
      fCompile32 = True: fCompile64 = False
      gCompile.CompilerPath = gConfig.FBWINcompiler32
   end if
   if gConfig.Builds(idxBuild).Is64bit then
      gCompile.CompilerPath = gConfig.FBWINcompiler64
      fCompile32 = False: fCompile64 = True
   End if
   
   
   ' Check to see if the compiler exists
   If AfxFileExists(gCompile.CompilerPath) = 0 Then 
      MessageBox( HWND_FRMMAIN, L(202,"Invalid defined compiler path."), L(201,"Error"), _
                        MB_OK Or MB_ICONINFORMATION Or MB_DEFBUTTON1 )
      Function = False: Exit Function
   End If
   
   
   ' Set some compile flags depending on the type of compile requested.
   gCompile.CompileID = wID
   Select Case wID
      Case IDM_BUILDEXECUTE: gCompile.RunAfterCompile = True
      Case IDM_QUICKRUN:     gCompile.RunAfterCompile = True
      Case IDM_COMPILE:      gCompile.RunAfterCompile = False
   End Select
   
   
   Dim pDocMain As clsDocument Ptr 
   If gApp.IsProjectActive Then
      pDocMain = gApp.Projects(idx).GetMainDocumentPtr()
      If pDocMain = 0 Then
         MessageBox( HWND_FRMMAIN, L(208,"No Main file specified for the project."), L(201,"Error"), _
                           MB_OK Or MB_ICONINFORMATION Or MB_DEFBUTTON1 )
         Function = False: Exit Function
      End If
   Else
      pDocMain = gTTabCtl.GetActiveDocumentPtr()
   End If
   If pDocMain = 0 Then return 0
   
   ' If this is a QuickRun then we need to copy the current source code to a temporary bas file
   ' and compile that bas file instead. The resulting bas file will be deleted after the compiling
   ' has completed and exe will be deleted when the exe is terminated. 
   if wID = IDM_QUICKRUN THEN
      if pDocMain->IsNewFlag then 
         gCompile.MainFolder = AfxGetExePathName
      else
         gCompile.MainFolder = AfxStrPathname("PATH", pDocMain->DiskFilename)
      end if
      gCompile.MainFilename = GetTemporaryFilename(gCompile.MainFolder, "bas")
      gCompile.MainName     = AfxStrPathname("NAME", gCompile.MainFilename)
      
      ' Copy the contents of the active document to the temporary source file.
      f = freefile
      open gCompile.MainFilename for output as #f
      put #f, , pDocMain->GetText
      close #f
   else
      gCompile.MainFilename = pDocMain->DiskFilename
      gCompile.MainName     = AfxStrPathname("NAME", pDocMain->DiskFilename)
      gCompile.MainFolder   = AfxStrPathname("PATH", pDocMain->DiskFilename)
   END IF
   
   gCompile.ResourceFile = WStr("")    ' default that there is no resource
   gCompile.CompileFlags = gConfig.Builds(idxBuild).wszOptions
   
   ' If the path to the WinFBX (Afx) library is valid then add that path
   ' as a -i switch to the compiler.
   if AfxIsFolder(gConfig.AfxPath) THEN
      gCompile.CompileFlags = gCompile.CompileFlags + " -i " + chr(34) + gConfig.AfxPath + chr(34)
   else
      ' WinFBX was not specified in the compiler setup so let's look for it
      if AfxIsFolder(AfxGetExePathName & "src") then
         gCompile.CompileFlags = gCompile.CompileFlags + " -i " + _
              chr(34) + AfxGetExePathName & "src" + chr(34)
      END IF
   END IF
   
   If gApp.IsProjectActive Then
      Dim pDocResource As clsDocument Ptr = gApp.Projects(idx).GetResourceDocumentPtr()
      if pDocResource then gCompile.ResourceFile = pDocResource->DiskFilename

      gCompile.ObjFolder = gCompile.MainFolder & wstr(".wfbe\")
      gCompile.ObjFolderShort = wstr(".\.wfbe\")

      If fCompile32 Then 
         gCompile.ObjID = wstr("32.o")
         gCompile.CompileFlags = gCompile.CompileFlags + " " + gApp.Projects(idx).ProjectOther32           
      End If
      If fCompile64 Then 
         gCompile.ObjID = wstr("64.o")
         gCompile.CompileFlags = gCompile.CompileFlags + " " + gApp.Projects(idx).ProjectOther64           
      End If   
      
      ' Make sure the folders exist
      SHCreateDirectory( 0, gCompile.ObjFolder )

   Else
      ' No active project
      gCompile.CompileFlags = gCompile.CompileFlags + " " + gConfig.CompilerSwitches   
   End If


   ' Search main source code for any user embedded compile directives. These will override
   ' anything that was set at the default or project level.
   redim directives(any) as COMPILE_DIRECTIVES
   pDocMain->CompileDirectives(directives())
   
   for i as long = lbound(directives) to ubound(directives)
      select case directives(i).DirectiveFlag 
         case IDM_CONSOLE
            n = instr(gCompile.CompileFlags, " -S GUI ")
            if n THEN gCompile.CompileFlags = AfxStrDelete(gCompile.CompileFlags, n, 8)
            gCompile.CompileFlags = gCompile.CompileFlags + " " + wstr(" -s console ")
         case IDM_GUI
            n = instr(gCompile.CompileFlags, " -S CONSOLE ")
            if n THEN gCompile.CompileFlags = AfxStrDelete(gCompile.CompileFlags, n, 12)
            gCompile.CompileFlags = gCompile.CompileFlags + " " + wstr(" -s gui ")
         case IDM_RESOURCE
            gCompile.ResourceFile = directives(i).DirectiveText
      END select
   next

   ' Need to determine the output filename in order to search for any existing
   ' running process.
   gCompile.OutputFilename = gCompile.MainFolder & gCompile.MainName & wstr(".exe")
   If Instr(Ucase(gCompile.CompileFlags), wstr(" -DLL")) Then
      gCompile.OutputFilename = gCompile.MainFolder & gCompile.MainName & wstr(".dll")
   End If
   If Instr(Ucase(gCompile.CompileFlags), wstr(" -DYLIB")) Then
      gCompile.OutputFilename = gCompile.MainFolder & gCompile.MainName & wstr(".dll")
   End If
   If Instr(Ucase(gCompile.CompileFlags), wstr(" -LIB")) Then
      gCompile.OutputFilename = gCompile.MainFolder & wstr("lib") & gCompile.MainName & wstr(".a")
   End If

   ' Need to test if the resulting application to be compiled is actually running
   ' in memory. This would cause the compile to fail.
   If IsProcessRunning(@gCompile.OutputFilename) Then
      MessageBox( HWND_FRMMAIN, L(200,"Program running..."), L(201,"Error"), _
                        MB_OK Or MB_ICONINFORMATION Or MB_DEFBUTTON1 )
      Function = False: Exit Function
   End If
      
   ' Run any precompile tools
   for y as long = LBound(gConfig.Tools) To UBound(gConfig.Tools)
      if gConfig.Tools(y).Action = USERTOOL_ACTION_PRECOMPILE then
         ExecuteUserTool(y)
      end if   
   NEXT

   ' Change to the output code directory
   ChDir gCompile.MainFolder
   
   ' Indicate in the StatusBar that we have started to compile
   SetCompileStatusBarMessage( L(203, "Compiling..."), 0 )
   AfxDoEvents
   
   gCompile.StartTime = Timer
   gCompiling = true    
   
   dim as HCURSOR hCurSave = GetCursor()
   SetCursor( LoadCursor(0, IDC_WAIT) )

   pDoc = gTTabCtl.GetActiveDocumentPtr()
   if pDoc THEN SciExec( pDoc->hWindow(0), SCI_SETCURSOR, SC_CURSORWAIT, 0 )


   ' If this is a project then we need to compile all of the modules first
   ' in order to create the necessary *.o object files
   If gApp.IsProjectActive Then
      nCount = gApp.Projects(idx).GetDocumentCount 
      For i = 0 To nCount - 1
         pDoc = gApp.Projects(idx).GetDocumentPtr(i)
         If pDoc->ProjectFileType = FILETYPE_MODULE Then
            ' Get the base name of the file for constructing the object filename
            wszTemp = AfxStrPathname("NAME", pDoc->DiskFilename)
            wszTemp = gCompile.ObjFolderShort & wszTemp & gCompile.ObjID
            
            ' Compare the source code file datetime to the object. If the source code
            ' date time is greater then we need to recompile it, otherwise we will simply
            ' link to the existing object file.
            Dim As FILETIME ft1 = AfxGetFileLastWriteTime(pDoc->DiskFilename) ' source file
            Dim As FILETIME ft2 = AfxGetFileLastWriteTime(wszTemp)  ' object file

            If (AfxFileTimeToVariantTime(ft1) > AfxFileTimeToVariantTime(ft2)) or _
               (wID = IDM_REBUILDALL)  Then

               wst = L(203, "Compiling...") + " " + AfxStrPathname("NAMEX", pDoc->DiskFilename) 
               SetCompileStatusBarMessage( wst, 0 )
               AfxDoEvents
 
               ' If spaces exist in the compiler path then convert the path to shortpathname in 
               ' order to work well with CreateProcess and FBC. Just using double quotes causes problems.
               if instr(gCompile.CompilerPath, " ") THEN
                  wszCommand = AfxGetShortPathName(gCompile.CompilerPath)
               else   
                  wszCommand = gCompile.CompilerPath
               END IF
               wszParams = " -c " + wDQ + pDoc->DiskFilename + wDQ + _
                           " -v -o " + wDQ + wszTemp + wDQ 
               RedirConsoleToFile( wszCommand, wszParams, sConsoleText )
               ' Need to check to see if any errors occurred while compiling this
               ' object file. If yes, then stop processing object files and display
               ' the error.
               if ParseLogForError(sConsoleText, false, wID) then   ' error detected
                  SetCursor( hCurSave )
                  pDoc = gTTabCtl.GetActiveDocumentPtr()
                  if pDoc THEN SciExec( pDoc->hWindow(0), SCI_SETCURSOR, SC_CURSORNORMAL, 0 )
                  return true
               end if
               sConsoleText = ""
            End If
            
         End If
      Next
   End If


   ' Compile the Main file. If this is a project then we also need to link
   ' in all of the *.o object files.
   wszParams = "-m " + wDQ + gCompile.MainFilename + wDQ 
   If Len(gCompile.ResourceFile) then
      wszParams = wszParams + " " + wDQ + gCompile.ResourceFile + wDQ
   end if   
   wszParams = wszParams + " -v " + gCompile.CompileFlags 
   
   If gApp.IsProjectActive Then
      nCount = gApp.Projects(idx).GetDocumentCount 
      For i = 0 To nCount - 1
         pDoc = gApp.Projects(idx).GetDocumentPtr(i)
         If pDoc->ProjectFileType = FILETYPE_MODULE Then
            ' Get the base name of the file for constructing the object filename
            wszTemp = AfxStrPathname("NAME", pDoc->DiskFilename)
            wszParams = wszParams + " " + gCompile.ObjFolderShort + wszTemp + gCompile.ObjID
         End If
      Next
   End If

   wst = L(203, "Compiling...") + " " + AfxStrPathname("NAMEX", gCompile.MainFilename) 
   SetCompileStatusBarMessage( wst, 0 )
   AfxDoEvents

   sConsoleText = ""
   ' If spaces exist in the compiler path then convert the path to shortpathname in 
   ' order to work well with CreateProcess and FBC. Just using double quotes causes problems.
   if instr(gCompile.CompilerPath, " ") THEN
      wszCommand = AfxGetShortPathName(gCompile.CompilerPath)
   else   
      wszCommand = gCompile.CompilerPath
   END IF
   RedirConsoleToFile( wszCommand, wszParams, sConsoleText )

   SetCursor( hCurSave )
   pDoc = gTTabCtl.GetActiveDocumentPtr()
   if pDoc THEN SciExec( pDoc->hWindow(0), SCI_SETCURSOR, SC_CURSORNORMAL, 0 )
   gCompile.EndTime = Timer
   gCompiling = false

   ' If this was a QuickRun then delete the temporary code file that was created.
   if wID = IDM_QUICKRUN THEN 
      kill gCompile.MainFilename
   end if   
   
   if ParseLogForError(sConsoleText, true, wID) = true then return false
   
   ' Run any postcompile tools
   for y as long = LBound(gConfig.Tools) To UBound(gConfig.Tools)
      if gConfig.Tools(y).Action = USERTOOL_ACTION_POSTCOMPILE then
         ExecuteUserTool(y)
      end if   
   NEXT

   If gCompile.RunAfterCompile Then 
      RunEXE( gCompile.OutputFilename, gApp.Projects(idx).ProjectCommandLine)
   End If
      
   Function = True   ' successful
End Function


'' July 2017, attempted to capture STDERR output from a running FB compiled
'' program in order to capture runtime errors. This code works okay on Windows 10
'' but does not PRINT on Windows 7. Also, dkl posted in FB forum that FB prints
'' to STDOUT rather than STDERR. This is too bad.

''
''
FUNCTION CreateChildProcess( byref zCmdLine AS zstring, _
                             byref zParams  AS zstring, _
                             byval hChildStdOutWrite AS HANDLE _
                             ) AS BOOLEAN
    
   dim ProcessInfo AS PROCESS_INFORMATION
   dim StartupInf  AS STARTUPINFO

   StartupInf.cb         = SIZEOF(STARTUPINFO)
   StartupInf.hStdError  = hChildStdOutWrite   
   StartupInf.hStdOutput = hChildStdOutWrite  
   StartupInf.dwFlags    = STARTF_USESTDHANDLES

'? "CreateProcess:  "; zCmdLine; " "; zParams
'?
'?
   
   IF CreateProcess( zCmdLine,  _    ' Create the child process.
                     zParams, _      ' Command line
                     BYVAL 0,     _  ' Process security attributes
                     BYVAL 0,     _  ' Primary thread security attributes
                     TRUE,        _  ' Handles are inherited
                     CREATE_NO_WINDOW, _  ' Creation flags  
                     BYVAL 0,     _  ' Use parent's environment
                     BYVAL 0,     _  ' Use parent's current directory
                     @StartupInf,  _  ' STARTUPINFO pointer
                     @ProcessInfo) = FALSE THEN
'      ? "error createprocess"
   ELSE
      ' Close handles to the child process and its primary thread.
      ' Some applications might keep these handles to monitor the status 
      ' of the child process, for example.
      CloseHandle( ProcessInfo.hProcess )
      CloseHandle( ProcessInfo.hThread )
      FUNCTION = TRUE
 END IF
END FUNCTION

''
''
SUB RedirConsoleToFile( byref szExe AS zstring, _
                        byref szCmdLine AS zstring, _
                        byref sConsoleText AS string _
                        )
   dim SecurityAttribute AS SECURITY_ATTRIBUTES
   dim hChildStdOutRead  AS PHANDLE
   dim hChildStdOutWrite AS PHANDLE
   dim dwReadBytes       AS long
   dim sBuffer AS STRING * 4096
  
   ' Set the bInheritHandle flag so pipe handles are inherited.
   SecurityAttribute.nLength              = SIZEOF(SECURITY_ATTRIBUTES)
   SecurityAttribute.bInheritHandle       = TRUE
   SecurityAttribute.lpSecurityDescriptor = NULL

   ' Create a pipe for the child process's STDOUT.
   IF CreatePipe(@hChildStdOutRead, @hChildStdOutWrite, @SecurityAttribute, BYVAL 0) = FALSE THEN
'      ? "error creating pipe"
   ELSE
      ' Ensure the read handle to the pipe for STDOUT is not inherited.
      IF SetHandleInformation(hChildStdOutRead, HANDLE_FLAG_INHERIT, BYVAL 0) = FALSE THEN
'         ? "error SetHandleInformation"
      ELSE
         ' Create the child process and read from its standard output
         IF CreateChildProcess(szExe, szCmdLine, hChildStdOutWrite) THEN
            CloseHandle(hChildStdOutWrite) ' To avoid ReadFile hanging at the end.
            DO
               IF ReadFile(hChildStdOutRead, BYVAL VARPTR(sBuffer), 4096, @dwReadBytes, BYVAL 0) = FALSE THEN 
                  '? "ERROR ReadFile. hChildStdOutRead=";hChildStdOutRead, "dwReadBytes=";dwReadBytes
                  '? "GetLastError: "; GetLastError
                  EXIT DO
               end if  
               '? "READ " & dwReadBytes & " BYTES=" & LEFT(sBuffer, dwReadBytes)
'               ? LEFT(sBuffer, dwReadBytes)
               sConsoleText = sConsoleText & LEFT(sBuffer, dwReadBytes)
               '(at this stage we could SAVE the buffer that we've read from the child to a file, but 
               ' thats an extra ReadFile+WriteFile compared to the direct-to-hFile method)
            LOOP
         END IF
      END IF
   END IF
END SUB

''
''
Function RunEXE( ByRef wszFileExe As CWSTR, _
                 ByRef wszParam As CWSTR _
                 ) As Long
                         
   dim as CWSTR wszPath = AfxStrPathname("PATH", wszFileExe)
   dim as CWSTR wszBatchFile = AfxGetExePathName + AfxStrPathname("NAME", wszFileExe) + ".bat"
   dim as CWSTR wszQuickRunExe = wszFileExe
   
   ' If option is specified to launch command window prior to running the
   ' compiled program then we do so by executing a shell to a batch file.
   ' This allows being able to see any runtime errors that might be thrown.
   ' Otherwise, simple shell to the program.
   if gConfig.RunViaCommandWindow THEN
      dim as long f = Freefile
      Open wszBatchFile For Output as #f
      print #f, "@echo off"
      print #f, Left(**wszPath, 2)     ' ensure on correct drive
      print #f, "cd "; chr(34); wszPath; chr(34)   ' change to correct folder
      print #f, chr(34); wszFileExe; chr(34); " "; wszParam
      print #f, "pause"
      print #f, "del "; chr(34); wszBatchFile; chr(34); " >> nul"
      close #f
      wszFileExe = wszBatchFile
      wszParam = ""
   END IF
   

   Dim ShExecInfo As SHELLEXECUTEINFOW  

   ' Run the EXE
   With ShExecInfo
      .cbSize       = Len(SHELLEXECUTEINFOW)
      .fMask        = SEE_MASK_NOCLOSEPROCESS
      .HWnd         = 0
      .lpVerb       = Null
      .lpFile       = wszFileExe
      .lpParameters = wszParam   
      .lpDirectory  = 0
      .nShow        = SW_SHOW
      .hInstApp     = 0 
   End With
   ShellExecuteEx(@ShExecInfo)

   ' If this was a QuickRun then add the filename to the global tracking array
   ' that checks if the file exists and then deletes it.
   if gCompile.CompileID = IDM_QUICKRUN THEN
      ' Give time for the application to start to run (especially if being run via
      ' the RunViaCommandWindow batch file, otherwise the exe will be deleted even
      ' before it gets a chance to execute.
      sleep 500
      gApp.AddQuickRunEXE(wszQuickRunExe)
   END IF
   
   Function = 0
End Function



' ========================================================================================
' Set the cursor to the error position based on the selected line in frmCompileResults
' ========================================================================================
Function SetDocumentErrorPosition( ByVal hLV As HWnd, Byval wID as long ) As Long

   Dim wszErrorLine As WString * MAX_PATH
   Dim wszErrorFile As WString * MAX_PATH
   
   Dim As Long idx
   Dim As Long nCurSel = ListView_GetSelection(hLV)
   If nCurSel < 0 Then Return 0 
   
   Dim pDoc As clsDocument Ptr 

   ' Get the line number and filename of the selected item
   FF_ListView_GetItemText(hLV, nCurSel, 0, @wszErrorLine, MAX_PATH)
   FF_ListView_GetItemText(hLV, nCurSel, 1, @wszErrorFile, MAX_PATH)

   ' If we are doing a QuickRun then it must be on the currently loaded
   ' and active file in the editor, therefore simply use that pDoc.
   if wID = IDM_QUICKRUN THEN
      pDoc = gTTabCtl.GetActiveDocumentPtr()
      
   else
      If AfxFileExists(wszErrorFile) = 0 Then Exit Function
   
      ' Position the editor to the Error line
      idx = gTTabCtl.GetTabIndexFromFilename( @wszErrorFile )
      If idx >= 0 Then
         gTTabCtl.SetFocusTab(idx)
         pDoc = gTTabCtl.GetActiveDocumentPtr()
      Else
         ' File is not already loaded. Load it now.
         pDoc = frmMain_OpenFileSafely(HWND_FRMMAIN, _
                                 False, _    ' bIsNewFile
                                 False, _    ' bIsTemplate
                                 true, _     ' bShowInTab
                                 false, _    ' bIsInclude
                                 @wszErrorFile, _  ' pwszName
                                 0 )  ' pDocIn
         
         ' Position all of the controls into place
         frmMain_PositionWindows(HWND_FRMMAIN)
      End If 
   END IF

   dim as hwnd hEdit = pDoc->hWndActiveScintilla
   SciExec( hEdit, SCI_GOTOLINE, Val(wszErrorLine)-1, 0 )

End Function

''
''
function ParseLogForError( byref wsLogSt as CWSTR, _
                           byval bAllowSuccessMessage as Boolean, _
                           Byval wID as long _
                           ) as Boolean
   ' Returns TRUE if error has been detected. This signals to the calling code
   ' that we can break out of performing any further compiles (eg. modules).
   Dim wszTemp As WString * MAX_PATH 

   Dim As Long Parenthesis_Start, Parenthesis_End, Error_Start       
   Dim As Long i, NumLines, NextLine, r, nCount
   Dim As Long NumWarnings, NumErrors, IsError, IsWarning
   Dim As Long nFirstErrorLine = -1
   Dim As HWnd hLV
   Dim As CWSTR wDQ, wst1, wst2, wst3, wst, wst_ucase, wszOutputMsg        
   dim as Boolean fGoodCompile

   wDQ = wchr(34) ' unicode double quotes

   ' Copy the log file text to the logfile textbox
   AfxSetWindowText( GetDlgItem( HWND_FRMOUTPUT, IDC_FRMOUTPUT_TXTLOGFILE), wsLogSt )
   
   hLV = GetDlgItem(HWND_FRMOUTPUT, IDC_FRMOUTPUT_LISTVIEW)
   ListView_DeleteAllItems( hLV )
   
   ' Parse the log string.
   NumLines = AfxStrParseCount( wsLogSt, vbCrLf)
 
   For NextLine = 1 To NumLines                   
   
      wst = Trim(AfxStrParse(wsLogSt, NextLine, vbCrlf))                 
      wst_ucase = Ucase(wst)
      
      ' Deal with the situation where we might be trying to compile a 64-bit application
      ' from a 32-bit operating system.
      If Left(**wst_ucase, 16) = WSTR("THIS VERSION OF ") Then
         ' Simply set the log file text and show the compiler log file window rather
         ' than the default compiler results window.
         SetCompileStatusBarMessage( "", 0 )
         TabCtrl_SetCurSel( GetDlgItem( HWND_FRMOUTPUT, IDC_FRMOUTPUT_TABCONTROL), 1 )
         AfxSetWindowText( GetDlgItem( HWND_FRMOUTPUT, IDC_FRMOUTPUT_TXTLOGFILE), wst )
         ShowWindow HWND_FRMOUTPUT, SW_SHOW
         frmMain_PositionWindows(HWND_FRMMAIN)
         return true
      End If   

      ' Save the FB version and copyright information. 
      If Left(**wst_ucase, 19) = WSTR("FREEBASIC COMPILER ") Then
         fGoodCompile = True
         wszOutputMsg = wszOutputMsg & wst & vbCrLf: Continue For
      End If        
      If Left(**wst_ucase, 13) = WSTR("COPYRIGHT (C)") Then
         fGoodCompile = True
         wszOutputMsg = wszOutputMsg & wst & vbCrLf: Continue For
      End If        
      
      ' Check to see if an error occurred in compiling a resource script.
      ' If there was a bad source name passed to the compiler (for 
      ' example, missing .rc file), then the output at the end of the
      ' log file is like the following:
      '
      ' Error!
      ' Could Not Open source file (p.RC)
      ' OBJ file Not made
      ' compiling resource FAILED: Error Code 1
      '
      ' Very sorry, but GoRC had a problem.
                                          
      ' Check to see if linking failed
      If (Left(**wst_ucase, 6) = WSTR("ERROR!")) or _
         (left(**wst_ucase, 34) = "VERY SORRY, BUT GORC HAD A PROBLEM") Then
         FF_ListView_InsertItem( hLV, NumWarnings, 0, WStr("0") )
         FF_ListView_InsertItem( hLV, NumWarnings, 1, WStr("") ) 
         FF_ListView_InsertItem( hLV, NumWarnings, 2, WStr("compiling resource FAILED: Error Code 1") ) 
         NumErrors = NumErrors + 1
      elseIf (Left(**wst_ucase, 5) = WSTR("ERROR")) Then
         FF_ListView_InsertItem( hLV, NumWarnings, 0, WStr("0") )
         FF_ListView_InsertItem( hLV, NumWarnings, 1, WStr("") ) 
         FF_ListView_InsertItem( hLV, NumWarnings, 2, WStr("compiling FAILED: Error Code 1") ) 
         NumErrors = NumErrors + 1
      elseIf (Left(**wst_ucase, 19) = WSTR("COMPILING C FAILED:")) Then
         FF_ListView_InsertItem( hLV, NumWarnings, 0, WStr("0") )
         FF_ListView_InsertItem( hLV, NumWarnings, 1, WStr("") ) 
         FF_ListView_InsertItem( hLV, NumWarnings, 2, WStr("compiling C FAILED: Error Code 1") ) 
         NumErrors = NumErrors + 1
      end if
      
      If Instr(**wst_ucase, WSTR("LINKING FAILED:")) Then 
         FF_ListView_InsertItem( hLV, NumWarnings, 0, WStr("0") ) 
         FF_ListView_InsertItem( hLV, NumWarnings, 1, WStr("") ) 
         FF_ListView_InsertItem( hLV, NumWarnings, 2, wst ) 
         NumErrors = NumErrors + 1
      End If
 
 
      ' Check for the "linking: " line because that line identifies the actual output filename being created
      If Left(**wst_ucase, 9) = WSTR("LINKING: ") Then 
         ' -o "WinFBE.exe" 
         i = Instr(wst, WSTR(" -o "))
         If i Then
            wszTemp = Mid(wst, i+4)
            i = Instr(wszTemp, wDQ & WSTR(" "))
            If i Then gCompile.OutputFilename = AfxStrRemove(Left(wszTemp, i), wDQ)
         End If      
      End If
      
      ' Check for any compiler warnings
      IsWarning = Instr(wst_ucase, WSTR(") WARNING "))
      If IsWarning Then Error_Start = IsWarning
      IsError   = Instr(wst_ucase, WSTR(") ERROR ")) 
      If IsError Then Error_Start = IsError
      
      If (Error_Start > 0) Then 
         'sample warning message
         'c:\freebasic\test.bas(1394) warning 1(1): Passing scalar as pointer, at parameter 2 (hwndOldFocus) of ONSETFOCUS()
         'sample error message
         'c:\freebasic\test.bas(17) error 41: Variable not declared, kjljjada in 'kjljjada Error'
         Parenthesis_Start = Instr(1, wst, WSTR("("))
         Parenthesis_End   = Instr(Parenthesis_Start, wst, WSTR(")"))
         If (Parenthesis_Start < Parenthesis_End) And _
            (Parenthesis_End <= Error_Start)      And _
            (Error_Start > 0) Then
   
            wst1 = AfxStrParse(wst, 1, WSTR("("))   ' filename
            wst2 = Mid( wst, Parenthesis_Start + 1, Parenthesis_End - Parenthesis_Start - 1)  ' line# 
            wst3 = Mid( wst, Error_Start + 1)  ' error message
 
            FF_ListView_InsertItem( hLV, NumWarnings+NumErrors, 0, wst2 )  ' line#
            FF_ListView_InsertItem( hLV, NumWarnings+NumErrors, 1, wst1 )  ' filename
            FF_ListView_InsertItem( hLV, NumWarnings+NumErrors, 2, wst3 )  ' error message
            
            If IsWarning Then 
               NumWarnings = NumWarnings + 1
            ElseIf IsError Then
               If nFirstErrorLine = -1 Then
                  nFirstErrorLine = NumWarnings + NumErrors
               End If
               NumErrors = NumErrors + 1
            End If            
         End If
      End If
 
   Next
 
   ' If there were no errors but the fGoodCompile flag was not set to True then the log file
   ' did not contain the required FB copyright notice. Something must have went wrong, like trying
   ' to call the 64 bit compiler using a 32 bit operating system. Show the message to the user via
   ' the Output window (log file).
   If fGoodCompile = False Then NumErrors = NumErrors + 1

   dim as CWSTR cwsCompileMsg = L(193, "Errors:") & " " & NumErrors & "  " & _
                                L(192, "Warnings:") & " " & NumWarnings 
                                

   If (NumWarnings = 0) AndAlso (NumErrors = 0) Then 
      ' Hide the Output window is already open but there is no longer any warnings
      ' or errors to show in the listview. Only close if the listview is active. Do not
      ' close if the user is looking at the logfile textbox.
      if bAllowSuccessMessage then  ' we are not compiling modules
         if IsWindowVisible(HWND_FRMOUTPUT) THEN
            if TabCtrl_GetCurSel( GetDlgItem(HWND_FRMOUTPUT, IDC_FRMOUTPUT_TABCONTROL) ) = 0 THEN
               ShowWindow( HWND_FRMOUTPUT, SW_HIDE )
               frmMain_PositionWindows(HWND_FRMMAIN)
            END IF   
         END IF 
         cwsCompileMsg = cwsCompileMsg + "  [ " + gCompile.OutputFilename + " ]"
         SetCompileStatusBarMessage( cwsCompileMsg, ghIconGood )
         If gConfig.HideCompile = False Then
            wst = wszOutputMsg & vbCrLf & _
                  L(194,"Primary Source:")     & " " & gCompile.MainFilename & vbCrLf & _
                  L(195,"Target Compilation:") & " " & gCompile.OutputFilename & vbCrLf & _
                  L(196,"Compile Time:")       & " " & Format(gCompile.EndTime-gCompile.StartTime, "###0.0") & " " & L(198,"seconds") & "." & vbCrLf & _
                  L(197,"File Size:")          & " " & FileLen(gCompile.OutputFilename) & " " & L(199,"bytes") & "."
            MessageBox( HWND_FRMMAIN, wst, L(191,"Compiler Results"), MB_ICONINFORMATION Or MB_OK) 
         else
            ' Simply sound a success
            MessageBeep(MB_OK)
         End If
      end if
   Else 
      ' There were errors and/or warnings
      If NumErrors > 0 Then gCompile.RunAfterCompile = False 
      
      cwsCompileMsg = cwsCompileMsg + "  [" + Format(Now, "yyyy-mm-dd h:mm:ss AM/PM") & "]" 
      SetCompileStatusBarMessage( cwsCompileMsg, ghIconBad )
      MessageBeep(MB_ICONWARNING)
      
      ' Position the Compiler Log to the first error/warning
      ListView_SelectItem( hLV, 0 )
      
      ' If fGoodCompile is False then something unusual occurred so better show the log file by default
      If fGoodCompile = False Then
         PositionOutputWindows(HWND_FRMOUTPUT)
      End If 
      ' If the Search Results, TODO, Notes tab is active then reposition to 
      ' the first tab for error listview
      select case TabCtrl_GetCurSel( GetDlgItem(HWND_FRMOUTPUT, IDC_FRMOUTPUT_TABCONTROL) ) 
         case 2, 3, 4
         TabCtrl_SetCurSel( GetDlgItem(HWND_FRMOUTPUT, IDC_FRMOUTPUT_TABCONTROL), 0 ) 
         PositionOutputWindows(HWND_FRMOUTPUT)
      end select
      ShowWindow( HWND_FRMOUTPUT, SW_SHOW)
      frmMain_PositionWindows(HWND_FRMMAIN)
      ' Set to error line position only after all windows have been shown and resized
      SetDocumentErrorPosition(hLV, wID)
      return true
   End If

   function = false

end function



