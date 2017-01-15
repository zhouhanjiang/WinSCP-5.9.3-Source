//---------------------------------------------------------------------------
#ifndef ToolsH
#define ToolsH

#include <comctrls.hpp>
#include <WinInterface.h>
#include <HelpIntfs.hpp>
#include <stdio.h>
#include <SessionData.h>
#include <Vcl.Graphics.hpp>
//---------------------------------------------------------------------------
void __fastcall CenterFormOn(TForm * Form, TControl * CenterOn);
bool __fastcall ExecuteShellAndWait(const UnicodeString Path, const UnicodeString Params);
bool __fastcall ExecuteShellAndWait(const UnicodeString Command);
bool __fastcall IsKeyPressed(int VirtualKey);
bool __fastcall UseAlternativeFunction();
bool __fastcall OpenInNewWindow();
void __fastcall ExecuteNewInstance(const UnicodeString & Param);
IShellLink * __fastcall CreateDesktopShortCut(const UnicodeString &Name,
  const UnicodeString &File, const UnicodeString & Params, const UnicodeString & Description,
  int SpecialFolder = -1, int IconIndex = 0, bool Return = false);
IShellLink * __fastcall CreateDesktopSessionShortCut(
  const UnicodeString & SessionName, UnicodeString Name,
  const UnicodeString & AdditionalParams,
  int SpecialFolder = -1, int IconIndex = SITE_ICON, bool Return = false);
UnicodeString __fastcall GetListViewStr(TListView * ListView);
void __fastcall LoadListViewStr(TListView * ListView, UnicodeString LayoutStr);
void __fastcall RestoreForm(UnicodeString Data, TForm * Form);
UnicodeString __fastcall StoreForm(TCustomForm * Form);
void __fastcall RestoreFormSize(UnicodeString Data, TForm * Form);
UnicodeString __fastcall StoreFormSize(TForm * Form);
TFontStyles __fastcall IntToFontStyles(int value);
int __fastcall FontStylesToInt(const TFontStyles value);
bool __fastcall SameFont(TFont * Font1, TFont * Font2);
TColor __fastcall GetWindowTextColor(TColor Color);
TColor __fastcall GetWindowColor(TColor Color);
TColor __fastcall GetNonZeroColor(TColor Color);
void __fastcall ValidateMaskEdit(TComboBox * Edit);
void __fastcall ValidateMaskEdit(TEdit * Edit);
void __fastcall ValidateMaskEdit(TMemo * Edit, bool Directory);
bool __fastcall IsWinSCPUrl(const UnicodeString & Url);
UnicodeString __fastcall SecureUrl(const UnicodeString & Url);
void __fastcall OpenBrowser(UnicodeString URL);
void __fastcall ShowHelp(const UnicodeString & HelpKeyword);
bool __fastcall IsFormatInClipboard(unsigned int Format);
bool __fastcall TextFromClipboard(UnicodeString & Text, bool Trim);
bool __fastcall NonEmptyTextFromClipboard(UnicodeString & Text);
HANDLE __fastcall OpenTextFromClipboard(const wchar_t *& Text);
void __fastcall CloseTextFromClipboard(HANDLE Handle);
void __fastcall ExitActiveControl(TForm * Form);
UnicodeString __fastcall ReadResource(const UnicodeString ResName);
bool __fastcall DumpResourceToFile(const UnicodeString ResName,
  const UnicodeString FileName);
void __fastcall BrowseForExecutable(TEdit * Control, UnicodeString Title,
  UnicodeString Filter, bool FileNameCommand, bool Escape);
void __fastcall BrowseForExecutable(TComboBox * Control, UnicodeString Title,
  UnicodeString Filter, bool FileNameCommand, bool Escape);
bool __fastcall FontDialog(TFont * Font);
bool __fastcall SaveDialog(UnicodeString Title, UnicodeString Filter,
  UnicodeString DefaultExt, UnicodeString & FileName);
bool __fastcall AutodetectProxy(UnicodeString & HostName, int & PortNumber);
bool __fastcall IsWin64();
void __fastcall CopyToClipboard(UnicodeString Text);
void __fastcall CopyToClipboard(TStrings * Strings);
void __fastcall ShutDownWindows();
void __fastcall SuspendWindows();
void __fastcall EditSelectBaseName(HWND Edit);
void __fastcall VerifyAndConvertKey(UnicodeString & FileName, TSshProt SshProt);
void __fastcall VerifyKey(UnicodeString FileName, TSshProt SshProt);
void __fastcall VerifyCertificate(const UnicodeString & FileName);
TStrings * __fastcall GetUnwrappedMemoLines(TMemo * Memo);
bool __fastcall DetectSystemExternalEditor(
  bool AllowDefaultEditor,
  UnicodeString & Executable, UnicodeString & ExecutableDescription,
  UnicodeString & UsageState, bool & TryNextTime);
//---------------------------------------------------------------------------
#define IUNKNOWN \
  virtual HRESULT __stdcall QueryInterface(const GUID& IID, void **Obj) \
  { \
    return TInterfacedObject::QueryInterface(IID, (void *)Obj); \
  } \
  \
  virtual ULONG __stdcall AddRef() \
  { \
    return TInterfacedObject::_AddRef(); \
  } \
  \
  virtual ULONG __stdcall Release() \
  { \
    return TInterfacedObject::_Release(); \
  }
//---------------------------------------------------------------------------
void __fastcall InitializeCustomHelp(ICustomHelpViewer * HelpViewer);
void __fastcall FinalizeCustomHelp();
//---------------------------------------------------------------------------
#endif
