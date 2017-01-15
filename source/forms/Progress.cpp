//---------------------------------------------------------------------
#include <vcl.h>
#pragma hdrstop

#include <Common.h>
#include <CoreMain.h>
#include <TextsWin.h>
#include <HelpWin.h>
#include <WinInterface.h>
#include <VCLCommon.h>
#include <CustomWinConfiguration.h>
#include <GUITools.h>
#include <BaseUtils.hpp>
#include <DateUtils.hpp>
#include <Consts.hpp>
#include <HistoryComboBox.hpp>

#include "Progress.h"
//---------------------------------------------------------------------
#pragma link "PathLabel"
#pragma link "PngImageList"
#pragma link "TB2Dock"
#pragma link "TB2Item"
#pragma link "TB2Toolbar"
#pragma link "TBX"
#pragma link "TB2ExtItems"
#pragma link "TBXExtItems"
#ifndef NO_RESOURCES
#pragma resource "*.dfm"
#endif
//---------------------------------------------------------------------
bool __fastcall TProgressForm::IsIndeterminateOperation(TFileOperation Operation)
{
  return (Operation == foCalculateSize);
}
//---------------------------------------------------------------------
UnicodeString __fastcall TProgressForm::ProgressStr(TFileOperationProgressType * ProgressData)
{
  static const int Captions[] = { 0, 0, PROGRESS_DELETE,
    PROGRESS_SETPROPERTIES, 0, PROGRESS_CUSTOM_COMAND, PROGRESS_CALCULATE_SIZE,
    PROGRESS_REMOTE_MOVE, PROGRESS_REMOTE_COPY, PROGRESS_GETPROPERTIES,
    PROGRESS_CALCULATE_CHECKSUM, PROGRESS_LOCK, PROGRESS_UNLOCK };
  DebugAssert((unsigned int)ProgressData->Operation >= 1 && ((unsigned int)ProgressData->Operation - 1) < LENOF(Captions));
  int Id;
  if ((ProgressData->Operation == foCopy) || (ProgressData->Operation == foMove))
  {
    Id = (ProgressData->Side == osLocal) ? PROGRESS_UPLOAD : PROGRESS_DOWNLOAD;
  }
  else
  {
    Id = Captions[(int)ProgressData->Operation - 1];
    DebugAssert(Id != 0);
  }
  UnicodeString Result = LoadStr(Id);
  if (!IsIndeterminateOperation(ProgressData->Operation))
  {
    Result = FORMAT(L"%d%% %s", (ProgressData->OverallProgress(), Result));
  }
  return Result;
}
//---------------------------------------------------------------------
__fastcall TProgressForm::TProgressForm(TComponent * AOwner, bool AllowMoveToQueue)
    : FData(), TForm(AOwner)
{
  FLastOperation = foNone;
  FLastTotalSizeSet = false;
  FDataGot = false;
  FDataReceived = false;
  FCancel = csContinue;
  FMoveToQueue = false;
  FMinimizedByMe = false;
  FUpdateCounter = 0;
  FDeleteToRecycleBin = false;
  FReadOnly = false;
  FShowAsModalStorage = NULL;
  FStarted = Now();
  FModalBeginHooked = false;
  FModalLevel = -1;
  UseSystemSettings(this);

  if (CustomWinConfiguration->OperationProgressOnTop)
  {
    FOperationProgress = TopProgress;
    FFileProgress = BottomProgress;
  }
  else
  {
    FOperationProgress = BottomProgress;
    FFileProgress = TopProgress;
  }

  FOnceDoneItems.Add(odoIdle, IdleOnceDoneItem);
  FOnceDoneItems.Add(odoDisconnect, DisconnectOnceDoneItem);
  FOnceDoneItems.Add(odoSuspend, SuspendOnceDoneItem);
  FOnceDoneItems.Add(odoShutDown, ShutDownOnceDoneItem);
  ResetOnceDoneOperation();
  HideComponentsPanel(this);
  SelectScaledImageList(ImageList);

  SetGlobalMinimizeHandler(this, GlobalMinimize);
  MoveToQueueItem->Visible = AllowMoveToQueue;
}
//---------------------------------------------------------------------------
__fastcall TProgressForm::~TProgressForm()
{
  // to prevent raising assertion (e.g. IsProgress == True)
  FData.Clear();

  ClearGlobalMinimizeHandler(GlobalMinimize);

  if (IsApplicationMinimized() && FMinimizedByMe)
  {
    ShowNotification(
      NULL, MainInstructions(LoadStr(BALLOON_OPERATION_COMPLETE)),
      qtInformation);
  }

  ReleaseAsModal(this, FShowAsModalStorage);
}
//---------------------------------------------------------------------
void __fastcall TProgressForm::UpdateControls()
{
  DebugAssert((FData.Operation >= foCopy) && (FData.Operation <= foUnlock) &&
    (FData.Operation != foRename));

  bool TransferOperation =
    ((FData.Operation == foCopy) || (FData.Operation == foMove));

  CancelItem->Enabled = !FReadOnly && (FCancel == csContinue);
  MoveToQueueItem->Enabled = !FMoveToQueue && (FCancel == csContinue);
  CycleOnceDoneItem->Visible =
    !FReadOnly &&
    (FData.Operation != foCalculateSize) &&
    (FData.Operation != foGetProperties) &&
    (FData.Operation != foCalculateChecksum);
  CycleOnceDoneItem->ImageIndex = CurrentOnceDoneItem()->ImageIndex;
  SpeedComboBoxItem->Visible = TransferOperation;

  if (FData.Operation != FLastOperation)
  {
    UnicodeString Animation;
    UnicodeString CancelCaption = Vcl_Consts_SMsgDlgCancel;
    int MoveToQueueImageIndex = -1;

    int MoveTransferToQueueImageIndex;
    if (FData.Side == osRemote)
    {
      MoveTransferToQueueImageIndex = 7;
    }
    else
    {
      MoveTransferToQueueImageIndex = 8;
    }

    switch (FData.Operation)
    {
      case foCopy:
        if (FData.Side == osRemote)
        {
          Animation = L"CopyRemote";
        }
        else
        {
          Animation = L"CopyLocal";
        }
        MoveToQueueImageIndex = MoveTransferToQueueImageIndex;
        break;

      case foMove:
        if (FData.Side == osRemote)
        {
          Animation = L"MoveRemote";
        }
        else
        {
          Animation = L"MoveLocal";
        }
        MoveToQueueImageIndex = MoveTransferToQueueImageIndex;
        break;

      case foDelete:
        Animation = DeleteToRecycleBin ? L"Recycle" : L"Delete";
        break;

      case foCalculateSize:
        Animation = L"CalculateSize";
        CancelCaption = LoadStr(SKIP_BUTTON);
        MoveToQueueImageIndex = MoveTransferToQueueImageIndex;
        break;

      case foSetProperties:
        Animation = "SetProperties";
        break;

      default:
        DebugAssert(
          (FData.Operation == foCustomCommand) ||
          (FData.Operation == foGetProperties) ||
          (FData.Operation == foCalculateChecksum) ||
          (FData.Operation == foLock) ||
          (FData.Operation == foUnlock) ||
          (FData.Operation == foRemoteCopy) ||
          (FData.Operation == foRemoteMove));
        break;
    }

    CancelItem->Caption = CancelCaption;

    TopProgress->Style = IsIndeterminateOperation(FData.Operation) ? pbstMarquee : pbstNormal;

    FFrameAnimation.Init(AnimationPaintBox, Animation);
    FFrameAnimation.Start();

    int Delta = 0;
    if (TransferOperation && !TransferPanel->Visible) Delta += TransferPanel->Height;
      else
    if (!TransferOperation && TransferPanel->Visible) Delta += -TransferPanel->Height;
    TransferPanel->Visible = TransferOperation;

    ClientHeight = ClientHeight + Delta;

    TargetLabel->Visible = TransferOperation;
    TargetPathLabel->Visible = TransferOperation;
    TargetPathLabel->UnixPath = (FData.Side == osLocal);

    FileLabel->UnixPath = (FData.Side == osRemote);
    PathLabel->Caption =
      LoadStr((FData.Operation == foCalculateSize) ? PROGRESS_PATH_LABEL : PROGRESS_FILE_LABEL);

    MoveToQueueItem->ImageIndex = MoveToQueueImageIndex;

    FLastOperation = FData.Operation;
    FLastTotalSizeSet = !FData.TotalSizeSet;
  }

  if (FLastTotalSizeSet != FData.TotalSizeSet)
  {
    StartTimeLabelLabel->Visible = !FData.TotalSizeSet;
    StartTimeLabel->Visible = !FData.TotalSizeSet;
    TimeLeftLabelLabel->Visible = FData.TotalSizeSet;
    TimeLeftLabel->Visible = FData.TotalSizeSet;
    FLastTotalSizeSet = FData.TotalSizeSet;
  }

  if ((FData.Operation == foCalculateSize) && DebugAlwaysTrue(!FData.Temp))
  {
    if (FData.Side == osRemote)
    {
      FileLabel->Caption = UnixExtractFileDir(FData.FullFileName);
    }
    else
    {
      FileLabel->Caption = ExtractFileDir(FData.FullFileName);
    }
  }
  else if ((FData.Side == osRemote) || !FData.Temp)
  {
    FileLabel->Caption = FData.FileName;
  }
  else
  {
    FileLabel->Caption = ExtractFileName(FData.FileName);
  }
  int OverallProgress = FData.OverallProgress();
  FOperationProgress->Position = OverallProgress;
  FOperationProgress->Hint = IsIndeterminateOperation(FData.Operation) ? UnicodeString() : FORMAT(L"%d%%", (OverallProgress));
  Caption = FormatFormCaption(this, ProgressStr(&FData));

  if (TransferOperation)
  {
    if ((FData.Side == osLocal) || !FData.Temp)
    {
      TargetPathLabel->Caption = FData.Directory;
    }
    else
    {
      TargetPathLabel->Caption = LoadStr(PROGRESS_TEMP_DIR);
    }

    StartTimeLabel->Caption = FData.StartTime.TimeString();
    if (FData.TotalSizeSet)
    {
      TimeLeftLabel->Caption = FormatDateTimeSpan(Configuration->TimeFormat,
        FData.TotalTimeLeft());
    }
    TimeElapsedLabel->Caption = FormatDateTimeSpan(Configuration->TimeFormat, FData.TimeElapsed());
    BytesTransferedLabel->Caption = FormatBytes(FData.TotalTransfered);
    CPSLabel->Caption = FORMAT(L"%s/s", (FormatBytes(FData.CPS())));
    FFileProgress->Position = FData.TransferProgress();
    FFileProgress->Hint = FORMAT(L"%d%%", (FFileProgress->Position));
  }
}
//---------------------------------------------------------------------
static __int64 DelayStartInterval = MSecsPerSec / 2;
static __int64 UpdateInterval = 1 * MSecsPerSec;
//---------------------------------------------------------------------
bool __fastcall TProgressForm::ReceiveData(bool Force, int ModalLevelOffset)
{
  bool Result = false;
  if (FDataGot && !FDataReceived)
  {
    // CPS limit is set set only once from TFileOperationProgressType::Start.
    // Needs to be set even when data are not accepted yet, otherwise we would
    // write default value to FData in TProgressForm::SetProgressData
    FCPSLimit = FData.CPSLimit;

    // Never popup over dialog that appeared later than we started
    // (this can happen from UpdateTimerTimer when application is
    // restored while overwrite confirmation dialog [or any other]
    // is already shown).
    // TODO We should probably take as-modal windows into account too
    // (for extreme cases like restoring while reconnecting [as-modal TAuthenticateForm]).
    if ((FModalLevel < 0) || (Application->ModalLevel + ModalLevelOffset <= FModalLevel))
    {
      // Delay showing the progress until the application is restored,
      // otherwise the form popups up unminimized.
      // See solution in TMessageForm::CMShowingChanged.
      if (!IsApplicationMinimized() &&
          (Force || (MilliSecondsBetween(Now(), FStarted) > DelayStartInterval)))
      {
        FDataReceived = true;
        SpeedComboBoxItem->Text = SetSpeedLimit(FCPSLimit);
        ShowAsModal(this, FShowAsModalStorage);
        // particularly needed for the case, when we are showing the form delayed
        // because application was minimized when operation started
        Result = true;
      }
      else if (!FModalBeginHooked && DebugAlwaysTrue(FModalLevel < 0))
      {
        // record state as of time, the window should be shown,
        // had not we implemented delayed show
        ApplicationEvents->OnModalBegin = ApplicationModalBegin;
        FModalBeginHooked = true;
        FModalLevel = Application->ModalLevel;
      }
    }
  }

  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::ApplicationModalBegin(TObject * /*Sender*/)
{
  // Popup before any modal dialog shows (typically overwrite confirmation,
  // as that popups nearly instantly, i.e. less than DelayStartInterval).
  // The Application->ModalLevel is already incremented, but we should treat it as
  // if it were not as the dialog is not created yet (so we can popup if we are not yet).
  ReceiveData(true, -1);
}
//---------------------------------------------------------------------
void __fastcall TProgressForm::SetProgressData(TFileOperationProgressType & AData)
{
  bool InstantUpdate = false;

  // workaround: to force displaing first file data immediately,
  // otherwise form dialog uses to be blank for first second
  // (until UpdateTimerTimer)
  if (FileLabel->Caption.IsEmpty() && !AData.FileName.IsEmpty())
  {
    InstantUpdate = true;
  }

  FData.AssignButKeepSuspendState(AData);
  FDataGot = true;
  if (!UpdateTimer->Enabled)
  {
    UpdateTimer->Interval = static_cast<int>(DelayStartInterval);
    UpdateTimer->Enabled = true;
    FSinceLastUpdate = 0;
  }

  if (ReceiveData(false, 0))
  {
    InstantUpdate = true;
  }

  if (InstantUpdate)
  {
    UpdateControls();
    Application->ProcessMessages();
  }
  if (ProcessGUI(FUpdateCounter % 5 == 0))
  {
    FUpdateCounter = 0;
  }
  FUpdateCounter++;

  AData.CPSLimit = FCPSLimit;
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::UpdateTimerTimer(TObject * /*Sender*/)
{
  // popup the progress window at least here, if SetProgressData is
  // not being called (typically this happens when using custom command
  // that launches long-lasting external process, such as visual diff)
  ReceiveData(false, 0);

  if (UpdateTimer->Interval == DelayStartInterval)
  {
    UpdateTimer->Interval = static_cast<int>(GUIUpdateInterval);
  }

  if (FDataReceived)
  {
    FSinceLastUpdate += UpdateTimer->Interval;
    if (FSinceLastUpdate >= UpdateInterval)
    {
      UpdateControls();
      FSinceLastUpdate = 0;
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::FormShow(TObject * /*Sender*/)
{
  CopySpeedLimits(CustomWinConfiguration->History[L"SpeedLimit"], SpeedComboBoxItem->Strings);
  ReceiveData(false, 0);
  if (FDataReceived)
  {
    UpdateControls();
  }
  // HACK: In command-line run (/upload), FormShow gets called twice,
  // leading to duplicate hook and memory leak. Make sure we unhook, just in case.
  // Calling unhook without hooking first is noop.
  UnhookFormActivation(this);
  HookFormActivation(this);
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::FormHide(TObject * /*Sender*/)
{
  UnhookFormActivation(this);
  // This is to counter the "infinite" timestamp in
  // TTerminalManager::ApplicationShowHint.
  // Because if form disappears on its own, hint is not hidden.
  Application->CancelHint();
  CustomWinConfiguration->History[L"SpeedLimit"] = SpeedComboBoxItem->Strings;
  UpdateTimer->Enabled = false;
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::CancelItemClick(TObject * /*Sender*/)
{
  CancelOperation();
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::Minimize(TObject * Sender)
{
  CallGlobalMinimizeHandler(Sender);
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::MinimizeItemClick(TObject * Sender)
{
  Minimize(Sender);
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::CancelOperation()
{
  DebugAssert(FDataReceived);
  if (!FData.Suspended)
  {
    // mostly useless, as suspend is called over copy of actual progress data
    FData.Suspend();
    UpdateControls();
    try
    {
      TCancelStatus ACancel;
      if (FData.TransferingFile &&
          (FData.TimeExpected() > GUIConfiguration->IgnoreCancelBeforeFinish))
      {
        int Result = MessageDialog(LoadStr(CANCEL_OPERATION_FATAL2), qtWarning,
          qaYes | qaNo | qaCancel, HELP_PROGRESS_CANCEL);
        switch (Result)
        {
          case qaYes:
            ACancel = csCancelTransfer; break;
          case qaNo:
            ACancel = csCancel; break;
          default:
            ACancel = csContinue; break;
        }
      }
      else
      {
        ACancel = csCancel;
      }

      if (FCancel < ACancel)
      {
        FCancel = ACancel;
        UpdateControls();
      }
    }
    __finally
    {
      FData.Resume();
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::GlobalMinimize(TObject * /*Sender*/)
{
  ApplicationMinimize();
  FMinimizedByMe = true;
}
//---------------------------------------------------------------------------
TTBCustomItem * __fastcall TProgressForm::CurrentOnceDoneItem()
{
  TOnceDoneItems::const_iterator Iterator = FOnceDoneItems.begin();
  while (Iterator != FOnceDoneItems.end())
  {
    if (Iterator->second->Checked)
    {
      return Iterator->second;
    }
    Iterator++;
  }

  DebugFail();
  return NULL;
}
//---------------------------------------------------------------------------
TOnceDoneOperation __fastcall TProgressForm::GetOnceDoneOperation()
{
  return FOnceDoneItems.LookupFirst(CurrentOnceDoneItem());
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::SetOnceDoneItem(TTBCustomItem * Item)
{
  TTBCustomItem * Current = CurrentOnceDoneItem();
  if (Current != Item)
  {
    Current->Checked = false;
    Item->Checked = true;
    // Not until we have any data to update.
    // Happens when set to odoDisconnect in command-line upload/download
    // mode from TCustomScpExplorerForm::FileOperationProgress.
    if (FDataGot)
    {
      UpdateControls();
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::SetOnceDoneOperation(TOnceDoneOperation value)
{
  SetOnceDoneItem(FOnceDoneItems.LookupSecond(value));
}
//---------------------------------------------------------------------------
bool __fastcall TProgressForm::GetAllowMinimize()
{
  return MinimizeItem->Visible;
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::SetAllowMinimize(bool value)
{
  MinimizeItem->Visible = value;
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::SetReadOnly(bool value)
{
  if (FReadOnly != value)
  {
    FReadOnly = value;
    if (!value)
    {
      ResetOnceDoneOperation();
    }
    UpdateControls();
  }
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::ResetOnceDoneOperation()
{
  SetOnceDoneOperation(odoIdle);
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::CMDialogKey(TCMDialogKey & Message)
{
  if (Message.CharCode == VK_TAB)
  {
    Toolbar->KeyboardOpen(L'\0', false);
    Message.Result = 1;
  }
  else
  {
    TForm::Dispatch(&Message);
  }
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::Dispatch(void * AMessage)
{
  TMessage & Message = *reinterpret_cast<TMessage *>(AMessage);
  if (Message.Msg == WM_CLOSE)
  {
    CancelOperation();
  }
  else if (Message.Msg == CM_DIALOGKEY)
  {
    CMDialogKey(reinterpret_cast<TCMDialogKey &>(Message));
  }
  else
  {
    TForm::Dispatch(AMessage);
  }
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::MoveToQueueItemClick(TObject * /*Sender*/)
{
  FMoveToQueue = true;
  UpdateControls();
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::OnceDoneItemClick(TObject * Sender)
{
  SetOnceDoneItem(dynamic_cast<TTBCustomItem *>(Sender));
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::CycleOnceDoneItemClick(TObject * /*Sender*/)
{
  TTBCustomItem * Item = CurrentOnceDoneItem();
  int Index = Item->Parent->IndexOf(Item);
  DebugAssert(Index >= 0);
  if (Index < Item->Parent->Count - 1)
  {
    Index++;
  }
  else
  {
    Index = 0;
  }
  SetOnceDoneItem(Item->Parent->Items[Index]);
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TProgressForm::ItemSpeed(const UnicodeString & Text,
  TTBXComboBoxItem * Item)
{
  // Keep in sync with TNonVisualDataModule::QueueItemSpeed
  FCPSLimit = GetSpeedLimit(Text);

  UnicodeString Result = SetSpeedLimit(FCPSLimit);
  SaveToHistory(Item->Strings, Result);
  CustomWinConfiguration->History[L"SpeedLimit"] = Item->Strings;

  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::SpeedComboBoxItemAcceptText(TObject * Sender,
  UnicodeString & NewText, bool & /*Accept*/)
{
  TTBXComboBoxItem * Item = dynamic_cast<TTBXComboBoxItem *>(Sender);
  NewText = ItemSpeed(NewText, Item);
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::SpeedComboBoxItemItemClick(TObject * Sender)
{
  TTBXComboBoxItem * Item = dynamic_cast<TTBXComboBoxItem *>(Sender);
  Item->Text = ItemSpeed(Item->Text, Item);
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::SpeedComboBoxItemAdjustImageIndex(
  TTBXComboBoxItem * Sender, const UnicodeString /*AText*/, int /*AIndex*/, int & ImageIndex)
{
  // Use fixed image (do not change image by item index)
  ImageIndex = Sender->ImageIndex;
}
//---------------------------------------------------------------------------
void __fastcall TProgressForm::SpeedComboBoxItemClick(TObject * Sender)
{
  ClickToolbarItem(DebugNotNull(dynamic_cast<TTBCustomItem *>(Sender)), false);
}
//---------------------------------------------------------------------------
