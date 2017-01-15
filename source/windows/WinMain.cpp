//---------------------------------------------------------------------------
#include <vcl.h>
#pragma hdrstop

#include <CoreMain.h>

#include <Log.h>
#include <TextsWin.h>
#include <TextsCore.h>
#include <HelpWin.h>

#include "CustomScpExplorer.h"
#include "TerminalManager.h"
#include "NonVisual.h"
#include "Glyphs.h"
#include "ProgParams.h"
#include "Setup.h"
#include "WinConfiguration.h"
#include "GUITools.h"
#include "Tools.h"
#include "WinApi.h"
#include <DateUtils.hpp>
//---------------------------------------------------------------------------
#pragma package(smart_init)
//---------------------------------------------------------------------------
void __fastcall GetLoginData(UnicodeString SessionName, TOptions * Options,
  TObjectList * DataList, UnicodeString & DownloadFile, bool NeedSession, TForm * LinkedForm)
{
  bool DefaultsOnly = false;

  if (StoredSessions->IsFolder(SessionName) ||
      StoredSessions->IsWorkspace(SessionName))
  {
    StoredSessions->GetFolderOrWorkspace(SessionName, DataList);
  }
  else
  {
    TSessionData * SessionData =
      StoredSessions->ParseUrl(SessionName, Options, DefaultsOnly,
        &DownloadFile);
    DataList->Add(SessionData);

    if (DataList->Count == 1)
    {
      TSessionData * SessionData = DebugNotNull(dynamic_cast<TSessionData *>(DataList->Items[0]));
      if (SessionData->SaveOnly)
      {
        Configuration->Usage->Inc(L"CommandLineSessionSave");
        TSessionData * SavedSession = DoSaveSession(SessionData, NULL, true, NULL);
        if (SavedSession == NULL)
        {
          Abort();
        }
        WinConfiguration->LastStoredSession = SavedSession->Name;
        DataList->Clear();
      }
      else if (!SessionData->PuttyProtocol.IsEmpty())
      {
        // putty does not support resolving environment variables in session settings
        // though it's hardly of any use here.
        SessionData->ExpandEnvironmentVariables();
        OpenSessionInPutty(GUIConfiguration->PuttyPath, SessionData);
        Abort();
      }
    }
  }

  if (DefaultsOnly && !NeedSession)
  {
    // No URL specified on command-line and no explicit command-line parameter
    // that requires session was specified => noop
    DataList->Clear();
  }
  else if ((DataList->Count == 0) ||
      !dynamic_cast<TSessionData *>(DataList->Items[0])->CanLogin ||
      DefaultsOnly)
  {
    // Note that GetFolderOrWorkspace never returns sites that !CanLogin,
    // so we should not get here with more then one site.
    // Though we should be good, if we ever do.

    // We get here when:
    // - we need session for explicit command-line operation
    // - after we handle "save" URL.
    // - the specified session does not contain enough information to login [= not even hostname]

    DebugAssert(DataList->Count <= 1);
    if (!DoLoginDialog(StoredSessions, DataList, LinkedForm))
    {
      Abort();
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall Upload(TTerminal * Terminal, TStrings * FileList, bool UseDefaults)
{
  UnicodeString TargetDirectory;
  TGUICopyParamType CopyParam = GUIConfiguration->DefaultCopyParam;

  TargetDirectory = UnixIncludeTrailingBackslash(Terminal->CurrentDirectory);

  int Options = coDisableQueue;
  int CopyParamAttrs = Terminal->UsableCopyParamAttrs(0).Upload;
  if (UseDefaults ||
      DoCopyDialog(true, false, FileList, TargetDirectory, &CopyParam, Options,
        CopyParamAttrs, NULL, NULL))
  {
    Terminal->CopyToRemote(FileList, TargetDirectory, &CopyParam, 0);
  }
}
//---------------------------------------------------------------------------
void __fastcall Download(TTerminal * Terminal, const UnicodeString FileName,
  bool UseDefaults)
{
  UnicodeString TargetDirectory;
  TGUICopyParamType CopyParam = GUIConfiguration->DefaultCopyParam;
  TStrings * FileList = NULL;

  try
  {
    FileList = new TStringList();
    TRemoteFile * File = Terminal->Files->FindFile(FileName);
    if (File == NULL)
    {
      throw Exception(FMTLOAD(FILE_NOT_EXISTS, (FileName)));
    }
    FileList->AddObject(FileName, File);
    UnicodeString LocalDirectory = ExpandFileName(Terminal->SessionData->LocalDirectory);
    if (LocalDirectory.IsEmpty())
    {
      LocalDirectory = GetPersonalFolder();
    }
    TargetDirectory = IncludeTrailingBackslash(LocalDirectory);

    int Options = coDisableQueue;
    int CopyParamAttrs = Terminal->UsableCopyParamAttrs(0).Download;
    if (UseDefaults ||
        DoCopyDialog(false, false, FileList, TargetDirectory, &CopyParam,
          Options, CopyParamAttrs, NULL, NULL))
    {
      Terminal->CopyToLocal(FileList, TargetDirectory, &CopyParam, 0);
    }
  }
  __finally
  {
    delete FileList;
  }
}
//---------------------------------------------------------------------------
void __fastcall Edit(TCustomScpExplorerForm * ScpExplorer, TStrings * FileList)
{
  ScpExplorer->StandaloneEdit(FileList->Strings[0]);
  Abort();
}
//---------------------------------------------------------------------------
void __fastcall SynchronizeDirectories(TTerminal * Terminal,
  TStrings * CommandParams,
  UnicodeString & LocalDirectory, UnicodeString & RemoteDirectory)
{
  if (CommandParams->Count >= 1)
  {
    LocalDirectory = CommandParams->Strings[0];
  }
  else if (!Terminal->SessionData->LocalDirectory.IsEmpty())
  {
    LocalDirectory = ExpandFileName(Terminal->SessionData->LocalDirectory);
  }
  else
  {
    LocalDirectory = WinConfiguration->ScpExplorer.LastLocalTargetDirectory;
  }

  if (CommandParams->Count >= 2)
  {
    RemoteDirectory = CommandParams->Strings[1];
  }
  else
  {
    RemoteDirectory = Terminal->CurrentDirectory;
  }
}
//---------------------------------------------------------------------------
void __fastcall FullSynchronize(TTerminal * Terminal, TCustomScpExplorerForm * ScpExplorer,
  TStrings * CommandParams, bool UseDefaults)
{
  UnicodeString LocalDirectory;
  UnicodeString RemoteDirectory;

  SynchronizeDirectories(Terminal, CommandParams, LocalDirectory, RemoteDirectory);

  bool SaveMode = true;
  // bit ugly
  TSynchronizeMode Mode = (TSynchronizeMode)GUIConfiguration->SynchronizeMode;
  if (ScpExplorer->DoFullSynchronizeDirectories(LocalDirectory,
        RemoteDirectory, Mode, SaveMode, UseDefaults))
  {
    if (SaveMode)
    {
      GUIConfiguration->SynchronizeMode = Mode;
    }

    Terminal->CloseOnCompletion();
  }
  else
  {
    Abort();
  }
}
//---------------------------------------------------------------------------
void __fastcall Synchronize(TTerminal * Terminal, TCustomScpExplorerForm * ScpExplorer,
  TStrings * CommandParams, bool UseDefaults)
{
  UnicodeString LocalDirectory;
  UnicodeString RemoteDirectory;

  SynchronizeDirectories(Terminal, CommandParams, LocalDirectory, RemoteDirectory);

  ScpExplorer->DoSynchronizeDirectories(LocalDirectory, RemoteDirectory, UseDefaults);
  Abort();
}
//---------------------------------------------------------------------------
void __fastcall ImportSitesIfAny()
{
  if (!WinConfiguration->AutoImportedFromPuttyOrFilezilla)
  {
    bool AnyPuttySession = GUIConfiguration->AnyPuttySessionForImport(StoredSessions);
    bool AnyFilezillaSession = GUIConfiguration->AnyFilezillaSessionForImport(StoredSessions);

    if (AnyPuttySession || AnyFilezillaSession)
    {
      UnicodeString PuttySource = LoadStrPart(IMPORT_SESSIONS2, 2);
      UnicodeString FilezillaSource = LoadStrPart(IMPORT_SESSIONS2, 3);
      UnicodeString Source;
      if (AnyPuttySession && AnyFilezillaSession)
      {
        Source = FORMAT(LoadStrPart(IMPORT_SESSIONS2, 4), (PuttySource, FilezillaSource));
      }
      else if (AnyPuttySession)
      {
        Source = PuttySource;
      }
      else if (AnyFilezillaSession)
      {
        Source = FilezillaSource;
      }
      else
      {
        DebugFail();
      }

      UnicodeString Message = FORMAT(LoadStrPart(IMPORT_SESSIONS2, 1), (Source));

      if (MessageDialog(Message, qtConfirmation,
            qaYes | qaNo, HELP_IMPORT_SESSIONS) == qaYes)
      {
        DoImportSessionsDialog(NULL);
      }

      WinConfiguration->AutoImportedFromPuttyOrFilezilla = true;
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall Usage(UnicodeString Param)
{
  while (!Param.IsEmpty())
  {
    UnicodeString Pair = CutToChar(Param, L',', true);
    if (!Pair.IsEmpty())
    {
      if (Pair[Pair.Length()] == L'+')
      {
        UnicodeString Key = Pair.SubString(1, Pair.Length() - 1).Trim();
        Configuration->Usage->Inc(Key);
      }
      else
      {
        UnicodeString Key = CutToChar(Pair, L':', true);
        Configuration->Usage->Set(Key, Pair.Trim());
      }
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall RecordWrapperVersions(UnicodeString ConsoleVersion, UnicodeString DotNetVersion)
{
  TUpdatesConfiguration Updates = WinConfiguration->Updates;
  if (!DotNetVersion.IsEmpty())
  {
    Updates.DotNetVersion = DotNetVersion;
  }
  if (!ConsoleVersion.IsEmpty())
  {
    Updates.ConsoleVersion = ConsoleVersion;
  }
  WinConfiguration->Updates = Updates;

  if (Configuration->Storage == stNul)
  {
    Configuration->SetDefaultStorage();
    try
    {
      THierarchicalStorage * Storage = Configuration->CreateConfigStorage();
      try
      {
        Storage->AccessMode = smReadWrite;
        if (Storage->OpenSubKey(Configuration->ConfigurationSubKey, true) &&
            Storage->OpenSubKey(L"Interface\\Updates", true, true))
        {
          if (!DotNetVersion.IsEmpty())
          {
            Storage->WriteString(L"DotNetVersion", DotNetVersion);
          }
          if (!ConsoleVersion.IsEmpty())
          {
            Storage->WriteString(L"ConsoleVersion", ConsoleVersion);
          }
        }
      }
      __finally
      {
        delete Storage;
      }
    }
    __finally
    {
      Configuration->SetNulStorage();
    }
  }
}
//---------------------------------------------------------------------------
static UnicodeString ColorToRGBStr(TColor Color)
{
  int RGB = ColorToRGB(Color);
  int R = GetRValue(RGB);
  int G = GetGValue(RGB);
  int B = GetBValue(RGB);
  UnicodeString Result = FORMAT(L"%.2x%.2x%.2x", (R, G, B));
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall UpdateStaticUsage()
{
  Configuration->Usage->Inc(L"Runs");

  Configuration->Usage->UpdateCurrentVersion();

  Configuration->Usage->Set(L"WindowsVersion", (WindowsVersionLong()));
  Configuration->Usage->Set(L"WindowsProductName", (WindowsProductName()));
  DWORD Type;
  GetWindowsProductType(Type);
  Configuration->Usage->Set(L"WindowsProductType", (static_cast<int>(Type)));
  Configuration->Usage->Set(L"Windows64", IsWin64());
  Configuration->Usage->Set(L"DefaultLocale",
    // See TGUIConfiguration::GetLocaleHex()
    IntToHex(static_cast<int>(GetDefaultLCID()), 4));
  Configuration->Usage->Set(L"Locale",
    IntToHex(static_cast<int>(WinConfiguration->Locale), 4));
  Configuration->Usage->Set(L"PixelsPerInch", Screen->PixelsPerInch);

  bool PixelsPerInchSystemDiffers = false;
  bool PixelsPerInchMonitorsDiffer = false;
  bool PixelsPerInchAxesDiffer = false;

  HINSTANCE ShCoreLibrary = LoadLibrary(L"shcore.dll");
  if (ShCoreLibrary != NULL)
  {
    GetDpiForMonitorProc GetDpiForMonitor =
      (GetDpiForMonitorProc)GetProcAddress(ShCoreLibrary, "GetDpiForMonitor");

    if (GetDpiForMonitor != NULL)
    {
      unsigned int PrimaryDpiX;
      unsigned int PrimaryDpiY;

      for (int Index = 0; Index < Screen->MonitorCount; Index++)
      {
        unsigned int DpiX;
        unsigned int DpiY;
        GetDpiForMonitor(Screen->Monitors[Index]->Handle, MDT_Default, &DpiX, &DpiY);

        if (DpiX != DpiY)
        {
          PixelsPerInchAxesDiffer = true;
        }

        if (Index == 0)
        {
          PrimaryDpiX = DpiX;
          PrimaryDpiY = DpiY;

          // PixelsPerInch is GetDeviceCaps(DC, LOGPIXELSY)
          if (DpiY != (unsigned int)Screen->PixelsPerInch)
          {
            PixelsPerInchSystemDiffers = true;
          }
        }
        else
        {
          if ((DpiX != PrimaryDpiX) ||
              (DpiY != PrimaryDpiY))
          {
            PixelsPerInchMonitorsDiffer = true;
          }
        }
      }
    }
  }

  if (PixelsPerInchSystemDiffers)
  {
    Configuration->Usage->Inc(L"PixelsPerInchSystemDiffered");
  }
  Configuration->Usage->Set(L"PixelsPerInchMonitorsDiffer", PixelsPerInchMonitorsDiffer);
  Configuration->Usage->Set(L"PixelsPerInchAxesDiffer", PixelsPerInchAxesDiffer);

  Configuration->Usage->Set(L"WorkAreaWidth", Screen->WorkAreaWidth);
  Configuration->Usage->Set(L"WorkAreaHeight", Screen->WorkAreaHeight);
  HDC DC = GetDC(NULL);
  int Planes = GetDeviceCaps(DC, PLANES);
  int BitsPixel = GetDeviceCaps(DC, BITSPIXEL);
  Configuration->Usage->Set(L"ColorDepth", Planes * BitsPixel);
  Configuration->Usage->Set(L"MonitorCount", Screen->MonitorCount);
  Configuration->Usage->Set(L"NotUseThemes", !UseThemes());
  Configuration->Usage->Set(L"ThemeDefaultFontSize", Application->DefaultFont->Size);
  Configuration->Usage->Set(L"ThemeIconFontSize", Screen->IconFont->Size);

  Configuration->Usage->Set(L"SysColorWindow", ColorToRGBStr(clWindow));
  Configuration->Usage->Set(L"SysColorBtnFace", ColorToRGBStr(clBtnFace));
  Configuration->Usage->Set(L"SysColorWindowText", ColorToRGBStr(clWindowText));

  UnicodeString ProgramsFolder;
  ::SpecialFolderLocation(CSIDL_PROGRAM_FILES, ProgramsFolder);
  ProgramsFolder = IncludeTrailingBackslash(ExpandFileName(ProgramsFolder));
  UnicodeString ExeName = ExpandFileName(Application->ExeName);
  bool InProgramFiles = AnsiSameText(ExeName.SubString(1, ProgramsFolder.Length()), ProgramsFolder);
  Configuration->Usage->Set(L"InProgramFiles", InProgramFiles);
  Configuration->Usage->Set(L"IsInstalled", IsInstalled());
  Configuration->Usage->Set(L"Wine", IsWine());
  Configuration->Usage->Set(L"NetFrameworkVersion", GetNetVersionStr());
  Configuration->Usage->Set(L"PowerShellVersion", GetPowerShellVersionStr());

  WinConfiguration->UpdateStaticUsage();

}
//---------------------------------------------------------------------------
void __fastcall MaintenanceTask()
{
  CoreMaintenanceTask();
}
//---------------------------------------------------------------------------
typedef std::vector<HWND> THandles;
typedef std::map<unsigned long, THandles> TProcesses;
//---------------------------------------------------------------------------
BOOL __stdcall EnumOtherInstances(HWND Handle, LPARAM AParam)
{
  TProcesses & Processes = *reinterpret_cast<TProcesses *>(AParam);

  unsigned long ProcessId;
  if (GetWindowThreadProcessId(Handle, &ProcessId) != 0)
  {
    Processes[ProcessId].push_back(Handle);
  }

  return TRUE;
}
//---------------------------------------------------------------------------
static bool __fastcall SendCopyDataMessage(HWND Window, TCopyDataMessage & Message)
{
  COPYDATASTRUCT CopyData;
  CopyData.cbData = sizeof(Message);
  CopyData.lpData = &Message;

  LRESULT SendResult =
    SendMessage(Window, WM_COPYDATA,
       reinterpret_cast<WPARAM>(HInstance), reinterpret_cast<LPARAM>(&CopyData));
  bool Result = (SendResult > 0);
  return Result;
}
//---------------------------------------------------------------------------
static void __fastcall FindOtherInstances(THandles & OtherInstances)
{
  TProcesses Processes;

  // FindWindow is optimization (if there's no hidden window, no point enumerating all windows to find some)
  if ((FindWindow(HIDDEN_WINDOW_NAME, NULL) != NULL) &&
      EnumWindows(EnumOtherInstances, reinterpret_cast<LPARAM>(&Processes)))
  {
    TCopyDataMessage Message;

    Message.Command = TCopyDataMessage::MainWindowCheck;

    TProcesses::const_iterator ProcessI = Processes.begin();
    while (ProcessI != Processes.end())
    {
      HWND HiddenWindow = NULL;
      THandles::const_iterator WindowI = ProcessI->second.begin();

      while ((HiddenWindow == NULL) && (WindowI != ProcessI->second.end()))
      {
        wchar_t ClassName[1024];
        if (GetClassName(*WindowI, ClassName, LENOF(ClassName)) != 0)
        {
          NULL_TERMINATE(ClassName);

          if (wcscmp(ClassName, HIDDEN_WINDOW_NAME) == 0)
          {
            HiddenWindow = *WindowI;
          }
        }
        WindowI++;
      }

      if (HiddenWindow != NULL)
      {
        WindowI = ProcessI->second.begin();

        while (WindowI != ProcessI->second.end())
        {
          if (*WindowI != HiddenWindow) // optimization
          {
            if (SendCopyDataMessage(*WindowI, Message))
            {
              OtherInstances.push_back(*WindowI);
              break;
            }
          }
          WindowI++;
        }
      }

      ProcessI++;
    }
  }
}
//---------------------------------------------------------------------------
bool __fastcall SendToAnotherInstance()
{
  THandles OtherInstances;
  FindOtherInstances(OtherInstances);

  bool Result = false;
  THandles::const_iterator I = OtherInstances.begin();
  while (!Result && (I != OtherInstances.end()))
  {
    HWND Handle = *I;

    TCopyDataMessage Message;
    Message.Command = TCopyDataMessage::CommandCanCommandLine;

    if (SendCopyDataMessage(Handle, Message))
    {
      // Restore window, if minimized
      ShowWindow(Handle, SW_RESTORE);
      // bring it to foreground
      SetForegroundWindow(Handle);

      Message.Command = TCopyDataMessage::CommandCommandLine;
      wcsncpy(Message.CommandLine, CmdLine, LENOF(Message.CommandLine));
      NULL_TERMINATE(Message.CommandLine);

      Result = SendCopyDataMessage(Handle, Message);
    }

    I++;
  }

  return Result;
}
//---------------------------------------------------------------------------
void __fastcall Refresh(const UnicodeString & Session, const UnicodeString & Path)
{
  THandles OtherInstances;
  FindOtherInstances(OtherInstances);

  THandles::const_iterator I = OtherInstances.begin();
  while (I != OtherInstances.end())
  {
    HWND Handle = *I;

    TCopyDataMessage Message;
    Message.Command = TCopyDataMessage::RefreshPanel;
    wcsncpy(Message.Refresh.Session, Session.c_str(), LENOF(Message.Refresh.Session));
    NULL_TERMINATE(Message.Refresh.Session);
    wcsncpy(Message.Refresh.Path, Path.c_str(), LENOF(Message.Refresh.Path));
    NULL_TERMINATE(Message.Refresh.Path);

    SendCopyDataMessage(Handle, Message);

    I++;
  }
}
//---------------------------------------------------------------------------
bool __fastcall ShowUpdatesIfAvailable()
{
  TUpdatesConfiguration Updates = WinConfiguration->Updates;
  int CurrentCompoundVer = Configuration->CompoundVersion;
  bool NoPopup = true;
  bool Result =
    Updates.ShowOnStartup &&
    Updates.HaveValidResultsForVersion(CurrentCompoundVer) &&
    !Updates.Results.Disabled &&
    ((Updates.Results.Version > CurrentCompoundVer) || !Updates.Results.Message.IsEmpty()) &&
    !Updates.ShownResults;
  if (Result)
  {
    Configuration->Usage->Inc(L"UpdateStartup");
    Result = CheckForUpdates(true);
    if (Result)
    {
      Configuration->Usage->Inc(L"UpdateDownloadOpensStartup");
    }
    NoPopup = false;
  }
  else if (WinConfiguration->ShowTips)
  {
    int Days = DaysBetween(WinConfiguration->TipsShown, Now());
    if ((Days >= Updates.Results.TipsIntervalDays) &&
        (WinConfiguration->RunsSinceLastTip >= Updates.Results.TipsIntervalDays))
    {
      UnicodeString Tip = FirstUnshownTip();
      if (!Tip.IsEmpty())
      {
        AutoShowNewTip();
        NoPopup = false;
      }
      else
      {
        Configuration->Usage->Inc(L"TipsNoUnseen");
      }
    }
  }

  if (NoPopup)
  {
    WinConfiguration->RunsSinceLastTip = WinConfiguration->RunsSinceLastTip + 1;
  }
  return Result;
}
//---------------------------------------------------------------------------
int __fastcall Execute()
{
  DebugAssert(StoredSessions);
  TProgramParams * Params = TProgramParams::Instance();
  DebugAssert(Params);

  // do not flash message boxes on startup
  SetOnForeground(true);

  // let installer know, that some instance of application is running
  CreateMutex(NULL, False, AppName.c_str());
  bool OnlyInstance = (GetLastError() == 0);

  UpdateStaticUsage();

  UnicodeString KeyFile;
  if (Params->FindSwitch(L"PrivateKey", KeyFile))
  {
    WinConfiguration->DefaultKeyFile = KeyFile;
  }

  UnicodeString ConsoleVersion;
  UnicodeString DotNetVersion;
  Params->FindSwitch(L"Console", ConsoleVersion);
  Params->FindSwitch(L"DotNet", DotNetVersion);
  if (!ConsoleVersion.IsEmpty() || !DotNetVersion.IsEmpty())
  {
    RecordWrapperVersions(ConsoleVersion, DotNetVersion);
  }
  if (!DotNetVersion.IsEmpty())
  {
    Configuration->Usage->Inc(L"ConsoleDotNet");
  }

  UnicodeString SwitchValue;
  if (Params->FindSwitch(L"loglevel", SwitchValue))
  {
    int StarPos = SwitchValue.Pos(L"*");
    if (StarPos > 0)
    {
      bool LogSensitive = true;
      SwitchValue.Delete(StarPos, 1);

      if ((StarPos <= SwitchValue.Length()) &&
          (SwitchValue[StarPos] == L'-'))
      {
        LogSensitive = false;
        SwitchValue.Delete(StarPos, 1);
      }

      SwitchValue = SwitchValue.Trim();

      Configuration->TemporaryLogSensitive(LogSensitive);
    }
    int LogProtocol;
    if (!SwitchValue.IsEmpty() && TryStrToInt(SwitchValue, LogProtocol) && (LogProtocol >= 0))
    {
      Configuration->TemporaryLogProtocol(LogProtocol);
    }
  }

  TConsoleMode Mode = cmNone;
  if (Params->FindSwitch(L"help") || Params->FindSwitch(L"h") || Params->FindSwitch(L"?"))
  {
    Mode = cmHelp;
  }
  else if (Params->FindSwitch(L"batchsettings"))
  {
    Mode = cmBatchSettings;
  }
  else if (Params->FindSwitch(KEYGEN_SWITCH))
  {
    Mode = cmKeyGen;
  }
  else if (Params->FindSwitch(FINGERPRINTSCAN_SWITCH))
  {
    Mode = cmFingerprintScan;
  }
  // We have to check for /console only after the other options,
  // as the /console is always used when we are run by winscp.com
  // (ambiguous use to pass console version)
  else if (Params->FindSwitch(L"Console") || Params->FindSwitch(SCRIPT_SWITCH) ||
      Params->FindSwitch(COMMAND_SWITCH))
  {
    Mode = cmScripting;
  }

  if (Mode != cmNone)
  {
    return Console(Mode);
  }

  TTerminalManager * TerminalManager = NULL;
  GlyphsModule = NULL;
  NonVisualDataModule = NULL;
  TStrings * CommandParams = new TStringList;
  try
  {
    TerminalManager = TTerminalManager::Instance();
    HANDLE ResourceModule = GUIConfiguration->ChangeToDefaultResourceModule();
    try
    {
      GlyphsModule = new TGlyphsModule(Application);
    }
    __finally
    {
      GUIConfiguration->ChangeResourceModule(ResourceModule);
    }
    NonVisualDataModule = new TNonVisualDataModule(Application);

    LogForm = NULL;

    // The default is 2.5s.
    // 20s is used by Office 2010 and Windows 10 Explorer.
    // Some applications use an infinite (Thunderbird, Firefox).
    // Overriden for some controls using THintInfo.HideTimeout
    Application->HintHidePause = 20000;
    HintWindowClass = __classid(TScreenTipHintWindow);

    UnicodeString IniFileName = Params->SwitchValue(INI_SWITCH);
    if (!IniFileName.IsEmpty())
    {
      UnicodeString IniFileNameExpanded = ExpandEnvironmentVariables(IniFileName);
      if (!FileExists(ApiPath(IniFileNameExpanded)))
      {
        // this should be displayed rather at the very beginning.
        // however for simplicity (GUI-only), we do it only here.
        MessageDialog(FMTLOAD(FILE_NOT_EXISTS, (IniFileNameExpanded)), qtError, qaOK);
      }
    }

    if (Params->FindSwitch(L"UninstallCleanup"))
    {
      MaintenanceTask();
      // The innosetup cannot skip UninstallCleanup run task for silent uninstalls,
      // workaround is that we create mutex in uninstaller, if it runs silent, and
      // ignore the UninstallCleanup, when the mutex exists.
      if ((OpenMutex(SYNCHRONIZE, false, L"WinSCPSilentUninstall") == NULL) &&
          (MessageDialog(MainInstructions(LoadStr(UNINSTALL_CLEANUP)), qtConfirmation,
            qaYes | qaNo, HELP_UNINSTALL_CLEANUP) == qaYes))
      {
        DoCleanupDialog(StoredSessions, Configuration);
      }
    }
    else if (Params->FindSwitch(L"RegisterForDefaultProtocols") ||
             Params->FindSwitch(L"RegisterAsUrlHandler")) // BACKWARD COMPATIBILITY
    {
      MaintenanceTask();
      if (CheckSafe(Params))
      {
        RegisterForDefaultProtocols();
        Configuration->DontSave();
      }
    }
    else if (Params->FindSwitch(L"UnregisterForProtocols"))
    {
      MaintenanceTask();
      if (CheckSafe(Params))
      {
        UnregisterForProtocols();
        Configuration->DontSave();
      }
    }
    else if (Params->FindSwitch(L"AddSearchPath"))
    {
      MaintenanceTask();
      if (CheckSafe(Params))
      {
        AddSearchPath(ExtractFilePath(Application->ExeName));
        Configuration->DontSave();
      }
    }
    else if (Params->FindSwitch(L"RemoveSearchPath"))
    {
      MaintenanceTask();
      if (CheckSafe(Params))
      {
        try
        {
          RemoveSearchPath(ExtractFilePath(Application->ExeName));
        }
        catch(...)
        {
          // ignore errors
          // (RemoveSearchPath is called always on uninstallation,
          // even if AddSearchPath was not used, so we would get the error
          // always for non-priviledged user)
        }
        Configuration->DontSave();
      }
    }
    else if (Params->FindSwitch(L"ImportSitesIfAny"))
    {
      MaintenanceTask();
      ImportSitesIfAny();
    }
    else if (Params->FindSwitch(L"Usage", SwitchValue))
    {
      MaintenanceTask();
      Usage(SwitchValue);
    }
    else if (Params->FindSwitch(L"Update"))
    {
      MaintenanceTask();
      CheckForUpdates(false);
    }
    else if (ShowUpdatesIfAvailable())
    {
      // noop
    }
    else if (Params->FindSwitch(L"Exit"))
    {
      // noop
      MaintenanceTask();
      Configuration->DontSave();
    }
    else if (Params->FindSwitch(L"MaintenanceTask"))
    {
      // Parameter /MaintenanceTask can be added to command-line when executing maintenance tasks
      // (e.g. from installer) just in case old version of WinSCP is called by mistake
      MaintenanceTask();
      Configuration->DontSave();
    }
    else
    {
      enum { pcNone, pcUpload, pcFullSynchronize, pcSynchronize, pcEdit, pcRefresh } ParamCommand;
      ParamCommand = pcNone;
      UnicodeString AutoStartSession;
      UnicodeString DownloadFile;
      bool UseDefaults = false;

      // do not check for temp dirs for service tasks (like RegisterAsUrlHandler)
      if (OnlyInstance &&
          WinConfiguration->TemporaryDirectoryCleanup)
      {
        TemporaryDirectoryCleanup();
      }

      WinConfiguration->CheckDefaultTranslation();

      if (!Params->Empty)
      {
        if (Params->FindSwitch(L"Defaults") && CheckSafe(Params))
        {
          UseDefaults = true;
        }

        if (Params->FindSwitch(UPLOAD_SWITCH, CommandParams))
        {
          ParamCommand = pcUpload;
          if (CommandParams->Count == 0)
          {
            throw Exception(NO_UPLOAD_LIST_ERROR);
          }
        }
        if (Params->FindSwitch(UPLOAD_IF_ANY_SWITCH, CommandParams))
        {
          if (CommandParams->Count > 0)
          {
            ParamCommand = pcUpload;
          }
        }
        else if (Params->FindSwitch(L"Synchronize", CommandParams, 2))
        {
          ParamCommand = pcFullSynchronize;
        }
        else if (Params->FindSwitch(L"KeepUpToDate", CommandParams, 2))
        {
          ParamCommand = pcSynchronize;
        }
        else if (Params->FindSwitch(L"Edit", CommandParams, 1) &&
                 (CommandParams->Count == 1))
        {
          ParamCommand = pcEdit;
        }
        else if (Params->FindSwitch(REFRESH_SWITCH, CommandParams, 1))
        {
          ParamCommand = pcRefresh;
        }
      }

      if (Params->ParamCount > 0)
      {
        AutoStartSession = Params->Param[1];
        Params->ParamsProcessed(1, 1);

        if ((ParamCommand == pcNone) &&
            (WinConfiguration->ExternalSessionInExistingInstance != OpenInNewWindow()) &&
            !Params->FindSwitch(NEWINSTANCE_SWICH) &&
            SendToAnotherInstance())
        {
          Configuration->Usage->Inc(L"SendToAnotherInstance");
          return 0;
        }
        UnicodeString CounterName;
        if (Params->FindSwitch(JUMPLIST_SWITCH))
        {
          CounterName = L"CommandLineJumpList";
        }
        else if (Params->FindSwitch(DESKTOP_SWITCH))
        {
          CounterName = L"CommandLineDesktop";
        }
        else if (Params->FindSwitch(SEND_TO_HOOK_SWITCH))
        {
          CounterName = L"CommandLineSendToHook";
        }
        else
        {
          CounterName = L"CommandLineSession2";
        }
        Configuration->Usage->Inc(CounterName);
      }
      else if (WinConfiguration->EmbeddedSessions && StoredSessions->Count)
      {
        AutoStartSession = StoredSessions->Sessions[0]->Name;
      }
      else
      {
        AutoStartSession = WinConfiguration->AutoStartSession;
      }

      if (ParamCommand == pcRefresh)
      {
        Refresh(AutoStartSession, (CommandParams->Count > 0 ? CommandParams->Strings[0] : UnicodeString()));
        return 0;
      }

      // from now flash message boxes on background
      SetOnForeground(false);

      bool CommandLineOperation = (ParamCommand != pcNone) || !DownloadFile.IsEmpty();
      bool NeedSession = CommandLineOperation;

      bool Retry;
      do
      {
        Retry = false;
        TObjectList * DataList = new TObjectList();
        try
        {
          GetLoginData(AutoStartSession, Params, DataList, DownloadFile, NeedSession, NULL);
          // GetLoginData now Aborts when session is needed and none is selected
          if (DebugAlwaysTrue(!NeedSession || (DataList->Count > 0)))
          {
            if (CheckSafe(Params))
            {
              UnicodeString LogFile;
              if (Params->FindSwitch(LOG_SWITCH, LogFile))
              {
                Configuration->TemporaryLogging(LogFile);
              }
              if (Params->FindSwitch(L"XmlLog", LogFile))
              {
                Configuration->TemporaryActionsLogging(LogFile);
              }
            }

            try
            {
              DebugAssert(!TerminalManager->ActiveTerminal);

              bool CanStart;
              if (DataList->Count > 0)
              {
                TerminalManager->ActiveTerminal =
                  TerminalManager->NewTerminals(DataList);
                CanStart = (TerminalManager->Count > 0);
              }
              else
              {
                DebugAssert(!NeedSession);
                CanStart = true;
              }

              if (!CanStart)
              {
                // do not prompt with login dialog, if connection of
                // auto-start session (typically from command line) failed
                if (AutoStartSession.IsEmpty())
                {
                  Retry = true;
                }
              }
              else
              {
                // from now on, we do not support runtime interface change
                CustomWinConfiguration->CanApplyInterfaceImmediately = false;
                TCustomScpExplorerForm * ScpExplorer = CreateScpExplorer();
                CustomWinConfiguration->AppliedInterface = CustomWinConfiguration->Interface;
                try
                {
                  // moved inside try .. __finally, because it can fail as well
                  TerminalManager->ScpExplorer = ScpExplorer;

                  if (CommandLineOperation)
                  {
                    Configuration->Usage->Inc(L"CommandLineOperation");
                  }

                  if (ParamCommand == pcUpload)
                  {
                    Upload(TerminalManager->ActiveTerminal, CommandParams, UseDefaults);
                  }
                  else if (ParamCommand == pcFullSynchronize)
                  {
                    FullSynchronize(TerminalManager->ActiveTerminal, ScpExplorer,
                      CommandParams, UseDefaults);
                  }
                  else if (ParamCommand == pcSynchronize)
                  {
                    Synchronize(TerminalManager->ActiveTerminal, ScpExplorer,
                      CommandParams, UseDefaults);
                  }
                  else if (ParamCommand == pcEdit)
                  {
                    Edit(ScpExplorer, CommandParams);
                  }
                  else if (!DownloadFile.IsEmpty())
                  {
                    Download(TerminalManager->ActiveTerminal, DownloadFile,
                      UseDefaults);
                  }

                  Application->Run();
                }
                __finally
                {
                  TerminalManager->ScpExplorer = NULL;
                  SAFE_DESTROY(ScpExplorer);
                }
              }
            }
            catch (Exception &E)
            {
              ShowExtendedException(&E);
            }
          }
        }
        __finally
        {
          delete DataList;
        }
      }
      while (Retry);
    }
  }
  __finally
  {
    delete NonVisualDataModule;
    NonVisualDataModule = NULL;
    ReleaseAnimationsModule();
    delete GlyphsModule;
    GlyphsModule = NULL;
    TTerminalManager::DestroyInstance();
    delete CommandParams;
  }

  return 0;
}
