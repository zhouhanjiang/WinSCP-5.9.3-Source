//---------------------------------------------------------------------------
#include <vcl.h>
#pragma hdrstop

#include <Consts.hpp>
#include <shlobj.h>
#include <stdio.h>
#define INITGUID
#include <propkey.h>
#include <powrprof.h>

#include <Common.h>
#include <TextsWin.h>
#include <TextsCore.h>
#include <HelpWin.h>
#include <Exceptions.h>
#include <CoreMain.h>
#include <RemoteFiles.h>
#include <PuttyTools.h>

#include "GUITools.h"
#include "VCLCommon.h"
#include "Setup.h"
#include "Tools.h"
#include <WinHelpViewer.hpp>
#include <PasTools.hpp>
#include <System.Win.ComObj.hpp>
#include <StrUtils.hpp>
#include <WinConfiguration.h>
#include <ProgParams.h>
//---------------------------------------------------------------------------
// WORKAROUND
// VCL includes wininet.h (even with NO_WIN32_LEAN_AND_MEAN)
// and it cannot be combined with winhttp.h as of current Windows SDK.
// This is hack to allow that.
// http://social.msdn.microsoft.com/Forums/windowsdesktop/en-US/8f468d9f-3f15-452c-803d-fc63ab3f684e/cannot-use-both-winineth-and-winhttph
#undef BOOLAPI
#undef SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
#undef SECURITY_FLAG_IGNORE_CERT_CN_INVALID

#define URL_COMPONENTS URL_COMPONENTS_ANOTHER
#define URL_COMPONENTSA URL_COMPONENTSA_ANOTHER
#define URL_COMPONENTSW URL_COMPONENTSW_ANOTHER

#define LPURL_COMPONENTS LPURL_COMPONENTS_ANOTHER
#define LPURL_COMPONENTSA LPURL_COMPONENTS_ANOTHER
#define LPURL_COMPONENTSW LPURL_COMPONENTS_ANOTHER

#define INTERNET_SCHEME INTERNET_SCHEME_ANOTHER
#define LPINTERNET_SCHEME LPINTERNET_SCHEME_ANOTHER

#define HTTP_VERSION_INFO HTTP_VERSION_INFO_ANOTHER
#define LPHTTP_VERSION_INFO LPHTTP_VERSION_INFO_ANOTHER

#include <winhttp.h>

#undef URL_COMPONENTS
#undef URL_COMPONENTSA
#undef URL_COMPONENTSW

#undef LPURL_COMPONENTS
#undef LPURL_COMPONENTSA
#undef LPURL_COMPONENTSW

#undef INTERNET_SCHEME
#undef LPINTERNET_SCHEME

#undef HTTP_VERSION_INFO
#undef LPHTTP_VERSION_INFO
//---------------------------------------------------------------------------
#pragma package(smart_init)
//---------------------------------------------------------------------------
TFontStyles __fastcall IntToFontStyles(int value)
{
  TFontStyles Result;
  for (int i = fsBold; i <= fsStrikeOut; i++)
  {
    if (value & 1)
    {
      Result << (TFontStyle)i;
    }
    value >>= 1;
  }
  return Result;
}
//---------------------------------------------------------------------------
int __fastcall FontStylesToInt(const TFontStyles value)
{
  int Result = 0;
  for (int i = fsStrikeOut; i >= fsBold; i--)
  {
    Result <<= 1;
    if (value.Contains((TFontStyle)i))
    {
      Result |= 1;
    }
  }
  return Result;
}
//---------------------------------------------------------------------------
bool __fastcall SameFont(TFont * Font1, TFont * Font2)
{
  // keep in sync with TFontConfiguration::operator !=
  return SameText(Font1->Name, Font2->Name) && (Font1->Size == Font2->Size) &&
      (Font1->Charset == Font2->Charset) && (Font1->Style == Font2->Style);
}
//---------------------------------------------------------------------------
TColor __fastcall GetWindowTextColor(TColor Color)
{
  return (Color == TColor(0)) ? clWindowText : Color;
}
//---------------------------------------------------------------------------
TColor __fastcall GetWindowColor(TColor Color)
{
  return (Color == TColor(0)) ? clWindow : Color;
}
//---------------------------------------------------------------------------
TColor __fastcall GetNonZeroColor(TColor Color)
{
  // 0,0,0 is "default color"
  if (Color == TColor(0))
  {
    // use near-black instead
    Color = TColor(RGB(1, 1, 1));
  }
  return Color;
}
//---------------------------------------------------------------------------
void __fastcall CenterFormOn(TForm * Form, TControl * CenterOn)
{
  TPoint ScreenPoint = CenterOn->ClientToScreen(TPoint(0, 0));
  Form->Left = ScreenPoint.x + (CenterOn->Width / 2) - (Form->Width / 2);
  Form->Top = ScreenPoint.y + (CenterOn->Height / 2) - (Form->Height / 2);
}
//---------------------------------------------------------------------------
UnicodeString __fastcall GetListViewStr(TListView * ListView)
{
  UnicodeString Result;
  for (int Index = 0; Index < ListView->Columns->Count; Index++)
  {
    AddToList(Result, IntToStr(ListView->Column[Index]->Width), L",");
  }
  // WORKAROUND
  // Adding an additional comma after the list,
  // to ensure that old versions that did not expect the pixels-per-inch part,
  // stop at the comma, otherwise they try to parse the
  // "last-column-width;pixels-per-inch" as integer and throw.
  // For the other instance of this hack, see TCustomListViewColProperties.GetParamsStr
  Result += L",;" + SavePixelsPerInch();
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall LoadListViewStr(TListView * ListView, UnicodeString ALayoutStr)
{
  UnicodeString LayoutStr = CutToChar(ALayoutStr, L';', true);
  int PixelsPerInch = LoadPixelsPerInch(CutToChar(ALayoutStr, L';', true));
  int Index = 0;
  while (!LayoutStr.IsEmpty() && (Index < ListView->Columns->Count))
  {
    int Width;
    if (TryStrToInt(CutToChar(LayoutStr, L',', true), Width))
    {
      ListView->Column[Index]->Width = LoadDimension(Width, PixelsPerInch);
    }
    Index++;
  }
}
//---------------------------------------------------------------------------
void __fastcall RestoreForm(UnicodeString Data, TForm * Form)
{
  DebugAssert(Form);
  if (!Data.IsEmpty())
  {
    Forms::TMonitor * Monitor = FormMonitor(Form);

    UnicodeString LeftStr = CutToChar(Data, L';', true);
    UnicodeString TopStr = CutToChar(Data, L';', true);
    UnicodeString RightStr = CutToChar(Data, L';', true);
    UnicodeString BottomStr = CutToChar(Data, L';', true);
    TWindowState State = (TWindowState)StrToIntDef(CutToChar(Data, L';', true), (int)wsNormal);
    int PixelsPerInch = LoadPixelsPerInch(CutToChar(Data, L';', true));

    TRect Bounds = Form->BoundsRect;
    int Left = StrToDimensionDef(LeftStr, PixelsPerInch, Bounds.Left);
    int Top = StrToDimensionDef(TopStr, PixelsPerInch, Bounds.Top);
    bool DefaultPos = (Left == -1) && (Top == -1);
    if (!DefaultPos)
    {
      Bounds.Left = Left;
      Bounds.Top = Top;
    }
    else
    {
      Bounds.Left = 0;
      Bounds.Top = 0;
    }
    Bounds.Right = StrToDimensionDef(RightStr, PixelsPerInch, Bounds.Right);
    Bounds.Bottom = StrToDimensionDef(BottomStr, PixelsPerInch, Bounds.Bottom);
    Form->WindowState = State;
    if (State == wsNormal)
    {
      // move to the target monitor
      OffsetRect(Bounds, Monitor->Left, Monitor->Top);

      // reduce window size to that of monitor size
      // (this does not cut window into monitor!)
      if (Bounds.Width() > Monitor->WorkareaRect.Width())
      {
        Bounds.Right -= (Bounds.Width() - Monitor->WorkareaRect.Width());
      }
      if (Bounds.Height() > Monitor->WorkareaRect.Height())
      {
        Bounds.Bottom -= (Bounds.Height() - Monitor->WorkareaRect.Height());
      }

      if (DefaultPos ||
          ((Bounds.Left < Monitor->Left) ||
           (Bounds.Left > Monitor->Left + Monitor->WorkareaRect.Width() - 20) ||
           (Bounds.Top < Monitor->Top) ||
           (Bounds.Top > Monitor->Top + Monitor->WorkareaRect.Height() - 20)))
      {
        if (Monitor->Primary)
        {
          if ((Application->MainForm == NULL) || (Application->MainForm == Form))
          {
            Form->Position = poDefaultPosOnly;
          }
          else
          {
            Form->Position = poOwnerFormCenter;
          }
          Form->Width = Bounds.Width();
          Form->Height = Bounds.Height();
        }
        else
        {
          // when positioning on non-primary monitor, we need
          // to handle that ourselves, so place window to center
          Form->SetBounds(Monitor->Left + ((Monitor->Width - Bounds.Width()) / 2),
            Monitor->Top + ((Monitor->Height - Bounds.Height()) / 2),
            Bounds.Width(), Bounds.Height());
          Form->Position = poDesigned;
        }
      }
      else
      {
        Form->Position = poDesigned;
        Form->BoundsRect = Bounds;
      }
    }
    else if (State == wsMaximized)
    {
      Form->Position = poDesigned;

      Bounds = Form->BoundsRect;
      OffsetRect(Bounds, Monitor->Left, Monitor->Top);
      Form->BoundsRect = Bounds;
    }
  }
  else if (Form->Position == poDesigned)
  {
    Form->Position = poDefaultPosOnly;
  }
}
//---------------------------------------------------------------------------
UnicodeString __fastcall StoreForm(TCustomForm * Form)
{
  DebugAssert(Form);
  TRect Bounds = Form->BoundsRect;
  OffsetRect(Bounds, -Form->Monitor->Left, -Form->Monitor->Top);
  return FORMAT(L"%d;%d;%d;%d;%d;%s", (SaveDimension(Bounds.Left), SaveDimension(Bounds.Top),
    SaveDimension(Bounds.Right), SaveDimension(Bounds.Bottom),
    // we do not want WinSCP to start minimized next time (we cannot handle that anyway).
    // note that WindowState is wsNormal when window in minimized for some reason.
    // actually it is wsMinimized only when minimized by MSVDM
    (int)(Form->WindowState == wsMinimized ? wsNormal : Form->WindowState),
    SavePixelsPerInch()));
}
//---------------------------------------------------------------------------
void __fastcall RestoreFormSize(UnicodeString Data, TForm * Form)
{
  // This has to be called only after DoFormWindowProc(CM_SHOWINGCHANGED).
  // See comment in ResizeForm.
  UnicodeString WidthStr = CutToChar(Data, L',', true);
  UnicodeString HeightStr = CutToChar(Data, L',', true);
  int PixelsPerInch = LoadPixelsPerInch(CutToChar(Data, L',', true));

  int Width = StrToDimensionDef(WidthStr, PixelsPerInch, Form->Width);
  int Height = StrToDimensionDef(HeightStr, PixelsPerInch, Form->Height);
  ResizeForm(Form, Width, Height);
}
//---------------------------------------------------------------------------
UnicodeString __fastcall StoreFormSize(TForm * Form)
{
  return FORMAT(L"%d,%d,%s", (Form->Width, Form->Height, SavePixelsPerInch()));
}
//---------------------------------------------------------------------------
bool __fastcall ExecuteShellAndWait(const UnicodeString Path, const UnicodeString Params)
{
  return ExecuteShellAndWait(Application->Handle, Path, Params,
    &Application->ProcessMessages);
}
//---------------------------------------------------------------------------
bool __fastcall ExecuteShellAndWait(const UnicodeString Command)
{
  return ExecuteShellAndWait(Application->Handle, Command,
    &Application->ProcessMessages);
}
//---------------------------------------------------------------------------
bool __fastcall IsKeyPressed(int VirtualKey)
{
  return FLAGSET(GetAsyncKeyState(VirtualKey), 0x8000);
}
//---------------------------------------------------------------------------
bool __fastcall UseAlternativeFunction()
{
  return IsKeyPressed(VK_SHIFT);
}
//---------------------------------------------------------------------------
bool __fastcall OpenInNewWindow()
{
  return UseAlternativeFunction();
}
//---------------------------------------------------------------------------
void __fastcall ExecuteNewInstance(const UnicodeString & Param)
{
  UnicodeString Arg = Param;
  if (!Arg.IsEmpty())
  {
    Arg = FORMAT(L"\"%s\" %s", (Arg, TProgramParams::FormatSwitch(NEWINSTANCE_SWICH)));
  }

  if (!ExecuteShell(Application->ExeName, Arg))
  {
    throw Exception(FMTLOAD(EXECUTE_APP_ERROR, (Application->ExeName)));
  }
}
//---------------------------------------------------------------------------
IShellLink * __fastcall CreateDesktopShortCut(const UnicodeString & Name,
  const UnicodeString &File, const UnicodeString & Params, const UnicodeString & Description,
  int SpecialFolder, int IconIndex, bool Return)
{
  IShellLink* pLink = NULL;

  if (SpecialFolder < 0)
  {
    SpecialFolder = CSIDL_DESKTOPDIRECTORY;
  }

  try
  {
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
         IID_IShellLink, (void **) &pLink)))
    {
      try
      {
        pLink->SetPath(File.c_str());
        pLink->SetDescription(Description.c_str());
        pLink->SetArguments(Params.c_str());
        pLink->SetShowCmd(SW_SHOW);
        // Explicitly setting icon file,
        // without this icons are not shown at least in Windows 7 jumplist
        pLink->SetIconLocation(File.c_str(), IconIndex);

        IPersistFile* pPersistFile;
        if (!Return &&
            SUCCEEDED(pLink->QueryInterface(IID_IPersistFile, (void **)&pPersistFile)))
        {
          try
          {
            LPMALLOC      ShellMalloc;
            LPITEMIDLIST  DesktopPidl;
            wchar_t DesktopDir[MAX_PATH];

            OleCheck(SHGetMalloc(&ShellMalloc));

            try
            {
              OleCheck(SHGetSpecialFolderLocation(NULL, SpecialFolder, &DesktopPidl));

              OleCheck(SHGetPathFromIDList(DesktopPidl, DesktopDir));
            }
            __finally
            {
              ShellMalloc->Free(DesktopPidl);
              ShellMalloc->Release();
            }

            WideString strShortCutLocation(DesktopDir);
            // Name can contain even path (e.g. to create quick launch icon)
            strShortCutLocation += UnicodeString(L"\\") + Name + L".lnk";
            OleCheck(pPersistFile->Save(strShortCutLocation.c_bstr(), TRUE));
          }
          __finally
          {
            pPersistFile->Release();
          }
        }

        // this is necessary for Windows 7 taskbar jump list links
        IPropertyStore * PropertyStore;
        if (SUCCEEDED(pLink->QueryInterface(IID_IPropertyStore, (void**)&PropertyStore)))
        {
          PROPVARIANT Prop;
          Prop.vt = VT_LPWSTR;
          Prop.pwszVal = Name.c_str();
          PropertyStore->SetValue(PKEY_Title, Prop);
          PropertyStore->Commit();
          PropertyStore->Release();
        }
      }
      catch(...)
      {
        pLink->Release();
        throw;
      }

      if (!Return)
      {
        pLink->Release();
        pLink = NULL;
      }
    }
  }
  catch(Exception & E)
  {
    throw ExtException(&E, LoadStr(CREATE_SHORTCUT_ERROR));
  }

  return pLink;
}
//---------------------------------------------------------------------------
IShellLink * __fastcall CreateDesktopSessionShortCut(
  const UnicodeString & SessionName, UnicodeString Name,
  const UnicodeString & AdditionalParams, int SpecialFolder, int IconIndex,
  bool Return)
{
  bool DefaultsOnly;
  UnicodeString InfoTip;

  bool IsFolder = StoredSessions->IsFolder(SessionName);
  bool IsWorkspace = StoredSessions->IsWorkspace(SessionName);

  if (IsFolder || IsWorkspace)
  {
    InfoTip = FMTLOAD(
      (IsFolder ? SHORTCUT_INFO_TIP_FOLDER : SHORTCUT_INFO_TIP_WORKSPACE),
      (SessionName));

    if (Name.IsEmpty())
    {
      // no slashes in filename
      Name = UnixExtractFileName(SessionName);
    }
  }
  else
  {
    // this should not be done for workspaces and folders
    TSessionData * SessionData =
      StoredSessions->ParseUrl(EncodeUrlString(SessionName), NULL, DefaultsOnly);
    InfoTip =
      FMTLOAD(SHORTCUT_INFO_TIP, (SessionName, SessionData->InfoTip));
    if (Name.IsEmpty())
    {
      // no slashes in filename
      Name = SessionData->LocalName;
    }
    delete SessionData;
  }

  return
    CreateDesktopShortCut(ValidLocalFileName(Name), Application->ExeName,
      FORMAT(L"\"%s\"%s%s", (EncodeUrlString(SessionName), (AdditionalParams.IsEmpty() ? L"" : L" "), AdditionalParams)),
      InfoTip, SpecialFolder, IconIndex, Return);
}
//---------------------------------------------------------------------------
template<class TEditControl>
void __fastcall ValidateMaskEditT(const UnicodeString & Mask, TEditControl * Edit, int ForceDirectoryMasks)
{
  DebugAssert(Edit != NULL);
  TFileMasks Masks(ForceDirectoryMasks);
  try
  {
    Masks = Mask;
  }
  catch(EFileMasksException & E)
  {
    ShowExtendedException(&E);
    Edit->SetFocus();
    // This does not work for TEdit and TMemo (descendants of TCustomEdit) anymore,
    // as it re-selects whole text on exception in TCustomEdit.CMExit
    Edit->SelStart = E.ErrorStart - 1;
    Edit->SelLength = E.ErrorLen;
    Abort();
  }
}
//---------------------------------------------------------------------------
void __fastcall ValidateMaskEdit(TComboBox * Edit)
{
  ValidateMaskEditT(Edit->Text, Edit, -1);
}
//---------------------------------------------------------------------------
void __fastcall ValidateMaskEdit(TEdit * Edit)
{
  ValidateMaskEditT(Edit->Text, Edit, -1);
}
//---------------------------------------------------------------------------
void __fastcall ValidateMaskEdit(TMemo * Edit, bool Directory)
{
  UnicodeString Mask = TFileMasks::ComposeMaskStr(GetUnwrappedMemoLines(Edit), Directory);
  ValidateMaskEditT(Mask, Edit, Directory ? 1 : 0);
}
//---------------------------------------------------------------------------
TStrings * __fastcall GetUnwrappedMemoLines(TMemo * Memo)
{
  // This removes soft linebreakes when text in memo wraps
  // (Memo->Lines includes soft linebreaks, while Memo->Text does not)
  return TextToStringList(Memo->Text);
}
//---------------------------------------------------------------------------
void __fastcall ExitActiveControl(TForm * Form)
{
  if (Form->ActiveControl != NULL)
  {
    TNotifyEvent OnExit = ((TEdit*)Form->ActiveControl)->OnExit;
    if (OnExit != NULL)
    {
      OnExit(Form->ActiveControl);
    }
  }
}
//---------------------------------------------------------------------------
bool __fastcall IsWinSCPUrl(const UnicodeString & Url)
{
  UnicodeString HomePageUrl = LoadStr(HOMEPAGE_URL);
  UnicodeString HttpHomePageUrl = ChangeUrlProtocol(HomePageUrl, HttpProtocol);
  DebugAssert(HomePageUrl != HttpHomePageUrl);
  return
    StartsText(HomePageUrl, Url) ||
    StartsText(HttpHomePageUrl, Url);
}
//---------------------------------------------------------------------------
UnicodeString __fastcall SecureUrl(const UnicodeString & Url)
{
  UnicodeString Result = Url;
  if (IsWinSCPUrl(Url) && IsHttpUrl(Url))
  {
    Result = ChangeUrlProtocol(Result, HttpsProtocol);
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall OpenBrowser(UnicodeString URL)
{
  if (IsWinSCPUrl(URL))
  {
    DebugAssert(!IsHttpUrl(URL));
    URL = CampaignUrl(URL);
  }
  ShellExecute(Application->Handle, L"open", URL.c_str(), NULL, NULL, SW_SHOWNORMAL);
}
//---------------------------------------------------------------------------
void __fastcall ShowHelp(const UnicodeString & AHelpKeyword)
{
  // see also AppendUrlParams
  UnicodeString HelpKeyword = AHelpKeyword;
  const wchar_t FragmentSeparator = L'#';
  UnicodeString HelpPath = CutToChar(HelpKeyword, FragmentSeparator, false);
  UnicodeString HelpUrl = FMTLOAD(DOCUMENTATION_KEYWORD_URL2, (HelpPath, Configuration->ProductVersion, GUIConfiguration->LocaleHex));
  AddToList(HelpUrl, HelpKeyword, FragmentSeparator);
  OpenBrowser(HelpUrl);
}
//---------------------------------------------------------------------------
bool __fastcall IsFormatInClipboard(unsigned int Format)
{
  bool Result = OpenClipboard(0);
  if (Result)
  {
    Result = IsClipboardFormatAvailable(Format);
    CloseClipboard();
  }
  return Result;
}
//---------------------------------------------------------------------------
HANDLE __fastcall OpenTextFromClipboard(const wchar_t *& Text)
{
  HANDLE Result = NULL;
  if (OpenClipboard(0))
  {
    // Check also for CF_TEXT?
    Result = GetClipboardData(CF_UNICODETEXT);
    if (Result != NULL)
    {
      Text = static_cast<const wchar_t*>(GlobalLock(Result));
    }
    else
    {
      CloseClipboard();
    }
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall CloseTextFromClipboard(HANDLE Handle)
{
  if (Handle != NULL)
  {
    GlobalUnlock(Handle);
  }
  CloseClipboard();
}
//---------------------------------------------------------------------------
bool __fastcall TextFromClipboard(UnicodeString & Text, bool Trim)
{
  const wchar_t * AText = NULL;
  HANDLE Handle = OpenTextFromClipboard(AText);
  bool Result = (Handle != NULL);
  if (Result)
  {
    Text = AText;
    if (Trim)
    {
      Text = Text.Trim();
    }
    CloseTextFromClipboard(Handle);
  }
  return Result;
}
//---------------------------------------------------------------------------
bool __fastcall NonEmptyTextFromClipboard(UnicodeString & Text)
{
  return
    TextFromClipboard(Text, true) &&
    !Text.IsEmpty();
}
//---------------------------------------------------------------------------
static bool __fastcall GetResource(
  const UnicodeString ResName, void *& Content, unsigned long & Size)
{
  HRSRC Resource = FindResourceEx(HInstance, RT_RCDATA, ResName.c_str(),
    MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
  bool Result = (Resource != NULL);
  if (Result)
  {
    Size = SizeofResource(HInstance, Resource);
    if (!Size)
    {
      throw Exception(FORMAT(L"Cannot get size of resource %s", (ResName)));
    }

    Content = LoadResource(HInstance, Resource);
    if (!Content)
    {
      throw Exception(FORMAT(L"Cannot read resource %s", (ResName)));
    }

    Content = LockResource(Content);
    if (!Content)
    {
      throw Exception(FORMAT(L"Cannot lock resource %s", (ResName)));
    }
  }

  return Result;
}
//---------------------------------------------------------------------------
bool __fastcall DumpResourceToFile(const UnicodeString ResName,
  const UnicodeString FileName)
{
  void * Content;
  unsigned long Size;
  bool Result = GetResource(ResName, Content, Size);

  if (Result)
  {
    FILE * f = _wfopen(ApiPath(FileName).c_str(), L"wb");
    if (!f)
    {
      throw Exception(FORMAT(L"Cannot create file %s", (FileName)));
    }
    if (fwrite(Content, 1, Size, f) != Size)
    {
      throw Exception(FORMAT(L"Cannot write to file %s", (FileName)));
    }
    fclose(f);
  }

  return Result;
}
//---------------------------------------------------------------------------
UnicodeString __fastcall ReadResource(const UnicodeString ResName)
{
  void * Content;
  unsigned long Size;
  UnicodeString Result;

  if (GetResource(ResName, Content, Size))
  {
    Result = UnicodeString(UTF8String(static_cast<char*>(Content), Size));
  }

  return Result;
}
//---------------------------------------------------------------------------
template <class T>
void __fastcall BrowseForExecutableT(T * Control, UnicodeString Title,
  UnicodeString Filter, bool FileNameCommand, bool Escape)
{
  UnicodeString Executable, Program, Params, Dir;
  Executable = Control->Text;
  if (FileNameCommand)
  {
    ReformatFileNameCommand(Executable);
  }
  SplitCommand(Executable, Program, Params, Dir);

  TOpenDialog * FileDialog = new TOpenDialog(Application);
  try
  {
    if (Escape)
    {
      Program = ReplaceStr(Program, L"\\\\", L"\\");
    }
    UnicodeString ExpandedProgram = ExpandEnvironmentVariables(Program);
    FileDialog->FileName = ExpandedProgram;
    UnicodeString InitialDir = ExtractFilePath(ExpandedProgram);
    if (!InitialDir.IsEmpty())
    {
      FileDialog->InitialDir = InitialDir;
    }
    FileDialog->Filter = Filter;
    FileDialog->Title = Title;

    if (FileDialog->Execute())
    {
      TNotifyEvent PrevOnChange = Control->OnChange;
      Control->OnChange = NULL;
      try
      {
        // preserve unexpanded file, if the destination has not changed actually
        if (!CompareFileName(ExpandedProgram, FileDialog->FileName))
        {
          Program = FileDialog->FileName;
          if (Escape)
          {
            Program = ReplaceStr(Program, L"\\", L"\\\\");
          }
        }
        Control->Text = FormatCommand(Program, Params);
      }
      __finally
      {
        Control->OnChange = PrevOnChange;
      }

      if (Control->OnExit != NULL)
      {
        Control->OnExit(Control);
      }
    }
  }
  __finally
  {
    delete FileDialog;
  }
}
//---------------------------------------------------------------------------
void __fastcall BrowseForExecutable(TEdit * Control, UnicodeString Title,
  UnicodeString Filter, bool FileNameCommand, bool Escape)
{
  BrowseForExecutableT(Control, Title, Filter, FileNameCommand, Escape);
}
//---------------------------------------------------------------------------
void __fastcall BrowseForExecutable(TComboBox * Control, UnicodeString Title,
  UnicodeString Filter, bool FileNameCommand, bool Escape)
{
  BrowseForExecutableT(Control, Title, Filter, FileNameCommand, Escape);
}
//---------------------------------------------------------------------------
bool __fastcall FontDialog(TFont * Font)
{
  bool Result;
  TFontDialog * Dialog = new TFontDialog(Application);
  try
  {
    Dialog->Device = fdScreen;
    Dialog->Options = TFontDialogOptions() << fdForceFontExist;
    Dialog->Font = Font;
    Result = Dialog->Execute();
    if (Result)
    {
      Font->Assign(Dialog->Font);
    }
  }
  __finally
  {
    delete Dialog;
  }
  return Result;
}
//---------------------------------------------------------------------------
bool __fastcall SaveDialog(UnicodeString Title, UnicodeString Filter,
  UnicodeString DefaultExt, UnicodeString & FileName)
{
  bool Result;
  #if 0
  TFileSaveDialog * Dialog = new TFileSaveDialog(Application);
  try
  {
    Dialog->Title = Title;
    FilterToFileTypes(Filter, Dialog->FileTypes);
    Dialog->DefaultExtension = DefaultExt;
    Dialog->FileName = FileName;
    UnicodeString DefaultFolder = ExtractFilePath(FileName);
    if (!DefaultFolder.IsEmpty())
    {
      Dialog->DefaultFolder = DefaultFolder;
    }
    Dialog->Options = Dialog->Options << fdoOverWritePrompt << fdoForceFileSystem <<
      fdoPathMustExist << fdoNoReadOnlyReturn;
    Result = Dialog->Execute();
    if (Result)
    {
      FileName = Dialog->FileName;
    }
  }
  __finally
  {
    delete Dialog;
  }
  #else
  TSaveDialog * Dialog = new TSaveDialog(Application);
  try
  {
    Dialog->Title = Title;
    Dialog->Filter = Filter;
    Dialog->DefaultExt = DefaultExt;
    Dialog->FileName = FileName;
    UnicodeString InitialDir = ExtractFilePath(FileName);
    if (!InitialDir.IsEmpty())
    {
      Dialog->InitialDir = InitialDir;
    }
    Dialog->Options = Dialog->Options << ofOverwritePrompt << ofPathMustExist <<
      ofNoReadOnlyReturn;
    Result = Dialog->Execute();
    if (Result)
    {
      FileName = Dialog->FileName;
    }
  }
  __finally
  {
    delete Dialog;
  }
  #endif
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall CopyToClipboard(UnicodeString Text)
{
  HANDLE Data;
  void * DataPtr;

  if (OpenClipboard(0))
  {
    try
    {
      size_t Size = (Text.Length() + 1) * sizeof(wchar_t);
      Data = GlobalAlloc(GMEM_MOVEABLE + GMEM_DDESHARE, Size);
      try
      {
        DataPtr = GlobalLock(Data);
        try
        {
          memcpy(DataPtr, Text.c_str(), Size);
          EmptyClipboard();
          SetClipboardData(CF_UNICODETEXT, Data);
        }
        __finally
        {
          GlobalUnlock(Data);
        }
      }
      catch(...)
      {
        GlobalFree(Data);
        throw;
      }
    }
    __finally
    {
      CloseClipboard();
    }
  }
  else
  {
    throw Exception(Vcl_Consts_SCannotOpenClipboard);
  }
}
//---------------------------------------------------------------------------
void __fastcall CopyToClipboard(TStrings * Strings)
{
  if (Strings->Count > 0)
  {
    CopyToClipboard(StringsToText(Strings));
  }
}
//---------------------------------------------------------------------------
bool __fastcall IsWin64()
{
  static int Result = -1;
  if (Result < 0)
  {
    Result = 0;
    BOOL Wow64Process = FALSE;
    if (IsWow64Process(GetCurrentProcess(), &Wow64Process))
    {
      if (Wow64Process)
      {
        Result = 1;
      }
    }
  }

  return (Result > 0);
}
//---------------------------------------------------------------------------
static void __fastcall AcquireShutDownPrivileges()
{
  HANDLE Token;
  // Get a token for this process.
  Win32Check(OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &Token));

  TOKEN_PRIVILEGES Priv;
  ZeroMemory(&Priv, sizeof(Priv));
  // Get the LUID for the shutdown privilege.
  // For hibernate/suspend, you need the same:
  // http://stackoverflow.com/questions/959589/is-there-any-win32-api-to-trigger-the-hibernate-or-suspend-mode-in-windows
  Win32Check(LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &Priv.Privileges[0].Luid));

  Priv.PrivilegeCount = 1;  // one privilege to set
  Priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

  // Get the shutdown privilege for this process.
  Win32Check(AdjustTokenPrivileges(Token, FALSE, &Priv, 0, (PTOKEN_PRIVILEGES)NULL, 0));
}
//---------------------------------------------------------------------------
void __fastcall ShutDownWindows()
{
  AcquireShutDownPrivileges();

  // Shut down the system and force all applications to close.
  Win32Check(ExitWindowsEx(EWX_SHUTDOWN | EWX_POWEROFF,
    SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED));
}
//---------------------------------------------------------------------------
void __fastcall SuspendWindows()
{
  AcquireShutDownPrivileges();

  // http://msdn.microsoft.com/en-us/library/windows/desktop/aa373201.aspx
  Win32Check(SetSuspendState(false, false, false));
}
//---------------------------------------------------------------------------
void __fastcall EditSelectBaseName(HWND Edit)
{
  UnicodeString Text;
  Text.SetLength(GetWindowTextLength(Edit) + 1);
  GetWindowText(Edit, Text.c_str(), Text.Length());

  int P = Text.LastDelimiter(L".");
  if (P > 0)
  {
    // SendMessage does not work, if edit control is not fully
    // initialized yet
    PostMessage(Edit, EM_SETSEL, 0, P - 1);
  }
}
//---------------------------------------------------------------------------
static void __fastcall ConvertKey(UnicodeString & FileName, TKeyType Type)
{
  UnicodeString Passphrase;

  UnicodeString Comment;
  if (IsKeyEncrypted(Type, FileName, Comment))
  {
    if (!InputDialog(
          LoadStr(PASSPHRASE_TITLE),
          FORMAT(LoadStr(PROMPT_KEY_PASSPHRASE), (Comment)),
          Passphrase, HELP_NONE, NULL, false, NULL, false))
    {
      Abort();
    }
  }

  TPrivateKey * PrivateKey = LoadKey(Type, FileName, Passphrase);

  try
  {
    FileName = ChangeFileExt(FileName, ".ppk");

    if (!SaveDialog(LoadStr(CONVERTKEY_SAVE_TITLE), LoadStr(CONVERTKEY_SAVE_FILTER), L"ppk", FileName))
    {
      Abort();
    }

    SaveKey(ktSSH2, FileName, Passphrase, PrivateKey);

    MessageDialog(MainInstructions(FMTLOAD(CONVERTKEY_SAVED, (FileName))), qtInformation, qaOK);
  }
  __finally
  {
    FreeKey(PrivateKey);
  }
}
//---------------------------------------------------------------------------
static void __fastcall DoVerifyKey(
  UnicodeString & FileName, TSshProt SshProt, bool Convert)
{
  if (!FileName.Trim().IsEmpty())
  {
    FileName = ExpandEnvironmentVariables(FileName);
    TKeyType Type = KeyType(FileName);
    // reason _wfopen failed
    int Error = errno;
    UnicodeString Message;
    UnicodeString HelpKeyword = HELP_LOGIN_KEY_TYPE;
    UnicodeString PuttygenPath;
    std::unique_ptr<TStrings> MoreMessages;
    switch (Type)
    {
      case ktOpenSSHPEM:
      case ktOpenSSHNew:
      case ktSSHCom:
        {
          UnicodeString TypeName = ((Type == ktOpenSSHPEM) || (Type == ktOpenSSHNew)) ? L"OpenSSH" : L"ssh.com";
          Message = FMTLOAD(KEY_TYPE_UNSUPPORTED2, (FileName, TypeName));

          if (Convert)
          {
            Configuration->Usage->Inc(L"PrivateKeyConvertSuggestionsNative");
            UnicodeString ConvertMessage = FMTLOAD(KEY_TYPE_CONVERT3, (TypeName, RemoveMainInstructionsTag(Message)));
            Message = UnicodeString();
            if (MoreMessageDialog(ConvertMessage, NULL, qtConfirmation, qaOK | qaCancel, HelpKeyword) == qaOK)
            {
              ConvertKey(FileName, Type);
              Configuration->Usage->Inc(L"PrivateKeyConverted");
            }
            else
            {
              Abort();
            }
          }
          else
          {
            HelpKeyword = HELP_KEY_TYPE_UNSUPPORTED;
          }
        }
        break;

      case ktSSH1:
      case ktSSH2:
        if ((Type == ktSSH1) != (SshProt == ssh1only))
        {
          Message =
            MainInstructions(
              FMTLOAD(KEY_TYPE_DIFFERENT_SSH,
                (FileName, (Type == ktSSH1 ? L"SSH-1" : L"PuTTY SSH-2"))));
        }
        break;

      case ktSSH1Public:
      case ktSSH2PublicRFC4716:
      case ktSSH2PublicOpenSSH:
        // noop
        // Do not even bother checking SSH protocol version
        break;

      case ktUnopenable:
        Message = MainInstructions(FMTLOAD(KEY_TYPE_UNOPENABLE, (FileName)));
        if (Error != ERROR_SUCCESS)
        {
          MoreMessages.reset(TextToStringList(SysErrorMessageForError(Error)));
        }
        break;

      default:
        DebugFail();
        // fallthru
      case ktUnknown:
        Message = MainInstructions(FMTLOAD(KEY_TYPE_UNKNOWN2, (FileName)));
        break;
    }

    if (!Message.IsEmpty())
    {
      Configuration->Usage->Inc(L"PrivateKeySelectErrors");
      if (MoreMessageDialog(Message, MoreMessages.get(), qtWarning, qaIgnore | qaAbort,
           HelpKeyword) == qaAbort)
      {
        Abort();
      }
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall VerifyAndConvertKey(UnicodeString & FileName, TSshProt SshProt)
{
  DoVerifyKey(FileName, SshProt, true);
}
//---------------------------------------------------------------------------
void __fastcall VerifyKey(UnicodeString FileName, TSshProt SshProt)
{
  DoVerifyKey(FileName, SshProt, false);
}
//---------------------------------------------------------------------------
void __fastcall VerifyCertificate(const UnicodeString & FileName)
{
  if (!FileName.Trim().IsEmpty())
  {
    try
    {
      CheckCertificate(FileName);
    }
    catch (Exception & E)
    {
      if (ExceptionMessageDialog(&E, qtWarning, L"", qaIgnore | qaAbort) == qaAbort)
      {
        Abort();
      }
    }
  }
}
//---------------------------------------------------------------------------
bool __fastcall DetectSystemExternalEditor(
  bool AllowDefaultEditor,
  UnicodeString & Executable, UnicodeString & ExecutableDescription,
  UnicodeString & UsageState, bool & TryNextTime)
{
  bool Result = false;
  UnicodeString TempName = ExcludeTrailingBackslash(WinConfiguration->TemporaryDir()) + L".txt";
  if (FileExists(ApiPath(TempName)))
  {
    TryNextTime = true;
    UsageState = "F";
  }
  else
  {
    unsigned int File = FileCreate(ApiPath(TempName));
    if (File == (unsigned int)INVALID_HANDLE_VALUE)
    {
      TryNextTime = true;
      UsageState = "F";
    }
    else
    {
      FileClose(File);

      try
      {
        wchar_t ExecutableBuf[MAX_PATH];
        if (!SUCCEEDED(FindExecutable(TempName.c_str(), NULL, ExecutableBuf)))
        {
          UsageState = "N";
        }
        else
        {
          Executable = ExecutableBuf;
          if (Executable.IsEmpty() ||
              !FileExists(ApiPath(Executable)))
          {
            UsageState = "N";
          }
          else
          {
            UnicodeString ExecutableName = ExtractFileName(Executable);
            if (!AllowDefaultEditor &&
                SameText(ExecutableName, TEditorPreferences::GetDefaultExternalEditor()))
            {
              UsageState = "P";
              Executable = L"";
            }
            else if (SameText(ExecutableName, "openwith.exe"))
            {
              UsageState = "W";
              Executable = L"";
            }
            else
            {
              try
              {
                ExecutableDescription = Configuration->GetFileDescription(Executable);
              }
              catch(...)
              {
              }

              if (ExecutableDescription.IsEmpty())
              {
                ExecutableDescription = ExecutableName;
              }

              Result = true;
            }
          }
        }
      }
      __finally
      {
        DeleteFile(ApiPath(TempName));
      }
    }
  }
  return Result;
}
//---------------------------------------------------------------------------
// Code from http://gentoo.osuosl.org/distfiles/cl331.zip/io/
//---------------------------------------------------------------------------
// this was moved to global scope in past in some attempt to fix crashes,
// not sure it really helped
WINHTTP_CURRENT_USER_IE_PROXY_CONFIG IEProxyInfo;
//---------------------------------------------------------------------------
static bool __fastcall GetProxyUrlFromIE(UnicodeString & Proxy)
{
  bool Result = false;
  memset(&IEProxyInfo, 0, sizeof(IEProxyInfo));
  if (WinHttpGetIEProxyConfigForCurrentUser(&IEProxyInfo))
  {
    if (IEProxyInfo.lpszProxy != NULL)
    {
      UnicodeString IEProxy = IEProxyInfo.lpszProxy;
      Proxy = L"";
      while (Proxy.IsEmpty() && !IEProxy.IsEmpty())
      {
        UnicodeString Str = CutToChar(IEProxy, L';', true);
        if (Str.Pos(L"=") == 0)
        {
          Proxy = Str;
        }
        else
        {
          UnicodeString Protocol = CutToChar(Str, L'=', true);
          if (SameText(Protocol, L"http"))
          {
            Proxy = Str;
          }
        }
      }

      GlobalFree(IEProxyInfo.lpszProxy);
      Result = true;
    }
    if (IEProxyInfo.lpszAutoConfigUrl != NULL)
    {
      GlobalFree(IEProxyInfo.lpszAutoConfigUrl);
    }
    if (IEProxyInfo.lpszProxyBypass != NULL)
    {
      GlobalFree(IEProxyInfo.lpszProxyBypass);
    }
  }
  return Result;
}
//---------------------------------------------------------------------------
bool __fastcall AutodetectProxy(UnicodeString & HostName, int & PortNumber)
{
  bool Result = false;

  /* First we try for proxy info direct from the registry if
     it's available. */
  UnicodeString Proxy;
  WINHTTP_PROXY_INFO ProxyInfo;
  memset(&ProxyInfo, 0, sizeof(ProxyInfo));
  if (WinHttpGetDefaultProxyConfiguration(&ProxyInfo))
  {
    if (ProxyInfo.lpszProxy != NULL)
    {
      Proxy = ProxyInfo.lpszProxy;
      GlobalFree(ProxyInfo.lpszProxy);
      Result = true;
    }
    if (ProxyInfo.lpszProxyBypass != NULL)
    {
      GlobalFree(ProxyInfo.lpszProxyBypass);
    }
  }

  /* The next fallback is to get the proxy info from MSIE.  This is also
     usually much quicker than WinHttpGetProxyForUrl(), although sometimes
     it seems to fall back to that, based on the longish delay involved.
     Another issue with this is that it won't work in a service process
     that isn't impersonating an interactive user (since there isn't a
     current user), but in that case we just fall back to
     WinHttpGetProxyForUrl() */
  if (!Result)
  {
    Result = GetProxyUrlFromIE(Proxy);
  }

  if (Result)
  {
    if (Proxy.Trim().IsEmpty())
    {
      Result = false;
    }
    else
    {
      HostName = CutToChar(Proxy, L':', true);
      PortNumber = StrToIntDef(Proxy, ProxyPortNumber);
    }
  }

  // We can also use WinHttpGetProxyForUrl, but it is lengthy
  // See the source address of the code for example

  return Result;
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
class TWinHelpTester : public TInterfacedObject, public IWinHelpTester
{
public:
  virtual bool __fastcall CanShowALink(const UnicodeString ALink, const UnicodeString FileName);
  virtual bool __fastcall CanShowTopic(const UnicodeString Topic, const UnicodeString FileName);
  virtual bool __fastcall CanShowContext(const int Context, const UnicodeString FileName);
  virtual TStringList * __fastcall GetHelpStrings(const UnicodeString ALink);
  virtual UnicodeString __fastcall GetHelpPath();
  virtual UnicodeString __fastcall GetDefaultHelpFile();

  IUNKNOWN
};
//---------------------------------------------------------------------------
class TCustomHelpSelector : public TInterfacedObject, public IHelpSelector
{
public:
  __fastcall TCustomHelpSelector(const UnicodeString & Name);

  virtual int __fastcall SelectKeyword(TStrings * Keywords);
  virtual int __fastcall TableOfContents(TStrings * Contents);

  IUNKNOWN

private:
  UnicodeString FName;
};
//---------------------------------------------------------------------------
void __fastcall AssignHelpSelector(IHelpSelector * HelpSelector)
{
  _di_IHelpSystem HelpSystem;
  if (GetHelpSystem(HelpSystem))
  {
    HelpSystem->AssignHelpSelector(HelpSelector);
  }
}
//---------------------------------------------------------------------------
void __fastcall InitializeCustomHelp(ICustomHelpViewer * HelpViewer)
{
  _di_IHelpManager HelpManager;
  RegisterViewer(HelpViewer, HelpManager);

  // Register dummy tester that disables win help
  WinHelpTester = new TWinHelpTester();

  AssignHelpSelector(new TCustomHelpSelector(HelpViewer->GetViewerName()));
}
//---------------------------------------------------------------------------
void __fastcall FinalizeCustomHelp()
{
  AssignHelpSelector(NULL);
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
bool __fastcall TWinHelpTester::CanShowALink(const UnicodeString ALink,
  const UnicodeString FileName)
{
  return !Application->HelpFile.IsEmpty();
}
//---------------------------------------------------------------------------
bool __fastcall TWinHelpTester::CanShowTopic(const UnicodeString Topic,
  const UnicodeString FileName)
{
  DebugFail();
  return !Application->HelpFile.IsEmpty();
}
//---------------------------------------------------------------------------
bool __fastcall TWinHelpTester::CanShowContext(const int /*Context*/,
  const UnicodeString FileName)
{
  DebugFail();
  return !Application->HelpFile.IsEmpty();
}
//---------------------------------------------------------------------------
TStringList * __fastcall TWinHelpTester::GetHelpStrings(const UnicodeString ALink)
{
  DebugFail();
  TStringList * Result = new TStringList();
  Result->Add(ViewerName + L": " + ALink);
  return Result;
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TWinHelpTester::GetHelpPath()
{
  // never called on windows anyway
  return ExtractFilePath(Application->HelpFile);
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TWinHelpTester::GetDefaultHelpFile()
{
  return Application->HelpFile;
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
__fastcall TCustomHelpSelector::TCustomHelpSelector(const UnicodeString & Name) :
  FName(Name)
{
}
//---------------------------------------------------------------------------
int __fastcall TCustomHelpSelector::SelectKeyword(TStrings * /*Keywords*/)
{
  DebugFail();
  return 0;
}
//---------------------------------------------------------------------------
int __fastcall TCustomHelpSelector::TableOfContents(TStrings * Contents)
{
  return Contents->IndexOf(FName);
}
