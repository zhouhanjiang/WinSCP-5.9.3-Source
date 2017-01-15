//---------------------------------------------------------------------------
#include <vcl.h>
#pragma hdrstop

#include <Common.h>
#include <VCLCommon.h>
#include <TextsWin.h>
#include <CustomWinConfiguration.h>
#include <CoreMain.h>
#include <WinInterface.h>
#include <GUITools.h>

#include "RemoteTransfer.h"
//---------------------------------------------------------------------------
#pragma package(smart_init)
#pragma link "HistoryComboBox"
#ifndef NO_RESOURCES
#pragma resource "*.dfm"
#endif
//---------------------------------------------------------------------------
bool __fastcall DoRemoteCopyDialog(TStrings * Sessions, TStrings * Directories,
  TDirectRemoteCopy AllowDirectCopy, bool Multi, void *& Session, UnicodeString & Target, UnicodeString & FileMask,
  bool & DirectCopy, void * CurrentSession)
{
  bool Result;
  TRemoteTransferDialog * Dialog = SafeFormCreate<TRemoteTransferDialog>();
  try
  {
    Dialog->Init(Multi, Sessions, Directories, AllowDirectCopy, CurrentSession);
    Result = Dialog->Execute(Session, Target, FileMask, DirectCopy);
  }
  __finally
  {
    delete Dialog;
  }
  return Result;
}
//---------------------------------------------------------------------------
__fastcall TRemoteTransferDialog::TRemoteTransferDialog(TComponent * Owner)
  : TForm(Owner)
{
  UseSystemSettings(this);

  Caption = LoadStr(REMOTE_COPY_TITLE);
}
//---------------------------------------------------------------------------
void __fastcall TRemoteTransferDialog::Init(bool Multi, TStrings * Sessions,
  TStrings * Directories, TDirectRemoteCopy AllowDirectCopy, void * CurrentSession)
{
  FMulti = Multi;
  SessionCombo->Items = Sessions;
  FDirectories = Directories;
  DebugAssert(SessionCombo->Items->Count > 0);
  DebugAssert(SessionCombo->Items->Count == FDirectories->Count);
  FAllowDirectCopy = AllowDirectCopy;
  FCurrentSession = CurrentSession;
  LoadDialogImage(Image, L"Duplicate");
}
//---------------------------------------------------------------------------
bool __fastcall TRemoteTransferDialog::Execute(void *& Session, UnicodeString & Target,
  UnicodeString & FileMask, bool & DirectCopy)
{
  for (int Index = 0; Index < SessionCombo->Items->Count; Index++)
  {
    if (SessionCombo->Items->Objects[Index] == Session)
    {
      SessionCombo->ItemIndex = Index;
      break;
    }
  }
  DirectoryEdit->Items = CustomWinConfiguration->History[L"RemoteTarget"];
  DirectoryEdit->Text = UnixIncludeTrailingBackslash(Target) + FileMask;
  FDirectCopy = DirectCopy;
  UpdateNotDirectCopyCheck();
  bool Result = (ShowModal() == DefaultResult(this));
  if (Result)
  {
    Session = SessionCombo->Items->Objects[SessionCombo->ItemIndex];
    CustomWinConfiguration->History[L"RemoteTarget"] = DirectoryEdit->Items;
    Target = UnixExtractFilePath(DirectoryEdit->Text);
    FileMask = GetFileMask();
    DirectCopy = !NotDirectCopyCheck->Checked;
  }
  return Result;
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TRemoteTransferDialog::GetFileMask()
{
  return UnixExtractFileName(DirectoryEdit->Text);
}
//---------------------------------------------------------------------------
void __fastcall TRemoteTransferDialog::UpdateControls()
{
  EnableControl(NotDirectCopyCheck,
    IsCurrentSessionSelected() &&
    (FAllowDirectCopy != drcDisallow));
  EnableControl(OkButton, !DirectoryEdit->Text.IsEmpty());
}
//---------------------------------------------------------------------------
void __fastcall TRemoteTransferDialog::ControlChange(TObject * /*Sender*/)
{
  UpdateControls();
}
//---------------------------------------------------------------------------
void __fastcall TRemoteTransferDialog::FormShow(TObject * /*Sender*/)
{
  InstallPathWordBreakProc(DirectoryEdit);

  UpdateControls();
  DirectoryEdit->SetFocus();
}
//---------------------------------------------------------------------------
void __fastcall TRemoteTransferDialog::HelpButtonClick(TObject * /*Sender*/)
{
  FormHelp(this);
}
//---------------------------------------------------------------------------
void __fastcall TRemoteTransferDialog::SessionComboChange(TObject * /*Sender*/)
{
  DirectoryEdit->Text =
    UnixIncludeTrailingBackslash(FDirectories->Strings[SessionCombo->ItemIndex]) +
    UnixExtractFileName(DirectoryEdit->Text);
  UpdateNotDirectCopyCheck();
  UpdateControls();
}
//---------------------------------------------------------------------------
void __fastcall TRemoteTransferDialog::UpdateNotDirectCopyCheck()
{
  if (IsCurrentSessionSelected())
  {
    NotDirectCopyCheck->Checked = !FDirectCopy;
  }
  else
  {
    NotDirectCopyCheck->Checked = true;
  }
}
//---------------------------------------------------------------------------
void __fastcall TRemoteTransferDialog::FormCloseQuery(TObject * /*Sender*/,
  bool & /*CanClose*/)
{
  if (ModalResult == DefaultResult(this))
  {
    if (!IsFileNameMask(GetFileMask()) && FMulti)
    {
      UnicodeString Message =
        FormatMultiFilesToOneConfirmation(DirectoryEdit->Text, true);
      if (MessageDialog(Message, qtConfirmation, qaOK | qaCancel, HELP_NONE) == qaCancel)
      {
        Abort();
      }
    }

    if (IsCurrentSessionSelected() &&
        (FAllowDirectCopy == drcConfirmCommandSession) &&
        !NotDirectCopyCheck->Checked &&
        GUIConfiguration->ConfirmCommandSession)
    {
      TMessageParams Params(mpNeverAskAgainCheck);
      unsigned int Answer = MessageDialog(LoadStr(REMOTE_COPY_COMMAND_SESSION2),
        qtConfirmation, qaOK | qaCancel, HelpKeyword, &Params);
      if (Answer == qaNeverAskAgain)
      {
        GUIConfiguration->ConfirmCommandSession = false;
      }
      else if (Answer != qaOK)
      {
        Abort();
      }
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TRemoteTransferDialog::NotDirectCopyCheckClick(
  TObject * /*Sender*/)
{
  if (IsCurrentSessionSelected())
  {
    FDirectCopy = !NotDirectCopyCheck->Checked;
  }
}
//---------------------------------------------------------------------------
bool __fastcall TRemoteTransferDialog::IsCurrentSessionSelected()
{
  return (SessionCombo->Items->Objects[SessionCombo->ItemIndex] == FCurrentSession);
}
//---------------------------------------------------------------------------
