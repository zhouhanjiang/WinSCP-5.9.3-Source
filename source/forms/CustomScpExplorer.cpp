//---------------------------------------------------------------------------
#define NO_WIN32_LEAN_AND_MEAN
#include <vcl.h>
#pragma hdrstop

#include <Common.h>

#include "CustomScpExplorer.h"

#include <Bookmarks.h>
#include <Interface.h>
#include <Exceptions.h>
#include <CoreMain.h>
#include <FileSystems.h>
#include <TextsCore.h>
#include <TextsWin.h>
#include <HelpWin.h>

#include <VCLCommon.h>
#include <Log.h>
#include <Progress.h>
#include <SynchronizeProgress.h>

#include <DragExt.h>
#include <WinApi.h>

#include "GUITools.h"
#include "NonVisual.h"
#include "Glyphs.h"
#include "Tools.h"
#include "WinConfiguration.h"
#include "TerminalManager.h"
#include "EditorManager.h"
#include "ProgParams.h"
#include "Setup.h"
#include <Consts.hpp>
#include <DateUtils.hpp>
#include <TB2Common.hpp>
//---------------------------------------------------------------------------
#pragma package(smart_init)
#pragma link "CustomDirView"
#pragma link "CustomUnixDirView"
#pragma link "IEListView"
#pragma link "NortonLikeListView"
#pragma link "UnixDirView"
#pragma link "CustomDriveView"
#pragma link "UnixDriveView"
#pragma link "CustomDriveView"
#pragma link "TB2Dock"
#pragma link "TBXStatusBars"
#pragma link "TB2Item"
#pragma link "TB2Toolbar"
#pragma link "ThemePageControl"
#pragma link "PathLabel"
#ifndef NO_RESOURCES
#pragma resource "*.dfm"
#endif
//---------------------------------------------------------------------------
#define SAVE_SELECTION(DIRVIEW) \
  UnicodeString FocusFile = L""; \
  UnicodeString LastFocusedFile = L""; \
  if (DIRVIEW->ItemFocused) LastFocusedFile = DIRVIEW->ItemFocused->Caption; \
  { TListItem * ClosestUnselected = DIRVIEW->ClosestUnselected(DIRVIEW->ItemFocused); \
  if (ClosestUnselected) FocusFile = ClosestUnselected->Caption; }
#define RESTORE_SELECTION(DIRVIEW) \
  if (!LastFocusedFile.IsEmpty() && \
      (!DIRVIEW->ItemFocused || (DIRVIEW->ItemFocused->Caption != LastFocusedFile))) \
  { \
    TListItem *ItemToSelect = DIRVIEW->FindFileItem(FocusFile); \
    if (ItemToSelect) \
    { \
      DIRVIEW->ItemFocused = ItemToSelect; \
      DIRVIEW->ItemFocused->MakeVisible(False); \
    } \
  }
//---------------------------------------------------------------------------
#define WM_COMPONENT_HIDE (WM_WINSCP_USER + 4)
static const int SessionPanelCount = 4;
//---------------------------------------------------------------------------
class TMutexGuard
{
public:
  TMutexGuard(HANDLE AMutex, int Message = MUTEX_RELEASE_TIMEOUT,
    int Timeout = 5000)
  {
    FMutex = NULL;
    unsigned long WaitResult = WaitForSingleObject(AMutex, Timeout);
    if (WaitResult == WAIT_TIMEOUT)
    {
      throw Exception(LoadStr(MUTEX_RELEASE_TIMEOUT));
    }
    else
    {
      FMutex = AMutex;
    }
  }

  ~TMutexGuard()
  {
    if (FMutex != NULL)
    {
      ReleaseMutex(FMutex);
    }
  }

private:
  HANDLE FMutex;
};
//---------------------------------------------------------------------------
class TWindowLock
{
public:
  TWindowLock(TCustomScpExplorerForm * Form) :
    FForm(Form)
  {
    FForm->LockWindow();
  }

  ~TWindowLock()
  {
    FForm->UnlockWindow();
  }

private:
  TCustomScpExplorerForm * FForm;
};
//---------------------------------------------------------------------------
struct TTransferOperationParam
{
  TTransferOperationParam();

  UnicodeString TargetDirectory;
  bool Temp;
  bool DragDrop;
  int Options;
  TAutoSwitch Queue;
};
//---------------------------------------------------------------------------
TTransferOperationParam::TTransferOperationParam()
{
  Temp = false;
  DragDrop = false;
  Options = 0;
  Queue = asAuto;
}
//---------------------------------------------------------------------------
class TTransferPresetNoteData : public TObject
{
public:
  UnicodeString Message;
};
//---------------------------------------------------------------------------
class TTerminalNoteData : public TObject
{
public:
  TTerminal * Terminal;
};
//---------------------------------------------------------------------------
__fastcall TCustomScpExplorerForm::TCustomScpExplorerForm(TComponent* Owner):
    FFormRestored(false),
    TForm(Owner)
{
  FCurrentSide = osRemote;
  FEverShown = false;
  FDocks = new TList();
  RestoreParams();
  ConfigurationChanged();
  RemoteDirView->Invalidate();
  DebugAssert(NonVisualDataModule && !NonVisualDataModule->ScpExplorer);
  NonVisualDataModule->ScpExplorer = this;
  FAutoOperation = false;
  FForceExecution = false;
  FIgnoreNextDialogChar = 0;
  FErrorList = NULL;
  FSynchronizeProgressForm = NULL;
  FProgressForm = NULL;
  FRefreshLocalDirectory = false;
  FRefreshRemoteDirectory = false;
  FDDMoveSlipped = false;
  FDDExtMapFile = NULL;
  // CreateMutexW keeps failing with ERROR_NOACCESS
  FDDExtMutex = CreateMutexA(NULL, false, AnsiString(DRAG_EXT_MUTEX).c_str());
  DebugAssert(FDDExtMutex != NULL);
  FDDTargetControl = NULL;
  FDelayedDeletionTimer = NULL;
  FDelayedDeletionList = new TStringList();
  FDDFileList = NULL;
  FPendingTempSpaceWarn = false;
  FCapturedLog = NULL;
  FDragDropOperation = false;
  memset(&FHistoryMenu, 0, sizeof(FHistoryMenu));
  FAllowTransferPresetAutoSelect = true;
  FCopyParamDefault = L"";
  FSynchronizeController = NULL;
  FPendingQueueActionItem = NULL;
  FLockLevel = 0;
  FLockSuspendLevel = 0;
  FDisabledOnLockSuspend = false;
  FAlternativeDelete = false;
  FTrayIcon = new ::TTrayIcon(0);
  FTrayIcon->OnClick = TrayIconClick;
  FMaxQueueLength = 0;
  FLastContextPopupScreenPoint = TPoint(-1, -1);
  FTransferResumeList = NULL;
  FMoveToQueue = false;
  FStandaloneEditing = false;
  FOnFeedSynchronizeError = NULL;
  FNeedSession = false;

  FEditorManager = new TEditorManager();
  FEditorManager->OnFileChange = ExecutedFileChanged;
  FEditorManager->OnFileReload = ExecutedFileReload;
  FEditorManager->OnFileEarlyClosed = ExecutedFileEarlyClosed;
  FEditorManager->OnFileUploadComplete = ExecutedFileUploadComplete;

  FLocalEditors = new TList();

  FQueueStatus = NULL;
  FQueueStatusSection = new TCriticalSection();
  FQueueStatusInvalidated = false;
  FQueueItemInvalidated = false;
  FQueueActedItem = NULL;
  FQueueController = new TQueueController(QueueView3);

  FUserActionTimer = new TTimer(this);
  FUserActionTimer->Enabled = false;
  FUserActionTimer->Interval = 10;
  FUserActionTimer->OnTimer = UserActionTimer;

  FNotes = new TStringList();
  FNoteTimer = new TTimer(this);
  FNoteTimer->Enabled = false;
  FNoteTimer->OnTimer = NoteTimer;
  FOnNoteClick = NULL;

  FOle32Library = LoadLibrary(L"Ole32.dll");
  FDragMoveCursor = FOle32Library != NULL ?
    LoadCursor(FOle32Library, MAKEINTRESOURCE(2)) : NULL;

  UseSystemSettings(this);

  TTBXStringList * TransferList = dynamic_cast<TTBXStringList*>(
    static_cast<TObject*>(GetComponent(fcTransferList)));
  DebugAssert(TransferList != NULL);
  FTransferListHoverIndex = -1;
  TransferList->OnChange = TransferListChange;
  TransferList->OnDrawItem = TransferListDrawItem;

  SetSubmenu(dynamic_cast<TTBXCustomItem *>(static_cast<TObject *>(GetComponent(fcColorMenu))));
  SetSubmenu(NonVisualDataModule->ColorMenuItem);

  UseDesktopFont(SessionsPageControl);
  UpdateSessionsPageControlHeight();
  UseDesktopFont(RemoteDirView);
  UseDesktopFont(RemoteDriveView);
  UseDesktopFont(QueueView3);
  UseDesktopFont(QueueLabel);
  UseDesktopFont(RemoteStatusBar);

  reinterpret_cast<TLabel*>(QueueSplitter)->OnDblClick = QueueSplitterDblClick;
  QueueSplitter->ShowHint = true;
  RemotePanelSplitter->ShowHint = true;

  FSystemImageList = SharedSystemImageList(false);
  FSystemImageList->DrawingStyle = dsTransparent;

  FCustomCommandMenu = CreateTBXPopupMenu(this);
  FCustomCommandLocalFileList = NULL;
  FCustomCommandRemoteFileList = NULL;

  FSessionColors = new TPngImageList(this);
  FSessionColors->SetSize(GlyphsModule->ExplorerImages->Width, GlyphsModule->ExplorerImages->Height);
  FSessionColors->ColorDepth = cd32Bit;
  AddFixedSessionImages();
  SessionsPageControl->Images = FSessionColors;
  QueueLabel->FocusControl = QueueView3;
  UpdateQueueLabel();
  FRemoteDirViewWasFocused = true;

  CreateHiddenWindow();
  StartUpdates();
}
//---------------------------------------------------------------------------
__fastcall TCustomScpExplorerForm::~TCustomScpExplorerForm()
{
  // this has to be one of the very first things to do
  StopUpdateThread();

  SessionsPageControl->Images = NULL;
  SAFE_DESTROY(FSessionColors);
  SAFE_DESTROY(FSessionsDragDropFilesEx);

  delete FCustomCommandLocalFileList;
  delete FCustomCommandRemoteFileList;
  delete FCustomCommandMenu;

  delete FSystemImageList;
  FSystemImageList = NULL;

  delete FTrayIcon;
  FTrayIcon = NULL;

  FEditorManager->CloseInternalEditors(ForceCloseInternalEditor);
  delete FEditorManager;

  ForceCloseLocalEditors();
  delete FLocalEditors;

  if (FDelayedDeletionTimer)
  {
    DoDelayedDeletion(NULL);
    SAFE_DESTROY(FDelayedDeletionTimer);
  }
  SAFE_DESTROY(FDelayedDeletionList);
  // sometimes we do not get DDEnd so the list is not released
  SAFE_DESTROY(FDDFileList);

  DebugAssert(FSynchronizeController == NULL);

  CloseHandle(FDDExtMutex);
  FDDExtMutex = NULL;

  FreeLibrary(FOle32Library);
  FOle32Library = NULL;
  FDragMoveCursor = NULL;

  DebugAssert(!FErrorList);
  if (FEverShown)
  {
    // when window is never shown (like when running command-line operation),
    // particularly window site is not restored correctly (BoundsRect value set
    // in RestoreForm gets lost during handle allocation), so we do not want
    // it to be stored
    StoreParams();
  }
  Terminal = NULL;
  Queue = NULL;
  DebugAssert(NonVisualDataModule && (NonVisualDataModule->ScpExplorer == this));
  NonVisualDataModule->ScpExplorer = NULL;

  delete FQueueController;
  FQueueController = NULL;
  delete FQueueStatusSection;
  FQueueStatusSection = NULL;
  delete FQueueStatus;
  FQueueStatus = NULL;

  delete FUserActionTimer;
  FUserActionTimer = NULL;

  delete FNoteTimer;
  delete FNotes;
  delete FNoteData;

  SAFE_DESTROY(FDocks);

  SAFE_DESTROY(FHistoryMenu[0][0]);
  SAFE_DESTROY(FHistoryMenu[0][1]);
  SAFE_DESTROY(FHistoryMenu[1][0]);
  SAFE_DESTROY(FHistoryMenu[1][1]);

  if (FHiddenWindow != NULL)
  {
    DestroyWindow(FHiddenWindow);
    FHiddenWindow = NULL;
  }

}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RefreshPanel(const UnicodeString & Session, const UnicodeString & Path)
{

  std::unique_ptr<TSessionData> Data;
  if (!Session.IsEmpty())
  {
    bool DefaultsOnly;
    Data.reset(StoredSessions->ParseUrl(Session, NULL, DefaultsOnly));
  }

  TTerminalManager * Manager = TTerminalManager::Instance();
  for (int Index = 0; Index < Manager->Count; Index++)
  {
    TTerminal * Terminal = Manager->Terminals[Index];
    if (Session.IsEmpty() ||
        Terminal->SessionData->IsSameSite(Data.get()))
    {
      if (Path.IsEmpty())
      {
        Terminal->ClearCaches();
      }
      else
      {
        Terminal->DirectoryModified(Path, true);
      }
    }
  }

  // We should flag a pending refresh for the background terminals or busy foreground terminals
  if ((Terminal != NULL) && Terminal->Active &&
      CanCommandLineFromAnotherInstance() &&
      (Session.IsEmpty() || Terminal->SessionData->IsSameSite(Data.get())) &&
      (Path.IsEmpty() || UnixIsChildPath(Path, Terminal->CurrentDirectory)))
  {
    Terminal->ReloadDirectory();
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::WMCopyData(TMessage & Message)
{
  PCOPYDATASTRUCT CopyData = reinterpret_cast<PCOPYDATASTRUCT>(Message.LParam);

  size_t MessageSize = sizeof(TCopyDataMessage);
  bool Result = DebugAlwaysTrue(CopyData->cbData == MessageSize);
  if (Result)
  {
    const TCopyDataMessage & Message = *reinterpret_cast<const TCopyDataMessage *>(CopyData->lpData);

    Result = (Message.Version == TCopyDataMessage::Version1);

    if (Result)
    {
      switch (Message.Command)
      {
        case TCopyDataMessage::CommandCanCommandLine:
          Result = CanCommandLineFromAnotherInstance();
          break;

        case TCopyDataMessage::CommandCommandLine:
          {
            UnicodeString CommandLine(Message.CommandLine);
            Result = CommandLineFromAnotherInstance(CommandLine);
          }
          break;

        case TCopyDataMessage::RefreshPanel:
          RefreshPanel(Message.Refresh.Session, Message.Refresh.Path);
          break;

        case TCopyDataMessage::MainWindowCheck:
          Result = true;
          break;

        default:
          Result = false;
          break;
      }
    }
  }

  Message.Result = Result ? 1 : 0;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::CreateHiddenWindow()
{
  WNDCLASS WindowClass = {0};
  WindowClass.lpfnWndProc = DefWindowProc;
  WindowClass.hInstance = HInstance;
  WindowClass.lpszClassName = HIDDEN_WINDOW_NAME;

  FHiddenWindow = NULL;

  if (RegisterClass(&WindowClass))
  {
    FHiddenWindow = CreateWindow(HIDDEN_WINDOW_NAME, L"",
      WS_POPUP, 0, 0, 0, 0, 0, 0, HInstance, NULL);
  }
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::CanConsole()
{
  return (Terminal != NULL) && (Terminal->IsCapable[fcAnyCommand] || Terminal->IsCapable[fcSecondaryShell]);
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::CanCommandLineFromAnotherInstance()
{
  bool Result = !NonVisualDataModule->Busy;
  return Result;
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::CommandLineFromAnotherInstance(
  const UnicodeString & CommandLine)
{
  TProgramParams Params(CommandLine);
  bool Result = CanCommandLineFromAnotherInstance() && DebugAlwaysTrue(Params.ParamCount > 0);
  if (Result)
  {
    NonVisualDataModule->StartBusy();
    try
    {
      // this action is initiated from another process,
      // so it's likely that our window is not visible,
      // and user won't see what is going on
      Application->BringToFront();
      // reload sessions as we may be asked to open a session
      // just stored by another instance
      StoredSessions->Load();
      UnicodeString SessionName = Params.Param[1];
      std::unique_ptr<TObjectList> DataList(new TObjectList());
      UnicodeString DownloadFile; // unused
      GetLoginData(SessionName, &Params, DataList.get(), DownloadFile, true, this);
      if (DataList->Count > 0)
      {
        TTerminalManager * Manager = TTerminalManager::Instance();
        Manager->ActiveTerminal = Manager->NewTerminals(DataList.get());
      }
    }
    __finally
    {
      NonVisualDataModule->EndBusy();
    }
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SetTerminal(TTerminal * value)
{
  if (FTerminal != value)
  {
    TerminalChanging();
    FTerminal = value;
    bool PrevAllowTransferPresetAutoSelect = FAllowTransferPresetAutoSelect;
    FAllowTransferPresetAutoSelect = false;
    try
    {
      TerminalChanged();
    }
    __finally
    {
      FAllowTransferPresetAutoSelect = PrevAllowTransferPresetAutoSelect;
    }

    if (Terminal != NULL)
    {
      TransferPresetAutoSelect();
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::TerminalChanging()
{
  if (FTerminal != NULL)
  {
    UpdateTerminal(Terminal);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::TerminalChanged()
{
  RemoteDirView->Terminal = Terminal;
  NonVisualDataModule->ResetQueueOnceEmptyOperation();

  TManagedTerminal * ManagedTerminal = NULL;

  if (Terminal != NULL)
  {
    if (Terminal->Active)
    {
      Terminal->RefreshDirectory();
    }

    ManagedTerminal = dynamic_cast<TManagedTerminal *>(Terminal);
    DebugAssert(ManagedTerminal != NULL);

    if (WinConfiguration->PreservePanelState)
    {
      if (ManagedTerminal->RemoteExplorerState != NULL)
      {
        RemoteDirView->RestoreState(ManagedTerminal->RemoteExplorerState);
      }
      else
      {
        RemoteDirView->ClearState();
      }
    }

    InitStatusBar();
  }

  DoTerminalListChanged(false);

  if (ManagedTerminal != NULL)
  {
    // this has to be set only after the tab is switched from DoTerminalListChanged,
    // otherwise we are changing color of wrong tab
    SessionColor = (TColor)ManagedTerminal->StateData->Color;
  }

  UpdateTransferList();
  // Update panels Enable state before refreshing the labels
  UpdateControls();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SetQueue(TTerminalQueue * value)
{
  if (Queue != value)
  {
    if (FQueue != NULL)
    {
      FQueue->OnListUpdate = NULL;
      FQueue->OnQueueItemUpdate = NULL;
    }
    FQueue = value;
    if (FQueue != NULL)
    {
      DebugAssert(FQueue->OnListUpdate == NULL);
      FQueue->OnListUpdate = QueueListUpdate;
      DebugAssert(FQueue->OnQueueItemUpdate == NULL);
      FQueue->OnQueueItemUpdate = QueueItemUpdate;
    }
    QueueChanged();
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::QueueView3Deletion(TObject * /*Sender*/,
  TListItem * Item)
{
  if (FQueueActedItem == Item)
  {
    FQueueActedItem = NULL;
    if ((QueueView3->PopupMenu != NULL) &&
        (QueueView3->PopupMenu->PopupComponent == QueueView3))
    {
      // rather "trick", suggested by Jordan on jrsoftware.toolbar2000
      ReleaseCapture();
    }
  }

  if (Item->Data == FPendingQueueActionItem)
  {
    FPendingQueueActionItem = NULL;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateQueueStatus(bool QueueChanging)
{
  {
    TGuard Guard(FQueueStatusSection);

    FQueueStatusInvalidated = false;

    if (FQueue != NULL)
    {
      FQueueStatus = FQueue->CreateStatus(FQueueStatus);
    }
  }

  if ((FQueueStatus != NULL) && (FQueueStatus->Count > FMaxQueueLength))
  {
    FMaxQueueLength = FQueueStatus->Count;
    Configuration->Usage->SetMax(L"MaxQueueLength", FMaxQueueLength);
  }

  FQueueController->UpdateQueueStatus(FQueueStatus);
  SetQueueProgress();

  UpdateQueueView();

  bool IsEmpty = (FQueueStatus == NULL) || (FQueueStatus->Count == 0);

  if (IsEmpty && (Terminal != NULL))
  {
    TOnceDoneOperation OnceDoneOperation =
      NonVisualDataModule->CurrentQueueOnceEmptyOperation();
    NonVisualDataModule->ResetQueueOnceEmptyOperation();

    if ((FQueue != NULL) && !WinConfiguration->EnableQueueByDefault && !QueueChanging)
    {
      FQueue->Enabled = false;
    }

    if ((OnceDoneOperation != odoIdle) && !NonVisualDataModule->Busy)
    {
      Terminal->CloseOnCompletion(OnceDoneOperation, LoadStr(CLOSED_ON_QUEUE_EMPTY));
    }
  }
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TCustomScpExplorerForm::GetQueueProgressTitle()
{
  UnicodeString Result;
  if (FQueueStatus != NULL)
  {
    int ActiveAndPendingCount = FQueueStatus->ActiveAndPendingCount;
    if ((ActiveAndPendingCount == 1) && (FQueueStatus->ActiveCount == 1))
    {
      TFileOperationProgressType * ProgressData =
        FQueueStatus->Items[FQueueStatus->DoneCount]->ProgressData;
      if ((ProgressData != NULL) && ProgressData->InProgress)
      {
        Result = TProgressForm::ProgressStr(ProgressData);
      }
    }
    else if (ActiveAndPendingCount > 1)
    {
      Result = FMTLOAD(PROGRESS_IN_QUEUE, (ActiveAndPendingCount));
    }
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateQueueView()
{
  ComponentVisible[fcQueueView] =
    (WinConfiguration->QueueView.Show == qvShow) ||
    ((WinConfiguration->QueueView.Show == qvHideWhenEmpty) &&
     (FQueueStatus != NULL) && (FQueueStatus->Count > 0));
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::QueueChanged()
{
  if (FQueueStatus != NULL)
  {
    delete FQueueStatus;
    FQueueStatus = NULL;
  }
  UpdateQueueStatus(true);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::QueueListUpdate(TTerminalQueue * Queue)
{
  if (FQueue == Queue)
  {
    FQueueStatusInvalidated = true;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::QueueItemUpdate(TTerminalQueue * Queue,
  TQueueItem * Item)
{
  if (FQueue == Queue)
  {
    TGuard Guard(FQueueStatusSection);

    // this may be running in parallel with QueueChanged
    if (FQueueStatus != NULL)
    {
      TQueueItemProxy * QueueItem = FQueueStatus->FindByQueueItem(Item);

      if ((Item->Status == TQueueItem::qsDone) && (Terminal != NULL))
      {
        FRefreshLocalDirectory = (QueueItem == NULL) ||
          (!QueueItem->Info->ModifiedLocal.IsEmpty());
        FRefreshRemoteDirectory = (QueueItem == NULL) ||
          (!QueueItem->Info->ModifiedRemote.IsEmpty());
      }

      if (QueueItem != NULL)
      {
        QueueItem->UserData = (void*)true;
        FQueueItemInvalidated = true;
      }
    }
  }
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::IsQueueAutoPopup()
{
  // during standalone editing, we have no way to see/control queue,
  // so we have to always popup prompts automatically
  return FStandaloneEditing || GUIConfiguration->QueueAutoPopup;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RefreshQueueItems()
{
  if (FQueueStatus != NULL)
  {
    bool QueueAutoPopup = IsQueueAutoPopup();
    bool NeedRefresh = FQueueController->NeedRefresh();
    bool Refresh = FQueueItemInvalidated || NeedRefresh;
    FQueueItemInvalidated = false;

    int Limit = Refresh ? FQueueStatus->Count : FQueueStatus->DoneAndActiveCount;

    bool Updated = false;
    TQueueItemProxy * QueueItem;
    bool WasUserAction;
    for (int Index = 0; Index < Limit; Index++)
    {
      bool Update = false;
      QueueItem = FQueueStatus->Items[Index];
      WasUserAction = TQueueItem::IsUserActionStatus(QueueItem->Status);
      if (!NonVisualDataModule->Busy && QueueAutoPopup && WasUserAction &&
          (FPendingQueueActionItem == NULL))
      {
        FPendingQueueActionItem = QueueItem;
        FUserActionTimer->Enabled = true;
      }

      if ((bool)QueueItem->UserData)
      {
        QueueItem->UserData = (void*)false;
        QueueItem->Update();
        Updated = true;
        Update = true;
      }
      else if (FQueueController->QueueItemNeedsFrequentRefresh(QueueItem) || NeedRefresh)
      {
        Update = true;
      }

      if (Update)
      {
        FQueueController->RefreshQueueItem(QueueItem);
      }
    }

    if (Updated)
    {
      NonVisualDataModule->UpdateNonVisibleActions();
      SetQueueProgress();
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SetTaskbarListProgressState(TBPFLAG Flags)
{
  FTaskbarList->SetProgressState(GetMainForm()->Handle, Flags);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SetTaskbarListProgressValue(TFileOperationProgressType * ProgressData)
{
  if (ProgressData->Operation != foCalculateSize)
  {
    // implies TBPF_NORMAL
    FTaskbarList->SetProgressValue(GetMainForm()->Handle, ProgressData->OverallProgress(), 100);
  }
  else
  {
    SetTaskbarListProgressState(TBPF_INDETERMINATE);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SetQueueProgress()
{
  TTerminalManager::Instance()->QueueStatusUpdated();

  if ((FTaskbarList != NULL) && (FProgressForm == NULL))
  {
    if ((FQueueStatus != NULL) && (FQueueStatus->ActiveCount > 0))
    {
      if (FQueueStatus->ActiveCount == 1)
      {
        TFileOperationProgressType * ProgressData;
        if ((FQueueStatus->Items[FQueueStatus->DoneCount] != NULL) &&
            ((ProgressData = FQueueStatus->Items[FQueueStatus->DoneCount]->ProgressData) != NULL) &&
            ProgressData->InProgress)
        {
          SetTaskbarListProgressValue(ProgressData);
        }
        else
        {
          SetTaskbarListProgressState(TBPF_NOPROGRESS);
        }
      }
      else
      {
        SetTaskbarListProgressState(TBPF_INDETERMINATE);
      }
    }
    else
    {
      SetTaskbarListProgressState(TBPF_NOPROGRESS);
    }
  }

  UpdateQueueLabel();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateQueueLabel()
{
  UnicodeString Caption = LoadStr(QUEUE_CAPTION);
  if (FQueueStatus != NULL)
  {
    int ActiveAndPendingCount = FQueueStatus->ActiveAndPendingCount;
    if (ActiveAndPendingCount > 0)
    {
      Caption = FORMAT("%s (%d)", (Caption, ActiveAndPendingCount));
    }
  }
  QueueLabel->Caption = Caption;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateTransferList()
{
  TTBXStringList * TransferList = dynamic_cast<TTBXStringList*>(
    static_cast<TComponent*>(GetComponent(fcTransferList)));
  TTBXDropDownItem * TransferDropDown = dynamic_cast<TTBXDropDownItem*>(
    static_cast<TComponent*>(GetComponent(fcTransferDropDown)));
  TransferList->Strings->BeginUpdate();
  try
  {
    TransferList->Strings->Assign(GUIConfiguration->CopyParamList->NameList);
    TransferList->Strings->Insert(0, StripHotkey(LoadStr(COPY_PARAM_DEFAULT)));
    TransferList->ItemIndex = GUIConfiguration->CopyParamIndex + 1;
    if (FTransferDropDownHint.IsEmpty())
    {
      FTransferDropDownHint = TransferDropDown->Hint;
    }
    // this way we get name for "default" settings (COPY_PARAM_DEFAULT)
    UnicodeString Name = TransferList->Strings->Strings[TransferList->ItemIndex];
    TransferDropDown->Text = StripHotkey(Name);
    TransferDropDown->Hint = FORMAT(L"%s|%s:\n%s",
      (FTransferDropDownHint, StripHotkey(Name),
       GUIConfiguration->CurrentCopyParam.GetInfoStr(L"; ",
         FLAGMASK(Terminal != NULL, Terminal->UsableCopyParamAttrs(0).General))));
    // update the label, otherwise when it is updated only on the first draw
    // of the list, it is drawn "bold" for some reason
    FTransferListHoverIndex = TransferList->ItemIndex;
    UpdateTransferLabel();
  }
  __finally
  {
    TransferList->Strings->EndUpdate();
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateCustomCommandsToolbar()
{
  TTBXToolbar * Toolbar = dynamic_cast<TTBXToolbar *>(GetComponent(fcCustomCommandsBand));
  DebugAssert(Toolbar != NULL);

  NonVisualDataModule->UpdateCustomCommandsToolbar(Toolbar);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateActions()
{
  TForm::UpdateActions();

  if (ComponentVisible[fcCustomCommandsBand])
  {
    UpdateCustomCommandsToolbar();
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateSessionsPageControlHeight()
{
  SessionsPageControl->Height = SessionsPageControl->GetTabsHeight();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ConfigurationChanged()
{
  DebugAssert(Configuration && RemoteDirView);
  RemoteDirView->DDAllowMove = WinConfiguration->DDAllowMoveInit;
  RemoteDirView->DimmHiddenFiles = WinConfiguration->DimmHiddenFiles;
  RemoteDirView->ShowHiddenFiles = WinConfiguration->ShowHiddenFiles;
  RemoteDirView->FormatSizeBytes = WinConfiguration->FormatSizeBytes;
  RemoteDirView->ShowInaccesibleDirectories = WinConfiguration->ShowInaccesibleDirectories;

  if (RemoteDirView->RowSelect != WinConfiguration->FullRowSelect)
  {
    RemoteDirView->RowSelect = WinConfiguration->FullRowSelect;
    // selection is not redrawn automatically when RowSelect changes
    RemoteDirView->Invalidate();
  }

  RemoteDriveView->DDAllowMove = WinConfiguration->DDAllowMoveInit;
  RemoteDriveView->DimmHiddenDirs = WinConfiguration->DimmHiddenFiles;
  RemoteDriveView->ShowHiddenDirs = WinConfiguration->ShowHiddenFiles;
  RemoteDriveView->ShowInaccesibleDirectories = WinConfiguration->ShowInaccesibleDirectories;

  UpdateSessionsPageControlHeight();

  SetDockAllowDrag(!WinConfiguration->LockToolbars);
  UpdateToolbarDisplayMode();

  UpdateTransferList();

  if (Terminal != NULL)
  {
    TransferPresetAutoSelect();
  }

  UpdateQueueView();
  UpdateControls();

  // this can be called even before constuctor finishes.
  if (FEditorManager != NULL)
  {
    FEditorManager->ProcessFiles(FileConfigurationChanged, NULL);
  }

  // this can be called even before constuctor finishes.
  if (FLocalEditors != NULL)
  {
    for (int Index = 0; Index < FLocalEditors->Count; Index++)
    {
      ReconfigureEditorForm(static_cast<TForm *>(FLocalEditors->Items[Index]));
    }
  }

  if (ComponentVisible[fcCustomCommandsBand])
  {
    UpdateCustomCommandsToolbar();
  }

  // show only when keeping queue items forever,
  // otherwise, its enough to have in in the context menu
  QueueDeleteAllDoneQueueToolbarItem->Visible =
    WinConfiguration->QueueKeepDoneItems && (WinConfiguration->QueueKeepDoneItemsFor < 0);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::FileConfigurationChanged(
  const UnicodeString FileName, TEditedFileData * /*Data*/, TObject * Token,
  void * /*Arg*/)
{
  if (Token != NULL)
  {
    TForm * Editor = dynamic_cast<TForm*>(Token);
    DebugAssert(Editor != NULL);
    ReconfigureEditorForm(Editor);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::EnableDDTransferConfirmation(TObject * /*Sender*/)
{
  WinConfiguration->DDTransferConfirmation = asOn;
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::CopyParamDialog(
  TTransferDirection Direction, TTransferType Type, bool Temp,
  TStrings * FileList, UnicodeString & TargetDirectory, TGUICopyParamType & CopyParam,
  bool Confirm, bool DragDrop, int Options)
{
  bool Result = true;
  DebugAssert(Terminal && Terminal->Active);
  // Temp means d&d here so far, may change in future!
  if (Temp && (Direction == tdToLocal) && (Type == ttMove) &&
      !WinConfiguration->DDAllowMove)
  {
    TMessageParams Params(mpNeverAskAgainCheck);
    unsigned int Answer = MessageDialog(LoadStr(DND_DOWNLOAD_MOVE_WARNING), qtWarning,
      qaOK | qaCancel, HELP_DND_DOWNLOAD_MOVE_WARNING, &Params);
    if (Answer == qaNeverAskAgain)
    {
      WinConfiguration->DDAllowMove = true;
    }
    else if (Answer == qaCancel)
    {
      Result = false;
    }
  }

  // these parameters are known in advance
  int Params =
    FLAGMASK(Type == ttMove, cpDelete);
  bool ToTemp = (Temp && (Direction == tdToLocal));
  if (Result && Confirm && WinConfiguration->ConfirmTransferring)
  {
    bool DisableNewerOnly =
      (!Terminal->IsCapable[fcNewerOnlyUpload] && (Direction == tdToRemote)) ||
      ToTemp;
    Options |=
      FLAGMASK(ToTemp, coTemp) |
      coDoNotShowAgain;
    TUsableCopyParamAttrs UsableCopyParamAttrs = Terminal->UsableCopyParamAttrs(Params);
    int CopyParamAttrs = (Direction == tdToRemote ?
      UsableCopyParamAttrs.Upload : UsableCopyParamAttrs.Download) |
      FLAGMASK(DisableNewerOnly, cpaNoNewerOnly);
    int OutputOptions =
      FLAGMASK(DragDrop && (WinConfiguration->DDTransferConfirmation == asAuto),
        cooDoNotShowAgain);
    std::unique_ptr<TSessionData> SessionData(SessionDataForCode());
    Result = DoCopyDialog(Direction == tdToRemote, Type == ttMove,
      FileList, TargetDirectory, &CopyParam, Options, CopyParamAttrs, SessionData.get(), &OutputOptions);

    if (Result)
    {
      if (FLAGSET(OutputOptions, cooDoNotShowAgain))
      {
        if (DragDrop)
        {
          if (WinConfiguration->DDTransferConfirmation == asAuto)
          {
            PopupTrayBalloon(NULL, LoadStr(DD_TRANSFER_CONFIRM_OFF2), qtInformation,
              NULL, 0, EnableDDTransferConfirmation, NULL);
          }
          WinConfiguration->DDTransferConfirmation = asOff;
        }
        else
        {
          WinConfiguration->ConfirmTransferring = false;
        }
      }
      else
      {
        // User exclicitly unchecked "do not show again",
        // so show him the dialog the next time
        if (DragDrop && (WinConfiguration->DDTransferConfirmation == asAuto))
        {
          WinConfiguration->DDTransferConfirmation = asOn;
        }
      }
    }
  }

  if (Result && CopyParam.Queue && !ToTemp)
  {

    Configuration->Usage->Inc(L"TransfersOnBackground");

    // these parameter are known only after transfer dialog
    Params |=
      (CopyParam.QueueNoConfirmation ? cpNoConfirmation : 0);

    if (CopyParam.QueueIndividually)
    {
      for (int Index = 0; Index < FileList->Count; Index++)
      {
        TStrings * FileList1 = new TStringList();
        try
        {
          FileList1->AddObject(FileList->Strings[Index], FileList->Objects[Index]);
          AddQueueItem(Queue, Direction, FileList1, TargetDirectory, CopyParam, Params);
        }
        __finally
        {
          delete FileList1;
        }
      }
    }
    else
    {
      AddQueueItem(Queue, Direction, FileList, TargetDirectory, CopyParam, Params);
    }
    Result = false;

    ClearTransferSourceSelection(Direction);
  }

  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ClearTransferSourceSelection(TTransferDirection Direction)
{
  TOperationSide Side = ((Direction == tdToRemote) ? osLocal : osRemote);
  if (HasDirView[Side])
  {
    DirView(Side)->SelectAll(smNone);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::AddQueueItem(
  TTerminalQueue * Queue, TTransferDirection Direction, TStrings * FileList,
  const UnicodeString TargetDirectory, const TCopyParamType & CopyParam,
  int Params)
{
  DebugAssert(Queue != NULL);

  bool SingleFile = false;
  if (FileList->Count == 1)
  {
    if (Direction == tdToRemote)
    {
      UnicodeString FileName = FileList->Strings[0];
      SingleFile = FileExists(ApiPath(FileName));
    }
    else
    {
      TRemoteFile * File = static_cast<TRemoteFile *>(FileList->Objects[0]);
      SingleFile = !File->IsDirectory;
    }
  }

  TQueueItem * QueueItem;
  if (Direction == tdToRemote)
  {
    QueueItem = new TUploadQueueItem(Terminal, FileList, TargetDirectory,
      &CopyParam, Params, SingleFile);
  }
  else
  {
    QueueItem = new TDownloadQueueItem(Terminal, FileList, TargetDirectory,
      &CopyParam, Params, SingleFile);
  }
  AddQueueItem(Queue, QueueItem, Terminal);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::AddQueueItem(TTerminalQueue * Queue, TQueueItem * QueueItem, TTerminal * Terminal)
{
  if (Queue->IsEmpty)
  {
    TManagedTerminal * ManagedTerminal = DebugNotNull(dynamic_cast<TManagedTerminal *>(Terminal));
    ManagedTerminal->QueueOperationStart = Now();
  }
  Queue->AddItem(QueueItem);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RestoreFormParams()
{
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RestoreParams()
{
  DebugAssert(FDocks != NULL);
  for (int Index = 0; Index < ComponentCount; Index++)
  {
    TTBDock * Dock = dynamic_cast<TTBDock *>(Components[Index]);
    if ((Dock != NULL) && (Dock->Tag == 0))
    {
      FDocks->Add(Dock);
    }
  }

  CollectItemsWithTextDisplayMode(this);

  ConfigurationChanged();

  QueuePanel->Height = LoadDimension(WinConfiguration->QueueView.Height, WinConfiguration->QueueView.HeightPixelsPerInch);
  LoadListViewStr(QueueView3, WinConfiguration->QueueView.Layout);
  QueueDock->Visible = WinConfiguration->QueueView.ToolBar;
  QueueLabel->Visible = WinConfiguration->QueueView.Label;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::StoreParams()
{
  WinConfiguration->QueueView.Height = QueuePanel->Height;
  WinConfiguration->QueueView.HeightPixelsPerInch = Screen->PixelsPerInch;
  WinConfiguration->QueueView.Layout = GetListViewStr(QueueView3);
  WinConfiguration->QueueView.ToolBar = QueueDock->Visible;
  WinConfiguration->QueueView.Label = QueueLabel->Visible;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::CreateParams(TCreateParams & Params)
{
  if (!FFormRestored)
  {
    FFormRestored = true;
    RestoreFormParams();
  }
  TForm::CreateParams(Params);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SetDockAllowDrag(bool value)
{
  DebugAssert(FDocks != NULL);
  for (int Index = 0; Index < FDocks->Count; Index++)
  {
    static_cast<TTBDock*>(FDocks->Items[Index])->AllowDrag = value;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::LoadToolbarsLayoutStr(UnicodeString LayoutStr)
{
  SetDockAllowDrag(true);
  ::LoadToolbarsLayoutStr(this, LayoutStr);
  SetDockAllowDrag(!WinConfiguration->LockToolbars);
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TCustomScpExplorerForm::GetToolbarsLayoutStr()
{
  UnicodeString Result;
  SetDockAllowDrag(true);
  Result = ::GetToolbarsLayoutStr(this);
  SetDockAllowDrag(!WinConfiguration->LockToolbars);
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::FileOperationProgress(
  TFileOperationProgressType & ProgressData)
{
  // Download to temporary local directory
  if (FPendingTempSpaceWarn && ProgressData.InProgress && ProgressData.TotalSizeSet)
  {
    bool Continue = true;
    FPendingTempSpaceWarn = false;
    DoWarnLackOfTempSpace(ProgressData.Directory, ProgressData.TotalSize, Continue);
    if (!Continue)
    {
      Abort();
    }
  }

  // operation is being executed and we still didn't show up progress form
  if (ProgressData.InProgress && !FProgressForm)
  {
    FProgressForm = new TProgressForm(Application, (FTransferResumeList != NULL));
    // As progress window has delayed show now, we need to lock ourselves,
    // (at least) until then
    LockWindow();
    FProgressForm->DeleteToRecycleBin = (ProgressData.Side == osLocal ?
      (WinConfiguration->DeleteToRecycleBin != FAlternativeDelete) :
      ((Terminal->SessionData->DeleteToRecycleBin != FAlternativeDelete) &&
       !Terminal->SessionData->RecycleBinPath.IsEmpty()));
    // When main window is hidden, synchronisation form does not exist,
    // we suppose "/upload" or URL download mode
    if (!Visible && (ProgressData.Operation != foCalculateSize) &&
        (ProgressData.Operation != foCalculateChecksum) &&
        (FSynchronizeProgressForm == NULL) &&
        !FStandaloneEditing)
    {
      FProgressForm->OnceDoneOperation = odoDisconnect;
    }

    if (FTaskbarList != NULL)
    {
      // Actually, do not know what hides the progress once the operation finishes
      SetTaskbarListProgressState(TBPF_NORMAL);
    }
  }
  // operation is finished (or terminated), so we hide progress form
  else if (!ProgressData.InProgress && FProgressForm)
  {
    UnlockWindow();
    SAFE_DESTROY(FProgressForm);

    SetQueueProgress();

    if (ProgressData.Operation == foCalculateSize)
    {
      // When calculating size before transfer, the abort caused by
      // cancel flag set due to "MoveToQueue" below,
      // is not propagated back to us in ExecuteFileOperation,
      // as it is silently swallowed.
      // So we explicitly (re)throw it here.
      if (FMoveToQueue)
      {
        Abort();
      }
    }
    else
    {
      if ((ProgressData.Cancel == csContinue) &&
          !FAutoOperation)
      {
        OperationComplete(ProgressData.StartTime);
      }
    }
  }

  if (FProgressForm)
  {
    FProgressForm->SetProgressData(ProgressData);

    if (FTaskbarList != NULL)
    {
      DebugAssert(ProgressData.InProgress);
      SetTaskbarListProgressValue(&ProgressData);
    }

    if (FProgressForm->Cancel > csContinue)
    {
      if (FProgressForm->Cancel > ProgressData.Cancel)
      {
        ProgressData.Cancel = FProgressForm->Cancel;
      }
      // cancel cancels even the move
      FMoveToQueue = false;
    }
    else if (FProgressForm->MoveToQueue)
    {
      FMoveToQueue = true;
      if (ProgressData.Cancel < csCancel)
      {
        ProgressData.Cancel = csCancel;
      }
    }

    if ((FTransferResumeList != NULL) &&
        ProgressData.InProgress &&
        ((ProgressData.Operation == foCopy) || (ProgressData.Operation == foMove)) &&
        !ProgressData.FullFileName.IsEmpty())
    {
      if ((FTransferResumeList->Count == 0) ||
          (FTransferResumeList->Strings[FTransferResumeList->Count - 1] != ProgressData.FullFileName))
      {
        // note that we do not recognize directories from files here
        FTransferResumeList->Add(ProgressData.FullFileName);
      }
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::OperationComplete(
  const TDateTime & StartTime)
{
  if (GUIConfiguration->BeepOnFinish &&
      (Now() - StartTime > GUIConfiguration->BeepOnFinishAfter))
  {
    UnicodeString BeepSound = GUIConfiguration->BeepSound;
    DWORD Sound;
    if (!ExtractFileExt(BeepSound).IsEmpty())
    {
      Sound = SND_FILENAME;
    }
    else
    {
      Sound = SND_ALIAS;
    }
    PlaySound(BeepSound.c_str(), NULL, Sound | SND_ASYNC);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::OperationProgress(
  TFileOperationProgressType & ProgressData)
{
  FileOperationProgress(ProgressData);
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::PanelOperation(TOperationSide /*Side*/,
  bool DragDrop)
{
  return (!DragDrop && (DropSourceControl == NULL)) ||
    (DropSourceControl == RemoteDirView);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DoOperationFinished(
  TFileOperation Operation, TOperationSide Side,
  bool /*Temp*/, const UnicodeString & FileName, bool Success,
  TOnceDoneOperation & OnceDoneOperation)
{
  if (!FAutoOperation)
  {
    // no selection on "/upload", form servers only as event handler
    // (it is not displayed)
    if (PanelOperation(Side, FDragDropOperation) &&
        Visible && (Operation != foCalculateSize) &&
        (Operation != foGetProperties) && (Operation != foCalculateChecksum))
    {
      TCustomDirView * DView = DirView(Side);
      UnicodeString FileNameOnly = ExtractFileName(FileName, (Side == osRemote));
      TListItem *Item = DView->FindFileItem(FileNameOnly);
      // this can happen when local drive is unplugged in the middle of the operation
      if (Item != NULL)
      {
        if (Success) Item->Selected = false;
        if (DView->ViewStyle == vsReport)
        {
          TRect DisplayRect = Item->DisplayRect(drBounds);
          if (DisplayRect.Bottom > DView->ClientHeight)
          {
            DView->Scroll(0, Item->Top - DView->TopItem->Top);
          }
        }
        Item->MakeVisible(false);
      }
    }

    if ((Operation == foCopy) || (Operation == foMove))
    {
      if (Side == osLocal)
      {
        Configuration->Usage->Inc(L"UploadedFiles");
      }
      else
      {
        Configuration->Usage->Inc(L"DownloadedFiles");
      }
    }
  }

  if (Success && (FSynchronizeController != NULL))
  {
    if (Operation == foCopy)
    {
      DebugAssert(Side == osLocal);
      FSynchronizeController->LogOperation(soUpload, FileName);
    }
    else if (Operation == foDelete)
    {
      DebugAssert(Side == osRemote);
      FSynchronizeController->LogOperation(soDelete, FileName);
    }
  }

  if (FProgressForm)
  {
    OnceDoneOperation = FProgressForm->OnceDoneOperation;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::OperationFinished(
  TFileOperation Operation, TOperationSide Side,
  bool Temp, const UnicodeString & FileName, Boolean Success,
  TOnceDoneOperation & OnceDoneOperation)
{
  DoOperationFinished(Operation, Side, Temp, FileName, Success,
    OnceDoneOperation);
}
//---------------------------------------------------------------------------
TCustomDirView * __fastcall TCustomScpExplorerForm::DirView(TOperationSide Side)
{
  DebugAssert(GetSide(Side) == osRemote);
  DebugUsedParam(Side);
  return RemoteDirView;
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::DirViewEnabled(TOperationSide Side)
{
  DebugAssert(GetSide(Side) == osRemote);
  DebugUsedParam(Side);
  return (Terminal != NULL);
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::GetEnableFocusedOperation(
  TOperationSide Side, int FilesOnly)
{
  return DirView(Side)->AnyFileSelected(true, (FilesOnly != 0), true);
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::GetEnableSelectedOperation(
  TOperationSide Side, int FilesOnly)
{
  return DirView(Side)->AnyFileSelected(false, (FilesOnly != 0), true);
}
//---------------------------------------------------------------------------
struct THistoryItemData
{
  short int Side;
  short int Index;
};
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::HistoryGo(TOperationSide Side, int Index)
{
  DirView(Side)->HistoryGo(Index);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::HistoryItemClick(System::TObject* Sender)
{
  TTBCustomItem * Item = dynamic_cast<TTBCustomItem *>(Sender);
  THistoryItemData Data = *reinterpret_cast<THistoryItemData*>(&(Item->Tag));
  HistoryGo((TOperationSide)Data.Side, Data.Index);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateHistoryMenu(TOperationSide Side,
  bool Back)
{
  if (FHistoryMenu[Side == osLocal][Back] != NULL)
  {
    TCustomDirView * DView = DirView(Side);
    TTBXPopupMenu * Menu = FHistoryMenu[Side == osLocal][Back];

    int ICount = Back ? DView->BackCount : DView->ForwardCount;
    if (ICount > 10)
    {
      ICount = 10;
    }
    Menu->Items->Clear();
    THistoryItemData Data;
    Data.Side = Side;
    for (short int i = 1; i <= ICount; i++)
    {
      TTBCustomItem * Item = new TTBXItem(Menu);
      Data.Index = static_cast<short int>(i * (Back ? -1 : 1));
      Item->Caption = MinimizeName(DView->HistoryPath[Data.Index], 50, (Side == osRemote));
      Item->Hint = DView->HistoryPath[Data.Index];
      DebugAssert(sizeof(int) == sizeof(THistoryItemData));
      Item->Tag = *reinterpret_cast<int*>(&Data);
      Item->OnClick = HistoryItemClick;
      Menu->Items->Add(Item);
    }
  }
}
//---------------------------------------------------------------------------
TTBXPopupMenu * __fastcall TCustomScpExplorerForm::HistoryMenu(
  TOperationSide Side, bool Back)
{
  if (FHistoryMenu[Side == osLocal][Back] == NULL)
  {
    // workaround
    // In Pascal the size of TTBXPopupMenu is 132, in C++ 136,
    // operator new allocates memory in Pascal code, but calls inline
    // contructor in C++, leading in problems, the function does
    // both in Pascal code
    FHistoryMenu[Side == osLocal][Back] = CreateTBXPopupMenu(this);
    UpdateHistoryMenu(Side, Back);
  }
  return FHistoryMenu[Side == osLocal][Back];
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DirViewHistoryChange(
      TCustomDirView *Sender)
{
  TOperationSide Side = (Sender == DirView(osRemote) ? osRemote : osLocal);
  UpdateHistoryMenu(Side, true);
  UpdateHistoryMenu(Side, false);
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::CustomCommandRemoteAllowed()
{
  // remote custom commands can be executed only if the server supports shell commands
  // or have secondary shell
  return (FTerminal != NULL) && (FTerminal->IsCapable[fcSecondaryShell] || FTerminal->IsCapable[fcShellAnyCommand]);
}
//---------------------------------------------------------------------------
int __fastcall TCustomScpExplorerForm::CustomCommandState(
  const TCustomCommandType & Command, bool OnFocused, TCustomCommandListType ListType)
{
  int Result;

  TFileCustomCommand RemoteCustomCommand;
  TLocalCustomCommand LocalCustomCommand;
  TFileCustomCommand * NonInteractiveCustomCommand =
    FLAGCLEAR(Command.Params, ccLocal) ? &RemoteCustomCommand : &LocalCustomCommand;
  TInteractiveCustomCommand InteractiveCustomCommand(NonInteractiveCustomCommand);
  UnicodeString Cmd = InteractiveCustomCommand.Complete(Command.Command, false);

  if (FLAGCLEAR(Command.Params, ccLocal))
  {
    int AllowedState = CustomCommandRemoteAllowed() ? 1 : 0;
    // custom command that does not operate with files can be executed anytime ...
    if (!NonInteractiveCustomCommand->IsFileCommand(Cmd))
    {
      if ((ListType == ccltAll) || (ListType == ccltNonFile))
      {
        // ... but do not show such command in remote file menu (TODO)
        Result = AllowedState;
      }
      else
      {
        Result = -1;
      }
    }
    else
    {
      if ((ListType == ccltAll) || (ListType == ccltFile))
      {
        Result = ((FCurrentSide == osRemote) && DirView(osRemote)->AnyFileSelected(OnFocused, false, true)) ? AllowedState : 0;
      }
      else
      {
        Result = -1;
      }
    }
  }
  else
  {
    // custom command that does not operate with files can be executed anytime
    if (!NonInteractiveCustomCommand->IsFileCommand(Cmd))
    {
      Result = ((ListType == ccltAll) || (ListType == ccltNonFile)) ? 1 : -1;
    }
    else if (LocalCustomCommand.HasLocalFileName(Cmd))
    {
      if ((ListType == ccltAll) || (ListType == ccltFile))
      {
        // special case is "diff"-style command that can be executed over any side,
        // if we have both sides
        Result =
          // Cannot have focus on both panels, so we have to call AnyFileSelected
          // directly (instead of EnableSelectedOperation) to pass
          // false to FocusedFileOnlyWhenFocused when panel is inactive.
          ((HasDirView[osLocal] && DirView(osLocal)->AnyFileSelected(false, false, (FCurrentSide == osLocal))) &&
            DirView(osRemote)->AnyFileSelected(false, false, (FCurrentSide == osRemote))) ? 1 : 0;
      }
      else if (ListType == ccltBoth)
      {
        Result = 1;
      }
      else
      {
        Result = -1;
      }
    }
    else
    {
      if ((ListType == ccltAll) || (ListType == ccltFile))
      {
        Result = DirView(FCurrentSide)->AnyFileSelected(OnFocused, false, true) ? 1 : 0;
      }
      else
      {
        Result = -1;
      }
    }
  }

  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::CustomCommand(TStrings * FileList,
  const TCustomCommandType & ACommand, TStrings * ALocalFileList)
{

  TCustomCommandData Data(Terminal);
  UnicodeString Site = Terminal->SessionData->SessionKey;
  std::unique_ptr<TCustomCommand> CustomCommandForOptions;
  if (FLAGCLEAR(ACommand.Params, ccLocal))
  {
    CustomCommandForOptions.reset(new TRemoteCustomCommand(Data, Terminal->CurrentDirectory));
  }
  else
  {
    CustomCommandForOptions.reset(new TLocalCustomCommand(Data, Terminal->CurrentDirectory, DefaultDownloadTargetDirectory()));
  }

  std::unique_ptr<TStrings> CustomCommandOptions(CloneStrings(WinConfiguration->CustomCommandOptions));
  if (ACommand.AnyOptionWithFlag(TCustomCommandType::ofRun))
  {
    if (!DoCustomCommandOptionsDialog(
          &ACommand, CustomCommandOptions.get(), TCustomCommandType::ofRun, CustomCommandForOptions.get(), Site))
    {
      Abort();
    }
  }

  UnicodeString CommandCommand = ACommand.GetCommandWithExpandedOptions(CustomCommandOptions.get(), Site);

  if (FLAGCLEAR(ACommand.Params, ccLocal))
  {
    if (EnsureCommandSessionFallback(fcShellAnyCommand))
    {
      TRemoteCustomCommand RemoteCustomCommand(Data, Terminal->CurrentDirectory);
      TWinInteractiveCustomCommand InteractiveCustomCommand(
        &RemoteCustomCommand, ACommand.Name, ACommand.HomePage);

      UnicodeString Command = InteractiveCustomCommand.Complete(CommandCommand, false);

      Configuration->Usage->Inc(L"RemoteCustomCommandRuns2");

      bool Capture = FLAGSET(ACommand.Params, ccShowResults) || FLAGSET(ACommand.Params, ccCopyResults);
      TCaptureOutputEvent OutputEvent = NULL;

      DebugAssert(FCapturedLog == NULL);
      if (Capture)
      {
        FCapturedLog = new TStringList();
        OutputEvent = TerminalCaptureLog;
      }

      try
      {
        if (!RemoteCustomCommand.IsFileCommand(Command))
        {
          Terminal->AnyCommand(RemoteCustomCommand.Complete(Command, true),
            OutputEvent);
        }
        else
        {
          Terminal->CustomCommandOnFiles(Command, ACommand.Params, FileList, OutputEvent);
        }

        if ((FCapturedLog != NULL) && (FCapturedLog->Count > 0))
        {
          if (FLAGSET(ACommand.Params, ccCopyResults))
          {
            CopyToClipboard(FCapturedLog);
          }

          if (FLAGSET(ACommand.Params, ccShowResults))
          {
            DoConsoleDialog(Terminal, L"", FCapturedLog);
          }
        }
      }
      __finally
      {
        SAFE_DESTROY(FCapturedLog);
      }
    }
  }
  else
  {
    TLocalCustomCommand LocalCustomCommand(Data, Terminal->CurrentDirectory, DefaultDownloadTargetDirectory());
    TWinInteractiveCustomCommand InteractiveCustomCommand(
      &LocalCustomCommand, ACommand.Name, ACommand.HomePage);

    UnicodeString Command = InteractiveCustomCommand.Complete(CommandCommand, false);

    bool FileListCommand = LocalCustomCommand.IsFileListCommand(Command);
    bool LocalFileCommand = LocalCustomCommand.HasLocalFileName(Command);

    Configuration->Usage->Inc(L"LocalCustomCommandRuns2");

    if (!LocalCustomCommand.IsFileCommand(Command))
    {
      ExecuteShell(LocalCustomCommand.Complete(Command, true));
    }
    // remote files?
    else if ((FCurrentSide == osRemote) || LocalFileCommand)
    {
      TStrings * LocalFileList = NULL;
      TStrings * RemoteFileList = NULL;
      try
      {
        if (LocalFileCommand)
        {
          if (ALocalFileList == NULL)
          {
            DebugAssert(HasDirView[osLocal]);
            // Cannot have focus on both panels, so we have to call AnyFileSelected
            // directly (instead of EnableSelectedOperation) to pass
            // false to FocusedFileOnlyWhenFocused
            DebugAssert(DirView(osLocal)->AnyFileSelected(false, false, false));
            LocalFileList = DirView(osLocal)->CreateFileList(false, true, NULL);
          }
          else
          {
            LocalFileList = ALocalFileList;
          }

          if (FileListCommand)
          {
            if (LocalFileList->Count != 1)
            {
              throw Exception(LoadStr(CUSTOM_COMMAND_SELECTED_UNMATCH1));
            }
          }
          else
          {
            if ((LocalFileList->Count != 1) &&
                (FileList->Count != 1) &&
                (LocalFileList->Count != FileList->Count))
            {
              throw Exception(LoadStr(CUSTOM_COMMAND_SELECTED_UNMATCH));
            }
          }
        }

        UnicodeString RootTempDir;
        UnicodeString TempDir;

        bool RemoteFiles = FLAGSET(ACommand.Params, ccRemoteFiles);
        if (!RemoteFiles)
        {
          TemporarilyDownloadFiles(FileList, false, RootTempDir, TempDir, false, false, true);
        }

        try
        {
          TDateTimes RemoteFileTimes;

          if (RemoteFiles)
          {
            RemoteFileList = FileList;
          }
          else
          {
            RemoteFileList = new TStringList();

            TMakeLocalFileListParams MakeFileListParam;
            MakeFileListParam.FileList = RemoteFileList;
            MakeFileListParam.FileTimes = &RemoteFileTimes;
            MakeFileListParam.IncludeDirs = FLAGSET(ACommand.Params, ccApplyToDirectories);
            MakeFileListParam.Recursive =
              FLAGSET(ACommand.Params, ccRecursive) && !FileListCommand;

            ProcessLocalDirectory(TempDir, Terminal->MakeLocalFileList, &MakeFileListParam);
          }

          bool NonBlocking = FileListCommand && RemoteFiles;

          TFileOperationProgressType Progress(&OperationProgress, &OperationFinished);

          if (!NonBlocking)
          {
            Progress.Start(foCustomCommand, osRemote, FileListCommand ? 1 : FileList->Count);
            DebugAssert(FProgressForm != NULL);
            FProgressForm->ReadOnly = true;
          }

          try
          {
            if (FileListCommand)
            {
              UnicodeString LocalFile;
              // MakeFileList does not delimit filenames
              UnicodeString FileList = MakeFileList(RemoteFileList);

              if (LocalFileCommand)
              {
                DebugAssert(LocalFileList->Count == 1);
                LocalFile = LocalFileList->Strings[0];
              }

              TLocalCustomCommand CustomCommand(Data,
                Terminal->CurrentDirectory, DefaultDownloadTargetDirectory(), L"", LocalFile, FileList);
              UnicodeString ShellCommand = CustomCommand.Complete(Command, true);

              if (NonBlocking)
              {
                ExecuteShell(ShellCommand);
              }
              else
              {
                ExecuteShellAndWait(ShellCommand);
              }
            }
            else if (LocalFileCommand)
            {
              if (LocalFileList->Count == 1)
              {
                UnicodeString LocalFile = LocalFileList->Strings[0];

                for (int Index = 0; Index < RemoteFileList->Count; Index++)
                {
                  UnicodeString FileName = RemoteFileList->Strings[Index];
                  TLocalCustomCommand CustomCommand(Data,
                    Terminal->CurrentDirectory, DefaultDownloadTargetDirectory(), FileName, LocalFile, L"");
                  ExecuteShellAndWait(CustomCommand.Complete(Command, true));
                }
              }
              else if (RemoteFileList->Count == 1)
              {
                UnicodeString FileName = RemoteFileList->Strings[0];

                for (int Index = 0; Index < LocalFileList->Count; Index++)
                {
                  TLocalCustomCommand CustomCommand(
                    Data, Terminal->CurrentDirectory, DefaultDownloadTargetDirectory(),
                    FileName, LocalFileList->Strings[Index], L"");
                  ExecuteShellAndWait(CustomCommand.Complete(Command, true));
                }
              }
              else
              {
                if (LocalFileList->Count != RemoteFileList->Count)
                {
                  throw Exception(LoadStr(CUSTOM_COMMAND_PAIRS_DOWNLOAD_FAILED));
                }

                for (int Index = 0; Index < LocalFileList->Count; Index++)
                {
                  UnicodeString FileName = RemoteFileList->Strings[Index];
                  TLocalCustomCommand CustomCommand(
                    Data, Terminal->CurrentDirectory, DefaultDownloadTargetDirectory(),
                    FileName, LocalFileList->Strings[Index], L"");
                  ExecuteShellAndWait(CustomCommand.Complete(Command, true));
                }
              }
            }
            else
            {
              for (int Index = 0; Index < RemoteFileList->Count; Index++)
              {
                TLocalCustomCommand CustomCommand(Data,
                  Terminal->CurrentDirectory, DefaultDownloadTargetDirectory(),
                  RemoteFileList->Strings[Index], L"", L"");
                ExecuteShellAndWait(CustomCommand.Complete(Command, true));
              }
            }
          }
          __finally
          {
            if (!NonBlocking)
            {
              Progress.Stop();
            }
          }

          DebugAssert(!FAutoOperation);

          if (!RemoteFiles)
          {
            TempDir = IncludeTrailingBackslash(TempDir);
            for (int Index = 0; Index < RemoteFileList->Count; Index++)
            {
              UnicodeString FileName = RemoteFileList->Strings[Index];
              if (DebugAlwaysTrue(SameText(TempDir, FileName.SubString(1, TempDir.Length()))) &&
                  // Skip directories for now, they require recursion,
                  // and we do not have original nested files times available here yet.
                  // The check is redundant as FileAge fails for directories anyway.
                  !DirectoryExists(FileName))
              {
                UnicodeString RemoteDir =
                  UnixExtractFileDir(
                    UnixIncludeTrailingBackslash(FTerminal->CurrentDirectory) +
                    ToUnixPath(FileName.SubString(TempDir.Length() + 1, FileName.Length() - TempDir.Length())));

                TDateTime NewTime;
                if (FileAge(FileName, NewTime) &&
                    (NewTime != RemoteFileTimes[Index]))
                {
                  TGUICopyParamType CopyParam = GUIConfiguration->CurrentCopyParam;
                  TemporaryFileCopyParam(CopyParam);
                  CopyParam.FileMask = L"";

                  FAutoOperation = true;
                  std::unique_ptr<TStrings> TemporaryFilesList(new TStringList());
                  TemporaryFilesList->Add(FileName);

                  FTerminal->CopyToRemote(TemporaryFilesList.get(), RemoteDir, &CopyParam, cpTemporary);
                }
              }
            }
          }
        }
        __finally
        {
          FAutoOperation = false;
          if (!RootTempDir.IsEmpty() && DebugAlwaysTrue(!RemoteFiles))
          {
            RecursiveDeleteFile(ExcludeTrailingBackslash(RootTempDir), false);
          }
        }
      }
      __finally
      {
        if (RemoteFileList != FileList)
        {
          delete RemoteFileList;
        }
        if (LocalFileList != ALocalFileList)
        {
          delete LocalFileList;
        }
      }
    }
    // local files
    else
    {
      std::unique_ptr<TStrings> SelectedFileList(DirView(osLocal)->CreateFileList(false, true, NULL));

      std::unique_ptr<TStrings> LocalFileList(new TStringList());

      for (int Index = 0; Index < SelectedFileList->Count; Index++)
      {
        UnicodeString FileName = SelectedFileList->Strings[Index];
        if (DirectoryExists(FileName))
        {
          if (FLAGSET(ACommand.Params, ccApplyToDirectories))
          {
            LocalFileList->Add(FileName);
          }

          if (FLAGSET(ACommand.Params, ccRecursive))
          {
            TMakeLocalFileListParams MakeFileListParam;
            MakeFileListParam.FileList = LocalFileList.get();
            MakeFileListParam.FileTimes = NULL;
            MakeFileListParam.IncludeDirs = FLAGSET(ACommand.Params, ccApplyToDirectories);
            MakeFileListParam.Recursive = true;

            ProcessLocalDirectory(FileName, Terminal->MakeLocalFileList, &MakeFileListParam);
          }
        }
        else
        {
          LocalFileList->Add(FileName);
        }
      }

      if (FileListCommand)
      {
        UnicodeString FileList = MakeFileList(LocalFileList.get());
        TLocalCustomCommand CustomCommand(
          Data, Terminal->CurrentDirectory, DefaultDownloadTargetDirectory(),
          L"", L"", FileList);
        ExecuteShell(CustomCommand.Complete(Command, true));
      }
      else
      {
        TFileOperationProgressType Progress(&OperationProgress, &OperationFinished);

        Progress.Start(foCustomCommand, osRemote, FileListCommand ? 1 : LocalFileList->Count);
        DebugAssert(FProgressForm != NULL);
        FProgressForm->ReadOnly = true;

        try
        {
          for (int Index = 0; Index < LocalFileList->Count; Index++)
          {
            UnicodeString FileName = LocalFileList->Strings[Index];
            TLocalCustomCommand CustomCommand(
              Data, Terminal->CurrentDirectory, DefaultDownloadTargetDirectory(),
              FileName, L"", L"");
            ExecuteShellAndWait(CustomCommand.Complete(Command, true));
          }
        }
        __finally
        {
          Progress.Stop();
        }
      }
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::BothCustomCommand(
  const TCustomCommandType & Command)
{
  DebugAssert(FCustomCommandLocalFileList != NULL);
  DebugAssert(FCustomCommandRemoteFileList != NULL);
  DebugAssert(FCustomCommandLocalFileList->Count == FCustomCommandRemoteFileList->Count);

  TStrings * LocalFileList = new TStringList();
  TStrings * RemoteFileList = new TStringList();
  try
  {
    for (int Index = 0; Index < FCustomCommandLocalFileList->Count; Index++)
    {
      LocalFileList->Clear();
      LocalFileList->AddObject(
        FCustomCommandLocalFileList->Strings[Index],
        FCustomCommandLocalFileList->Objects[Index]);
      RemoteFileList->Clear();
      RemoteFileList->AddObject(
        FCustomCommandRemoteFileList->Strings[Index],
        FCustomCommandRemoteFileList->Objects[Index]);

      CustomCommand(RemoteFileList, Command, LocalFileList);
    }
  }
  __finally
  {
    delete LocalFileList;
    delete RemoteFileList;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::CustomCommandMenu(
  TAction * Action, TStrings * LocalFileList, TStrings * RemoteFileList)
{
  delete FCustomCommandLocalFileList;
  delete FCustomCommandRemoteFileList;
  // takeover ownership,
  // the lists must survive the MenuPopup as OnClick occurs only after it exits
  FCustomCommandLocalFileList = LocalFileList;
  FCustomCommandRemoteFileList = RemoteFileList;

  TButton * Button = dynamic_cast<TButton *>(Action->ActionComponent);
  if (Button != NULL)
  {
    FCustomCommandMenu->Items->Clear();

    NonVisualDataModule->CreateCustomCommandsMenu(FCustomCommandMenu->Items, false, false, ccltBoth);
    MenuPopup(FCustomCommandMenu, Button);
  }
  else
  {
    NonVisualDataModule->CreateCustomCommandsMenu(Action, false, ccltBoth);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::TerminalCaptureLog(
  const UnicodeString & AddedLine, TCaptureOutputType OutputType)
{
  DebugAssert(FCapturedLog != NULL);
  if ((OutputType == cotOutput) || (OutputType == cotError))
  {
    FCapturedLog->Add(AddedLine);
  }
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::IsFileControl(TObject * Control,
  TOperationSide Side)
{
  return (Side == osRemote) &&
    ((Control == RemoteDirView) || (Control == RemoteDriveView));
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DirViewContextPopupDefaultItem(
  TOperationSide Side, TTBXCustomItem * Item, TDoubleClickAction DoubleClickAction)
{
  TTBItemOptions O;
  O = Item->Options;
  TCustomDirView * DView = DirView(Side);
  if ((DView->ItemFocused != NULL) &&
      (WinConfiguration->DoubleClickAction == DoubleClickAction) &&
      // when resolving links is disabled, default action is to enter the directory,
      // no matter what DoubleClickAction is configured to
      ((Side != osRemote) || Terminal->ResolvingSymlinks) &&
      // Can only Edit files, but can Open/Copy even directories
      ((DoubleClickAction != dcaEdit) ||
       !DView->ItemIsDirectory(DView->ItemFocused)))
  {
    Item->Options = O << tboDefault;
  }
  else
  {
    Item->Options = O >> tboDefault;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DirViewContextPopup(
  TOperationSide Side, Byte PopupComponent, const TPoint &MousePos)
{
  TCustomDirView * DView = DirView(Side);
  TListItem * Item = DView->ItemFocused;
  if ((DView->GetItemAt(MousePos.x, MousePos.y) == Item) &&
      EnableFocusedOperation[Side])
  {
    TPoint ClientPoint;
    ClientPoint = ((MousePos.x < 0) && (MousePos.y < 0)) ?
      TPoint(0, 0) : MousePos;
    FLastContextPopupScreenPoint = DView->ClientToScreen(ClientPoint);

    reinterpret_cast<TPopupMenu*>(GetComponent(PopupComponent))->Popup(
      FLastContextPopupScreenPoint.x, FLastContextPopupScreenPoint.y);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemoteDirViewContextPopup(
      TObject * /*Sender*/, const TPoint &MousePos, bool &Handled)
{
  DirViewContextPopupDefaultItem(osRemote, NonVisualDataModule->RemoteOpenMenuItem, dcaOpen);
  DirViewContextPopupDefaultItem(osRemote, NonVisualDataModule->RemoteEditMenuItem, dcaEdit);
  DirViewContextPopupDefaultItem(osRemote, NonVisualDataModule->RemoteCopyMenuItem, dcaCopy);

  DirViewContextPopup(osRemote, fcRemotePopup, MousePos);
  Handled = true;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ReloadLocalDirectory(const UnicodeString Directory)
{
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::BatchStart(void *& /*Storage*/)
{
  DebugAssert(FErrorList == NULL);
  if (WinConfiguration->ContinueOnError)
  {
    FErrorList = new TStringList();
    Configuration->Usage->Inc(L"ContinuationsOnError");
  }
  NonVisualDataModule->StartBusy();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::BatchEnd(void * /*Storage*/)
{
  NonVisualDataModule->EndBusy();
  if (FErrorList)
  {
    HandleErrorList(FErrorList);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateCopyParamCounters(
  const TCopyParamType & CopyParam)
{
  if (!CopyParam.IncludeFileMask.Masks.IsEmpty())
  {
    Configuration->Usage->Inc(L"FileMaskUses");
  }
  if (IsEffectiveFileNameMask(CopyParam.FileMask))
  {
    Configuration->Usage->Inc(L"OperationMaskUses");
  }
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::ExecuteFileOperation(TFileOperation Operation,
  TOperationSide Side, TStrings * FileList, bool NoConfirmation, void * Param)
{
  void * BatchStorage;
  BatchStart(BatchStorage);
  bool Result;
  try
  {
    if ((Operation == foCopy) || (Operation == foMove))
    {
      TTransferDirection Direction = (Side == osLocal ? tdToRemote : tdToLocal);
      TTransferType Type = (Operation == foCopy ? ttCopy : ttMove);
      UnicodeString TargetDirectory;
      bool Temp = false;
      bool DragDrop = false;
      int Options = 0;
      TAutoSwitch UseQueue = asAuto;
      if (Param != NULL)
      {
        TTransferOperationParam& TParam =
          *static_cast<TTransferOperationParam*>(Param);
        TargetDirectory = TParam.TargetDirectory;
        Temp = TParam.Temp;
        DragDrop = TParam.DragDrop;
        Options = TParam.Options;
        UseQueue = TParam.Queue;
      }
      TGUICopyParamType CopyParam = GUIConfiguration->CurrentCopyParam;
      switch (UseQueue)
      {
        case asOn:
          CopyParam.Queue = true;
          break;

        case asOff:
          CopyParam.Queue = false;
          break;

        case asAuto:
        default:
          // keep default
          break;
      }
      Result =
        CopyParamDialog(Direction, Type, Temp, FileList, TargetDirectory,
          CopyParam, !NoConfirmation, DragDrop, Options);
      if (Result)
      {
        DebugAssert(Terminal);
        bool SelectionRestored = false;
        TCustomDirView * DView = NULL;
        if (HasDirView[Side])
        {
          DView = DirView(Side);
          DView->SaveSelection();
          DView->SaveSelectedNames();
        }

        UpdateCopyParamCounters(CopyParam);

        std::unique_ptr<TStringList> TransferResumeList(new TStringList());
        DebugAssert(FTransferResumeList == NULL);
        FTransferResumeList =
          Terminal->IsCapable[fcMoveToQueue] ? TransferResumeList.get() : NULL;
        FMoveToQueue = false;

        int Params = FLAGMASK(Operation == foMove, cpDelete);

        try
        {
          TStrings * PermanentFileList;
          std::unique_ptr<TStrings> PermanentFileListOwner;

          try
          {
            if (Side == osLocal)
            {
              PermanentFileList = FileList;

              Params |= FLAGMASK(Temp, cpTemporary);
              Terminal->CopyToRemote(FileList, TargetDirectory, &CopyParam, Params);
              if (Operation == foMove)
              {
                ReloadLocalDirectory();
                if (DView != NULL)
                {
                  DView->RestoreSelection();
                }
                SelectionRestored = true;
              }
            }
            else
            {
              // Clone the file list as it may refer to current directory files,
              // which get destroyed, when the source directory is reloaded after foMove operation.
              // We should actually clone the file list for whole ExecuteFileOperation to protect against reloads.
              // But for a hotfix, we are not going to do such a big change.
              PermanentFileListOwner.reset(TRemoteFileList::CloneStrings(FileList));
              PermanentFileList = PermanentFileListOwner.get();

              try
              {
                Terminal->CopyToLocal(FileList, TargetDirectory, &CopyParam,
                  Params);
              }
              __finally
              {
                if (Operation == foMove)
                {
                  if (DView != NULL)
                  {
                    DView->RestoreSelection();
                  }
                  SelectionRestored = true;
                }
                ReloadLocalDirectory(TargetDirectory);
              }
            }
          }
          catch (EAbort &)
          {
            if (FMoveToQueue)
            {
              Params |=
                (CopyParam.QueueNoConfirmation ? cpNoConfirmation : 0);

              DebugAssert(CopyParam.TransferSkipList == NULL);
              DebugAssert(CopyParam.TransferResumeFile.IsEmpty());
              if (TransferResumeList->Count > 0)
              {
                CopyParam.TransferResumeFile = TransferResumeList->Strings[TransferResumeList->Count - 1];
                TransferResumeList->Delete(TransferResumeList->Count - 1);
              }

              CopyParam.TransferSkipList = TransferResumeList.release();

              // not really needed, just to keep it consistent with TransferResumeList
              FTransferResumeList = NULL;
              FMoveToQueue = false;

              Configuration->Usage->Inc("MovesToBackground");

              AddQueueItem(Queue, Direction, PermanentFileList, TargetDirectory, CopyParam, Params);
              ClearTransferSourceSelection(Direction);
            }

            throw;
          }
        }
        __finally
        {
          if (!SelectionRestored && (DView != NULL))
          {
            DView->DiscardSavedSelection();
          }
          FTransferResumeList = NULL;
        }
      }
    }
    else if (Operation == foRename)
    {
      DebugAssert(DirView(Side)->ItemFocused);
      DirView(Side)->ItemFocused->EditCaption();
      Result = true;
    }
    else if (Operation == foDelete)
    {
      DebugAssert(FileList->Count);
      // We deliberately do not toggle alternative flag (Param), but use OR,
      // because the Param is set only when command is invoked using Shift-Del/F8 keyboard
      // shortcut of CurrentDeleteAlternativeAction
      bool Alternative =
        bool(Param) || UseAlternativeFunction();
      bool Recycle;
      if (Side == osLocal)
      {
        Recycle = (WinConfiguration->DeleteToRecycleBin != Alternative);
      }
      else
      {
        Recycle = (Terminal->SessionData->DeleteToRecycleBin != Alternative) &&
          !Terminal->SessionData->RecycleBinPath.IsEmpty() &&
          !Terminal->IsRecycledFile(FileList->Strings[0]);
      }

      Result =
        !(Recycle ? WinConfiguration->ConfirmRecycling : WinConfiguration->ConfirmDeleting);
      if (!Result)
      {
        UnicodeString Query;
        if (FileList->Count == 1)
        {
          if (Side == osLocal)
          {
            Query = ExtractFileName(FileList->Strings[0]);
          }
          else
          {
            Query = UnixExtractFileName(FileList->Strings[0]);
          }
          Query = FMTLOAD(
            (Recycle ? CONFIRM_RECYCLE_FILE : CONFIRM_DELETE_FILE), (Query));
        }
        else
        {
          Query = FMTLOAD(
            (Recycle ? CONFIRM_RECYCLE_FILES : CONFIRM_DELETE_FILES), (FileList->Count));
        }

        TMessageParams Params(mpNeverAskAgainCheck);
        Params.ImageName = L"Delete file";
        unsigned int Answer = MessageDialog(MainInstructions(Query), qtConfirmation,
          qaOK | qaCancel, HELP_DELETE_FILE, &Params);
        if (Answer == qaNeverAskAgain)
        {
          Result = true;
          if (Recycle)
          {
            WinConfiguration->ConfirmRecycling = false;
          }
          else
          {
            WinConfiguration->ConfirmDeleting = false;
          }
        }
        else
        {
          Result = (Answer == qaOK);
        }
      }

      if (Result)
      {
        DeleteFiles(Side, FileList, FLAGMASK(Alternative, dfAlternative));
      }
    }
    else if (Operation == foSetProperties)
    {
      RemoteDirView->SaveSelectedNames();
      Result = SetProperties(Side, FileList);
    }
    else if (Operation == foCustomCommand)
    {
      DebugAssert(Param);
      DebugAssert(Side == osRemote);

      RemoteDirView->SaveSelectedNames();
      const TCustomCommandType * Command = static_cast<const TCustomCommandType*>(Param);
      CustomCommand(FileList, *Command, NULL);
      Result = true;
    }
    else if ((Operation == foRemoteMove) || (Operation == foRemoteCopy))
    {
      DebugAssert(Side == osRemote);
      Result = RemoteTransferFiles(FileList, NoConfirmation,
        (Operation == foRemoteMove), reinterpret_cast<TTerminal *>(Param));
    }
    else if (Operation == foLock)
    {
      DebugAssert(Side == osRemote);
      LockFiles(FileList, true);
      Result = true;
    }
    else if (Operation == foUnlock)
    {
      DebugAssert(Side == osRemote);
      LockFiles(FileList, false);
      Result = true;
    }
    else
    {
      DebugFail();
    }
  }
  __finally
  {
    BatchEnd(BatchStorage);
  }
  return Result;
}
//---------------------------------------------------------------------------
TOperationSide __fastcall TCustomScpExplorerForm::GetSide(TOperationSide Side)
{
  if (Side == osCurrent)
  {
    Side = FCurrentSide;
  }

  return Side;
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::ExecuteFileOperation(TFileOperation Operation,
  TOperationSide Side, bool OnFocused, bool NoConfirmation, void * Param)
{
  Side = GetSide(Side);

  bool Result;
  TStrings * FileList = DirView(Side)->CreateFileList(OnFocused, (Side == osLocal), NULL);
  try
  {
    Result = ExecuteFileOperation(Operation, Side, FileList, NoConfirmation, Param);
  }
  __finally
  {
    delete FileList;
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ExecuteFileOperationCommand(
  TFileOperation Operation, TOperationSide Side, bool OnFocused,
  bool NoConfirmation, void * Param)
{
  if (ExecuteFileOperation(Operation, Side, OnFocused, NoConfirmation, Param))
  {
    if ((Operation == foCopy) || (Operation == foMove))
    {
      if (GetSide(Side) == osLocal)
      {
        Configuration->Usage->Inc(L"UploadsCommand");
      }
      else
      {
        Configuration->Usage->Inc(L"DownloadsCommand");
      }
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ExecuteCopyOperationCommand(
  TOperationSide Side, bool OnFocused, unsigned int Flags)
{
  TTransferOperationParam Param;
  if ((WinConfiguration->Interface != ifCommander) ||
      WinConfiguration->ScpCommander.ExplorerKeyboardShortcuts)
  {
    Flags &= ~cocShortCutHint;
  }
  TCustomDirView * DView = DirView(Side);
  Param.Options =
    FLAGMASK(FLAGSET(Flags, cocShortCutHint), coShortCutHint) |
    FLAGMASK(SelectedAllFilesInDirView(DView), coAllFiles);
  if (FLAGSET(Flags, cocQueue))
  {
    Param.Queue = asOn;
  }
  else if (FLAGSET(Flags, cocNonQueue))
  {
    Param.Queue = asOff;
  }
  ExecuteFileOperationCommand(foCopy, Side, OnFocused, false, &Param);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::HandleErrorList(TStringList *& ErrorList)
{
  try
  {
    if (ErrorList->Count)
    {
      UnicodeString Message = MainInstructions(FMTLOAD(ERROR_LIST_COUNT, (ErrorList->Count)));
      if (MessageDialog(Message, qtError,
          qaOK | qaCancel, HELP_NONE) == qaOK)
      {
        unsigned int Answer;
        int Index = 0;
        do
        {
          DebugAssert(Index >= 0 && Index < ErrorList->Count);
          TQueryButtonAlias Aliases[2];
          Aliases[0].Button = qaYes;
          Aliases[0].Alias = LoadStr(PREV_BUTTON);
          Aliases[1].Button = qaNo;
          Aliases[1].Alias = LoadStr(NEXT_BUTTON);
          TMessageParams Params;
          Params.Aliases = Aliases;
          Params.AliasesCount = LENOF(Aliases);

          Answer = MoreMessageDialog(
            FMTLOAD(ERROR_LIST_NUMBER, (Index+1, ErrorList->Count, ErrorList->Strings[Index])),
            dynamic_cast<TStrings *>(ErrorList->Objects[Index]), qtError,
            (Index ? qaYes : 0) | (Index < ErrorList->Count - 1 ? qaNo : 0) |
            qaOK, HELP_NONE, &Params);

          if (Answer == qaNo)
          {
            Index++;
          }
          if (Answer == qaYes)
          {
            Index--;
          }
        }
        while (Answer != qaOK);
      }
    }
  }
  __finally
  {
    TStrings * List = ErrorList;
    ErrorList = NULL;
    for (int i = 0; i < List->Count; i++)
    {
      delete List->Objects[i];
    }
    delete List;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ExecuteRemoteFile(
  const UnicodeString & FullFileName, TRemoteFile * File, TExecuteFileBy ExecuteFileBy)
{
  // needed for checking filemasks, as there's no directory object
  // associated with the file object
  File->FullFileName = FullFileName;

  TFileMasks::TParams MaskParams;
  MaskParams.Size = File->Size;
  MaskParams.Modification = File->Modification;

  ExecuteFile(osRemote, ExecuteFileBy, NULL, FullFileName, File, MaskParams);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::EditNew(TOperationSide Side)
{
  DebugAssert(!WinConfiguration->DisableOpenEdit);

  Side = GetSide(Side);

  TCustomDirView * CurrentDirView = DirView(osCurrent);
  TListItem * FocusedItem = CurrentDirView->ItemFocused;
  UnicodeString Name;
  if ((FocusedItem != NULL) && !CurrentDirView->ItemIsParentDirectory(FocusedItem))
  {
    Name = CurrentDirView->ItemFileName(FocusedItem);
  }
  else
  {
    Name = LoadStr(NEW_FILE);
  }
  UnicodeString Names = Name;
  std::unique_ptr<TStrings> History(CloneStrings(CustomWinConfiguration->History[L"EditFile"]));
  if (InputDialog(LoadStr(EDIT_FILE_CAPTION), LoadStr(EDIT_FILE_PROMPT), Names,
        HELP_EDIT_NEW, History.get(), true))
  {
    while (!Names.IsEmpty())
    {
      Name = CutToChar(Names, FileMasksDelimiters[1], false);
      CustomWinConfiguration->History[L"EditFile"] = History.get();
      UnicodeString TargetFileName;
      UnicodeString LocalFileName;
      UnicodeString RootTempDir;
      UnicodeString TempDir;
      UnicodeString RemoteDirectory;
      bool ExistingFile = false;
      if (Side == osRemote)
      {
        Name = AbsolutePath(FTerminal->CurrentDirectory, Name);

        TRemoteFile * File = NULL;
        if (FTerminal->FileExists(Name, &File))
        {
          try
          {
            ExecuteRemoteFile(Name, File, efDefaultEditor);
            ExistingFile = true;
          }
          __finally
          {
            delete File;
          }
        }

        if (!ExistingFile)
        {
          RemoteDirectory = UnixExtractFilePath(Name);
          TemporaryDirectoryForRemoteFiles(
            RemoteDirectory, GUIConfiguration->CurrentCopyParam, TempDir, RootTempDir);

          TargetFileName = UnixExtractFileName(Name);
          TCopyParamType CopyParam = GUIConfiguration->CurrentCopyParam;
          LocalFileName = TempDir +
            // We probably do not want to trim the VMS version here
            FTerminal->ChangeFileName(&CopyParam, TargetFileName, osRemote, false);
        }
      }
      else
      {
        if (ExtractFilePath(Name).IsEmpty())
        {
          LocalFileName = IncludeTrailingBackslash(DirView(Side)->PathName) + Name;
        }
        else
        {
          LocalFileName = ExpandFileName(Name);
        }

        TargetFileName = ExtractFileName(Name);
      }

      if (!ExistingFile)
      {
        if (!FileExists(ApiPath(LocalFileName)))
        {
          int File = FileCreate(ApiPath(LocalFileName));
          if (File < 0)
          {
            if (!RootTempDir.IsEmpty())
            {
              RecursiveDeleteFile(ExcludeTrailingBackslash(RootTempDir), false);
            }
            throw Exception(FMTLOAD(CREATE_FILE_ERROR, (LocalFileName)));
          }
          else
          {
            FileClose(File);
          }
        }

        TExecuteFileBy ExecuteFileBy = efDefaultEditor;
        const TEditorData * ExternalEditor = NULL;
        TFileMasks::TParams MaskParams; // size not known
        ExecuteFileNormalize(ExecuteFileBy, ExternalEditor, TargetFileName,
          false, MaskParams);

        CustomExecuteFile(Side, ExecuteFileBy, LocalFileName, TargetFileName,
          ExternalEditor, RootTempDir, RemoteDirectory);
      }
    }
  }
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::RemoteExecuteForceText(
  TExecuteFileBy ExecuteFileBy, const TEditorData * ExternalEditor)
{
  DebugAssert((ExecuteFileBy == efExternalEditor) ==
    ((ExternalEditor != NULL) && (ExternalEditor->Editor == edExternal)));
  DebugAssert(ExecuteFileBy != efDefaultEditor);

  return
    ((ExecuteFileBy == efInternalEditor)) ||
    ((ExecuteFileBy == efExternalEditor) && ExternalEditor->ExternalEditorText);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::CustomExecuteFile(TOperationSide Side,
  TExecuteFileBy ExecuteFileBy, UnicodeString FileName, UnicodeString OriginalFileName,
  const TEditorData * ExternalEditor, UnicodeString LocalRootDirectory,
  UnicodeString RemoteDirectory)
{
  DebugAssert(!WinConfiguration->DisableOpenEdit);
  DebugAssert((ExecuteFileBy == efExternalEditor) ==
    ((ExternalEditor != NULL) && (ExternalEditor->Editor == edExternal)));
  DebugAssert(ExecuteFileBy != efDefaultEditor);

  Side = GetSide(Side);

  std::unique_ptr<TEditedFileData> Data(new TEditedFileData);
  if (Side == osRemote)
  {
    Data->Terminal = Terminal;
    Data->Queue = Queue;
    Data->SessionData = CloneCurrentSessionData();
    Data->ForceText = RemoteExecuteForceText(ExecuteFileBy, ExternalEditor);
    Data->RemoteDirectory = RemoteDirectory;
    Data->SessionName = Terminal->SessionData->SessionName;
    Data->LocalRootDirectory = LocalRootDirectory;
    Data->OriginalFileName = OriginalFileName;
    Data->Command = L""; // will be changed later for external editor
  }

  if (ExecuteFileBy == efInternalEditor)
  {
    if (Side == osRemote)
    {
      UnicodeString Caption = UnixIncludeTrailingBackslash(RemoteDirectory) + OriginalFileName +
        L" - " + Terminal->SessionData->SessionName;
      TForm * Editor;
      try
      {
        Editor = ShowEditorForm(FileName, this, FEditorManager->FileChanged,
          FEditorManager->FileReload, FEditorManager->FileClosed,
          SaveAllInternalEditors, AnyInternalEditorModified,
          Caption, FStandaloneEditing, SessionColor);
      }
      catch(...)
      {
        if (!LocalRootDirectory.IsEmpty())
        {
          RecursiveDeleteFile(ExcludeTrailingBackslash(LocalRootDirectory), false);
        }
        throw;
      }

      FEditorManager->AddFileInternal(FileName, Data.release(), Editor);
    }
    else
    {
      DebugAssert(!FStandaloneEditing);
      TForm * Editor =
        ShowEditorForm(FileName, this, NULL, NULL, LocalEditorClosed,
          SaveAllInternalEditors, AnyInternalEditorModified,
          L"", false, SessionColor);
      FLocalEditors->Add(Editor);
    }
  }
  else
  {
    HANDLE Process;

    if (ExecuteFileBy == efExternalEditor)
    {
      UnicodeString Program, Params, Dir;
      Data->Command = ExternalEditor->ExternalEditor;
      ReformatFileNameCommand(Data->Command);
      SplitCommand(Data->Command, Program, Params, Dir);
      Params = ExpandFileNameCommand(Params, FileName);
      Program = ExpandEnvironmentVariables(Program);
      if (!ExecuteShell(Program, Params, Process))
      {
        throw Exception(FMTLOAD(EDITOR_ERROR, (Program)));
      }
    }
    else
    {
      DebugAssert(Side == osRemote);
      if (!ExecuteShell(FileName, L"", Process))
      {
        throw Exception(FMTLOAD(EXECUTE_FILE_ERROR, (FileName)));
      }
    }

    if ((Side == osLocal) ||
        ((ExecuteFileBy == efShell) &&
         !WinConfiguration->Editor.SDIShellEditor) ||
        ((ExecuteFileBy == efExternalEditor) &&
         !ExternalEditor->SDIExternalEditor))
    {
      // no need for handle
      if (Process != NULL)
      {
        DebugCheck(CloseHandle(Process));
      }
      Process = INVALID_HANDLE_VALUE;
    }
    else
    {
      if (Process == NULL)
      {
        throw ExtException(LoadStr(OPEN_FILE_NO_PROCESS2), L"", HELP_OPEN_FILE_NO_PROCESS);
      }
    }

    if (Side == osRemote)
    {
      FEditorManager->AddFileExternal(FileName, Data.release(), Process);
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SaveInternalEditor(
  const UnicodeString /*FileName*/, TEditedFileData * /*Data*/, TObject * Token,
  void * /*Arg*/)
{
  if (Token != NULL)
  {
    EditorFormFileSave(static_cast<TForm *>(Token));
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SaveAllInternalEditors(TObject * /*Sender*/)
{
  for (int Index = 0; Index < FLocalEditors->Count; Index++)
  {
    EditorFormFileSave(static_cast<TForm *>(FLocalEditors->Items[Index]));
  }

  FEditorManager->ProcessFiles(SaveInternalEditor, NULL);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::InternalEditorModified(
  const UnicodeString /*FileName*/, TEditedFileData * /*Data*/, TObject * Token,
  void * Arg)
{
  if ((Token != NULL) &&
      IsEditorFormModified(static_cast<TForm *>(Token)))
  {
    *static_cast<bool *>(Arg) = true;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::AnyInternalEditorModified(
  TObject * /*Sender*/, bool & Modified)
{
  for (int Index = 0; !Modified && (Index < FLocalEditors->Count); Index++)
  {
    if (IsEditorFormModified(static_cast<TForm *>(FLocalEditors->Items[Index])))
    {
      Modified = true;
    }
  }

  if (!Modified)
  {
    FEditorManager->ProcessFiles(InternalEditorModified, &Modified);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::LocalEditorClosed(TObject * Sender, bool /*Forced*/)
{
  DebugCheck(FLocalEditors->Extract(Sender) >= 0);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::TemporaryDirectoryForRemoteFiles(
  UnicodeString RemoteDirectory, TCopyParamType CopyParam,
  UnicodeString & Result, UnicodeString & RootDirectory)
{
  if (!WinConfiguration->TemporaryDirectoryDeterministic)
  {
    RootDirectory = IncludeTrailingBackslash(WinConfiguration->TemporaryDir());
    Result = RootDirectory;
  }
  else
  {
    RootDirectory = L"";
    Result = WinConfiguration->ExpandedTemporaryDirectory();
    Result = IncludeTrailingBackslash(Result);
  }

  if (WinConfiguration->TemporaryDirectoryAppendSession)
  {
    Result = IncludeTrailingBackslash(Result + CopyParam.ValidLocalPath(Terminal->SessionData->SessionName));
  }

  if (WinConfiguration->TemporaryDirectoryAppendPath)
  {
    if (!RemoteDirectory.IsEmpty() && (RemoteDirectory[1] == L'/'))
    {
      RemoteDirectory.Delete(1, 1);
    }
    Result = IncludeTrailingBackslash(Result + CopyParam.ValidLocalPath(FromUnixPath(RemoteDirectory)));
  }

  if (!ForceDirectories(ApiPath(Result)))
  {
    throw EOSExtException(FMTLOAD(CREATE_TEMP_DIR_ERROR, (Result)));
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::TemporarilyDownloadFiles(
  TStrings * FileList, bool ForceText, UnicodeString & RootTempDir, UnicodeString & TempDir,
  bool AllFiles, bool GetTargetNames, bool AutoOperation)
{
  TCopyParamType CopyParam = GUIConfiguration->CurrentCopyParam;
  if (ForceText)
  {
    CopyParam.TransferMode = tmAscii;
  }
  TemporaryFileCopyParam(CopyParam);
  if (AllFiles)
  {
    CopyParam.IncludeFileMask = TFileMasks();
  }

  if (TempDir.IsEmpty())
  {
    TemporaryDirectoryForRemoteFiles(FTerminal->CurrentDirectory, CopyParam, TempDir, RootTempDir);
  }

  DebugAssert(!FAutoOperation);
  FAutoOperation = AutoOperation;
  Terminal->ExceptionOnFail = true;
  try
  {
    try
    {
      // turn off confirmations, as for MDI editors we may possibly download
      // the same file over
      Terminal->CopyToLocal(FileList, TempDir, &CopyParam,
        cpNoConfirmation | cpTemporary);

      if (GetTargetNames)
      {
        for (int i = 0; i < FileList->Count; i++)
        {
          FileList->Strings[i] =
            Terminal->ChangeFileName(&CopyParam, UnixExtractFileName(FileList->Strings[i]), osRemote, false);
        }
      }
    }
    catch(...)
    {
      if (!RootTempDir.IsEmpty())
      {
        RecursiveDeleteFile(ExcludeTrailingBackslash(RootTempDir), false);
      }
      throw;
    }
  }
  __finally
  {
    FAutoOperation = false;
    Terminal->ExceptionOnFail = false;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::EditorAutoConfig()
{
  // Do not waste time checking for default editor list the next time,
  // or testing if default editor is not notepad.
  bool TryNextTime = false;
  UnicodeString UsageState;

  if (!WinConfiguration->EditorList->IsDefaultList())
  {
    UsageState = "H";
  }
  else
  {
    UnicodeString Executable;
    UnicodeString ExecutableDescription;
    if (DetectSystemExternalEditor(false, Executable, ExecutableDescription, UsageState, TryNextTime))
    {
      UnicodeString Message =
        FMTLOAD(EDITOR_AUTO_CONFIG2, (ExecutableDescription, ExecutableDescription));

      unsigned int Answer =
        MessageDialog(Message, qtConfirmation, qaOK | qaCancel, HELP_EDITOR_AUTO_CONFIG);
      if (Answer != qaOK)
      {
        UsageState = "R";
      }
      else
      {
        UsageState = "A";
        TEditorData EditorData;
        EditorData.Editor = edExternal;
        EditorData.ExternalEditor = FormatCommand(Executable, L"");
        EditorData.DecideExternalEditorText();

        TEditorList EditorList;
        EditorList = *WinConfiguration->EditorList;
        EditorList.Insert(0, new TEditorPreferences(EditorData));
        WinConfiguration->EditorList = &EditorList;
      }
    }
  }

  WinConfiguration->OfferedEditorAutoConfig = !TryNextTime;
  WinConfiguration->Usage->Set(L"EditorAutoConfig", UsageState);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ExecuteFileNormalize(
  TExecuteFileBy & ExecuteFileBy, const TEditorData *& ExternalEditor,
  const UnicodeString & FileName, bool Local, const TFileMasks::TParams & MaskParams)
{
  if (ExecuteFileBy == efDefaultEditor)
  {
    if (!WinConfiguration->OfferedEditorAutoConfig)
    {
      EditorAutoConfig();
    }

    const TEditorPreferences * Editor =
      WinConfiguration->DefaultEditorForFile(FileName, Local, MaskParams);
    if ((Editor == NULL) || (Editor->Data->Editor == edInternal))
    {
      ExecuteFileBy = efInternalEditor;
      ExternalEditor = NULL;
    }
    else if (Editor->Data->Editor == edOpen)
    {
      ExecuteFileBy = efShell;
      ExternalEditor = NULL;
    }
    else
    {
      ExecuteFileBy = efExternalEditor;
      ExternalEditor = Editor->Data;
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ExecuteFile(TOperationSide Side,
  TExecuteFileBy ExecuteFileBy, const TEditorData * ExternalEditor,
  UnicodeString FullFileName, TObject * Object, const TFileMasks::TParams & MaskParams)
{

  UnicodeString OriginalFileName;
  UnicodeString LocalRootDirectory;
  UnicodeString RemoteDirectory;
  ExecuteFileNormalize(ExecuteFileBy, ExternalEditor, FullFileName,
    (Side == osLocal), MaskParams);

  UnicodeString Counter;
  UnicodeString LocalFileName;
  if (Side == osRemote)
  {
    // We need to trim VMS version here, so that we use name without version
    // when uploading back to create a new version of the file
    OriginalFileName = FTerminal->GetBaseFileName(UnixExtractFileName(FullFileName));
    RemoteDirectory = UnixExtractFilePath(FullFileName);
    TObject * Token = NULL;
    UnicodeString LocalDirectory;
    if (!FEditorManager->CanAddFile(RemoteDirectory, OriginalFileName,
           Terminal->SessionData->SessionName, Token, LocalRootDirectory,
           LocalDirectory))
    {
      if (Token != NULL)
      {
        TForm * Form = dynamic_cast<TForm *>(Token);
        if (Form->WindowState == wsMinimized)
        {
          ShowWindow(Form->Handle, SW_RESTORE);
        }
        else
        {
          Form->SetFocus();
        }
        Abort();
      }
      else
      {
        throw Exception(FMTLOAD(ALREADY_EDITED_EXTERNALLY_OR_UPLOADED, (OriginalFileName)));
      }
    }

    TStringList * FileList1 = new TStringList();
    try
    {
      FileList1->AddObject(FullFileName, Object);
      TemporarilyDownloadFiles(FileList1,
        RemoteExecuteForceText(ExecuteFileBy, ExternalEditor),
        LocalRootDirectory, LocalDirectory, true, true, true);
      LocalFileName = LocalDirectory + FileList1->Strings[0];
    }
    __finally
    {
      delete FileList1;
    }

    switch (ExecuteFileBy)
    {
      case efShell:
        Counter = "RemoteFilesExecuted";
        break;

      case efInternalEditor:
        Counter = "RemoteFilesOpenedInInternalEditor";
        break;

      case efExternalEditor:
        Counter = "RemoteFilesOpenedInExternalEditor";
        break;

      default:
        DebugFail();
    }
  }
  else
  {
    LocalFileName = FullFileName;
    OriginalFileName = ExtractFileName(FullFileName);

    switch (ExecuteFileBy)
    {
      case efShell:
        Counter = "LocalFilesExecuted";
        break;

      case efInternalEditor:
        Counter = "LocalFilesOpenedInInternalEditor";
        break;

      case efExternalEditor:
        Counter = "LocalFilesOpenedInExternalEditor";
        break;

      default:
        DebugFail();
    }
  }

  Configuration->Usage->Inc(Counter);

  CustomExecuteFile(Side, ExecuteFileBy, LocalFileName, OriginalFileName,
    ExternalEditor, LocalRootDirectory, RemoteDirectory);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ExecuteFile(TOperationSide Side,
  TExecuteFileBy ExecuteFileBy, const TEditorData * ExternalEditor,
  bool AllSelected, bool OnFocused)
{
  DebugAssert(!WinConfiguration->DisableOpenEdit);
  DebugAssert((ExecuteFileBy == efExternalEditor) ==
    ((ExternalEditor != NULL) && (ExternalEditor->Editor == edExternal)));

  Side = GetSide(Side);

  TCustomDirView * DView = DirView(Side);
  TStrings * FileList = AllSelected ?
    DView->CreateFileList(OnFocused, Side == osLocal) :
    DView->CreateFocusedFileList(Side == osLocal);
  try
  {
    DebugAssert(AllSelected || (FileList->Count == 1));
    for (int i = 0; i < FileList->Count; i++)
    {
      UnicodeString ListFileName = FileList->Strings[i];
      UnicodeString FileNameOnly = (Side == osRemote) ?
        UnixExtractFileName(ListFileName) : ExtractFileName(ListFileName);
      TListItem * Item = DView->FindFileItem(FileNameOnly);
      if (!DView->ItemIsDirectory(Item))
      {
        UnicodeString FullFileName;
        if (Side == osRemote)
        {
          FullFileName = RemoteDirView->Path + ListFileName;
        }
        else
        {
          FullFileName = ListFileName;
        }

        TFileMasks::TParams MaskParams;
        MaskParams.Size = DView->ItemFileSize(Item);
        TDateTimePrecision Precision;
        MaskParams.Modification = DView->ItemFileTime(Item, Precision);

        ExecuteFile(Side, ExecuteFileBy, ExternalEditor, FullFileName,
          FileList->Objects[i], MaskParams);
      }
    }
  }
  __finally
  {
    delete FileList;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::TemporaryFileCopyParam(TCopyParamType & CopyParam)
{
  // do not forget to add additional options to TemporarilyDownloadFiles, and AS
  CopyParam.FileNameCase = ncNoChange;
  CopyParam.PreserveRights = false;
  CopyParam.PreserveReadOnly = false;
  CopyParam.ReplaceInvalidChars = true;
  CopyParam.IncludeFileMask = TFileMasks();
  CopyParam.NewerOnly = false;
  CopyParam.FileMask = L"";
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ExecutedFileChanged(const UnicodeString FileName,
  TEditedFileData * Data, HANDLE UploadCompleteEvent)
{
  TTerminalManager * Manager = TTerminalManager::Instance();
  if ((Data->Terminal == NULL) || !Data->Terminal->Active)
  {
    if (!NonVisualDataModule->Busy)
    {
      UnicodeString FileNameOnly = ExtractFileName(FileName);
      UnicodeString EditInactiveSessionReopenAcceptedCounter = L"EditInactiveSessionReopenAccepted";
      UnicodeString EditInactiveSessionReopenRejectedCounter = L"EditInactiveSessionReopenRejected";
      if (Data->Terminal == NULL)
      {
        TTerminal * SameSiteTerminal = Manager->FindActiveTerminalForSite(Data->SessionData);
        if (SameSiteTerminal != NULL)
        {
          UnicodeString Message =
            FMTLOAD(EDIT_SESSION_REATTACH,
              (FileNameOnly, Data->SessionName, FileNameOnly));
          if (MessageDialog(Message, qtConfirmation, qaOK | qaCancel) == qaOK)
          {
            Data->Terminal = Terminal;
            Data->Queue = Manager->FindQueueForTerminal(Terminal);
            Data->SessionName = Terminal->SessionData->SessionName;
            // We might also overwrite session data
            Configuration->Usage->Inc(EditInactiveSessionReopenAcceptedCounter);
          }
          else
          {
            Configuration->Usage->Inc(EditInactiveSessionReopenRejectedCounter);
            Abort();
          }
        }
      }
      // foreground session should reconnect itself
      else if (Terminal != Data->Terminal)
      {
        UnicodeString Message =
          MainInstructions(
            FMTLOAD(EDIT_SESSION_RECONNECT, (Data->SessionName, FileNameOnly)));
        if (MessageDialog(Message, qtConfirmation, qaOK | qaCancel) == qaOK)
        {
          Manager->SetActiveTerminalWithAutoReconnect(Data->Terminal);
          Configuration->Usage->Inc(EditInactiveSessionReopenAcceptedCounter);
        }
        else
        {
          Configuration->Usage->Inc(EditInactiveSessionReopenRejectedCounter);
          Abort();
        }
      }
    }

    if ((Data->Terminal == NULL) || !Data->Terminal->Active)
    {
      Configuration->Usage->Inc(L"EditInactiveSession");
      // Prevent this when not idle (!NonVisualDataModule->Busy)?
      throw Exception(FMTLOAD(EDIT_SESSION_CLOSED2,
        (ExtractFileName(FileName), Data->SessionName)));
    }
  }

  TStrings * FileList = new TStringList();
  try
  {
    FileList->Add(FileName);

    // consider using the same settings (preset) as when the file was downloaded
    TGUICopyParamType CopyParam = GUIConfiguration->CurrentCopyParam;
    TemporaryFileCopyParam(CopyParam);
    if (Data->ForceText)
    {
      CopyParam.TransferMode = tmAscii;
    }
    // so i do not need to worry if masking algorithm works in all cases
    // ("" means "copy file name", no masking is actually done)
    if (ExtractFileName(FileName) == Data->OriginalFileName)
    {
      CopyParam.FileMask = L"";
    }
    else
    {
      CopyParam.FileMask = DelimitFileNameMask(Data->OriginalFileName);
    }

    DebugAssert(Data->Queue != NULL);

    int Params = cpNoConfirmation | cpTemporary;
    TQueueItem * QueueItem = new TUploadQueueItem(Data->Terminal, FileList,
      Data->RemoteDirectory, &CopyParam, Params, true);
    QueueItem->CompleteEvent = UploadCompleteEvent;
    AddQueueItem(Data->Queue, QueueItem, Data->Terminal);
  }
  __finally
  {
    delete FileList;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ExecutedFileReload(
  const UnicodeString FileName, const TEditedFileData * Data)
{
  // Sanity check, we should not be busy otherwise user would not be able to click Reload button.
  DebugAssert(!NonVisualDataModule->Busy);

  if ((Data->Terminal == NULL) || !Data->Terminal->Active)
  {
    throw Exception(FMTLOAD(EDIT_SESSION_CLOSED_RELOAD,
      (ExtractFileName(FileName), Data->SessionName)));
  }

  TTerminal * PrevTerminal = TTerminalManager::Instance()->ActiveTerminal;
  TTerminalManager::Instance()->ActiveTerminal = Data->Terminal;
  NonVisualDataModule->StartBusy();
  try
  {
    std::unique_ptr<TRemoteFile> File;
    UnicodeString RemoteFileName =
      UnixIncludeTrailingBackslash(Data->RemoteDirectory) + Data->OriginalFileName;
    FTerminal->ExceptionOnFail = true;
    try
    {
      TRemoteFile * AFile = NULL;
      FTerminal->ReadFile(RemoteFileName, AFile);
      File.reset(AFile);
      if (!File->HaveFullFileName)
      {
        File->FullFileName = RemoteFileName;
      }
    }
    __finally
    {
      FTerminal->ExceptionOnFail = false;
    }
    std::unique_ptr<TStrings> FileList(new TStringList());
    FileList->AddObject(RemoteFileName, File.get());

    UnicodeString RootTempDir = Data->LocalRootDirectory;
    UnicodeString TempDir = ExtractFilePath(FileName);

    TemporarilyDownloadFiles(FileList.get(), Data->ForceText, RootTempDir,
      TempDir, true, true, true);

    // sanity check, the target file name should be still the same
    DebugAssert(ExtractFileName(FileName) == FileList->Strings[0]);
  }
  __finally
  {
    NonVisualDataModule->EndBusy();
    // it actually may not exist anymore...
    TTerminalManager::Instance()->ActiveTerminal = PrevTerminal;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ExecutedFileEarlyClosed(
  const TEditedFileData * Data, bool & KeepOpen)
{
  // Command is set for external editors only (not for "shell" open).
  if (!Data->Command.IsEmpty())
  {
    bool AnyFound = false;
    bool AnyMDI = false;
    bool AnyNonMDI = false;
    bool AnyDetect = false;
    TEditorList * EditorList = new TEditorList();
    try
    {
      *EditorList = *WinConfiguration->EditorList;
      for (int i = 0; i < EditorList->Count; i++)
      {
        const TEditorPreferences * Editor = EditorList->Editors[i];
        if ((Editor->Data->Editor == edExternal) &&
            (Editor->Data->ExternalEditor == Data->Command))
        {
          AnyFound = true;
          if (Editor->Data->SDIExternalEditor)
          {
            AnyNonMDI = true;
            if (Editor->Data->DetectMDIExternalEditor)
            {
              AnyDetect = true;
            }
          }
          else
          {
            AnyMDI = true;
          }
        }
      }

      bool EnableMDI = false;
      bool DisableDetect = false;

      if (AnyMDI)
      {
        KeepOpen = true;
        if (AnyNonMDI)
        {
          // there is at least one instance of the editor with MDI support enabled,
          // and one with disabled, enable it for all instances
          EnableMDI = true;
        }
      }
      else if (AnyFound && !AnyDetect)
      {
        // at least once instance found but all have MDI autodetection disabled
        // => close the file (default action)
      }
      else
      {
        // no instance of the editor has MDI support enabled

        TMessageParams Params;
        if (AnyFound)
        {
          // there is at least one instance of the editor
          Params.Params |= mpNeverAskAgainCheck;
        }
        unsigned int Answer = MessageDialog(FMTLOAD(EDITOR_EARLY_CLOSED2, (Data->OriginalFileName)), qtWarning,
          qaYes | qaNo, HELP_EDITOR_EARLY_CLOSED, &Params);
        switch (Answer)
        {
          case qaNeverAskAgain:
            DisableDetect = true;
            break;

          case qaNo:
            EnableMDI = true;
            KeepOpen = true;
            break;
        }
      }

      if (AnyFound && (EnableMDI || DisableDetect))
      {
        bool Changed = false;
        for (int i = 0; i < EditorList->Count; i++)
        {
          const TEditorPreferences * Editor = EditorList->Editors[i];
          if ((Editor->Data->Editor == edExternal) &&
              (Editor->Data->ExternalEditor == Data->Command) &&
              ((EnableMDI && Editor->Data->SDIExternalEditor) ||
               (DisableDetect && Editor->Data->DetectMDIExternalEditor)))
          {
            Changed = true;
            TEditorPreferences * UpdatedEditor = new TEditorPreferences(*Editor);
            if (EnableMDI)
            {
              UpdatedEditor->GetData()->SDIExternalEditor = false;
            }
            if (DisableDetect)
            {
              UpdatedEditor->GetData()->DetectMDIExternalEditor = false;
            }
            EditorList->Change(i, UpdatedEditor);
          }
        }

        if (Changed)
        {
          WinConfiguration->EditorList = EditorList;
        }
      }
    }
    __finally
    {
      delete EditorList;
    }
  }
  else
  {
    // "open" case

    MessageDialog(FMTLOAD(APP_EARLY_CLOSED, (Data->OriginalFileName)), qtWarning,
      qaOK, HELP_APP_EARLY_CLOSED);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ExecutedFileUploadComplete(TObject * Sender)
{
  EditorFormFileUploadComplete(DebugNotNull(dynamic_cast<TForm *>(Sender)));
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemoteDirViewEnter(TObject * /*Sender*/)
{
  SideEnter(osRemote);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemoteDriveViewEnter(TObject * /*Sender*/)
{
  MakeNextInTabOrder(RemoteDirView, RemoteDriveView);
  SideEnter(osRemote);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SideEnter(TOperationSide Side)
{
  FCurrentSide = Side;
  if (Visible)
  {
    UpdateControls();
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DeleteFiles(TOperationSide Side,
  TStrings * FileList, bool Alternative)
{
  DebugAssert(Terminal);
  TCustomDirView * DView = DirView(Side);
  DView->SaveSelection();
  DView->SaveSelectedNames();
  DebugAssert(!FAlternativeDelete);
  FAlternativeDelete = Alternative;

  try
  {
    if (Side == osRemote)
    {
      Terminal->DeleteFiles(FileList, FLAGMASK(Alternative, dfAlternative));
    }
    else
    {
      try
      {
        Terminal->DeleteLocalFiles(FileList, FLAGMASK(Alternative, dfAlternative));
      }
      __finally
      {
        ReloadLocalDirectory();
      }
    }
    FAlternativeDelete = false;
  }
  catch(...)
  {
    FAlternativeDelete = false;
    DView->DiscardSavedSelection();
    throw;
  }
  DView->RestoreSelection();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::LockFiles(TStrings * FileList, bool Lock)
{
  DebugAssert(Terminal);
  RemoteDirView->SaveSelection();
  RemoteDirView->SaveSelectedNames();

  try
  {
    if (Lock)
    {
      Terminal->LockFiles(FileList);
    }
    else
    {
      Terminal->UnlockFiles(FileList);
    }
  }
  catch(...)
  {
    RemoteDirView->DiscardSavedSelection();
    throw;
  }
  RemoteDirView->RestoreSelection();
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::RemoteTransferDialog(TTerminal *& Session,
  TStrings * FileList, UnicodeString & Target, UnicodeString & FileMask, bool & DirectCopy,
  bool NoConfirmation, bool Move)
{
  if (RemoteDriveView->DropTarget != NULL)
  {
    Target = RemoteDriveView->NodePathName(RemoteDriveView->DropTarget);
  }
  else if (RemoteDirView->DropTarget != NULL)
  {
    DebugAssert(RemoteDirView->ItemIsDirectory(RemoteDirView->DropTarget));
    Target = RemoteDirView->ItemFullFileName(RemoteDirView->DropTarget);
  }
  else
  {
    Target = RemoteDirView->Path;
  }

  if (Session == NULL)
  {
    Session = TTerminalManager::Instance()->ActiveTerminal;
  }
  Target = UnixIncludeTrailingBackslash(Target);
  if (FileList->Count == 1)
  {
    FileMask = DelimitFileNameMask(UnixExtractFileName(FileList->Strings[0]));
  }
  else
  {
    FileMask = L"*.*";
  }
  DirectCopy = FTerminal->IsCapable[fcRemoteCopy] || FTerminal->IsCapable[fcSecondaryShell];
  bool Result = true;
  if (!NoConfirmation)
  {
    bool Multi = (FileList->Count > 1);

    if (Move)
    {
      Result = DoRemoteMoveDialog(Multi, Target, FileMask);
    }
    else
    {
      DebugAssert(Terminal != NULL);
      // update Terminal->StateData->RemoteDirectory
      UpdateTerminal(Terminal);
      TStrings * Sessions = TTerminalManager::Instance()->TerminalList;
      TStrings * Directories = new TStringList;
      try
      {
        for (int Index = 0; Index < Sessions->Count; Index++)
        {
          TManagedTerminal * Terminal =
            dynamic_cast<TManagedTerminal *>(Sessions->Objects[Index]);
          Directories->Add(Terminal->StateData->RemoteDirectory);
        }

        TDirectRemoteCopy AllowDirectCopy;
        if (FTerminal->IsCapable[fcRemoteCopy] || FTerminal->CommandSessionOpened)
        {
          DebugAssert(DirectCopy);
          AllowDirectCopy = drcAllow;
        }
        else if (FTerminal->IsCapable[fcSecondaryShell])
        {
          DebugAssert(DirectCopy);
          AllowDirectCopy = drcConfirmCommandSession;
        }
        else
        {
          DebugAssert(!DirectCopy);
          AllowDirectCopy = drcDisallow;
        }
        void * ASession = Session;
        Result = DoRemoteCopyDialog(Sessions, Directories, AllowDirectCopy,
          Multi, ASession, Target, FileMask, DirectCopy, TTerminalManager::Instance()->ActiveTerminal);
        Session = static_cast<TTerminal *>(ASession);
      }
      __finally
      {
        delete Directories;
      }
    }
  }
  return Result;
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::RemoteTransferFiles(
  TStrings * FileList, bool NoConfirmation, bool Move, TTerminal * Session)
{
  bool DirectCopy;
  UnicodeString Target, FileMask;
  bool Result = RemoteTransferDialog(Session, FileList, Target, FileMask, DirectCopy, NoConfirmation, Move);
  if (Result)
  {
    if (!Move && !DirectCopy)
    {
      Configuration->Usage->Inc("RemoteCopyTemp");

      UnicodeString RootTempDir;
      UnicodeString TempDir;

      TemporarilyDownloadFiles(FileList, false, RootTempDir, TempDir, false, false, false);

      TStrings * TemporaryFilesList = new TStringList();

      try
      {
        TMakeLocalFileListParams MakeFileListParam;
        MakeFileListParam.FileList = TemporaryFilesList;
        MakeFileListParam.FileTimes = NULL;
        MakeFileListParam.IncludeDirs = true;
        MakeFileListParam.Recursive = false;

        ProcessLocalDirectory(TempDir, FTerminal->MakeLocalFileList, &MakeFileListParam);

        TTerminalManager::Instance()->ActiveTerminal = Session;

        if (TemporaryFilesList->Count > 0)
        {
          TGUICopyParamType CopyParam = GUIConfiguration->CurrentCopyParam;
          CopyParam.FileMask = FileMask;

          DebugAssert(!FAutoOperation);
          FAutoOperation = true;
          FTerminal->CopyToRemote(TemporaryFilesList, Target, &CopyParam, cpTemporary);
        }
      }
      __finally
      {
        delete TemporaryFilesList;
        FAutoOperation = false;
        if (!RootTempDir.IsEmpty())
        {
          RecursiveDeleteFile(ExcludeTrailingBackslash(RootTempDir), false);
        }
      }
    }
    else
    {
      RemoteDirView->SaveSelection();
      RemoteDirView->SaveSelectedNames();

      try
      {
        if (Move)
        {
          Configuration->Usage->Inc("RemoteMove");

          Terminal->MoveFiles(FileList, Target, FileMask);
        }
        else
        {
          Configuration->Usage->Inc("RemoteCopyDirect");

          DebugAssert(DirectCopy);
          DebugAssert(Session == FTerminal);

          if (FTerminal->IsCapable[fcRemoteCopy] ||
              FTerminal->CommandSessionOpened ||
              CommandSessionFallback())
          {
            Terminal->CopyFiles(FileList, Target, FileMask);
          }
        }
      }
      catch(...)
      {
        RemoteDirView->DiscardSavedSelection();
        throw;
      }
      RemoteDirView->RestoreSelection();
    }
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::CreateDirectory(TOperationSide Side)
{
  Side = GetSide(Side);
  TRemoteProperties Properties = GUIConfiguration->NewDirectoryProperties;
  TRemoteProperties * AProperties = (Side == osRemote ? &Properties : NULL);
  UnicodeString Name = LoadStr(NEW_FOLDER);
  int AllowedChanges =
    FLAGMASK(Terminal->IsCapable[fcModeChanging], cpMode);
  bool SaveSettings = false;

  if (DoCreateDirectoryDialog(Name, AProperties, AllowedChanges, SaveSettings))
  {
    TWindowLock Lock(this);
    if (Side == osRemote)
    {
      if (SaveSettings)
      {
        GUIConfiguration->NewDirectoryProperties = Properties;
      }
      RemoteDirView->CreateDirectoryEx(Name, &Properties);
    }
    else
    {
      DirView(Side)->CreateDirectory(Name);
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::HomeDirectory(TOperationSide Side)
{
  TWindowLock Lock(this);
  DirView(Side)->ExecuteHomeDirectory();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::OpenDirectory(TOperationSide Side)
{
  DoOpenDirectoryDialog(odBrowse, Side);
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::OpenBookmark(UnicodeString Local, UnicodeString Remote)
{
  UnicodeString Path;
  if (FCurrentSide == osRemote)
  {
    Path = Remote;
  }
  else
  {
    Path = Local;
  }

  bool Result = !Path.IsEmpty();
  if (Result)
  {
    // While we might get here when the session is closed (from location profiles),
    // it's not a problem as the Path setter is noop then
    DirView(FCurrentSide)->Path = Path;
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemoteDirViewGetSelectFilter(
      TCustomDirView *Sender, bool Select, TFileFilter &Filter)
{
  DebugAssert(Sender);
  if (DoSelectMaskDialog(Sender, Select, &Filter, Configuration))
  {
    Configuration->Usage->Inc(L"MaskSelections");
  }
  else
  {
    Abort();
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::CalculateSize(
  TStrings * FileList, __int64 & Size, TCalculateSizeStats & Stats,
  bool & Close)
{
  // terminal can be already closed (e.g. dropped connection)
  if (Terminal != NULL)
  {
    try
    {
      Terminal->CalculateFilesSize(FileList, Size, 0, NULL, true, &Stats);
    }
    catch(...)
    {
      if (!Terminal->Active)
      {
        Close = true;
      }
      throw;
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::CalculateChecksum(const UnicodeString & Alg,
  TStrings * FileList, TCalculatedChecksumEvent OnCalculatedChecksum,
  bool & Close)
{
  // terminal can be already closed (e.g. dropped connection)
  if (Terminal != NULL)
  {
    Configuration->Usage->Inc(L"ChecksumCalculated");

    try
    {
      Terminal->CalculateFilesChecksum(Alg, FileList, NULL, OnCalculatedChecksum);
    }
    catch(...)
    {
      if (!Terminal->Active)
      {
        Close = true;
      }
      throw;
    }
  }
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::SetProperties(TOperationSide Side, TStrings * FileList)
{
  bool Result;
  if (Side == osRemote)
  {
    TRemoteTokenList * GroupList = NULL;
    TRemoteTokenList * UserList = NULL;

    try
    {
      TRemoteProperties CurrentProperties;

      if (Terminal->LoadFilesProperties(FileList))
      {
        RemoteDirView->Invalidate();
      }

      bool CapableGroupChanging = Terminal->IsCapable[fcGroupChanging];
      bool CapableOwnerChanging = Terminal->IsCapable[fcOwnerChanging];
      if (CapableGroupChanging || CapableOwnerChanging)
      {
        if (CapableGroupChanging)
        {
          GroupList = Terminal->Groups->Duplicate();
        }
        if (CapableOwnerChanging)
        {
          UserList = Terminal->Users->Duplicate();
        }
        TRemoteDirectory * Files = Terminal->Files;
        int Count = Files->Count;
        if (Count > 100)
        {
          Count = 100;
        }
        for (int Index = 0; Index < Count; Index++)
        {
          TRemoteFile * File = Files->Files[Index];
          if (CapableGroupChanging)
          {
            GroupList->AddUnique(File->Group);
          }
          if (CapableOwnerChanging)
          {
            UserList->AddUnique(File->Owner);
          }
        }

        // if we haven't collected tokens for all files in current directory,
        // make sure we collect them at least for all selected files.
        // (note that so far the files in FileList has to be from current direcotry)
        if (Count < Files->Count)
        {
          for (int Index = 0; Index < FileList->Count; Index++)
          {
            TRemoteFile * File = (TRemoteFile *)(FileList->Objects[Index]);
            if (CapableGroupChanging)
            {
              GroupList->AddUnique(File->Group);
            }
            if (CapableOwnerChanging)
            {
              UserList->AddUnique(File->Owner);
            }
          }
        }
      }

      CurrentProperties = TRemoteProperties::CommonProperties(FileList);

      int Flags = 0;
      if (Terminal->IsCapable[fcModeChanging]) Flags |= cpMode;
      if (CapableOwnerChanging) Flags |= cpOwner;
      if (CapableGroupChanging) Flags |= cpGroup;

      TCalculateChecksumEvent CalculateChecksumEvent = NULL;
      if (Terminal->IsCapable[fcCalculatingChecksum])
      {
        CalculateChecksumEvent = CalculateChecksum;
      }

      std::unique_ptr<TStrings> ChecksumAlgs(new TStringList());
      Terminal->GetSupportedChecksumAlgs(ChecksumAlgs.get());

      TRemoteProperties NewProperties = CurrentProperties;
      Result =
        DoPropertiesDialog(FileList, RemoteDirView->PathName,
          GroupList, UserList, ChecksumAlgs.get(), &NewProperties, Flags,
          Terminal->IsCapable[fcGroupOwnerChangingByID],
          CalculateSize, CalculateChecksumEvent);
      if (Result)
      {
        NewProperties = TRemoteProperties::ChangedProperties(CurrentProperties, NewProperties);
        Terminal->ChangeFilesProperties(FileList, &NewProperties);
      }
    }
    __finally
    {
      delete GroupList;
      delete UserList;
    }
  }
  else
  {
    DirView(Side)->DisplayPropertiesMenu();
    Result = true;
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::KeyProcessed(Word & Key, TShiftState Shift)
{
  if (Shift * AllKeyShiftStates() == (TShiftState() << ssAlt))
  {
    FIgnoreNextDialogChar = Key;
  }
  Key = 0;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::CheckCustomCommandShortCut(
  TCustomCommandList * List, Word & Key, Classes::TShiftState Shift, TShortCut KeyShortCut)
{
  const TCustomCommandType * Command = List->Find(KeyShortCut);
  if (Command != NULL)
  {
    KeyProcessed(Key, Shift);
    if (CustomCommandState(*Command, false, ccltAll) > 0)
    {
      ExecuteFileOperationCommand(foCustomCommand, osRemote,
        false, false, const_cast<TCustomCommandType *>(Command));
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::KeyDown(Word & Key, Classes::TShiftState Shift)
{
  if (QueueView3->Focused() && (QueueView3->OnKeyDown != NULL))
  {
    QueueView3->OnKeyDown(QueueView3, Key, Shift);
  }

  if (!DirView(osCurrent)->IsEditing())
  {
    TShortCut KeyShortCut = ShortCut(Key, Shift);
    for (int Index = 0; Index < NonVisualDataModule->ExplorerActions->ActionCount; Index++)
    {
      TAction * Action = (TAction *)NonVisualDataModule->ExplorerActions->Actions[Index];
      if (((Action->ShortCut == KeyShortCut) ||
           (Action->SecondaryShortCuts->IndexOfShortCut(KeyShortCut) >= 0)) &&
          AllowedAction(Action, aaShortCut))
      {
        // Has to be called before the action as the dialog char is already in queue.
        // So when the action consumes message queue, we already need to have the
        // FIgnoreNextDialogChar set
        KeyProcessed(Key, Shift);
        // Reset reference to previous component (when menu/toolbar was clicked).
        // Needed to detect that action was invoked by keyboard shortcut
        // in TNonVisualDataModule::ExplorerActionsExecute
        Action->ActionComponent = NULL;
        Action->Execute();
        return;
      }
    }
    for (int i = 0; i < TTerminalManager::Instance()->Count; i++)
    {
      if (NonVisualDataModule->OpenSessionShortCut(i) == KeyShortCut)
      {
        KeyProcessed(Key, Shift);
        TTerminalManager::Instance()->ActiveTerminalIndex = i;
        return;
      }
    }
    if (Key == VK_TAB && Shift.Contains(ssCtrl))
    {
      KeyProcessed(Key, Shift);
      TTerminalManager::Instance()->CycleTerminals(!Shift.Contains(ssShift));
    }

    if (IsCustomShortCut(KeyShortCut))
    {
      CheckCustomCommandShortCut(WinConfiguration->CustomCommandList, Key, Shift, KeyShortCut);
      CheckCustomCommandShortCut(WinConfiguration->ExtensionList, Key, Shift, KeyShortCut);

      if (WinConfiguration->SharedBookmarks != NULL)
      {
        TBookmark * Bookmark = WinConfiguration->SharedBookmarks->FindByShortCut(KeyShortCut);
        if ((Bookmark != NULL) &&
            OpenBookmark(Bookmark->Local, Bookmark->Remote))
        {
          KeyProcessed(Key, Shift);
        }
      }
    }
  }

  TForm::KeyDown(Key, Shift);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::InitStatusBar()
{
  const TSessionInfo & SessionInfo = Terminal->GetSessionInfo();
  const TFileSystemInfo & FileSystemInfo = Terminal->GetFileSystemInfo();
  TTBXStatusBar * SessionStatusBar = (TTBXStatusBar *)GetComponent(fcStatusBar);
  DebugAssert(Terminal);

  int Offset = SessionStatusBar->Panels->Count - SessionPanelCount;

  bool SecurityEnabled = !SessionInfo.SecurityProtocolName.IsEmpty();
  SessionStatusBar->Panels->Items[Offset + 0]->Enabled = SecurityEnabled;
  // expanded from ?: to avoid memory leaks
  if (SecurityEnabled)
  {
    SessionStatusBar->Panels->Items[Offset + 0]->Hint =
      FMTLOAD(STATUS_SECURE, (SessionInfo.SecurityProtocolName));
  }
  else
  {
    SessionStatusBar->Panels->Items[Offset + 0]->Hint = LoadStr(STATUS_INSECURE);
  }

  if (FileSystemInfo.ProtocolName.IsEmpty())
  {
    SessionStatusBar->Panels->Items[Offset + 1]->Caption = SessionInfo.ProtocolName;
  }
  else
  {
    SessionStatusBar->Panels->Items[Offset + 1]->Caption = FileSystemInfo.ProtocolName;
  }
  SessionStatusBar->Panels->Items[Offset + 1]->Hint = LoadStr(STATUS_PROTOCOL_HINT);

  SessionStatusBar->Panels->Items[Offset + 2]->Enabled =
    (!SessionInfo.CSCompression.IsEmpty() || !SessionInfo.SCCompression.IsEmpty());
  if (SessionInfo.CSCompression == SessionInfo.SCCompression)
  {
    SessionStatusBar->Panels->Items[Offset + 2]->Hint =
      FMTLOAD(STATUS_COMPRESSION_HINT, (DefaultStr(SessionInfo.CSCompression, LoadStr(NO_STR))));
  }
  else
  {
    SessionStatusBar->Panels->Items[Offset + 2]->Hint = FMTLOAD(STATUS_COMPRESSION2_HINT,
      (DefaultStr(SessionInfo.CSCompression, LoadStr(NO_STR)),
       DefaultStr(SessionInfo.SCCompression, LoadStr(NO_STR))));
  }

  SessionStatusBar->Panels->Items[Offset + 3]->Hint = LoadStr(STATUS_DURATION_HINT);

  UpdateStatusBar();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateStatusBar()
{
  TTBXStatusBar * SessionStatusBar = (TTBXStatusBar *)GetComponent(fcStatusBar);
  DebugAssert(SessionStatusBar != NULL);
  if (!Terminal || !Terminal->Active || Terminal->Status < ssOpened)
  {
    // note: (Terminal->Status < sshReady) currently never happens here,
    // so STATUS_CONNECTING is never used
    SessionStatusBar->SimplePanel = true;
    SessionStatusBar->SimpleText = LoadStr(
      !Terminal || !Terminal->Active ? STATUS_NOT_CONNECTED : STATUS_CONNECTING);
  }
  else
  {
    DebugAssert(Terminal);
    SessionStatusBar->SimplePanel = false;
    const TSessionInfo & SessionInfo = Terminal->GetSessionInfo();

    if (!FNote.IsEmpty())
    {
      SessionStatusBar->Panels->Items[0]->Caption = FNote;
    }
    else
    {
      UpdateStatusPanelText(SessionStatusBar->Panels->Items[0]);
    }

    SessionStatusBar->Panels->Items[0]->Hint = FNoteHints;

    SessionStatusBar->Panels->Items[SessionStatusBar->Panels->Count - 1]->Caption =
      FormatDateTimeSpan(Configuration->TimeFormat, Now() - SessionInfo.LoginTime);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateStatusPanelText(TTBXStatusPanel * Panel)
{
  Panel->Caption = L"";
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::Idle()
{

  if (FShowing || FStandaloneEditing)
  {
    FEditorManager->Check();

    // make sure that Idle is called before update queue, as it may invoke QueueEvent
    // that needs to know if queue view is visible (and it may be closed after queue update)
    TTerminalManager::Instance()->Idle();
  }

  if (FShowing)
  {
    if (!NonVisualDataModule->Busy &&
        // Menu is opened or drag&drop is going on
        (Mouse->Capture == NULL))
    {
      if (FRefreshRemoteDirectory)
      {
        if ((Terminal != NULL) && (Terminal->Status == ssOpened))
        {
          Terminal->RefreshDirectory();
        }
        FRefreshRemoteDirectory = false;
      }
      if (FRefreshLocalDirectory)
      {
        ReloadLocalDirectory();
        FRefreshLocalDirectory = false;
      }

      if (WinConfiguration->RefreshRemotePanel)
      {
        TManagedTerminal * ManagedTerminal =
          dynamic_cast<TManagedTerminal *>(Terminal);
        if ((ManagedTerminal != NULL) && (Terminal->Status == ssOpened) &&
            (Now() - ManagedTerminal->DirectoryLoaded >
               WinConfiguration->RefreshRemotePanelInterval))
        {
          RemoteDirView->ReloadDirectory();
        }
      }
    }
  }

  if (FShowing || FStandaloneEditing)
  {
    if (FQueueStatusInvalidated)
    {
      UpdateQueueStatus(false);
    }

    RefreshQueueItems();
  }

  if (FShowing)
  {
    UpdateStatusBar();
  }

  FIgnoreNextDialogChar = 0;

}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UserActionTimer(TObject * /*Sender*/)
{
  try
  {
    FUserActionTimer->Enabled = false;
    if (IsQueueAutoPopup() && (FPendingQueueActionItem != NULL))
    {
      if (TQueueItem::IsUserActionStatus(FPendingQueueActionItem->Status))
      {
        FPendingQueueActionItem->ProcessUserAction();
      }
    }
  }
  __finally
  {
    FPendingQueueActionItem = NULL;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ApplicationMinimize(TObject * /*Sender*/)
{
  if (WinConfiguration->MinimizeToTray)
  {
    UpdateTrayIcon();
    FTrayIcon->Visible = true;
    if (Visible)
    {
      ShowWindow(Handle, SW_HIDE);
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ApplicationRestore(TObject * /*Sender*/)
{
  // WORKAROUND
  // When restoring maximized window from minimization,
  // rarely some controls do not align properly.
  // Two instances seen (both for Commander):
  // - When restoring, window is temporarily narrower (not maximizer),
  //   causing toolbars on TopDock to wrap and dock to expand horizontally.
  //   Once maximized already, top dock shinks back, but the session PageControl,
  //   do not align up, leaving space between TopDock and PageControl.
  // - Similar issue seem with LocalDirView not aligning down to status bar.
  for (int Index = 0; Index < ControlCount; Index++)
  {
    RealignControl(Controls[Index]);
  }

  if (FTrayIcon->Visible)
  {
    FTrayIcon->Visible = false;
    if (Visible)
    {
      ShowWindow(Handle, SW_SHOW);
    }
  }

  if (FNeedSession && DebugAlwaysTrue(Terminal == NULL))
  {
    FNeedSession = false;
    NonVisualDataModule->StartBusy();
    try
    {
      NeedSession(false);
    }
    __finally
    {
      NonVisualDataModule->EndBusy();
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateTrayIcon()
{
  FTrayIcon->Hint = Caption;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ApplicationTitleChanged()
{
  UpdateTrayIcon();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RestoreApp()
{
  // workaround
  // TApplication.WndProc sets TApplication.FAppIconic to false,
  // on WM_ACTIVATEAPP, what renders TApplication.Restore no-op function.
  // But WM_ACTIVATEAPP message can be received even when
  // the main window is minimized to the tray and internal editor window is focused
  // (after another application was previously active)
  if (::IsIconic(Handle))
  {
    if (!IsAppIconic())
    {
      SetAppIconic(true);
    }
  }
  ::ApplicationRestore();
  Application->BringToFront();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::TrayIconClick(TObject * /*Sender*/)
{
  RestoreApp();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::NewSession(bool FromSite, const UnicodeString & SessionUrl)
{
  if (OpenInNewWindow())
  {
    // todo: Pass FromSite
    ExecuteNewInstance(SessionUrl);
  }
  else
  {
    TTerminalManager::Instance()->NewSession(FromSite, SessionUrl);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DuplicateSession()
{
  // current working directories become defaults here, what is not right
  TSessionData * SessionData = CloneCurrentSessionData();
  try
  {
    if (OpenInNewWindow())
    {
      UnicodeString SessionName = StoredSessions->HiddenPrefix + Terminal->SessionData->Name;
      StoredSessions->NewSession(SessionName, SessionData);
      // modified only, explicit
      StoredSessions->Save(false, true);
      // encode session name because of slashes in hierarchical sessions
      ExecuteNewInstance(EncodeUrlString(SessionName));
    }
    else
    {
      TTerminalManager * Manager = TTerminalManager::Instance();
      TTerminal * Terminal = Manager->NewTerminal(SessionData);
      Manager->ActiveTerminal = Terminal;
    }
  }
  __finally
  {
    delete SessionData;
  }
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::CanCloseQueue()
{
  DebugAssert(FQueue != NULL);
  bool Result = FQueue->IsEmpty;
  if (!Result)
  {
    SetFocus();
    Result = (MessageDialog(LoadStr(PENDING_QUEUE_ITEMS2), qtWarning, qaOK | qaCancel, HELP_NONE) == qaOK);
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::CloseSession()
{
  if (CanCloseQueue())
  {
    TTerminalManager::Instance()->FreeActiveTerminal();
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::OpenStoredSession(TSessionData * Data)
{
  if (OpenInNewWindow())
  {
    // encode session name because of slashes in hierarchical sessions
    ExecuteNewInstance(EncodeUrlString(Data->Name));
  }
  else
  {
    TTerminalManager * Manager = TTerminalManager::Instance();
    TTerminal * Terminal = Manager->NewTerminal(Data);
    Manager->ActiveTerminal = Terminal;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::OpenFolderOrWorkspace(const UnicodeString & Name)
{
  if (OpenInNewWindow())
  {
    ExecuteNewInstance(Name);
  }
  else
  {
    TTerminalManager * Manager = TTerminalManager::Instance();
    std::unique_ptr<TObjectList> DataList(new TObjectList());
    StoredSessions->GetFolderOrWorkspace(Name, DataList.get());
    Manager->ActiveTerminal = Manager->NewTerminals(DataList.get());
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::FormCloseQuery(TObject * /*Sender*/,
      bool &CanClose)
{
  if (Terminal != NULL)
  {
    if (Terminal->Active && WinConfiguration->ConfirmClosingSession)
    {
      unsigned int Result;
      TMessageParams Params(mpNeverAskAgainCheck);
      UnicodeString Message;
      int Answers = qaOK | qaCancel;
      if (TTerminalManager::Instance()->Count > 1)
      {
        if (!WinConfiguration->AutoSaveWorkspace)
        {
          Message = LoadStr(CLOSE_SESSIONS_WORKSPACE2);
          Answers = qaYes | qaNo | qaCancel;
        }
        else
        {
          Message = MainInstructions(LoadStr(CLOSE_SESSIONS));
        }
      }
      else
      {
        Message = MainInstructions(FMTLOAD(CLOSE_SESSION, (Terminal->SessionData->SessionName)));
      }

      if (WinConfiguration->AutoSaveWorkspace)
      {
        Message =
          FORMAT("%s\n\n%s", (Message,
            FMTLOAD(AUTO_WORKSPACE, (WorkspaceName()))));
      }

      SetFocus();
      Result = MessageDialog(Message, qtConfirmation,
        Answers, HELP_NONE, &Params);

      if (Result == qaNeverAskAgain)
      {
        WinConfiguration->ConfirmClosingSession = false;
      }

      if (Result == qaNo)
      {
        CanClose = SaveWorkspace(true);
        // note that the workspace will be saved redundatly again from FormClose
      }
      else
      {
        CanClose =
          (Result == qaOK) ||
          (Result == qaYes) || // CLOSE_SESSIONS_WORKSPACE variant
          (Result == qaNeverAskAgain);
      }
    }

    if (CanClose)
    {
      CanClose = CanCloseQueue();
    }
  }

  if (CanClose)
  {
    CanClose =
      FEditorManager->CloseInternalEditors(CloseInternalEditor) &&
      FEditorManager->CloseExternalFilesWithoutProcess();

    if (CanClose)
    {
      while (CanClose && (FLocalEditors->Count > 0))
      {
        int PrevCount = FLocalEditors->Count;
        static_cast<TForm *>(FLocalEditors->Items[0])->Close();
        CanClose = (FLocalEditors->Count < PrevCount);
      }

      if (CanClose)
      {
        CanClose = FEditorManager->Empty(true);
        if (!CanClose)
        {
          SetFocus();
          CanClose =
            (MessageDialog(
              LoadStr(PENDING_EDITORS), qtWarning, qaIgnore | qaCancel, HELP_NONE) == qaIgnore);
        }
      }
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::CloseInternalEditor(TObject * Sender)
{
  TForm * Form = dynamic_cast<TForm *>(Sender);
  DebugAssert(Form != NULL);
  Form->Close();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ForceCloseInternalEditor(TObject * Sender)
{
  TForm * Form = dynamic_cast<TForm *>(Sender);
  delete Form;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ForceCloseLocalEditors()
{

  while (FLocalEditors->Count > 0)
  {
    delete static_cast<TForm *>(FLocalEditors->Items[0]);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemoteDirViewDisplayProperties(
      TObject *Sender)
{
  TStrings *FileList = ((TUnixDirView*)Sender)->CreateFileList(True, False, NULL);
  try
  {
    SetProperties(osRemote, FileList);
  }
  __finally
  {
    delete FileList;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ComponentShowing(Byte Component, bool value)
{
  if (value)
  {
    if (Component == fcCustomCommandsBand)
    {
      UpdateCustomCommandsToolbar();
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SetComponentVisible(Byte Component, Boolean value)
{
  TControl * Control = GetComponent(Component);
  DebugAssert(Control);
  bool Changed = (Control->Visible != value);
  if (Changed)
  {
    ComponentShowing(Component, value);

    TWinControl * WinControl = dynamic_cast<TWinControl*>(Control);
    bool WasFocused = (WinControl != NULL) && (ActiveControl != NULL) &&
      ((ActiveControl == WinControl) || (ActiveControl->Parent == WinControl));
    if (value)
    {
      int RemainingHeight = Control->Parent->ClientHeight;
      int RemainingWidth = Control->Parent->ClientWidth;
      for (int i = 0; i < Control->Parent->ControlCount; i++)
      {
        TControl * ChildControl = Control->Parent->Controls[i];
        if (ChildControl->Visible)
        {
          switch (ChildControl->Align)
          {
            case alTop:
            case alBottom:
              RemainingHeight -= ChildControl->Height;
              break;

            case alLeft:
            case alRight:
              RemainingWidth -= ChildControl->Width;
              break;
          }
        }
      }

      int Reserve = ScaleByTextHeight(this, 32);
      // queue in explorer, trees in commander
      if (Control->Height > RemainingHeight - Reserve)
      {
        Control->Height = RemainingHeight / 2;
      }

      if (Control->Width > RemainingWidth - Reserve)
      {
        Control->Width = RemainingWidth / 2;
      }
    }
    Control->Visible = value;
    if (WasFocused && Visible)
    {
      DirView(osCurrent)->SetFocus();
    }

    FixControlsPlacement();
  }
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::GetComponentVisible(Byte Component)
{
  TControl * Control = GetComponent(Component);
  if (Control == NULL)
  {
    return false;
  }
  else
  {
    return Control->Visible;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::FixControlsPlacement()
{
  if (RemoteDirView->ItemFocused != NULL)
  {
    RemoteDirView->ItemFocused->MakeVisible(false);
  }
  QueueSplitter->Visible = QueuePanel->Visible;
  RemotePanelSplitter->Visible = RemoteDriveView->Visible;
}
//---------------------------------------------------------------------------
TControl * __fastcall TCustomScpExplorerForm::GetComponent(Byte Component)
{
  switch (Component) {
    case fcStatusBar: return RemoteStatusBar;
    case fcRemotePopup: return reinterpret_cast<TControl *>(NonVisualDataModule->RemoteFilePopup);
    case fcQueueView: return QueuePanel;
    case fcQueueToolbar: return QueueDock;
    case fcRemoteTree: return RemoteDriveView;
    case fcSessionsTabs: return SessionsPageControl;
    default: return NULL;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DirViewColumnRightClick(
      TObject *Sender, TListColumn *Column, TPoint &Point)
{
  DebugAssert(NonVisualDataModule && Column && Sender);
  NonVisualDataModule->ListColumn = Column;
  TPoint ScreenPoint = ((TControl*)Sender)->ClientToScreen(Point);
  TPopupMenu * DirViewColumnMenu;
  if (Sender == RemoteDirView)
  {
    DirViewColumnMenu = NonVisualDataModule->RemoteDirViewColumnPopup;
    NonVisualDataModule->RemoteSortByExtColumnPopupItem->Visible =
      (Column->Index == uvName);
    NonVisualDataModule->RemoteFormatSizeBytesPopupItem->Visible =
      (Column->Index == uvSize);
  }
  else
  {
    DirViewColumnMenu = NonVisualDataModule->LocalDirViewColumnPopup;
    NonVisualDataModule->LocalSortByExtColumnPopupItem->Visible =
      (Column->Index == dvName);
    NonVisualDataModule->LocalFormatSizeBytesPopupItem->Visible =
      (Column->Index == dvSize);
  }
  DirViewColumnMenu->Popup(ScreenPoint.x, ScreenPoint.y);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DirViewExecFile(
      TObject *Sender, TListItem *Item, bool &AllowExec)
{
  DoDirViewExecFile(Sender, Item, AllowExec);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DoDirViewExecFile(TObject * Sender,
  TListItem * Item, bool & AllowExec)
{
  DebugAssert(Sender && Item && Configuration);
  DebugAssert(AllowExec);
  TCustomDirView * ADirView = (TCustomDirView *)Sender;
  bool Remote = (ADirView == DirView(osRemote));
  bool ResolvedSymlinks = !Remote || Terminal->ResolvingSymlinks;

  // Anything special is done on double click only (not on "open" indicated by FForceExecution),
  // on files only (not directories)
  // and only when symlinks are resolved (apply to remote panel only)
  if (!ADirView->ItemIsDirectory(Item) &&
      (ResolvedSymlinks || FForceExecution))
  {
    if ((WinConfiguration->DoubleClickAction != dcaOpen) &&
        !FForceExecution &&
        ResolvedSymlinks)
    {
      if (WinConfiguration->DoubleClickAction == dcaCopy)
      {
        ExecuteFileOperation(foCopy,
          (ADirView == DirView(osRemote) ? osRemote : osLocal),
          true, !WinConfiguration->CopyOnDoubleClickConfirmation);
        AllowExec = false;
      }
      else if (WinConfiguration->DoubleClickAction == dcaEdit)
      {
        if (!Remote || !WinConfiguration->DisableOpenEdit)
        {
          ExecuteFile(osCurrent, efDefaultEditor);
          AllowExec = false;
        }
      }
      else
      {
        DebugFail();
      }
    }

    // if we have not done anything special, fall back to default behavior
    if (AllowExec)
    {
      if (Remote && !WinConfiguration->DisableOpenEdit)
      {
        ExecuteFile(osRemote, efShell);
        AllowExec = false;
      }
    }
  }
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::GetHasDirView(TOperationSide Side)
{
  return ((Side == osRemote) || (Side == osCurrent));
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::CompareDirectories()
{
  DebugFail();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SynchronizeDirectories()
{
  DebugFail();
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::DoSynchronizeDirectories(
  UnicodeString & LocalDirectory, UnicodeString & RemoteDirectory, bool UseDefaults)
{
  TSynchronizeParamType Params;
  Params.LocalDirectory = LocalDirectory;
  Params.RemoteDirectory = RemoteDirectory;
  int UnusedParams =
    (GUIConfiguration->SynchronizeParams &
      (spPreviewChanges | spTimestamp | spNotByTime | spBySize));
  Params.Params = GUIConfiguration->SynchronizeParams & ~UnusedParams;
  Params.Options = GUIConfiguration->SynchronizeOptions;
  bool SaveSettings = false;
  TSynchronizeController Controller(&DoSynchronize, &DoSynchronizeInvalid,
    &DoSynchronizeTooManyDirectories);
  DebugAssert(FSynchronizeController == NULL);
  FSynchronizeController = &Controller;
  bool Result;
  try
  {
    TCopyParamType CopyParam = GUIConfiguration->CurrentCopyParam;
    int CopyParamAttrs = Terminal->UsableCopyParamAttrs(0).Upload;
    int Options =
      FLAGMASK(SynchronizeAllowSelectedOnly(), soAllowSelectedOnly);
    DebugAssert(FOnFeedSynchronizeError == NULL);
    Result = DoSynchronizeDialog(Params, &CopyParam, Controller.StartStop,
      SaveSettings, Options, CopyParamAttrs, GetSynchronizeOptions, SynchronizeSessionLog,
      FOnFeedSynchronizeError, UseDefaults);
    if (Result)
    {
      if (SaveSettings)
      {
        GUIConfiguration->SynchronizeParams = Params.Params | UnusedParams;
        GUIConfiguration->SynchronizeOptions = Params.Options;
      }
      else
      {
        if (FLAGSET(GUIConfiguration->SynchronizeOptions, soSynchronizeAsk) &&
            FLAGCLEAR(Params.Options, soSynchronizeAsk) &&
            FLAGSET(Params.Options, soSynchronize))
        {
          GUIConfiguration->SynchronizeOptions =
            (GUIConfiguration->SynchronizeOptions & ~soSynchronizeAsk) |
            soSynchronize;
        }
      }
      LocalDirectory = Params.LocalDirectory;
      RemoteDirectory = Params.RemoteDirectory;
    }
  }
  __finally
  {
    FSynchronizeController = NULL;
    DebugAssert(FOnFeedSynchronizeError == NULL);
    FOnFeedSynchronizeError = NULL;
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DoSynchronize(
  TSynchronizeController * /*Sender*/, const UnicodeString LocalDirectory,
  const UnicodeString RemoteDirectory, const TCopyParamType & CopyParam,
  const TSynchronizeParamType & Params, TSynchronizeChecklist ** Checklist,
  TSynchronizeOptions * Options, bool Full)
{
  if (Terminal->Status == ssOpened)
  {
    try
    {
      int PParams = Params.Params;
      if (!Full)
      {
        PParams |= TTerminal::spNoRecurse | TTerminal::spUseCache |
          TTerminal::spDelayProgress | TTerminal::spSubDirs;
      }
      else
      {
        // if keepuptodate is non-recursive,
        // full sync before has to be non-recursive as well
        if (FLAGCLEAR(Params.Options, soRecurse))
        {
          PParams |= TTerminal::spNoRecurse;
        }
      }
      Synchronize(LocalDirectory, RemoteDirectory, smRemote, CopyParam,
        PParams, Checklist, Options);
    }
    catch(Exception & E)
    {
      ShowExtendedExceptionEx(Terminal, &E);
      throw;
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DoSynchronizeInvalid(
  TSynchronizeController * /*Sender*/, const UnicodeString Directory,
  const UnicodeString ErrorStr)
{
  if (!Directory.IsEmpty())
  {
    SimpleErrorDialog(FMTLOAD(WATCH_ERROR_DIRECTORY, (Directory)), ErrorStr);
  }
  else
  {
    SimpleErrorDialog(LoadStr(WATCH_ERROR_GENERAL), ErrorStr);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DoSynchronizeTooManyDirectories(
  TSynchronizeController * /*Sender*/, int & MaxDirectories)
{
  if (MaxDirectories < GUIConfiguration->MaxWatchDirectories)
  {
    MaxDirectories = GUIConfiguration->MaxWatchDirectories;
  }
  else
  {
    TMessageParams Params(mpNeverAskAgainCheck);
    unsigned int Result = MessageDialog(
      FMTLOAD(TOO_MANY_WATCH_DIRECTORIES, (MaxDirectories, MaxDirectories)),
      qtConfirmation, qaYes | qaNo, HELP_TOO_MANY_WATCH_DIRECTORIES, &Params);

    if ((Result == qaYes) || (Result == qaNeverAskAgain))
    {
      MaxDirectories *= 2;
      if (Result == qaNeverAskAgain)
      {
        GUIConfiguration->MaxWatchDirectories = MaxDirectories;
      }
    }
    else
    {
      Abort();
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::Synchronize(const UnicodeString LocalDirectory,
  const UnicodeString RemoteDirectory, TSynchronizeMode Mode,
  const TCopyParamType & CopyParam, int Params, TSynchronizeChecklist ** Checklist,
  TSynchronizeOptions * Options)
{
  DebugAssert(!FAutoOperation);
  void * BatchStorage;
  BatchStart(BatchStorage);
  FAutoOperation = true;

  bool AnyOperation = false;
  TDateTime StartTime = Now();
  TSynchronizeChecklist * AChecklist = NULL;
  try
  {
    FSynchronizeProgressForm = new TSynchronizeProgressForm(Application, true, true);
    if (FLAGCLEAR(Params, TTerminal::spDelayProgress))
    {
      FSynchronizeProgressForm->Start();
    }

    AChecklist = Terminal->SynchronizeCollect(LocalDirectory, RemoteDirectory,
      static_cast<TTerminal::TSynchronizeMode>(Mode),
      &CopyParam, Params | spNoConfirmation, TerminalSynchronizeDirectory,
      Options);

    SAFE_DESTROY(FSynchronizeProgressForm);

    FSynchronizeProgressForm = new TSynchronizeProgressForm(Application, true, false);
    if (FLAGCLEAR(Params, TTerminal::spDelayProgress))
    {
      FSynchronizeProgressForm->Start();
    }

    for (int Index = 0; !AnyOperation && (Index < AChecklist->Count); Index++)
    {
      AnyOperation = AChecklist->Item[Index]->Checked;
    }

    // No need to call if !AnyOperation
    Terminal->SynchronizeApply(AChecklist, LocalDirectory, RemoteDirectory,
      &CopyParam, Params | spNoConfirmation, TerminalSynchronizeDirectory);
  }
  __finally
  {
    if (Checklist == NULL)
    {
      delete AChecklist;
    }
    else
    {
      *Checklist = AChecklist;
    }

    FAutoOperation = false;
    SAFE_DESTROY(FSynchronizeProgressForm);
    BatchEnd(BatchStorage);
    ReloadLocalDirectory();
    if (AnyOperation)
    {
      OperationComplete(StartTime);
    }
  }
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::SynchronizeAllowSelectedOnly()
{
  // can be called from command line
  return Visible &&
    ((DirView(osRemote)->SelCount > 0) ||
     (HasDirView[osLocal] && (DirView(osLocal)->SelCount > 0)));
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SynchronizeSessionLog(const UnicodeString & Message)
{
  LogSynchronizeEvent(Terminal, Message);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::GetSynchronizeOptions(
  int Params, TSynchronizeOptions & Options)
{
  if (FLAGSET(Params, spSelectedOnly) && SynchronizeAllowSelectedOnly())
  {
    Options.Filter = new TStringList();
    Options.Filter->CaseSensitive = false;
    Options.Filter->Duplicates = Types::dupAccept;

    if (DirView(osRemote)->SelCount > 0)
    {
      DirView(osRemote)->CreateFileList(false, false, Options.Filter);
    }
    if (HasDirView[osLocal] && (DirView(osLocal)->SelCount > 0))
    {
      DirView(osLocal)->CreateFileList(false, false, Options.Filter);
    }
    Options.Filter->Sort();
  }
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::DoFullSynchronizeDirectories(
  UnicodeString & LocalDirectory, UnicodeString & RemoteDirectory,
  TSynchronizeMode & Mode, bool & SaveMode, bool UseDefaults)
{
  bool Result;
  int Params = GUIConfiguration->SynchronizeParams;

  bool SaveSettings = false;
  int Options =
    FLAGMASK(!Terminal->IsCapable[fcTimestampChanging], fsoDisableTimestamp) |
    FLAGMASK(SynchronizeAllowSelectedOnly(), fsoAllowSelectedOnly);
  TCopyParamType CopyParam = GUIConfiguration->CurrentCopyParam;
  TUsableCopyParamAttrs CopyParamAttrs = Terminal->UsableCopyParamAttrs(0);
  Result = UseDefaults ||
    DoFullSynchronizeDialog(Mode, Params, LocalDirectory, RemoteDirectory,
      &CopyParam, SaveSettings, SaveMode, Options, CopyParamAttrs);
  if (Result)
  {
    Configuration->Usage->Inc(L"Synchronizations");
    UpdateCopyParamCounters(CopyParam);

    TSynchronizeOptions SynchronizeOptions;
    GetSynchronizeOptions(Params, SynchronizeOptions);

    if (SaveSettings)
    {
      GUIConfiguration->SynchronizeParams = Params;
    }
    else
    {
      SaveMode = false;
    }

    TDateTime StartTime = Now();

    TSynchronizeChecklist * Checklist = NULL;
    try
    {
      DebugAssert(!FAutoOperation);
      FAutoOperation = true;

      try
      {
        FSynchronizeProgressForm = new TSynchronizeProgressForm(Application, true, true);
        FSynchronizeProgressForm->Start();

        Checklist = Terminal->SynchronizeCollect(LocalDirectory, RemoteDirectory,
          static_cast<TTerminal::TSynchronizeMode>(Mode),
          &CopyParam, Params | spNoConfirmation, TerminalSynchronizeDirectory,
          &SynchronizeOptions);
      }
      __finally
      {
        FAutoOperation = false;
        SAFE_DESTROY(FSynchronizeProgressForm);
      }

      if (Checklist->Count == 0)
      {
        UnicodeString Message = MainInstructions(LoadStr(COMPARE_NO_DIFFERENCES));
        MessageDialog(Message, qtInformation, qaOK,
          HELP_SYNCHRONIZE_NO_DIFFERENCES);
      }
      else if (FLAGCLEAR(Params, spPreviewChanges) ||
               DoSynchronizeChecklistDialog(Checklist, Mode, Params,
                 LocalDirectory, RemoteDirectory, CustomCommandMenu))
      {
        DebugAssert(!FAutoOperation);
        void * BatchStorage;
        BatchStart(BatchStorage);
        FAutoOperation = true;

        if (FLAGSET(Params, spPreviewChanges))
        {
          StartTime = Now();
        }

        try
        {
          FSynchronizeProgressForm = new TSynchronizeProgressForm(Application, true, false);
          FSynchronizeProgressForm->Start();

          Terminal->SynchronizeApply(Checklist, LocalDirectory, RemoteDirectory,
            &CopyParam, Params | spNoConfirmation, TerminalSynchronizeDirectory);
        }
        __finally
        {
          FAutoOperation = false;
          SAFE_DESTROY(FSynchronizeProgressForm);
          BatchEnd(BatchStorage);
          ReloadLocalDirectory();
        }
      }
    }
    __finally
    {
      delete Checklist;
    }

    OperationComplete(StartTime);
  }

  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::TerminalSynchronizeDirectory(
  const UnicodeString LocalDirectory, const UnicodeString RemoteDirectory,
  bool & Continue, bool /*Collect*/)
{
  DebugAssert(FSynchronizeProgressForm != NULL);
  if (!FSynchronizeProgressForm->Started)
  {
    FSynchronizeProgressForm->Start();
  }
  FSynchronizeProgressForm->SetData(LocalDirectory, RemoteDirectory, Continue);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::StandaloneEdit(const UnicodeString & FileName)
{
  UnicodeString FullFileName = AbsolutePath(FTerminal->CurrentDirectory, FileName);

  TRemoteFile * File = NULL;
  Terminal->ReadFile(FullFileName, File);
  if (File != NULL)
  {
    std::unique_ptr<TRemoteFile> FileOwner(File);
    TAutoFlag Flag(FStandaloneEditing);

    ExecuteRemoteFile(FullFileName, File, efInternalEditor);

    Application->ShowMainForm = false;

    Application->Run();
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ExploreLocalDirectory()
{
  DebugFail();
}
//---------------------------------------------------------------------------
TSessionData * __fastcall TCustomScpExplorerForm::CloneCurrentSessionData()
{
  std::unique_ptr<TSessionData> SessionData(new TSessionData(L""));
  SessionData->Assign(Terminal->SessionData);
  UpdateSessionData(SessionData.get());
  TTerminalManager::Instance()->UpdateSessionCredentials(SessionData.get());
  if (Terminal->SessionData->IsWorkspace)
  {
    // Have to reset the "Workspace/XXX" name which would become user-visible
    // once IsWorkspace is cleared
    SessionData->Name = UnicodeString();
    SessionData->IsWorkspace = false;
  }
  return SessionData.release();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SaveCurrentSession()
{
  TSessionData * SessionData = CloneCurrentSessionData();
  try
  {
    TSessionData * EditingSessionData = StoredSessions->FindSame(SessionData);

    DoSaveSession(SessionData, EditingSessionData, true, NULL);
  }
  __finally
  {
    delete SessionData;
  }
}
//---------------------------------------------------------------------------
TObjectList * __fastcall TCustomScpExplorerForm::DoCollectWorkspace()
{
  TTerminalManager * Manager = TTerminalManager::Instance();
  std::unique_ptr<TObjectList> DataList(new TObjectList());

  if (DebugAlwaysTrue(Terminal != NULL))
  {
    // Update (Managed)Terminal->StateData
    UpdateTerminal(Terminal);
  }
  Manager->SaveWorkspace(DataList.get());

  return DataList.release();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DoSaveWorkspace(
  const UnicodeString & Name, TObjectList * DataList, bool SavePasswords)
{
  WinConfiguration->LastWorkspace = Name;

  if (!SavePasswords)
  {
    for (int Index = 0; Index < DataList->Count; Index++)
    {
      TSessionData * SessionData = dynamic_cast<TSessionData *>(DataList->Items[Index]);

      if (SessionData->Link.IsEmpty())
      {
        SessionData->ClearSessionPasswords();
      }
    }
  }

  StoredSessions->NewWorkspace(Name, DataList);
  // modified only, explicit
  StoredSessions->Save(false, true);
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TCustomScpExplorerForm::WorkspaceName()
{
  return
    DefaultStr(WinConfiguration->LastWorkspace,
      DefaultStr(WinConfiguration->AutoWorkspace, LoadStr(NEW_WORKSPACE)));
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::SaveWorkspace(bool EnableAutoSave)
{

  std::unique_ptr<TObjectList> DataList(DoCollectWorkspace());

  bool AnyNonStoredSessionWithPassword = false;
  bool AnyNonStoredNonWorkspaceSessionWithPassword = false;
  bool AllNonStoredSessionsAnonymous = true;

  TTerminalManager * Manager = TTerminalManager::Instance();
  for (int Index = 0; Index < DataList->Count; Index++)
  {
    TSessionData * SessionData = dynamic_cast<TSessionData *>(DataList->Items[Index]);

    if (SessionData->Link.IsEmpty())
    {
      if (SessionData->HasAnySessionPassword())
      {
        AnyNonStoredSessionWithPassword = true;
        if (!Manager->Terminals[Index]->SessionData->IsWorkspace)
        {
          AnyNonStoredNonWorkspaceSessionWithPassword = true;
        }
      }

      if (!SameText(SessionData->UserName, AnonymousUserName))
      {
        AllNonStoredSessionsAnonymous = false;
      }
    }
  }

  bool SavePasswords;
  bool * PSavePasswords;
  bool NotRecommendedSavingPasswords =
    !CustomWinConfiguration->UseMasterPassword &&
    !AllNonStoredSessionsAnonymous;

  if (Configuration->DisablePasswordStoring ||
      !AnyNonStoredSessionWithPassword)
  {
    PSavePasswords = NULL;
    SavePasswords = false;
  }
  else
  {
    PSavePasswords = &SavePasswords;
    SavePasswords =
      !AnyNonStoredNonWorkspaceSessionWithPassword ||
      !NotRecommendedSavingPasswords;
  }

  UnicodeString Name = WorkspaceName();
  bool CreateShortcut = false;

  bool Result =
    DoSaveWorkspaceDialog(
      Name, PSavePasswords, NotRecommendedSavingPasswords, CreateShortcut,
      EnableAutoSave);

  if (Result)
  {
    DoSaveWorkspace(Name, DataList.get(), SavePasswords);

    if (CreateShortcut)
    {
      TOperationVisualizer Visualizer;
      UnicodeString AdditionalParams = TProgramParams::FormatSwitch(DESKTOP_SWITCH);
      CreateDesktopSessionShortCut(Name, L"", AdditionalParams, -1, WORKSPACE_ICON);
    }

    if (EnableAutoSave)
    {
      WinConfiguration->AutoSaveWorkspace = true;
      WinConfiguration->AutoWorkspace = Name;
      if (PSavePasswords != NULL)
      {
        WinConfiguration->AutoSaveWorkspacePasswords = SavePasswords;
      }
    }
  }

  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateTerminal(TTerminal * Terminal)
{
  TManagedTerminal * ManagedTerminal = dynamic_cast<TManagedTerminal *>(Terminal);
  DebugAssert(ManagedTerminal != NULL);

  SAFE_DESTROY(ManagedTerminal->RemoteExplorerState);

  if (WinConfiguration->PreservePanelState)
  {
    ManagedTerminal->RemoteExplorerState = RemoteDirView->SaveState();
  }

  UpdateSessionData(ManagedTerminal->StateData);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateSessionData(TSessionData * Data)
{
  // Keep in sync with TSessionData::CopyStateData

  DebugAssert(Data != NULL);

  // cannot use RemoteDirView->Path, because it is empty if connection
  // was already closed
  // also only peek, we may not be connected at all atm
  // so make sure we do not try retrieving current directory from the server
  // (particularly with FTP)
  UnicodeString ACurrentDirectory = Terminal->PeekCurrentDirectory();
  if (!ACurrentDirectory.IsEmpty())
  {
    Data->RemoteDirectory = ACurrentDirectory;
  }
  Data->Color = SessionColor;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ToolBarResize(TObject *Sender)
{
  TTBXToolbar * Toolbar = dynamic_cast<TTBXToolbar*>(Sender);
  DebugAssert(Toolbar != NULL);

  for (int i = 0; i < Toolbar->Items->Count; i++)
  {
    TTBXCustomDropDownItem * DropDownItem;
    DropDownItem = dynamic_cast<TTBXCustomDropDownItem *>(Toolbar->Items->Items[i]);
    if (DropDownItem != NULL)
    {
      ToolbarItemResize(DropDownItem,
        Toolbar->Width - (Toolbar->View->BaseSize.x - DropDownItem->EditWidth) -
        Toolbar->NonClientWidth);
      break;
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ToolbarItemResize(TTBXCustomDropDownItem * Item, int Width)
{
  Item->EditWidth = Width;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ToolbarGetBaseSize(
  TTBCustomToolbar * Toolbar, TPoint & ASize)
{
  for (int i = 0; i < Toolbar->Items->Count; i++)
  {
    TTBXCustomDropDownItem * DropDownItem;
    DropDownItem = dynamic_cast<TTBXCustomDropDownItem *>(Toolbar->Items->Items[i]);
    if (DropDownItem != NULL)
    {
      ASize.x -= DropDownItem->EditWidth;
      ASize.x += ScaleByTextHeight(this, 50) /* minimal combo width */;
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DoWarnLackOfTempSpace(
  const UnicodeString Path, __int64 RequiredSpace, bool & Continue)
{
  if (WinConfiguration->DDWarnLackOfTempSpace)
  {
    UnicodeString ADrive = ExtractFileDrive(ExpandFileName(Path));
    if (!ADrive.IsEmpty())
    {
      __int64 FreeSpace = DiskFree((Byte)(ADrive[1]-'A'+1));
      DebugAssert(RequiredSpace >= 0);
      __int64 RequiredWithReserve;
      RequiredWithReserve = (__int64)(RequiredSpace * WinConfiguration->DDWarnLackOfTempSpaceRatio);
      if (FreeSpace < RequiredWithReserve)
      {
        unsigned int Result;
        TMessageParams Params(mpNeverAskAgainCheck);
        Result = MessageDialog(FMTLOAD(DD_WARN_LACK_OF_TEMP_SPACE, (Path,
          FormatBytes(FreeSpace), FormatBytes(RequiredSpace))),
          qtWarning, qaYes | qaNo, HELP_DD_WARN_LACK_OF_TEMP_SPACE, &Params);

        if (Result == qaNeverAskAgain)
        {
          WinConfiguration->DDWarnLackOfTempSpace = false;
        }

        Continue = (Result == qaYes || Result == qaNeverAskAgain);
      }
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::AddBookmark(TOperationSide Side)
{
  DoOpenDirectoryDialog(odAddBookmark, Side);
}
//---------------------------------------------------------------------------
TStrings * __fastcall TCustomScpExplorerForm::CreateVisitedDirectories(TOperationSide Side)
{
  // we should better use TCustomDirView::FCaseSensitive, but it is private
  TStringList * VisitedDirectories = CreateSortedStringList((Side == osRemote));
  try
  {
    TCustomDirView * DView = DirView(Side);

    for (int Index = -DView->BackCount; Index <= DView->ForwardCount; Index++)
    {
      VisitedDirectories->Add(DView->HistoryPath[Index]);
    }
  }
  catch (...)
  {
    delete VisitedDirectories;
    throw;
  }
  return VisitedDirectories;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DoOpenDirectoryDialog(
  TOpenDirectoryMode Mode, TOperationSide Side)
{
  bool Continue = true;
  if (Mode == odAddBookmark)
  {
    TMessageParams Params(mpNeverAskAgainCheck);
    Params.NeverAskAgainTitle = LoadStr(ADD_BOOKMARK_SHARED);
    Params.NeverAskAgainCheckedInitially = WinConfiguration->UseSharedBookmarks;

    UnicodeString Message = MainInstructions(FMTLOAD(ADD_BOOKMARK_CONFIRM, (DirView(Side)->PathName)));
    unsigned int Answer =
      MessageDialog(Message,
        qtConfirmation, qaOK | qaCancel, HELP_ADD_BOOKMARK_CONFIRM, &Params);
    if (Answer == qaNeverAskAgain)
    {
      Continue = true;
      WinConfiguration->UseSharedBookmarks = true;
    }
    else if (Answer == qaOK)
    {
      Continue = true;
      WinConfiguration->UseSharedBookmarks = false;
    }
    else
    {
      Continue = false;
    }
  }

  if (Continue)
  {
    TStrings * VisitedDirectories = CreateVisitedDirectories(Side);
    try
    {
      UnicodeString Name = DirView(Side)->PathName;
      if (::DoOpenDirectoryDialog(Mode, Side, Name, VisitedDirectories, Terminal,
            // do not allow switching to location profiles,
            // if we are not connected
            HasDirView[osLocal] && (Terminal != NULL)))
      {
        TWindowLock Lock(this);
        DirView(Side)->Path = Name;
      }
    }
    __finally
    {
      delete VisitedDirectories;
    }
  }
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::CommandSessionFallback()
{
  bool Result = true;

  DebugAssert(!FTerminal->CommandSessionOpened);

  try
  {
    TTerminalManager::ConnectTerminal(FTerminal->CommandSession, false);
  }
  catch(Exception & E)
  {
    ShowExtendedExceptionEx(FTerminal->CommandSession, &E);
    Result = false;
  }
  return Result;
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::EnsureCommandSessionFallback(TFSCapability Capability)
{
  bool Result = FTerminal->IsCapable[Capability] ||
    FTerminal->CommandSessionOpened;

  if (!Result)
  {
    DebugAssert(FTerminal->IsCapable[fcSecondaryShell]);
    if (!GUIConfiguration->ConfirmCommandSession)
    {
      Result = true;
    }
    else
    {
      TMessageParams Params(mpNeverAskAgainCheck);
      const TFileSystemInfo & FileSystemInfo = Terminal->GetFileSystemInfo();
      unsigned int Answer = MessageDialog(FMTLOAD(PERFORM_ON_COMMAND_SESSION2,
        (FileSystemInfo.ProtocolName, FileSystemInfo.ProtocolName)), qtConfirmation,
        qaOK | qaCancel, HELP_PERFORM_ON_COMMAND_SESSION, &Params);
      if (Answer == qaNeverAskAgain)
      {
        GUIConfiguration->ConfirmCommandSession = false;
        Result = true;
      }
      else if (Answer == qaOK)
      {
        Result = true;
      }
    }

    if (Result)
    {
      Result = CommandSessionFallback();
    }
  }

  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::OpenConsole(UnicodeString Command)
{
  if (EnsureCommandSessionFallback(fcAnyCommand))
  {
    DoConsoleDialog(Terminal, Command);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::FileControlDDDragEnter(
      TObject *Sender, _di_IDataObject /*DataObj*/, int /*grfKeyState*/,
      const TPoint & /*Point*/, int & /*dwEffect*/, bool & Accept)
{
  if (IsFileControl(DropSourceControl, osRemote) &&
      (FDDExtMapFile != NULL))
  {
    Accept = true;
  }

  FDDTargetControl = dynamic_cast<TControl*>(Sender);
  DebugAssert(FDDTargetControl != NULL);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SessionsDDDragEnter(
  _di_IDataObject DataObj, int KeyState,
  const TPoint & Point, int & Effect, bool & Accept)
{
  FileControlDDDragEnter(SessionsPageControl, DataObj, KeyState, Point, Effect, Accept);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::QueueDDDragEnter(
  _di_IDataObject DataObj, int KeyState,
  const TPoint & Point, int & Effect, bool & Accept)
{
  FileControlDDDragEnter(QueueView3, DataObj, KeyState, Point, Effect, Accept);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::FileControlDDDragLeave(
  TObject * Sender)
{
  DebugUsedParam(Sender);
  DebugAssert(FDDTargetControl == Sender);
  FDDTargetControl = NULL;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SessionsDDDragLeave()
{
  FileControlDDDragLeave(SessionsPageControl);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::QueueDDDragLeave()
{
  FileControlDDDragLeave(QueueView3);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::AddEditLink(TOperationSide Side, bool Add)
{
  DebugAssert(GetSide(Side) == osRemote);
  DebugUsedParam(Side);

  bool Edit = false;
  TRemoteFile * File = NULL;
  UnicodeString FileName;
  UnicodeString PointTo;
  bool SymbolicLink = true;

  if (RemoteDirView->ItemFocused)
  {
    DebugAssert(RemoteDirView->ItemFocused->Data);
    File = (TRemoteFile *)RemoteDirView->ItemFocused->Data;

    Edit = !Add && File->IsSymLink && Terminal->SessionData->ResolveSymlinks;
    if (Edit)
    {
      FileName = File->FileName;
      PointTo = File->LinkTo;
    }
    else
    {
      PointTo = File->FileName;
    }
  }

  if (DoSymlinkDialog(FileName, PointTo, osRemote, SymbolicLink, Edit,
        Terminal->IsCapable[fcHardLink]))
  {
    Configuration->Usage->Inc(L"RemoteLinksCreated");

    if (Edit)
    {
      DebugAssert(File->FileName == FileName);
      int Params = dfNoRecursive;
      Terminal->ExceptionOnFail = true;
      try
      {
        Terminal->DeleteFile(L"", File, &Params);
      }
      __finally
      {
        Terminal->ExceptionOnFail = false;
      }
    }
    Terminal->CreateLink(FileName, PointTo, SymbolicLink);
  }
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::CanAddEditLink(TOperationSide Side)
{
  return
    (Terminal != NULL) &&
    ((GetSide(Side) != osRemote) ||
     (Terminal->ResolvingSymlinks &&
      Terminal->IsCapable[fcSymbolicLink]));
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::LinkFocused()
{
  return
    (FCurrentSide == osRemote) &&
    (RemoteDirView->ItemFocused != NULL) &&
    ((TRemoteFile *)RemoteDirView->ItemFocused->Data)->IsSymLink &&
    Terminal->SessionData->ResolveSymlinks;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ExecuteCurrentFile()
{
  DebugAssert(!WinConfiguration->DisableOpenEdit);
  FForceExecution = true;
  try
  {
    DirView(osCurrent)->ExecuteCurrentFile();
  }
  __finally
  {
    FForceExecution = false;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ExecuteCurrentFileWith(bool OnFocused)
{
  TEditorData ExternalEditor;
  ExternalEditor.Editor = edExternal;
  bool Remember = false;

  if (DoEditorPreferencesDialog(&ExternalEditor, Remember, epmAdHoc,
        (GetSide(osCurrent) == osRemote)))
  {
    if (Remember)
    {
      TEditorList * EditorList = new TEditorList;
      try
      {
        *EditorList = *WinConfiguration->EditorList;

        bool Found = false;
        int i = 0;
        while (!Found && (i < EditorList->Count))
        {
          const TEditorPreferences * Editor = EditorList->Editors[i];
          if ((Editor->Data->Editor == edExternal) &&
              (Editor->Data->ExternalEditor == ExternalEditor.ExternalEditor))
          {
            Found = true;
          }
          i++;
        }

        if (!Found)
        {
          EditorList->Add(new TEditorPreferences(ExternalEditor));
          WinConfiguration->EditorList = EditorList;
        }
      }
      __finally
      {
        delete EditorList;
      }
    }

    ExecuteFile(osCurrent, efExternalEditor, &ExternalEditor, true, OnFocused);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::TerminalRemoved(TObject * Sender)
{
  FEditorManager->ProcessFiles(FileTerminalRemoved, Sender);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::FileTerminalRemoved(const UnicodeString FileName,
  TEditedFileData * Data, TObject * /*Token*/, void * Arg)
{
  TTerminal * Terminal = static_cast<TTerminal *>(Arg);
  DebugAssert(Terminal != NULL);

  if (Data->Terminal == Terminal)
  {
    Data->Terminal = NULL;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::LastTerminalClosed(TObject * /*Sender*/)
{
  UpdateControls();
  SessionColor = TColor(0);
  UpdateRemotePathComboBox(false);
  try
  {
    NeedSession(true);
  }
  catch (EAbort &)
  {
    // swallow
    // The TTerminalManager does not expect the OnLastTerminalClose to throw without trying to connect
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::NeedSession(bool ReloadSessions)
{
  try
  {
    TTerminalManager::Instance()->NewSession(false, L"", ReloadSessions, this);
  }
  __finally
  {
    if (!WinConfiguration->KeepOpenWhenNoSession &&
        (!Terminal || !Terminal->Active))
    {
      TerminateApplication();
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DoTerminalListChanged(bool Force)
{
  TStrings * TerminalList = TTerminalManager::Instance()->TerminalList;
  int ActiveTerminalIndex = TTerminalManager::Instance()->ActiveTerminalIndex;

  Configuration->Usage->SetMax(L"MaxOpenedSessions", TerminalList->Count);

  bool ListChanged = Force || (TerminalList->Count + 1 != SessionsPageControl->PageCount);
  if (!ListChanged)
  {
    int Index = 0;
    while (!ListChanged && (Index < TerminalList->Count))
    {
      ListChanged =
        (GetSessionTabTerminal(SessionsPageControl->Pages[Index]) != TerminalList->Objects[Index]) ||
        (SessionsPageControl->Pages[Index]->Caption != TerminalList->Strings[Index]);
      Index++;
    }
  }

  if (ListChanged)
  {
    SendMessage(SessionsPageControl->Handle, WM_SETREDRAW, 0, 0);
    try
    {
      FSessionColors->Clear();

      AddFixedSessionImages();

      while (SessionsPageControl->PageCount > 0)
      {
        delete SessionsPageControl->Pages[0];
      }

      for (int Index = 0; Index < TerminalList->Count; Index++)
      {
        TThemeTabSheet * TabSheet = new TThemeTabSheet(SessionsPageControl);
        TabSheet->Caption = TerminalList->Strings[Index];
        TTerminal * Terminal = dynamic_cast<TTerminal *>(TerminalList->Objects[Index]);
        TabSheet->Tag = reinterpret_cast<int>(Terminal);
        TabSheet->PageControl = SessionsPageControl;

        UpdateSessionTab(TabSheet);
      }

      TTabSheet * TabSheet = new TTabSheet(SessionsPageControl);
      TabSheet->PageControl = SessionsPageControl;
      TabSheet->ImageIndex = FNewSessionTabImageIndex;
      UpdateNewSessionTab();
    }
    __finally
    {
      SendMessage(SessionsPageControl->Handle, WM_SETREDRAW, 1, 0);
    }
  }

  SessionsPageControl->ActivePageIndex = ActiveTerminalIndex;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::TerminalListChanged(TObject * /*Sender*/)
{
  DoTerminalListChanged(false);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateNewSessionTab()
{
  TTabSheet * TabSheet = SessionsPageControl->Pages[SessionsPageControl->PageCount - 1];

  DebugAssert(TabSheet->ImageIndex == 0);

  TabSheet->Caption =
    WinConfiguration->SelectiveToolbarText ?
      StripHotkey(StripTrailingPunctuation(NonVisualDataModule->NewSessionAction->Caption)) :
      UnicodeString();
}
//---------------------------------------------------------------------------
TTerminal * __fastcall TCustomScpExplorerForm::GetSessionTabTerminal(TTabSheet * TabSheet)
{
  return reinterpret_cast<TTerminal *>(TabSheet->Tag);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateSessionTab(TTabSheet * TabSheet)
{
  if (DebugAlwaysTrue(TabSheet != NULL))
  {
    TManagedTerminal * ManagedTerminal =
      dynamic_cast<TManagedTerminal *>(GetSessionTabTerminal(TabSheet));
    if (DebugAlwaysTrue(ManagedTerminal != NULL))
    {
      TColor Color =
        (ManagedTerminal == FTerminal) ? FSessionColor : ManagedTerminal->StateData->Color;

      TabSheet->ImageIndex = AddSessionColor(Color);

      TThemeTabSheet * ThemeTabSheet = dynamic_cast<TThemeTabSheet *>(TabSheet);
      if (DebugAlwaysTrue(ThemeTabSheet != NULL))
      {
        ThemeTabSheet->Shadowed = !ManagedTerminal->Active;
      }
    }
  }
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::SessionTabSwitched()
{
  DebugAssert(SessionsPageControl->ActivePage != NULL);
  TTerminal * Terminal = GetSessionTabTerminal(SessionsPageControl->ActivePage);
  bool Result = (Terminal != NULL);
  if (Result)
  {
    TTerminalManager::Instance()->ActiveTerminal = Terminal;
  }
  else
  {
    try
    {
      NewSession(false);
    }
    __finally
    {
      DoTerminalListChanged(false);
    }

    FSessionsPageControlNewSessionTime = Now();
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SessionsPageControlChange(TObject * /*Sender*/)
{
  SessionTabSwitched();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::TransferListChange(TObject * Sender)
{
  TTBXStringList * TransferList = dynamic_cast<TTBXStringList *>(Sender);
  DebugAssert(TransferList != NULL);
  UnicodeString Name;
  if (TransferList->ItemIndex <= 0)
  {
    Name = L"";
  }
  else
  {
    Name = GUIConfiguration->CopyParamList->Names[TransferList->ItemIndex - 1];
  }
  if (FCopyParamAutoSelected.IsEmpty())
  {
    // if previous preset was not autoselected, make new preset the "default"
    FCopyParamDefault = Name;
  }
  GUIConfiguration->CopyParamCurrent = Name;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateTransferLabel()
{
  // sanity check
  bool ExistingPreset =
    (FTransferListHoverIndex >= 0) &&
    (FTransferListHoverIndex < 1 + GUIConfiguration->CopyParamList->Count);
  DebugAssert(ExistingPreset);
  if (ExistingPreset)
  {
    HDC DC = GetDC(0);
    TCanvas * Canvas = new TCanvas();
    try
    {
      Canvas->Handle = DC;
      Canvas->Font = ToolbarFont;

      UnicodeString Name;
      if (FTransferListHoverIndex == 0)
      {
        Name = L"";
      }
      else
      {
        Name = GUIConfiguration->CopyParamList->Names[FTransferListHoverIndex - 1];
      }

      TTBXLabelItem * TransferLabel = dynamic_cast<TTBXLabelItem*>(
        static_cast<TComponent*>(GetComponent(fcTransferLabel)));
      TTBXStringList * TransferList = dynamic_cast<TTBXStringList*>(
        static_cast<TObject*>(GetComponent(fcTransferList)));

      UnicodeString InfoStr =
        GUIConfiguration->CopyParamPreset[Name].
          GetInfoStr(L"; ",
            FLAGMASK(Terminal != NULL, Terminal->UsableCopyParamAttrs(0).General));
      int MaxWidth = TransferList->MinWidth - (2 * TransferLabel->Margin) - ScaleByTextHeight(this, 10);
      if (Canvas->TextExtent(InfoStr).cx > MaxWidth)
      {
        while (Canvas->TextExtent(InfoStr + Ellipsis).cx > MaxWidth)
        {
          InfoStr.SetLength(InfoStr.Length() - 1);
        }
        InfoStr += Ellipsis;
      }

      // UpdateCaption does not cause invalidation of whole submenu, while
      // setting Caption property does.
      // also it probably does not resize the label, even if necessary
      // (we do not want that anyway)
      TransferLabel->UpdateCaption(InfoStr);
    }
    __finally
    {
      Canvas->Handle = NULL;
      ReleaseDC(0, DC);
      delete Canvas;
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::TransferListDrawItem(
  TTBXCustomList * /*Sender*/, TCanvas * /*ACanvas*/, const TRect & /*ARect*/,
  int /*AIndex*/, int AHoverIndex, bool & /*DrawDefault*/)
{
  if (FTransferListHoverIndex != AHoverIndex)
  {
    FTransferListHoverIndex = AHoverIndex;
    UpdateTransferLabel();
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::WMAppCommand(TMessage & Message)
{
  int Command  = GET_APPCOMMAND_LPARAM(Message.LParam);
  TShiftState Shift = KeyDataToShiftState(GET_KEYSTATE_LPARAM(Message.LParam));
  if ((Shift * (TShiftState() << ssShift << ssAlt << ssCtrl)).Empty())
  {
    if (Command == APPCOMMAND_BROWSER_FAVORITES)
    {
      OpenDirectory(GetSide(osCurrent));
      Message.Result = 1;
    }
    else
    {
      TForm::Dispatch(&Message);
    }
  }
  else
  {
    TForm::Dispatch(&Message);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::CMDialogChar(TMessage & AMessage)
{
  TCMDialogChar & Message = reinterpret_cast<TCMDialogChar &>(AMessage);
  if ((FIgnoreNextDialogChar != 0) &&
      (toupper(Message.CharCode) == toupper(FIgnoreNextDialogChar)))
  {
    Message.Result = 1;
  }
  else
  {
    TForm::Dispatch(&Message);
  }
  FIgnoreNextDialogChar = 0;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::WMSysCommand(TMessage & Message)
{
  // The four low-order bits are used internally by Windows
  unsigned int Cmd = (Message.WParam & 0xFFF0);
  // SC_RESTORE, SC_MAXIMIZE, SC_MINIMIZE - buttons on windows title
  // SC_DEFAULT - double click on windows title (does not work, at least on WinXP)
  // 61730 - restore thru double click - undocumented
  // 61490 - maximize thru double click - undocumented
  if ((Cmd == SC_RESTORE) || (Cmd == SC_MAXIMIZE) ||
      (Cmd == SC_MINIMIZE) || (Cmd == SC_DEFAULT))
  {
    SysResizing(Cmd);
  }
  TForm::Dispatch(&Message);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::WMQueryEndSession(TMessage & Message)
{
  // We were actually never able to make ENDSESSION_CRITICAL happen.
  // Also there no point returning TRUE as we are not able to
  // handle the abrupt termination caused by subsequent WM_ENDSESSION cleanly.
  // Hence the process termination might be safer :)
  if ((Message.LParam != ENDSESSION_CRITICAL) &&
      (((FQueue != NULL) && !FQueue->IsEmpty) || (FProgressForm != NULL)))
  {
    Message.Result = FALSE;
  }
  else
  {
    Message.Result = TRUE;
  }
  // Do not call default handling as that triggers OnCloseQuery,
  // where our implementation will popup configuration dialogs, what we do not want,
  // as per Vista guidelines:
  // msdn.microsoft.com/en-us/library/ms700677.aspx
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::WMEndSession(TWMEndSession & Message)
{
  if (Message.EndSession && IsApplicationMinimized())
  {
    // WORKAROUND
    // TApplication.WndProc() calls Application.Terminate() before Halt(),
    // when it receives WM_ENDSESSION,
    // but that sometimes (particularly when application is minimized) cause crashes.
    // Crash popups message that blocks log off.
    // Obviously application cannot shutdown cleanly after WM_ENDSESSION,
    // so we call ExitProcess() immediately, not even trying to cleanup.
    // It still causes beep, so there's likely some popup, but it does not block
    // log off.
    ExitProcess(0);
  }
  TForm::Dispatch(&Message);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SysResizing(unsigned int /*Cmd*/)
{
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DoShow()
{
  // only now are the controls resized finally, so the size constraints
  // will not conflict with possibly very small window size
  RestoreFormParams();

  FixControlsPlacement();

  if (Position == poDefaultPosOnly)
  {
    CutFormToDesktop(this);
  }

  TForm::DoShow();

  FSessionsDragDropFilesEx->DragDropControl = SessionsPageControl;
  FQueueDragDropFilesEx->DragDropControl = QueueView3;

  if (Terminal == NULL)
  {
    StartingDisconnected();
  }

  FShowing = true;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::StartingDisconnected()
{
  DoTerminalListChanged(true);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::PopupTrayBalloon(TTerminal * Terminal,
  const UnicodeString & Str, TQueryType Type, Exception * E, unsigned int Seconds,
  TNotifyEvent OnBalloonClick, TObject * UserData)
{
  bool Do;
  UnicodeString Message;
  if (E == NULL)
  {
    Message = Str;
    Do = true;
  }
  else
  {
    Do = ExceptionMessage(E, Message);
  }

  if (Do && WinConfiguration->BalloonNotifications)
  {
    UnicodeString Title;
    if ((Terminal == NULL) && (Type == qtInformation) && ExtractMainInstructions(Message, Title))
    {
      Message = TrimLeft(Message);
    }
    else
    {
      Message = UnformatMessage(Message);
      const ResourceString * Captions[] = { &_SMsgDlgConfirm, &_SMsgDlgWarning,
        &_SMsgDlgError, &_SMsgDlgInformation, NULL };
      Title = LoadResourceString(Captions[Type]);
      if (Terminal != NULL)
      {
        Title = FORMAT(L"%s - %s",
          (TTerminalManager::Instance()->TerminalTitle(Terminal), Title));
      }
    }

    if (Seconds == 0)
    {
      Seconds = WinConfiguration->NotificationsTimeout;
    }
    FTrayIcon->PopupBalloon(Title, Message, Type, Seconds * MSecsPerSec, OnBalloonClick, UserData);
  }
}
//---------------------------------------------------------------------------
unsigned int __fastcall TCustomScpExplorerForm::MoreMessageDialog(const UnicodeString Message,
    TStrings * MoreMessages, TQueryType Type, unsigned int Answers,
    UnicodeString HelpKeyword, const TMessageParams * Params,
    TTerminal * Terminal)
{
  if (((WinConfiguration->ContinueOnError && (FErrorList != NULL)) ||
       (FOnFeedSynchronizeError != NULL)) &&
      (Params != NULL) && (Params->Params & mpAllowContinueOnError) )
  {
    if (FOnFeedSynchronizeError != NULL)
    {
      FOnFeedSynchronizeError(Message, MoreMessages, Type, HelpKeyword);
    }
    else
    {
      DebugAssert(FErrorList != NULL);
      TStringList * MoreMessagesCopy = NULL;
      if (MoreMessages)
      {
        MoreMessagesCopy = new TStringList();
        MoreMessagesCopy->Assign(MoreMessages);
      }
      FErrorList->AddObject(Message, MoreMessagesCopy);
    }

    PopupTrayBalloon(Terminal, Message, Type);

    return ContinueAnswer(Answers);
  }
  else
  {
    bool UseBalloon = FTrayIcon->Visible;
    if (UseBalloon)
    {
      PopupTrayBalloon(Terminal, Message, Type);
    }
    unsigned int Result;
    try
    {
      Result = ::MoreMessageDialog(Message, MoreMessages, Type, Answers, HelpKeyword, Params);
    }
    __finally
    {
      // cancel only balloon we popped up, otherwise we may cancel notification
      // balloon that was there before the message dialog
      // (such as "dd confirmation opt in balloon)
      if (UseBalloon)
      {
        FTrayIcon->CancelBalloon();
      }
    }
    return Result;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ShowExtendedException(
  TTerminal * Terminal, Exception * E)
{
  if (FTrayIcon->Visible)
  {
    PopupTrayBalloon(Terminal, L"", qtError, E);
  }

  // particularly prevent opening new session from jump list,
  // while exception is shown
  NonVisualDataModule->StartBusy();
  try
  {
    ShowExtendedExceptionEx(Terminal, E);
  }
  __finally
  {
    NonVisualDataModule->EndBusy();
    FTrayIcon->CancelBalloon();
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::TerminalReady()
{
  // cannot rely on active page being page for active terminal,
  // as it can happen that active page is the "new session" page
  // (e.g. when reconnecting active terminal, while login dialog
  // invoked from "new session" page is modal)
  int ActiveTerminalIndex = TTerminalManager::Instance()->ActiveTerminalIndex;
  UpdateSessionTab(SessionsPageControl->Pages[ActiveTerminalIndex]);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::InactiveTerminalException(
  TTerminal * Terminal, Exception * E)
{
  Notify(Terminal, L"", qtError, false, NULL, NULL, E);

  if (!Terminal->Active)
  {
    int Index = TTerminalManager::Instance()->IndexOf(Terminal);
    if (DebugAlwaysTrue((Index >= 0) && (Index < SessionsPageControl->PageCount)))
    {
      UpdateSessionTab(SessionsPageControl->Pages[Index]);
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::Notify(TTerminal * Terminal,
  UnicodeString Message, TQueryType Type,
  bool Important, TNotifyEvent OnClick, TObject * UserData, Exception * E)
{
  if ((E == NULL) ||
      ExceptionMessage(E, Message))
  {
    unsigned int Seconds = WinConfiguration->NotificationsTimeout;
    if (Important)
    {
      Seconds *= 5;
    }

    UnicodeString NoteMessage(UnformatMessage(Message));
    if (Terminal != NULL)
    {
      NoteMessage = FORMAT(L"%s: %s",
        (TTerminalManager::Instance()->TerminalTitle(Terminal), NoteMessage));
    }

    if (WinConfiguration->BalloonNotifications)
    {
      AddNote(NoteMessage);
      PopupTrayBalloon(Terminal, Message, Type, NULL, Seconds, OnClick, UserData);
    }
    else
    {
      FlashOnBackground();
      PostNote(NoteMessage, Seconds, OnClick, UserData);
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::QueueEmptyNoteClicked(TObject * Sender)
{
  RestoreApp();

  TTerminalNoteData * TerminalNoteData = dynamic_cast<TTerminalNoteData *>(Sender);
  if (DebugAlwaysTrue(TerminalNoteData != NULL) &&
      !NonVisualDataModule->Busy)
  {
    TTerminal * Terminal = TerminalNoteData->Terminal;
    TTerminalManager::Instance()->ActiveTerminal = Terminal;
    if (!ComponentVisible[fcQueueView])
    {
      ToggleQueueVisibility();
      GoToQueue();
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::QueueEvent(TTerminal * ATerminal,
  TTerminalQueue * /*Queue*/, TQueueEvent Event)
{
  TManagedTerminal * ManagedTerminal = DebugNotNull(dynamic_cast<TManagedTerminal *>(ATerminal));
  UnicodeString Message;
  TNotifyEvent OnClick = NULL;
  TObject * UserData = NULL;
  bool QueueInvisible = !ComponentVisible[fcQueueView] || IsApplicationMinimized();
  switch (Event)
  {
    case qeEmptyButMonitored:
      if ((ATerminal != Terminal) || QueueInvisible)
      {
        Message = LoadStr(BALLOON_QUEUE_EMPTY);
        OnClick = QueueEmptyNoteClicked;
        TTerminalNoteData * TerminalNoteData = new TTerminalNoteData();
        TerminalNoteData->Terminal = ATerminal;
        UserData = TerminalNoteData;
      }
      break;

    case qeEmpty:
      OperationComplete(ManagedTerminal->QueueOperationStart);
      break;

    case qePendingUserAction:
      if ((ATerminal != Terminal) ||
          (QueueInvisible && !IsQueueAutoPopup()))
      {
        Message = LoadStr(BALLOON_QUEUE_USER_ACTION);
      }
      break;

    default:
      DebugFail();
  }

  if (!Message.IsEmpty())
  {
    Notify(ATerminal, Message, qtInformation, false, OnClick, UserData);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemoteFileControlDDCreateDragFileList(
  TObject * /*Sender*/, TFileList * FileList, bool & Created)
{
  if (FDDExtMapFile != NULL)
  {
    CloseHandle(FDDExtMapFile);
    FDDExtMapFile = NULL;
  }

  if (WinConfiguration->DDExtEnabled)
  {
    if (!WinConfiguration->DDExtInstalled)
    {
      Configuration->Usage->Inc(L"DownloadsDragDropExternalExtNotInstalled");
      throw ExtException(NULL, LoadStr(DRAGEXT_TARGET_NOT_INSTALLED2), HELP_DRAGEXT_TARGET_NOT_INSTALLED);
    }
    DDExtInitDrag(FileList, Created);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DDExtInitDrag(TFileList * FileList,
  bool & Created)
{
  FDragExtFakeDirectory =
    ExcludeTrailingBackslash(WinConfiguration->TemporaryDir());
  if (!ForceDirectories(ApiPath(FDragExtFakeDirectory)))
  {
    throw Exception(FMTLOAD(CREATE_TEMP_DIR_ERROR, (FDragExtFakeDirectory)));
  }
  FileList->AddItem(NULL, FDragExtFakeDirectory);

  Created = true;

  FDDExtMapFile = CreateFileMappingA((HANDLE)0xFFFFFFFF, NULL, PAGE_READWRITE,
    0, sizeof(TDragExtCommStruct), AnsiString(DRAG_EXT_MAPPING).c_str());

  {
    TMutexGuard Guard(FDDExtMutex, DRAGEXT_MUTEX_RELEASE_TIMEOUT);
    TDragExtCommStruct* CommStruct;
    CommStruct = static_cast<TDragExtCommStruct*>(MapViewOfFile(FDDExtMapFile,
      FILE_MAP_ALL_ACCESS, 0, 0, 0));
    DebugAssert(CommStruct != NULL);
    CommStruct->Version = TDragExtCommStruct::CurrentVersion;
    CommStruct->Dragging = true;
    wcsncpy(CommStruct->DropDest, FDragExtFakeDirectory.c_str(),
      LENOF(CommStruct->DropDest));
    NULL_TERMINATE(CommStruct->DropDest);
    UnmapViewOfFile(CommStruct);
  }

  FDDMoveSlipped = false;
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::RemoteFileControlFileOperation(
  TObject * Sender, TFileOperation Operation, bool NoConfirmation, void * Param)
{
  bool Result;
  if (Sender == RemoteDirView)
  {
    Result = ExecuteFileOperation(Operation, osRemote, true, NoConfirmation, Param);
  }
  else
  {
    DebugAssert(Sender == RemoteDriveView);
    TStrings * FileList = RemoteDriveView->DragFileList();
    try
    {
      Result = ExecuteFileOperation(Operation, osRemote, FileList, NoConfirmation, Param);
    }
    __finally
    {
      delete FileList;
    }
  }

  if (FDDTargetControl == RemoteDriveView)
  {
    RemoteDriveView->UpdateDropTarget();
  }
  // foRemoteMove happens when file/dir is dragged within the remote tree view
  // or from tree view to dir view.
  // foMove happens when file/dir is dragged from remote tree view outside of
  // application via dragex
  if (((Operation == foRemoteMove) || (Operation == foMove)) &&
      (DropSourceControl == RemoteDriveView))
  {
    RemoteDriveView->UpdateDropSource();
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemoteFileControlDDEnd(TObject * Sender)
{
  // This also handles drops of remote files to queue.
  // Drops of local files (uploads) are handled in QueueDDProcessDropped.
  SAFE_DESTROY(FDDFileList);

  if ((FDDExtMapFile != NULL) || (FDDTargetControl == QueueView3))
  {
    try
    {
      TDragResult DDResult = (Sender == RemoteDirView) ?
        RemoteDirView->LastDDResult : RemoteDriveView->LastDDResult;

      // Focus is moved to the target application,
      // but as we are going to present the UI, we need to steal the focus back.
      // This most likely won't work though (windows does not allow application
      // to steal focus most of the time)
      Application->BringToFront();

      // On older version of Windows we never got drMove here, see also comment below.
      // On Windows 10, we get the "move".
      if ((DDResult == drCopy) || (DDResult == drMove) || (DDResult == drInvalid))
      {
        UnicodeString TargetDirectory;
        TFileOperation Operation;

        // drInvalid may mean drMove, see comment below
        switch (DDResult)
        {
          case drCopy:
            Operation = foCopy;
            break;
          case drMove:
            Operation = foMove;
            break;
          default:
            DebugFail();
          case drInvalid:
            // prefer "copy" for safety
            Operation = FLAGSET(FLastDropEffect, DROPEFFECT_MOVE) ? foMove : foCopy;
            break;
        }

        if (FDDMoveSlipped)
        {
          Operation = foMove;
        }

        TTransferOperationParam Param;
        bool Internal;
        bool ForceQueue;
        if (!DDGetTarget(Param.TargetDirectory, ForceQueue, Internal))
        {
          // we get drInvalid both if d&d was intercepted by ddext,
          // and when users drops on no-drop location.
          // we tell the difference by existence of response from ddext,
          // so we ignore absence of response in this case
          if (DDResult != drInvalid)
          {
            // here we know that the extension is installed,
            // as it is checked as soon as drag&drop starts from
            // RemoteFileControlDDCreateDragFileList
            Configuration->Usage->Inc(L"DownloadsDragDropExternalExtTargetUnknown");
            throw ExtException(NULL, LoadStr(DRAGEXT_TARGET_UNKNOWN2), HELP_DRAGEXT_TARGET_UNKNOWN);
          }
        }
        else
        {
          // download using ddext
          Param.Temp = false;
          Param.DragDrop = true;
          if (ForceQueue)
          {
            Param.Queue = asOn;
          }
          if (Sender == RemoteDirView)
          {
            Param.Options = FLAGMASK(SelectedAllFilesInDirView(RemoteDirView), coAllFiles);
          }

          if (RemoteFileControlFileOperation(Sender, Operation,
                (WinConfiguration->DDTransferConfirmation == asOff), &Param))
          {
            Configuration->Usage->Inc(
              Internal ? L"DownloadsDragDropInternal" : L"DownloadsDragDropExternalExt");
          }
        }
      }
    }
    __finally
    {
      CloseHandle(FDDExtMapFile);
      FDDExtMapFile = NULL;
      RemoveDir(ApiPath(FDragExtFakeDirectory));
      FDragExtFakeDirectory = L"";
    }
  }

  if (!FDragDropSshTerminate.IsEmpty())
  {
    throw ESshTerminate(NULL, FDragDropSshTerminate, FDragDropOnceDoneOperation);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemoteFileControlDDGiveFeedback(
  TObject * Sender, int dwEffect, HRESULT & /*Result*/)
{
  HCURSOR SlippedCopyCursor;

  FDDMoveSlipped =
    (FDragMoveCursor != NULL) &&
    (!WinConfiguration->DDAllowMoveInit) && (dwEffect == DROPEFFECT_Copy) &&
    ((IsFileControl(FDDTargetControl, osRemote) && (GetKeyState(VK_CONTROL) >= 0) &&
      FTerminal->IsCapable[fcRemoteMove]) ||
     (IsFileControl(FDDTargetControl, osLocal) && (GetKeyState(VK_SHIFT) < 0)));

  SlippedCopyCursor = FDDMoveSlipped ? FDragMoveCursor : Dragdrop::DefaultCursor;

  DragDropFiles(Sender)->CHCopy = SlippedCopyCursor;
  DragDropFiles(Sender)->CHScrollCopy = SlippedCopyCursor;

  // Remember drop effect so we know (when user drops files), if we copy or move
  FLastDropEffect = dwEffect;
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::DDGetTarget(
  UnicodeString & Directory, bool & ForceQueue, bool & Internal)
{
  bool Result;
  if (FDDTargetControl == QueueView3)
  {
    Directory = DefaultDownloadTargetDirectory();
    Result = true;
    Internal = true;
    ForceQueue = true;
  }
  else
  {
    ForceQueue = false;

    Enabled = false;
    try
    {
      int Timer = 0;
      Result = false;
      while (!Result && (Timer < WinConfiguration->DDExtTimeout))
      {
        {
          TMutexGuard Guard(FDDExtMutex, DRAGEXT_MUTEX_RELEASE_TIMEOUT);
          TDragExtCommStruct* CommStruct;
          CommStruct = static_cast<TDragExtCommStruct*>(MapViewOfFile(FDDExtMapFile,
            FILE_MAP_ALL_ACCESS, 0, 0, 0));
          DebugAssert(CommStruct != NULL);
          Result = !CommStruct->Dragging;
          if (Result)
          {
            Directory = ExtractFilePath(CommStruct->DropDest);
            Internal = false;
          }
          UnmapViewOfFile(CommStruct);
        }
        if (!Result)
        {
          Sleep(50);
          Timer += 50;
          Application->ProcessMessages();
        }
      }
    }
    __finally
    {
      Enabled = true;
    }
  }

  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::AddDelayedDirectoryDeletion(
  const UnicodeString TempDir, int SecDelay)
{
  TDateTime Alarm = Now() + (double)((double)SecDelay*OneMillisecond);
  FDelayedDeletionList->AddObject(TempDir, reinterpret_cast<TObject*>(Alarm.FileDate()));
  if (FDelayedDeletionTimer == NULL)
  {
    DebugAssert(HandleAllocated());
    FDelayedDeletionTimer = new TTimer(this);
    FDelayedDeletionTimer->Interval = 10000;
    FDelayedDeletionTimer->OnTimer = DoDelayedDeletion;
  }
  else
  {
    FDelayedDeletionTimer->Enabled = true;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DoDelayedDeletion(TObject * Sender)
{
  DebugAssert(FDelayedDeletionList != NULL);

  TDateTime N = Now();
  TDateTime Alert;
  UnicodeString Directory;

  for (int Index = FDelayedDeletionList->Count-1; Index >= 0; Index--)
  {
    Alert = FileDateToDateTime(reinterpret_cast<int>(FDelayedDeletionList->Objects[Index]));
    if ((N >= Alert) || (Sender == NULL))
    {
      Directory = FDelayedDeletionList->Strings[Index];
      if (DeleteDirectory(ExcludeTrailingBackslash(Directory)))
      {
        FDelayedDeletionList->Delete(Index);
      }
    }
  }

  if (FDelayedDeletionList->Count == 0)
  {
    FDelayedDeletionTimer->Enabled = false;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemoteFileControlDDTargetDrop()
{
  if (IsFileControl(FDDTargetControl, osRemote) ||
      (FDDTargetControl == SessionsPageControl))
  {
    TTerminal * TargetTerminal = NULL;
    TFileOperation Operation = foNone;
    if (FDDTargetControl == SessionsPageControl)
    {
      TPoint Point = SessionsPageControl->ScreenToClient(Mouse->CursorPos);
      int Index = SessionsPageControl->IndexOfTabAt(Point.X, Point.Y);
      // do not allow dropping on the "+" tab
      TargetTerminal = GetSessionTabTerminal(SessionsPageControl->Pages[Index]);
      if (TargetTerminal != NULL)
      {
        if ((FLastDropEffect == DROPEFFECT_MOVE) &&
            (TargetTerminal == TTerminalManager::Instance()->ActiveTerminal))
        {
          Operation = foRemoteMove;
        }
        else
        {
          Operation = foRemoteCopy;
        }
      }
    }
    else
    {
      // when move from remote side is disabled, we allow copying inside the remote
      // panel, but we interpret is as moving (we also slip in the move cursor)
      if ((FLastDropEffect == DROPEFFECT_MOVE) ||
          (!WinConfiguration->DDAllowMoveInit && (FLastDropEffect == DROPEFFECT_COPY) &&
           FDDMoveSlipped))
      {
        Operation = foRemoteMove;
      }
      else if (FLastDropEffect == DROPEFFECT_COPY)
      {
        Operation = foRemoteCopy;
      }
    }

    if (Operation != foNone)
    {
      RemoteFileControlFileOperation(DropSourceControl,
        Operation, (WinConfiguration->DDTransferConfirmation == asOff), TargetTerminal);
    }
    // abort drag&drop
    Abort();
  }
  else if ((FDDExtMapFile == NULL) && (FLastDropEffect != DROPEFFECT_NONE) &&
           // Drops of remote files to queue are handled in RemoteFileControlDDEnd
           (FDDTargetControl != QueueView3))
  {
    DebugAssert(!FDragTempDir.IsEmpty());
    TTransferType Type;
    UnicodeString TempDir = FDragTempDir;
    // We clear FDragTempDir before calling
    // just in case it fail (raises exception)
    FDragTempDir = L"";
    Type = (FLastDropEffect & DROPEFFECT_MOVE ? ttMove : Type = ttCopy);

    TGUICopyParamType CopyParams = GUIConfiguration->CurrentCopyParam;
    // empty directory parameter means temp directory -> don't display it!
    UnicodeString TargetDir = L"";
    int Options = 0;

    if (!CopyParamDialog(tdToLocal, Type, true, FDDFileList,
          TargetDir, CopyParams, (WinConfiguration->DDTransferConfirmation != asOff), true, Options))
    {
      Abort();
    }

    // TargetDir is set when dropped on local file control
    // (this was workaround for legacy dirview event handling, now it should be
    // made prettier)
    if (TargetDir.IsEmpty())
    {
      TargetDir = TempDir;

      if (ForceDirectories(ApiPath(TargetDir)))
      {
        DebugAssert(Terminal && !TargetDir.IsEmpty());
        FPendingTempSpaceWarn = true;
        try
        {
          FDragDropOperation = true;
          int Params = cpTemporary |
            (Type == ttMove ? cpDelete : 0);
          DDDownload(FDDFileList, TargetDir, &CopyParams, Params);
          Configuration->Usage->Inc(L"DownloadsDragDropExternalTemp");
        }
        __finally
        {
          FDragDropOperation = false;
          FPendingTempSpaceWarn = false;
          AddDelayedDirectoryDeletion(TargetDir, WinConfiguration->DDDeleteDelay);
        }
      }
      else
      {
        throw Exception(FMTLOAD(CREATE_TEMP_DIR_ERROR, (TargetDir)));
      }
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DDDownload(TStrings * FilesToCopy,
  const UnicodeString TargetDir, const TCopyParamType * CopyParam, int Params)
{
  void * BatchStorage;
  BatchStart(BatchStorage);
  try
  {
    UpdateCopyParamCounters(*CopyParam);
    Terminal->CopyToLocal(FilesToCopy, TargetDir, CopyParam, Params);
    if (FLAGSET(Params, cpDelete) && (DropSourceControl == RemoteDriveView))
    {
      RemoteDriveView->UpdateDropSource();
    }
  }
  __finally
  {
    BatchEnd(BatchStorage);
  }
}
//---------------------------------------------------------------------------
class TFakeDataObjectFilesEx : public TDataObjectFilesEx
{
public:
        __fastcall TFakeDataObjectFilesEx(TFileList * AFileList, bool RenderPIDL,
    bool RenderFilename) : TDataObjectFilesEx(AFileList, RenderPIDL, RenderFilename)
  {
  }

  virtual bool __fastcall AllowData(const tagFORMATETC & FormatEtc)
  {
    return (FormatEtc.cfFormat == CF_HDROP) ? false :
      TDataObjectFilesEx::AllowData(FormatEtc);
  }
};
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemoteFileControlDDCreateDataObject(
  TObject * Sender, TDataObject *& DataObject)
{
  if (FDDExtMapFile != NULL)
  {
    TFileList * FileList = DragDropFiles(Sender)->FileList;
    if (!FileList->RenderPIDLs() || !FileList->RenderNames())
    {
      Abort();
    }

    if (FileList->Count > 0)
    {
      TDataObjectFilesEx * FilesObject = new TFakeDataObjectFilesEx(FileList, true, true);
      if (!FilesObject->IsValid(true, true))
      {
        FilesObject->_Release();
      }
      else
      {
        DataObject = FilesObject;
      }
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::GoToCommandLine()
{
  DebugFail();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::GoToTree()
{
  ComponentVisible[fcRemoteTree] = true;
  RemoteDriveView->SetFocus();
}
//---------------------------------------------------------------------------
TStrings * __fastcall TCustomScpExplorerForm::PanelExport(TOperationSide Side,
  TPanelExport Export)
{

  TCustomDirView * DirView = this->DirView(Side);
  std::unique_ptr<TStrings> ExportData(new TStringList());
  switch (Export)
  {
    case pePath:
      ExportData->Add(DirView->PathName);
      break;

    case peFileList:
    case peFullFileList:
      {
        bool FullPath = (Export == peFullFileList);
        DirView->CreateFileList(false, FullPath, ExportData.get());
        for (int Index = 0; Index < ExportData->Count; Index++)
        {
          if (ExportData->Strings[Index].Pos(L" ") > 0)
          {
            ExportData->Strings[Index] = FORMAT(L"\"%s\"", (ExportData->Strings[Index]));
          }
        }
      }
      break;

    default:
      DebugFail();
  }
  return ExportData.release();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::PanelExport(TOperationSide Side,
  TPanelExport Export, TPanelExportDestination Destination)
{
  std::unique_ptr<TStrings> ExportData(PanelExport(Side, Export));
  PanelExportStore(Side, Export, Destination, ExportData.get());
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::PanelExportStore(TOperationSide /*Side*/,
  TPanelExport /*Export*/, TPanelExportDestination Destination,
  TStrings * ExportData)
{
  if (Destination == pedClipboard)
  {
    TInstantOperationVisualizer Visualizer;
    CopyToClipboard(ExportData);
  }
  else
  {
    DebugFail();
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::Filter(TOperationSide Side)
{
  TCustomDirView * DirView = this->DirView(Side);
  TFileFilter Filter;
  DefaultFileFilter(Filter);
  Filter.Masks = DirView->Mask;
  if (DoFilterMaskDialog(DirView, &Filter))
  {
    DirView->Mask = TFileMasks::NormalizeMask(Filter.Masks);
    Configuration->Usage->Inc(L"Filters");
  }
}
//---------------------------------------------------------------------------
TQueueOperation __fastcall TCustomScpExplorerForm::DefaultQueueOperation()
{
  return FQueueController->DefaultOperation();
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::AllowQueueOperation(
  TQueueOperation Operation, void ** Param)
{
  switch (Operation)
  {
    case qoPreferences:
      return true;

    case qoGoTo:
      return ComponentVisible[fcQueueView] && QueueView3->Enabled;

    case qoOnceEmpty:
      return !FQueueController->Empty;

    default:
      return FQueueController->AllowOperation(Operation, Param);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::GoToQueue()
{
  if (DebugAlwaysTrue(QueueView3->Visible))
  {
    QueueView3->SetFocus();
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ExecuteQueueOperation(
  TQueueOperation Operation, void * Param)
{
  if (Operation == qoGoTo)
  {
    GoToQueue();
  }
  else if (Operation == qoPreferences)
  {
    PreferencesDialog(pmQueue);
  }
  else
  {
    FQueueController->ExecuteOperation(Operation, Param);
  }
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::GetQueueEnabled()
{
  return (Queue != NULL) && Queue->Enabled;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ToggleQueueEnabled()
{
  DebugAssert(Queue != NULL);
  if (Queue != NULL)
  {
    Queue->Enabled = !Queue->Enabled;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::QueueView3ContextPopup(
  TObject * /*Sender*/, TPoint & /*MousePos*/, bool & /*Handled*/)
{
  FQueueActedItem = QueueView3->ItemFocused;
}
//---------------------------------------------------------------------------
/*virtual*/ int __fastcall TCustomScpExplorerForm::GetStaticComponentsHeight()
{
  return TopDock->Height + QueueSplitter->Height;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::QueueSplitterCanResize(
  TObject * /*Sender*/, int & NewSize, bool & Accept)
{
  // when queue is hidden by double-clicking splitter, stray attempt to
  // resize the panel with strange value arrives, make sure it is ignored
  if (ComponentVisible[fcQueueView])
  {
    int HeightLimit = ClientHeight - GetStaticComponentsHeight() -
      RemotePanel->Constraints->MinHeight;

    if (NewSize > HeightLimit)
    {
      NewSize = HeightLimit;
    }
  }
  else
  {
    Accept = false;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::QueueView3StartDrag(TObject * /*Sender*/,
  TDragObject *& /*DragObject*/)
{
  FQueueActedItem = QueueView3->ItemFocused;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::QueueView3DragOver(TObject * /*Sender*/,
  TObject * Source, int X, int Y, TDragState /*State*/, bool & Accept)
{
  Accept = true;
  if (Source == QueueView3)
  {
    TListItem * DropTarget = QueueView3->GetItemAt(X, Y);
    Accept = (DropTarget != NULL) && (FQueueActedItem != NULL);
    if (Accept)
    {
      TQueueItemProxy * QueueItem;
      TQueueItemProxy * DestQueueItem;

      QueueItem = static_cast<TQueueItemProxy *>(FQueueActedItem->Data);
      DestQueueItem = static_cast<TQueueItemProxy *>(DropTarget->Data);
      Accept = (QueueItem != DestQueueItem) &&
        (QueueItem->Status == TQueueItem::qsPending) &&
        (DestQueueItem->Status == TQueueItem::qsPending);
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::QueueView3DragDrop(TObject * /*Sender*/,
  TObject * /*Source*/, int /*X*/, int /*Y*/)
{
  if ((FQueueActedItem != NULL) && (QueueView3->DropTarget != NULL))
  {
    TQueueItemProxy * QueueItem;
    TQueueItemProxy * DestQueueItem;

    QueueItem = static_cast<TQueueItemProxy *>(FQueueActedItem->Data);
    DestQueueItem = static_cast<TQueueItemProxy *>(QueueView3->DropTarget->Data);
    QueueItem->Move(DestQueueItem);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::QueueView3Enter(TObject * /*Sender*/)
{
  if ((QueueView3->ItemFocused == NULL) &&
      (QueueView3->Items->Count > 0))
  {
    QueueView3->ItemFocused = QueueView3->Items->Item[0];
  }

  QueueLabelUpdateStatus();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::QueueView3Exit(TObject * /*Sender*/)
{
  QueueLabelUpdateStatus();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::QueueLabelUpdateStatus()
{
  QueueLabel->UpdateStatus();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::QueueView3SelectItem(
  TObject * /*Sender*/, TListItem * /*Item*/, bool Selected)
{
  if (Selected)
  {
    NonVisualDataModule->UpdateNonVisibleActions();
  }
}
//---------------------------------------------------------------------------
TDragDropFilesEx * __fastcall TCustomScpExplorerForm::DragDropFiles(TObject * Sender)
{
  TDragDropFilesEx * Result = NULL;
  if (Sender == SessionsPageControl)
  {
    Result = FSessionsDragDropFilesEx;
  }
  else if (Sender == QueueView3)
  {
    Result = FQueueDragDropFilesEx;
  }
  else
  {
    TCustomDirView * DirView = dynamic_cast<TCustomDirView *>(Sender);
    if (DirView != NULL)
    {
      Result = DirView->DragDropFilesEx;
    }
    else
    {
      TCustomDriveView * DriveView = dynamic_cast<TCustomDriveView *>(Sender);
      if (DriveView != NULL)
      {
        Result = DriveView->DragDropFilesEx;
      }
    }
  }
  DebugAssert(Result != NULL);
  return Result;
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::SelectedAllFilesInDirView(TCustomDirView * DView)
{
  return (DView->SelCount == DView->FilesCount);
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::DraggingAllFilesFromDirView(TOperationSide Side, TStrings * FileList)
{
  return HasDirView[Side] && (DropSourceControl == DirView(Side)) && (FileList->Count == DirView(Side)->FilesCount);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemoteFileControlDragDropFileOperation(
  TObject * Sender, int Effect, UnicodeString TargetPath, bool ForceQueue)
{
  TFileOperation Operation;

  switch (Effect)
  {
    case DROPEFFECT_MOVE:
      Operation = foMove;
      break;

    case DROPEFFECT_COPY:
    // occures on WinXP (reported by user)
    default:
      Operation = foCopy;
      break;
  };

  TDragDropFilesEx * DragDropFilesEx = DragDropFiles(Sender);
  // see a comment in TUnixDirView::PerformItemDragDropOperation
  if (DragDropFilesEx->FileList->Count > 0)
  {
    TStrings * FileList = new TStringList();
    try
    {
      for (int Index = 0; Index < DragDropFilesEx->FileList->Count; Index++)
      {
        FileList->Add(DragDropFilesEx->FileList->Items[Index]->Name);
      }

      FDragDropOperation = true;
      TTransferOperationParam Param;
      Param.TargetDirectory = TargetPath;
      // upload, no temp dirs
      Param.Temp = false;
      Param.DragDrop = true;
      Param.Options =
        FLAGMASK(DraggingAllFilesFromDirView(osLocal, FileList), coAllFiles);
      if (ForceQueue)
      {
        Param.Queue = asOn;
      }
      if (ExecuteFileOperation(Operation, osLocal, FileList,
            (WinConfiguration->DDTransferConfirmation == asOff), &Param))
      {
        if (IsFileControl(DropSourceControl, osLocal))
        {
          Configuration->Usage->Inc(L"UploadsDragDropInternal");
        }
        else
        {
          Configuration->Usage->Inc(L"UploadsDragDropExternal");
        }
      }
    }
    __finally
    {
      FDragDropOperation = false;
      delete FileList;
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemoteFileControlDDFileOperation(
  TObject * Sender, int Effect, UnicodeString /*SourcePath*/,
  UnicodeString TargetPath, bool & /*DoOperation*/)
{
  RemoteFileControlDragDropFileOperation(Sender, Effect, TargetPath, false);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemoteFileContolDDChooseEffect(
  TObject * Sender, int grfKeyState, int & dwEffect)
{
  // if any drop effect is allowed at all (e.g. no drop to self and drop to parent)
  if ((dwEffect != DROPEFFECT_None) &&
      IsFileControl(DropSourceControl, osRemote))
  {
    // do not allow drop on remote panel (on free space, still allow drop on directories)
    if ((Sender == RemoteDirView) && (DropSourceControl == RemoteDirView) &&
        (RemoteDirView->DropTarget == NULL))
    {
      dwEffect = DROPEFFECT_None;
    }
    else
    {
      if (dwEffect == DROPEFFECT_Copy)
      {
        bool MoveCapable = FTerminal->IsCapable[fcRemoteMove];
        // currently we support copying always (at least via temporary directory);
        // remove associated checks once this all proves stable and working
        bool CopyCapable = true;
        // if we do not support neither of operations, there's no discussion
        if (!MoveCapable && !CopyCapable)
        {
          dwEffect = DROPEFFECT_None;
        }
        // when moving is disabled, we need to keep effect to "copy",
        // which will be later interpretted as move (with slipped-in cursor)
        else if (!WinConfiguration->DDAllowMoveInit && FLAGCLEAR(grfKeyState, MK_CONTROL))
        {
          // no-op, keep copy
        }
        else
        {
          // The default effect inside remote panel is move,
          // unless we do not support it, but support copy
          if (FLAGCLEAR(grfKeyState, MK_CONTROL))
          {
            dwEffect = MoveCapable ? DROPEFFECT_Move : DROPEFFECT_Copy;
          }
          else
          {
            // with ctrl-down, we want copy unless it is not supported
            dwEffect = CopyCapable ? DROPEFFECT_Copy : DROPEFFECT_None;
          }
        }
      }
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemoteFileControlDDDragFileName(
  TObject * Sender, TRemoteFile * File, UnicodeString & FileName)
{
  if (FDDTotalSize >= 0)
  {
    if (File->IsDirectory)
    {
      FDDTotalSize = -1;
    }
    else
    {
      FDDTotalSize += File->Size;
    }
  }
  DebugAssert(!FDragTempDir.IsEmpty());
  // TODO: this is quite ineffective
  // TODO: what if invalid character replacement is disabled?
  FileName = FDragTempDir + GUIConfiguration->CurrentCopyParam.ValidLocalFileName(File->FileName);

  UnicodeString TransferFileName;
  if (Sender == RemoteDriveView)
  {
    TransferFileName = UnixExcludeTrailingBackslash(File->FullFileName);
  }
  else
  {
    TransferFileName = File->FileName;
  }
  FDDFileList->AddObject(TransferFileName, File);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemoteFileControlDDDragDetect(
  TObject * /*Sender*/, int /*grfKeyState*/, const TPoint & /*DetectStart*/,
  const TPoint & /*Point*/, TDragDetectStatus /*DragStatus*/)
{
  // sometimes we do not get DDEnd so the list is not released
  SAFE_DESTROY(FDDFileList);
  FDDFileList = new TStringList();
  FDragTempDir = WinConfiguration->TemporaryDir();
  FDDTotalSize = 0;
  FDragDropSshTerminate = L"";
  FDragDropOnceDoneOperation = odoIdle;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemoteFileControlDDQueryContinueDrag(
  TObject * /*Sender*/, BOOL /*FEscapePressed*/, int /*grfKeyState*/,
  HRESULT & Result)
{
  if (Result == DRAGDROP_S_DROP)
  {
    try
    {
      GlobalDragImageList->HideDragImage();
      try
      {
        RemoteFileControlDDTargetDrop();
      }
      catch(ESshTerminate & E)
      {
        DebugAssert(E.MoreMessages == NULL); // not supported
        DebugAssert(!E.Message.IsEmpty());
        FDragDropSshTerminate = E.Message;
        FDragDropOnceDoneOperation = E.Operation;
      }
    }
    catch (Exception &E)
    {
      // If downloading fails we need to cancel drag&drop, otherwise
      // Explorer shows error
      // But by the way exception probably never reach this point as
      // it's catched on way.
      // Fatal exceptions get here (like when opening a secondary shell extension for file duplication fails).
      Result = DRAGDROP_S_CANCEL;
      ShowExtendedException(Terminal, &E);
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DirViewMatchMask(
  TObject * /*Sender*/, UnicodeString FileName, bool Directory, __int64 Size,
  TDateTime Modification, UnicodeString Masks, bool & Matches, bool AllowImplicitMatches)
{
  TFileMasks::TParams MaskParams;
  MaskParams.Size = Size;
  MaskParams.Modification = Modification;
  // this does not re-parse the mask if it is the same as the last time
  FDirViewMatchMask = Masks;
  bool ImplicitMatch;
  Matches =
    // RecurseInclude parameter has no effect as the Path is empty
    FDirViewMatchMask.Matches(FileName, Directory, UnicodeString(L""), &MaskParams, true, ImplicitMatch) &&
    (AllowImplicitMatches || !ImplicitMatch);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DirViewGetOverlay(
  TObject * Sender, TListItem * Item, WORD & Indexes)
{
  TCustomDirView * DirView = reinterpret_cast<TCustomDirView *>(Sender);
  UnicodeString Ext;
  if (DirView == RemoteDirView)
  {
    Ext = UnixExtractFileExt(DirView->ItemFileName(Item));
  }
  else
  {
    Ext = ExtractFileExt(DirView->ItemFileName(Item));
  }

  if (SameText(Ext, Configuration->PartialExt))
  {
    Indexes |= oiPartial;
  }
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::CanPasteToDirViewFromClipBoard()
{
  return
    DirViewEnabled(osCurrent) &&
    DirView(osCurrent)->CanPasteFromClipBoard();
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::CanPasteFromClipBoard()
{
  bool Result = false;

  if (CanPasteToDirViewFromClipBoard())
  {
    Result = true;
  }
  else
  {
    UnicodeString ClipboardText;
    if (NonEmptyTextFromClipboard(ClipboardText))
    {
      if (StoredSessions->IsUrl(ClipboardText))
      {
        Result = true;
      }
      else
      {
        Result = DirViewEnabled(osCurrent);
      }
    }
  }

  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::PasteFromClipBoard()
{
  if (CanPasteToDirViewFromClipBoard())
  {
    DirView(osCurrent)->PasteFromClipBoard();
  }
  else
  {
    UnicodeString ClipboardText;
    if (NonEmptyTextFromClipboard(ClipboardText))
    {
      if (StoredSessions->IsUrl(ClipboardText))
      {
        NewSession(false, ClipboardText);
      }
      else
      {
        DirView(osCurrent)->Path = ClipboardText;
      }
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::FileListFromClipboard()
{
  // TBD
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SelectSameExt(bool Select)
{
  TCustomDirView * CurrentDirView = DirView(osCurrent);
  if (DebugAlwaysTrue(CurrentDirView->ItemFocused != NULL))
  {
    UnicodeString FileName = CurrentDirView->ItemFileName(CurrentDirView->ItemFocused);
    UnicodeString Ext;
    if (GetSide(osCurrent) == osRemote)
    {
      Ext = UnixExtractFileExt(FileName);
    }
    else
    {
      Ext = ExtractFileExt(FileName);
    }
    if (Ext.IsEmpty())
    {
      Ext = L".";
    }
    TFileFilter Filter;
    Filter.Masks = FORMAT(L"*%s", (Ext));
    Filter.Directories = false;
    CurrentDirView->SelectFiles(Filter, Select);
  }
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TCustomScpExplorerForm::FileStatusBarText(
  const TStatusFileInfo & FileInfo, TOperationSide Side)
{
  UnicodeString Result;

  if ((Side == osRemote) && (Terminal == NULL))
  {
   // noop
  }
  else
  {
    Result =
      FMTLOAD(FILE_INFO_FORMAT,
        (FormatBytes(FileInfo.SelectedSize),
         FormatBytes(FileInfo.FilesSize),
         FormatNumber(FileInfo.SelectedCount),
         FormatNumber(FileInfo.FilesCount)));
  }

  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::FileStatusBarPanelClick(
  TTBXStatusPanel * Panel, TOperationSide Side)
{
  if (Panel->Index == 1)
  {
    ToggleShowHiddenFiles();
  }
  else if (Panel->Index == 2)
  {
    Filter(Side);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateFileStatusBar(
  TTBXStatusBar * StatusBar, const TStatusFileInfo & FileInfo, TOperationSide Side)
{
  DebugAssert(!StatusBar->SimplePanel);
  StatusBar->Panels->Items[0]->Caption = FileStatusBarText(FileInfo, Side);
  UpdateFileStatusExtendedPanels(StatusBar, FileInfo);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateFileStatusExtendedPanels(
  TTBXStatusBar * StatusBar, const TStatusFileInfo & FileInfo)
{
  DebugAssert(StatusBar->Panels->Count >= 3);

  TTBXStatusPanel * HiddenFilesPanel = StatusBar->Panels->Items[1];
  if (FileInfo.HiddenCount > 0)
  {
    HiddenFilesPanel->Caption = FMTLOAD(FILE_INFO_HIDDEN2, (FormatNumber(FileInfo.HiddenCount)));
    HiddenFilesPanel->ViewPriority = 90; // <100 allows hiding panel when it does not fit
  }
  else
  {
    HiddenFilesPanel->ViewPriority = 0;
    // not really necessary, just to cleanup no-longer-valid data
    HiddenFilesPanel->Caption = L"";
  }

  TTBXStatusPanel * FilteredFilesPanel = StatusBar->Panels->Items[2];
  if (FileInfo.FilteredCount > 0)
  {
    FilteredFilesPanel->Caption = FMTLOAD(FILE_INFO_FILTERED2, (FormatNumber(FileInfo.FilteredCount)));
    FilteredFilesPanel->ViewPriority = 90; // <100 allows hiding panel when it does not fit
  }
  else
  {
    FilteredFilesPanel->ViewPriority = 0;
    // not really necessary, just to cleanup no-longer-valid data
    FilteredFilesPanel->Caption = L"";
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemoteStatusBarClick(
  TObject * /*Sender*/)
{
  if (RemoteDirView->Enabled)
  {
    RemoteDirView->SetFocus();
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ToggleQueueVisibility()
{
  TQueueViewConfiguration Config = WinConfiguration->QueueView;
  switch (Config.Show)
  {
    case qvShow:
      if ((FQueueStatus != NULL) && (FQueueStatus->Count > 0))
      {
        Config.Show = qvHide;
      }
      else
      {
        Config.Show = Config.LastHideShow;
      }
      break;

    case qvHideWhenEmpty:
      if (ComponentVisible[fcQueueView])
      {
        Config.Show = qvHide;
      }
      else
      {
        Config.LastHideShow = Config.Show;
        Config.Show = qvShow;
      }
      break;

    case qvHide:
      Config.LastHideShow = Config.Show;
      Config.Show = qvShow;
      break;
  }
  WinConfiguration->QueueView = Config;
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TCustomScpExplorerForm::PathForCaption()
{
  UnicodeString Result;
  if (FTerminal != NULL)
  {
    switch (WinConfiguration->PathInCaption)
    {
      case picShort:
        {
          Result = UnixExtractFileName(FTerminal->CurrentDirectory);
          if (Result.IsEmpty())
          {
            Result = FTerminal->CurrentDirectory;
          }
        }
        break;

      case picFull:
        Result = FTerminal->CurrentDirectory;
        break;
    }
  }

  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateControls()
{
  TTerminalManager::Instance()->UpdateAppTitle();
  // WORAKRDOUND: Disabling list view when it is not showing yet does not set its
  // background to gray on Windows 7 (works on Windows 10).
  // See also EnableControl
  if (Showing)
  {
    bool HasTerminal = (Terminal != NULL);
    if (HasTerminal)
    {
      if (!RemoteDirView->Enabled)
      {
        RemoteDirView->Enabled = true;
        if (FRemoteDirViewWasFocused)
        {
          ActiveControl = RemoteDirView;
        }
      }
      RemoteDriveView->Enabled = true;
      RemoteDirView->Color = (FSessionColor != 0 ? FSessionColor : clWindow);
      RemoteDriveView->Color = RemoteDirView->Color;
    }
    else
    {
      if (RemoteDirView->Enabled)
      {
        // This is first called when the form is being constructed
        // (not anymore due to Showing test above)
        // but the false is overriden in the constructor later.
        // An even later in TScpCommanderForm::DoShow()
        FRemoteDirViewWasFocused = (ActiveControl == RemoteDirView);
        EnableControl(RemoteDirView, false);
      }
      EnableControl(RemoteDriveView, false);
    }
    EnableControl(QueueView3, HasTerminal);
    QueueLabelUpdateStatus();
    reinterpret_cast<TTBCustomItem *>(GetComponent(fcRemotePathComboBox))->Enabled = HasTerminal;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DoDirViewLoaded(TCustomDirView * /*Sender*/)
{
  UpdateControls();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DirViewLoaded(
  TObject * Sender)
{
  TCustomDirView * DirView = dynamic_cast<TCustomDirView *>(Sender);
  DebugAssert(DirView != NULL);
  DoDirViewLoaded(DirView);
  TransferPresetAutoSelect();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::StartUpdates()
{
  TUpdatesConfiguration Updates = WinConfiguration->Updates;
  // first run after installation
  if (double(Updates.LastCheck) == 0)
  {
    // make sure next time there will be an update (if enabled)
    Updates.LastCheck = TDateTime(1);
    WinConfiguration->Updates = Updates;
  }
  else if ((double(Updates.Period) > 0) &&
           (Now() - Updates.LastCheck >= Updates.Period))
  {
    StartUpdateThread(UpdatesChecked);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdatesChecked()
{
  UnicodeString Message;
  bool New;
  TQueryType Type;
  GetUpdatesMessage(Message, New, Type, false);
  if (!Message.IsEmpty())
  {
    if (New)
    {
      Message = FMTLOAD(NEW_VERSION_CLICK, (Message));
    }
    if (!New && (Type != qtWarning))
    {
      PostNote(UnformatMessage(Message), 0, UpdatesNoteClicked, NULL);
    }
    else
    {
      Notify(NULL, Message, Type, true, UpdatesNoteClicked);
    }

    if (New)
    {
      Configuration->Usage->Inc(L"UpdateNotifications");
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdatesNoteClicked(TObject * /*Sender*/)
{
  RestoreApp();

  if (!NonVisualDataModule->Busy)
  {
    Configuration->Usage->Inc(L"UpdateNotificationsClicked");
    CheckForUpdates(true);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::GetTransferPresetAutoSelectData(
  TCopyParamRuleData & Data)
{
  DebugAssert(Terminal != NULL);
  Data.HostName = Terminal->SessionData->HostNameExpanded;
  Data.UserName = Terminal->SessionData->UserNameExpanded;
  Data.RemoteDirectory = RemoteDirView->PathName;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::TransferPresetAutoSelect()
{
  // Terminal can be null when we are changing local directory implicitly,
  // because it has been deleted, while no session is connected
  // (Login dialog is open)
  if (FAllowTransferPresetAutoSelect && (Terminal != NULL))
  {
    TCopyParamRuleData Data;
    GetTransferPresetAutoSelectData(Data);

    int CopyParamIndex = GUIConfiguration->CopyParamList->Find(Data);
    UnicodeString CopyParamCurrent = GUIConfiguration->CopyParamCurrent;

    if (CopyParamIndex < 0)
    {
      // there is no preset that matches autoselection
      // set preset that we consider "default"
      FCopyParamAutoSelected = L""; // forget last autoselected preset
      GUIConfiguration->CopyParamCurrent = FCopyParamDefault;
    }
    else
    {
      // there is preset matching autoselection
      UnicodeString CopyParamName = GUIConfiguration->CopyParamList->Names[CopyParamIndex];
      if (CopyParamName == FCopyParamAutoSelected)
      {
        // autoselected the same preset as the last time
        // make no change (i.e. preserve custom user preset, if any)
      }
      else
      {
        // autoselected the different preset then the last time (or there
        // was default preset set)
        FCopyParamAutoSelected = CopyParamName; // remember autoselection
        GUIConfiguration->CopyParamCurrent = CopyParamName;
      }
    }

    if (GUIConfiguration->CopyParamCurrent != CopyParamCurrent)
    {
      if (CopyParamIndex >= 0)
      {
        Configuration->Usage->Inc(L"CopyParamAutoSelects");
      }

      TTransferPresetNoteData * Data = new TTransferPresetNoteData;
      try
      {
        int Fmt =
          (CopyParamIndex < 0) ?
            (GUIConfiguration->CopyParamIndex < 0 ? COPY_PARAM_DEFAULT_NORM : COPY_PARAM_DEFAULT_CUSTOM) :
            COPY_PARAM_AUTOSELECTED;
        UnicodeString Message = FMTLOAD(Fmt, (StripHotkey(GUIConfiguration->CopyParamCurrent)));
        Data->Message = MainInstructions(Message);

        int CopyParamAttrs = Terminal->UsableCopyParamAttrs(0).General;
        UnicodeString Info = GUIConfiguration->CurrentCopyParam.GetInfoStr(L"\n",
          CopyParamAttrs);
        if (CopyParamIndex >= 0)
        {
          DebugAssert(GUIConfiguration->CopyParamList->Rules[CopyParamIndex] != NULL);
          Info = FORMAT(L"%s\n \n%s", (Info,
            FMTLOAD(COPY_PARAM_RULE,
              (GUIConfiguration->CopyParamList->Rules[CopyParamIndex]->GetInfoStr(L"\n")))));
        }
        Data->Message += L"\n\n" + Info;

        if (WinConfiguration->CopyParamAutoSelectNotice)
        {
          TransferPresetNoteMessage(Data, true);
        }
        else
        {
          PostNote(Message, 0, TransferPresetNoteClicked, Data);
          Data = NULL; // ownership passed
        }
      }
      __finally
      {
        delete Data;
      }
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::TransferPresetNoteMessage(
  TTransferPresetNoteData * NoteData, bool AllowNeverAskAgain)
{
  DebugAssert(NoteData != NULL);

  TMessageParams Params(AllowNeverAskAgain ? mpNeverAskAgainCheck : 0);

  TQueryButtonAlias Aliases[1];
  Aliases[0].Button = qaIgnore; // "ignore" is after "ok"
  Aliases[0].Alias = LoadStr(CONFIGURE_BUTTON);

  Params.Aliases = Aliases;
  Params.AliasesCount = LENOF(Aliases);

  unsigned int Result =
    MoreMessageDialog(NoteData->Message, NULL, qtInformation,
      qaOK | qaIgnore, HELP_COPY_PARAM_AUTOSELECTED, &Params);

  switch (Result)
  {
    case qaNeverAskAgain:
      DebugAssert(AllowNeverAskAgain);
      WinConfiguration->CopyParamAutoSelectNotice = false;
      break;

    case qaIgnore:
      PreferencesDialog(pmPresets);
      break;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::TransferPresetNoteClicked(TObject * Sender)
{
  // as of now useless, as this is used for notes only, never for balloons, ...
  RestoreApp();

  // .. and we should never be busy here
  if (DebugAlwaysTrue(!NonVisualDataModule->Busy))
  {
    TransferPresetNoteMessage(DebugNotNull(dynamic_cast<TTransferPresetNoteData *>(Sender)), false);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::PreferencesDialog(
  TPreferencesMode APreferencesMode)
{
  std::unique_ptr<TPreferencesDialogData> PreferencesData;
  TCopyParamRuleData Data;
  if (Terminal != NULL)
  {
    PreferencesData.reset(new TPreferencesDialogData());
    GetTransferPresetAutoSelectData(Data);
    PreferencesData->CopyParamRuleData = &Data;
  }
  DoPreferencesDialog(APreferencesMode, PreferencesData.get());
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::AdHocCustomCommandValidate(
  const TCustomCommandType & Command)
{
  if (CustomCommandState(Command, FEditingFocusedAdHocCommand, ccltAll) <= 0)
  {
    throw Exception(FMTLOAD(CUSTOM_COMMAND_IMPOSSIBLE, (Command.Command)));
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::AdHocCustomCommand(bool OnFocused)
{
  bool RemoteAllowed = CustomCommandRemoteAllowed();
  TCustomCommandType Command;
  // make sure we use local custom command when remote are not supported
  if (RemoteAllowed || FLAGSET(FLastCustomCommand.Params, ccLocal))
  {
    Command = FLastCustomCommand;
  }
  else
  {
    Command.Params = Command.Params | ccLocal;
  }
  Command.Name = LoadStr(CUSTOM_COMMAND_AD_HOC_NAME);
  FEditingFocusedAdHocCommand = OnFocused;
  bool LocalSide = (FCurrentSide == osLocal);
  int Options =
    FLAGMASK((!RemoteAllowed || LocalSide), ccoDisableRemote) |
    FLAGMASK(LocalSide, ccoDisableRemoteFiles);
  if (DoCustomCommandDialog(Command, WinConfiguration->CustomCommandList,
       ccmAdHoc, Options, AdHocCustomCommandValidate, NULL))
  {
    FLastCustomCommand = Command;
    UpdateCustomCommandsToolbar();
    ExecuteFileOperation(foCustomCommand, osRemote, OnFocused, false, &Command);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::LastCustomCommand(bool OnFocused)
{
  DebugAssert(!FLastCustomCommand.Command.IsEmpty());

  int State = CustomCommandState(FLastCustomCommand, OnFocused, ccltAll);
  DebugAssert(State > 0);
  if (State <= 0)
  {
    throw Exception(FMTLOAD(CUSTOM_COMMAND_IMPOSSIBLE, (FLastCustomCommand.Command)));
  }

  ExecuteFileOperation(foCustomCommand, osRemote, OnFocused, false, &FLastCustomCommand);
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::GetLastCustomCommand(bool OnFocused,
  TCustomCommandType & Command, int & State)
{
  bool Result = !FLastCustomCommand.Command.IsEmpty();

  if (Result)
  {
    Command = FLastCustomCommand;

    State = CustomCommandState(FLastCustomCommand, OnFocused, ccltAll);
  }

  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::WhatsThis()
{
  SendMessage(Handle, WM_SYSCOMMAND, SC_CONTEXTHELP, 0);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::BeforeAction()
{
  if (RemoteDirView->ItemFocused != NULL)
  {
    RemoteDirView->ItemFocused->CancelEdit();
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::PostComponentHide(Byte Component)
{
  DebugAssert(ComponentVisible[Component]);
  PostMessage(Handle, WM_COMPONENT_HIDE, Component, 0);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::QueueSplitterDblClick(TObject * /*Sender*/)
{
  // when queue panel is resized here directly, the status bar is stretched
  // over whole space the panel occupied
  PostComponentHide(fcQueueView);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::Dispatch(void * Message)
{
  TMessage * M = static_cast<TMessage*>(Message);
  switch (M->Msg)
  {
    case CM_DIALOGCHAR:
      CMDialogChar(*M);
      break;

    case WM_APPCOMMAND:
      WMAppCommand(*M);
      break;

    case WM_SYSCOMMAND:
      WMSysCommand(*M);
      break;

    case WM_QUERYENDSESSION:
      WMQueryEndSession(*M);
      break;

    case WM_ENDSESSION:
      WMEndSession(*reinterpret_cast<TWMEndSession *>(M));
      break;

    case WM_COMPONENT_HIDE:
      {
        Byte Component = static_cast<Byte>(M->WParam);
        // sanity check
        if (ComponentVisible[Component])
        {
          // special treatment
          if (Component == fcQueueView)
          {
            ToggleQueueVisibility();
            DebugAssert(!ComponentVisible[fcQueueView]);
          }
          else
          {
            ComponentVisible[Component] = false;
          }
        }
      }
      break;

    case WM_COPYDATA:
      WMCopyData(*M);
      break;

    case WM_MANAGES_CAPTION:
      // caption managed in TTerminalManager::UpdateAppTitle()
      M->Result = 1;
      break;

    case WM_WANTS_MOUSEWHEEL:
      M->Result = 1;
      break;

    case CM_SHOWINGCHANGED:
      CMShowingChanged(*M);
      break;

    case WM_CLOSE:
      WMClose(*M);
      break;

    default:
      TForm::Dispatch(Message);
      break;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::WMClose(TMessage & Message)
{
  // Cannot close window while we are busy.
  // We cannot test this in FormCloseQuery as that is called also from
  // Close(), which is called by CloseApplicationAction. So we can be busy
  // there already even, when it is legitimate to close the application.
  // Possibly a better place to handle this would be WMSysCommand.
  if (NonVisualDataModule->Busy)
  {
    Message.Result = 1;
  }
  else
  {
    TForm::Dispatch(&Message);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::CMShowingChanged(TMessage & Message)
{
  TForm::Dispatch(&Message);

  if (Showing && (Terminal == NULL))
  {
    // When we are starting minimized (i.e. from an installer),
    // postpone showing Login dialog until we get restored.
    // Otherwise the Login dialog (and Authentication window) show restored
    // over invidible (minimized) main window.
    if (WindowState == wsMinimized)
    {
      FNeedSession = true;
    }
    else
    {
      // This happens before application ever goes idle, so the toolbars would
      // stay enabled (initial state) until the Login dialog is dismissed.
      UpdateActions();
      NonVisualDataModule->StartBusy();
      try
      {
        // Need to process WM_ACTIVATEAPP before showing the Login dialog,
        // otherwise the dialog does not receive focus.
        // With Commander interface the ProcessMessages is called already
        // by TDriveView, but with Explorer interface, we need to call it explicily
        Application->ProcessMessages();
        // do not reload sessions, they have been loaded just now (optimization)
        NeedSession(false);
      }
      __finally
      {
        NonVisualDataModule->EndBusy();
      }
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::FormConstrainedResize(
  TObject * /*Sender*/, int & MinWidth, int & MinHeight, int & MaxWidth,
  int & MaxHeight)
{
  // workaround for bug in TWinControl.CalcConstraints
  // Check for empty rect (restore from iconic state) is done there only after
  // call to AdjustClientRect, which enlarges the rect (for forms).
  TRect R = GetClientRect();
  // when restoring from iconic state, place no restrictions
  if (IsRectEmpty(R))
  {
    MinWidth = 0;
    MinHeight = 0;
    MaxWidth = 32000;
    MaxHeight = 32000;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::GetSpaceAvailable(const UnicodeString Path,
  TSpaceAvailable & ASpaceAvailable, bool & Close)
{
  // terminal can be already closed (e.g. dropped connection)
  if ((Terminal != NULL) && Terminal->IsCapable[fcCheckingSpaceAvailable])
  {
    Configuration->Usage->Inc(L"SpaceAvailableChecks");

    try
    {
      Terminal->SpaceAvailable(Path, ASpaceAvailable);
    }
    catch(...)
    {
      if (!Terminal->Active)
      {
        Close = true;
      }
      throw;
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::FileSystemInfo()
{
  const TSessionInfo & SessionInfo = Terminal->GetSessionInfo();
  const TFileSystemInfo & FileSystemInfo = Terminal->GetFileSystemInfo(true);
  TGetSpaceAvailable OnGetSpaceAvailable = NULL;
  if (Terminal->IsCapable[fcCheckingSpaceAvailable])
  {
    OnGetSpaceAvailable = GetSpaceAvailable;
  }
  DoFileSystemInfoDialog(SessionInfo, FileSystemInfo, Terminal->CurrentDirectory,
    OnGetSpaceAvailable);
}
//---------------------------------------------------------------------------
TSessionData * __fastcall TCustomScpExplorerForm::SessionDataForCode()
{
  std::unique_ptr<TSessionData> Data(CloneCurrentSessionData());
  const TSessionInfo & SessionInfo = Terminal->GetSessionInfo();
  Data->HostKey = SessionInfo.HostKeyFingerprint;
  return Data.release();
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::GenerateUrl(TStrings * Paths)
{
  std::unique_ptr<TSessionData> Data(SessionDataForCode());
  DoGenerateUrlDialog(Data.get(), Paths);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SessionGenerateUrl()
{
  GenerateUrl(NULL);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::FileGenerateUrl()
{
  std::unique_ptr<TStrings> Paths(new TStringList());
  DirView(osCurrent)->CreateFileList(false, true, Paths.get());
  GenerateUrl(Paths.get());
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SetSessionColor(TColor value)
{
  if (value != FSessionColor)
  {
    FSessionColor = value;

    TColor C = (value != 0 ? value : Vcl::Graphics::clNone);

    TTBXColorItem * ColorItem = dynamic_cast<TTBXColorItem *>(
      static_cast<TObject *>(GetComponent(fcColorMenu)));
    DebugAssert(ColorItem != NULL);
    ColorItem->Color = C;

    NonVisualDataModule->ColorMenuItem->Color = C;

    // Is null when called from LastTerminalClosed
    if (Terminal != NULL)
    {
      SessionsPageControl->ActivePage->ImageIndex = AddSessionColor(value);
    }

    UpdateControls();
  }
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::CancelNote(bool Force)
{
  bool Result = FNoteTimer->Enabled;
  if (Result)
  {
    // cannot cancel note too early
    bool NotEarly =
      (Now() - FNoteShown >
          EncodeTimeVerbose(0, 0, (unsigned short)(WinConfiguration->NotificationsStickTime), 0));
    if (Force || NotEarly)
    {
      FNoteTimer->Enabled = false;
      FNote = L"";
      SAFE_DESTROY(FNoteData);
      FOnNoteClick = NULL;
      FNoteHints = FNotes->Text;
      FNoteHints.Delete(FNoteHints.Length() - 1, 2);
      UpdateStatusBar();
    }
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::NoteTimer(TObject * /*Sender*/)
{
  DebugAssert(FNoteTimer->Enabled);
  CancelNote(true);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::AddNote(UnicodeString Note, bool UpdateNow)
{
  int P = Note.Pos(L"\n");
  if (P > 0)
  {
    Note.SetLength(P - 1);
  }

  FNotes->Add(FORMAT(L"[%s] %s",
    (FormatDateTime(Configuration->TimeFormat, Now()), Note)));
  while (FNotes->Count > 10)
  {
    FNotes->Delete(0);
  }

  if (UpdateNow)
  {
    FNoteHints = FNotes->Text;
    FNoteHints.Delete(FNoteHints.Length() - 1, 2);
    UpdateStatusBar();
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::PostNote(UnicodeString Note,
  unsigned int Seconds, TNotifyEvent OnNoteClick, TObject * NoteData)
{
  int P = Note.Pos(L"\n");
  if (P > 0)
  {
    Note.SetLength(P - 1);
  }

  FNoteHints = FNotes->Text;
  FNoteHints.Delete(FNoteHints.Length() - 1, 2);
  FNote = Note;
  SAFE_DESTROY(FNoteData);
  FNoteData = NoteData;
  FOnNoteClick = OnNoteClick;
  AddNote(Note, false);
  UpdateStatusBar();
  FNoteShown = Now();
  FNoteTimer->Enabled = false;
  if (Seconds == 0)
  {
    Seconds = WinConfiguration->NotificationsTimeout;
  }
  FNoteTimer->Interval = Seconds * MSecsPerSec;
  FNoteTimer->Enabled = true;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ReadDirectoryCancelled()
{
  PostNote(LoadStr(DIRECTORY_READING_CANCELLED), 0, NULL, NULL);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SynchronizeBrowsingChanged()
{
  if (NonVisualDataModule->SynchronizeBrowsingAction->Checked)
  {
    Configuration->Usage->Inc(L"SynchronizeBrowsingEnabled");
  }

  PostNote(FORMAT(LoadStrPart(SYNC_DIR_BROWSE_TOGGLE, 1),
    (LoadStrPart(SYNC_DIR_BROWSE_TOGGLE,
      (NonVisualDataModule->SynchronizeBrowsingAction->Checked ? 2 : 3)))),
    0, NULL, NULL);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ToggleShowHiddenFiles()
{
  WinConfiguration->ShowHiddenFiles = !WinConfiguration->ShowHiddenFiles;
  PostNote(FORMAT(LoadStrPart(SHOW_HIDDEN_FILES_TOGGLE, 1),
    (LoadStrPart(SHOW_HIDDEN_FILES_TOGGLE,
      (WinConfiguration->ShowHiddenFiles ? 2 : 3)))), 0, NULL, NULL);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SetFormatSizeBytes(TFormatBytesStyle Style)
{
  WinConfiguration->FormatSizeBytes = Style;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ToggleAutoReadDirectoryAfterOp()
{
  Configuration->AutoReadDirectoryAfterOp = !Configuration->AutoReadDirectoryAfterOp;
  PostNote(FORMAT(LoadStrPart(AUTO_READ_DIRECTORY_TOGGLE, 1),
    (LoadStrPart(AUTO_READ_DIRECTORY_TOGGLE,
      (Configuration->AutoReadDirectoryAfterOp ? 2 : 3)))), 0, NULL, NULL);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::StatusBarPanelDblClick(
  TTBXCustomStatusBar * Sender, TTBXStatusPanel * Panel)
{
  if (Panel->Index == 0)
  {
    if (FOnNoteClick != NULL)
    {
      // prevent the user data from being freed by possible call
      // to CancelNote or PostNote during call to OnNoteClick
      std::unique_ptr<TObject> NoteData(FNoteData);
      TNotifyEvent OnNoteClick = FOnNoteClick;
      FNoteData = NULL;
      // need to cancel the note as we are going to delete its user data
      CancelNote(true);
      OnNoteClick(NoteData.get());
    }
  }
  if (Panel->Index >= Sender->Panels->Count - SessionPanelCount)
  {
    FileSystemInfo();
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::LockWindow()
{
  // workaround:
  // 1) for unknown reason, disabling window, while minimized,
  // prevents it from restoring, even if it was enabled again meanwhile
  // 2) when disabling the main window, while another has focus
  // minimize is no longer possible ("keep up to date" dialog)
  // Whouldn't we use IsApplicationMinimized() here?
  if ((FLockSuspendLevel == 0) && !IsIconic(Application->Handle) && (Screen->ActiveForm == this))
  {
    Enabled = false;
  }

  FLockLevel++;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UnlockWindow()
{
  DebugAssert(FLockLevel > 0);
  FLockLevel--;

  if (FLockLevel == 0)
  {
    DebugAssert(FLockSuspendLevel == 0);
    Enabled = true;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SuspendWindowLock()
{
  // We need to make sure that window is enabled when the last modal window closes
  // otherwise focus is not restored correctly.
  // So we re-enable the window when modal window opens and
  // disable it back after is closes
  if (FLockLevel > 0)
  {
    // while we have nesting counter, we know that we never be called
    // recursivelly as Application->OnModalBegin is called only
    // for the top-level modal window
    if (DebugAlwaysTrue(FLockSuspendLevel == 0))
    {
      // won't be disabled when conditions in LockWindow() were not satisfied
      FDisabledOnLockSuspend = !Enabled;
      // When minimized to tray (or actually when set to SW_HIDE),
      // setting Enabled makes the window focusable even when there's
      // modal window over it
      if (!FTrayIcon->Visible)
      {
        Enabled = true;
      }
    }
    FLockSuspendLevel++;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::ResumeWindowLock()
{
  if (FLockSuspendLevel > 0)
  {
    DebugAssert(FLockLevel > 0);
    FLockSuspendLevel--;
    // see comment in SuspendWindowLock
    if (DebugAlwaysTrue(FLockSuspendLevel == 0))
    {
      // Note that window can be enabled here, when we were minized to tray when
      // was SuspendWindowLock() called.

      // We should possibly do the same check as in LockWindow(),
      // if it is ever possible that the consitions change between
      // SuspendWindowLock() and ResumeWindowLock()
      Enabled = !FDisabledOnLockSuspend;
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateRemotePathComboBox(bool TextOnly)
{
  if (!TextOnly)
  {
    TTBXComboBoxItem * RemotePathComboBox =
      reinterpret_cast<TTBXComboBoxItem *>(GetComponent(fcRemotePathComboBox));

    TStrings * Items = RemotePathComboBox->Strings;
    Items->BeginUpdate();
    try
    {
      Items->Clear();
      if (Terminal != NULL)
      {
        UnicodeString APath = UnixExcludeTrailingBackslash(RemoteDirView->Path);
        while (!IsUnixRootPath(APath))
        {
          int P = APath.LastDelimiter(L'/');
          DebugAssert(P >= 0);
          Items->Insert(0, APath.SubString(P + 1, APath.Length() - P));
          APath.SetLength(P - 1);
        }
        Items->Insert(0, Customunixdirview_SUnixDefaultRootName);
      }
    }
    __finally
    {
      RemotePathComboBox->ItemIndex = Items->Count - 1;
      // Setting ItemIndex to -1 does not reset its text
      if (Items->Count == 0)
      {
        RemotePathComboBox->Text = L"";
      }
      Items->EndUpdate();
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemotePathComboBoxAdjustImageIndex(
  TTBXComboBoxItem * Sender, const UnicodeString /*AText*/, int AIndex,
  int & ImageIndex)
{
  if (AIndex < 0)
  {
    AIndex = Sender->ItemIndex;
  }
  ImageIndex = (AIndex < Sender->Strings->Count - 1 ? StdDirIcon : StdDirSelIcon);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemotePathComboBoxDrawItem(
  TTBXCustomList * /*Sender*/, TCanvas * /*ACanvas*/, TRect & ARect, int AIndex,
  int /*AHoverIndex*/, bool & /*DrawDefault*/)
{
  ARect.Left += (10 * AIndex);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemotePathComboBoxMeasureWidth(
  TTBXCustomList * /*Sender*/, TCanvas * /*ACanvas*/, int AIndex, int &AWidth)
{
  AWidth += (10 * AIndex);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemotePathComboBoxItemClick(
  TObject * Sender)
{
  TTBXComboBoxItem * RemotePathComboBox = dynamic_cast<TTBXComboBoxItem*>(Sender);

  UnicodeString APath = UnixExcludeTrailingBackslash(RemoteDirView->Path);
  int Index = RemotePathComboBox->ItemIndex;
  while (Index < RemotePathComboBox->Strings->Count - 1)
  {
    APath = UnixExtractFileDir(APath);
    Index++;
  }
  // VanDyke style paths
  if (APath.IsEmpty())
  {
    DebugAssert(RemotePathComboBox->ItemIndex == 0);
    APath = ROOTDIRECTORY;
  }
  if (RemoteDirView->Path != APath)
  {
    RemoteDirView->Path = APath;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemotePathComboBoxCancel(TObject * Sender)
{
  DebugAssert(Sender == GetComponent(fcRemotePathComboBox));
  DebugUsedParam(Sender);
  UpdateRemotePathComboBox(true);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DirViewEditing(
  TObject * Sender, TListItem * Item, bool & /*AllowEdit*/)
{
  TCustomDirView * DirView = dynamic_cast<TCustomDirView *>(Sender);
  DebugAssert(DirView != NULL);
  if (!WinConfiguration->RenameWholeName && !DirView->ItemIsDirectory(Item))
  {
    HWND Edit = ListView_GetEditControl(DirView->Handle);
    // OnEditing is called also from TCustomListView::CanEdit
    if (Edit != NULL)
    {
      EditSelectBaseName(Edit);
    }
  }
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
TDragDropFilesEx * __fastcall TCustomScpExplorerForm::CreateDragDropFilesEx()
{
  TDragDropFilesEx * Result = new TDragDropFilesEx(this);
  Result->AutoDetectDnD = false;
  Result->NeedValid = TFileExMustDnDSet() << nvFilename;
  Result->RenderDataOn = rdoEnterAndDropSync;
  Result->TargetEffects = TDropEffectSet() << deCopy << deMove;
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::CreateWnd()
{
  TForm::CreateWnd();
  if (FSessionsDragDropFilesEx == NULL)
  {
    FSessionsDragDropFilesEx = CreateDragDropFilesEx();
    FSessionsDragDropFilesEx->OnDragOver = SessionsDDDragOver;
    FSessionsDragDropFilesEx->OnProcessDropped = SessionsDDProcessDropped;
    FSessionsDragDropFilesEx->OnDragEnter = SessionsDDDragEnter;
    FSessionsDragDropFilesEx->OnDragLeave = SessionsDDDragLeave;
  }
  if (FQueueDragDropFilesEx == NULL)
  {
    FQueueDragDropFilesEx = CreateDragDropFilesEx();
    // No need to set OnDragOver as we do not have any restrictions
    FQueueDragDropFilesEx->OnProcessDropped = QueueDDProcessDropped;
    FQueueDragDropFilesEx->OnDragEnter = QueueDDDragEnter;
    FQueueDragDropFilesEx->OnDragLeave = QueueDDDragLeave;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DestroyWnd()
{
  TForm::DestroyWnd();
  FSessionsDragDropFilesEx->DragDropControl = NULL;
  FQueueDragDropFilesEx->DragDropControl = NULL;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::FormShow(TObject * /*Sender*/)
{
  SideEnter(FCurrentSide);
  FEverShown = true;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DoFindFiles(
  UnicodeString Directory, const TFileMasks & FileMask,
  TFileFoundEvent OnFileFound, TFindingFileEvent OnFindingFile)
{
  Configuration->Usage->Inc(L"FileFinds");
  FTerminal->FilesFind(Directory, FileMask, OnFileFound, OnFindingFile);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DoFocusRemotePath(UnicodeString Path)
{
  RemoteDirView->Path = UnixExtractFilePath(Path);
  TListItem * Item = RemoteDirView->FindFileItem(UnixExtractFileName(Path));
  if (Item != NULL)
  {
    RemoteDirView->ItemFocused = Item;
    RemoteDirView->ItemFocused->MakeVisible(false);
    RemoteDirView->SetFocus();
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemoteFindFiles()
{
  UnicodeString Path;
  if (DoFileFindDialog(RemoteDirView->Path, DoFindFiles, Path))
  {
    DoFocusRemotePath(Path);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::UpdateTaskbarList(ITaskbarList3 * TaskbarList)
{
  FTaskbarList = TaskbarList;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SessionsPageControlMouseDown(
  TObject * /*Sender*/, TMouseButton Button, TShiftState /*Shift*/, int X, int Y)
{
  int Index = SessionsPageControl->IndexOfTabAt(X, Y);
  if (Index >= 0)
  {
    if (Button == mbLeft)
    {
      // "Mouse down" is raised only after tab is switched.
      // If switching tab (switching session) takes long enough for user
      // to actually release the button, "mouse down" is still raised,
      // but we do not get "mouse up" event, so dragging is not cancelled,
      // prevent that by not beginning dragging in the first place.
      if (IsKeyPressed(VK_LBUTTON))
      {
        // when user clicks the "+", we get mouse down only after the session
        // is closed, when new session tab is already on X:Y, so dragging
        // starts, prevent that
        if (MilliSecondsBetween(Now(), FSessionsPageControlNewSessionTime) > 500)
        {
          TTerminal * Terminal = GetSessionTabTerminal(SessionsPageControl->Pages[Index]);
          if (Terminal != NULL)
          {
            SessionsPageControl->BeginDrag(false);
          }
        }
      }
    }
    else if (Button == mbMiddle)
    {
      // ignore middle-click for "New session tab"
      TTerminal * Terminal = GetSessionTabTerminal(SessionsPageControl->Pages[Index]);
      if (Terminal != NULL)
      {
        SessionsPageControl->ActivePageIndex = Index;
        // Switch to session tab (has to be session tab, due to previous check)
        if (DebugAlwaysTrue(SessionTabSwitched()))
        {
          CloseSession();
        }
      }
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SessionsPageControlDragDrop(
  TObject * /*Sender*/, TObject * /*Source*/, int X, int Y)
{
  int Index = SessionsPageControl->IndexOfTabAt(X, Y);
  // do not allow dropping on the "+" tab
  TTerminal * TargetTerminal = GetSessionTabTerminal(SessionsPageControl->Pages[Index]);
  if ((TargetTerminal != NULL) &&
      (SessionsPageControl->ActivePage->PageIndex != Index))
  {
    Configuration->Usage->Inc(L"SessionTabMoves");
    // this is almost redundant as we would recreate tabs in DoTerminalListChanged,
    // but we want to actually prevent that to avoid flicker
    SessionsPageControl->ActivePage->PageIndex = Index;
    TTerminal * Terminal = GetSessionTabTerminal(SessionsPageControl->ActivePage);
    TTerminalManager::Instance()->Move(Terminal, TargetTerminal);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SessionsPageControlDragOver(
  TObject * Sender, TObject * Source, int X, int Y,
  TDragState /*State*/, bool & Accept)
{
  Accept = (Sender == Source);
  if (Accept)
  {
    int Index = SessionsPageControl->IndexOfTabAt(X, Y);
    TTerminal * Terminal = GetSessionTabTerminal(SessionsPageControl->Pages[Index]);
    // do not allow dragging to the "+" tab
    Accept = (Terminal != NULL);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SessionsDDDragOver(int /*KeyState*/,
  const TPoint & Point, int & Effect)
{
  int Index = SessionsPageControl->IndexOfTabAt(Point.X, Point.Y);
  if (Index < 0)
  {
    Effect = DROPEFFECT_None;
  }
  else
  {
    TTerminal * TargetTerminal = GetSessionTabTerminal(SessionsPageControl->Pages[Index]);
    // do not allow dropping on the "+" tab
    if (TargetTerminal == NULL)
    {
      Effect = DROPEFFECT_None;
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SessionsDDProcessDropped(
  TObject * /*Sender*/, int /*KeyState*/, const TPoint & Point, int Effect)
{
  int Index = SessionsPageControl->IndexOfTabAt(Point.X, Point.Y);
  // do not allow dropping on the "+" tab
  TTerminal * TargetTerminal = GetSessionTabTerminal(SessionsPageControl->Pages[Index]);
  if (TargetTerminal != NULL)
  {
    DebugAssert(!IsFileControl(DropSourceControl, osRemote));
    if (!IsFileControl(DropSourceControl, osRemote))
    {
      TTerminalManager::Instance()->ActiveTerminal = TargetTerminal;
      RemoteFileControlDragDropFileOperation(SessionsPageControl, Effect,
        // Why don't we use Terminal->CurrentDirectory directly?
        TTerminalManager::Instance()->ActiveTerminal->CurrentDirectory, false);
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::QueueDDProcessDropped(
  TObject * /*Sender*/, int /*KeyState*/, const TPoint & /*Point*/, int Effect)
{
  // Downloads are handled in RemoteFileControlDDEnd
  if (!IsFileControl(DropSourceControl, osRemote))
  {
    RemoteFileControlDragDropFileOperation(QueueView3, Effect,
      Terminal->CurrentDirectory,
      // force queue
      true);
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::FormClose(TObject * /*Sender*/, TCloseAction & /*Action*/)
{

  FShowing = false;

  // Do not save empty workspace
  if (WinConfiguration->AutoSaveWorkspace && (Terminal != NULL))
  {
    std::unique_ptr<TObjectList> DataList(DoCollectWorkspace());
    UnicodeString Name = WorkspaceName();
    DoSaveWorkspace(
      Name, DataList.get(),
      !Configuration->DisablePasswordStoring &&
      WinConfiguration->AutoSaveWorkspacePasswords);
    WinConfiguration->LastStoredSession = Name;
  }

}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::RemoteDirViewRead(TObject * /*Sender*/)
{
  TManagedTerminal * ManagedTerminal =
    dynamic_cast<TManagedTerminal *>(RemoteDirView->Terminal);
  if (ManagedTerminal != NULL)
  {
    ManagedTerminal->DirectoryLoaded = Now();
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DirViewSelectItem(TObject * Sender,
  TListItem * /*Item*/, bool /*Selected*/)
{
  TCustomDirView * DirView = reinterpret_cast<TCustomDirView *>(Sender);
  switch (DirView->LastSelectMethod)
  {
    case smKeyboard:
      Configuration->Usage->Inc(L"KeyboardSelections");
      break;

    case smMouse:
      Configuration->Usage->Inc(L"MouseSelections");
      break;
  }
}
//---------------------------------------------------------------------------
int __fastcall TCustomScpExplorerForm::AddSessionColor(TColor Color)
{
  if (Color != 0)
  {
    AddSessionColorImage(FSessionColors, Color, FSessionColorMaskImageIndex);
    return FSessionColors->Count - 1;
  }
  else
  {
    return FSessionTabImageIndex;
  }
}
//---------------------------------------------------------------------------
int __fastcall TCustomScpExplorerForm::AddFixedSessionImage(int GlyphsSourceIndex)
{
  TPngImageCollectionItem * Item =
    GlyphsModule->ExplorerImages->PngImages->Items[GlyphsSourceIndex];
  FSessionColors->AddPng(Item->PngImage);
  return FSessionColors->Count - 1;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::AddFixedSessionImages()
{
  FNewSessionTabImageIndex = AddFixedSessionImage(NonVisualDataModule->NewSessionAction->ImageIndex);
  FSessionTabImageIndex = AddFixedSessionImage(SiteImageIndex);
  FSessionColorMaskImageIndex = AddFixedSessionImage(SiteColorMaskImageIndex);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::CollectItemsWithTextDisplayMode(TWinControl * Control)
{
  for (int Index = 0; Index < Control->ControlCount; Index++)
  {
    TControl * AControl = Control->Controls[Index];
    TWinControl * WinControl = dynamic_cast<TWinControl *>(AControl);
    if (WinControl != NULL)
    {
      CollectItemsWithTextDisplayMode(WinControl);
    }

    TTBCustomToolbar * Toolbar = dynamic_cast<TTBCustomToolbar *>(AControl);
    if (Toolbar != NULL)
    {
      // we care for top-level items only
      TTBCustomItem * Items = Toolbar->Items;
      for (int ItemIndex = 0; ItemIndex < Items->Count; ItemIndex++)
      {
        TTBCustomItem * Item = Items->Items[ItemIndex];
        if (((Item->DisplayMode == nbdmImageAndText) ||
             (dynamic_cast<TTBXLabelItem *>(Item) != NULL)) &&
            EligibleForImageDisplayMode(Item))
        {
          FItemsWithTextDisplayMode.insert(Item);
        }
      }
    }
  }
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::EligibleForImageDisplayMode(TTBCustomItem * /*Item*/)
{
  return true;
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::UpdateToolbarDisplayMode()
{
  bool SelectiveToolbarText = WinConfiguration->SelectiveToolbarText;
  TTBItemDisplayMode DisplayMode = (SelectiveToolbarText ? nbdmImageAndText : nbdmDefault);

  typedef std::set<TTBCustomItem *> TItemsWithTextDisplayMode;
  TItemsWithTextDisplayMode::iterator i = FItemsWithTextDisplayMode.begin();
  bool Result = true;
  while (Result && (i != FItemsWithTextDisplayMode.end()))
  {
    TTBCustomItem * Item = *i;
    TTBXLabelItem * Label = dynamic_cast<TTBXLabelItem *>(Item);
    if (Label != NULL)
    {
      // optimization
      if (Label->Visible == SelectiveToolbarText)
      {
        Result = false;
      }
      else
      {
        Label->Visible = SelectiveToolbarText;
      }
    }
    else
    {
      // optimization
      if (Item->DisplayMode == DisplayMode)
      {
        Result = false;
      }
      else
      {
        Item->DisplayMode = DisplayMode;
      }
    }
    ++i;
  }

  if (Result)
  {
    UpdateNewSessionTab();
  }

  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DisplaySystemContextMenu()
{
  DebugFail();
}
//---------------------------------------------------------------------------
bool __fastcall TCustomScpExplorerForm::IsBusy()
{
  // Among other, a lock level is non zero, while directory is being loaded,
  // even when it happens as a result of dirview navigation,
  // i.e. when TNonVisualDataModule is NOT FBusy.
  // That's why the TNonVisualDataModule::GetBusy calls this method.
  // Among other this prevents a panel auto update to occur while
  // directory is changing.
  return (FLockLevel > 0) || DirView(osCurrent)->IsEditing();
}
//---------------------------------------------------------------------------
Boolean __fastcall TCustomScpExplorerForm::AllowedAction(TAction * /*Action*/, TActionAllowed Allowed)
{
  // While the window is disabled, we still seem to process menu shortcuts at least,
  // so stop it at least here.
  // See also TCustomScpExplorerForm::RemoteDirViewBusy
  return
    (Allowed == aaUpdate) ||
    !NonVisualDataModule->Busy;
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::EditMenuItemPopup(TTBCustomItem * Sender, bool FromLink)
{
  NonVisualDataModule->EditMenuItemPopup(Sender, FromLink);
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::DirViewBusy(TObject * /*Sender*/, int Busy, bool & State)
{
  // This is somewhat redundant to LockWindow() call from
  // TTerminalManager::TerminalReadDirectoryProgress.
  // But disabling window is known not to block keyboard shorcuts
  // (see TCustomScpExplorerForm::AllowedAction), this hopefully works.
  if (Busy > 0)
  {
    if (NonVisualDataModule->Busy)
    {
      State = false;
    }
    else
    {
      NonVisualDataModule->StartBusy();
      LockWindow();
    }
  }
  else if (Busy < 0)
  {
    UnlockWindow();
    NonVisualDataModule->EndBusy();
  }
  else
  {
    State = NonVisualDataModule->Busy;
  }
}
//---------------------------------------------------------------------------
void __fastcall TCustomScpExplorerForm::SessionsPageControlContextPopup(TObject * /*Sender*/, TPoint & MousePos, bool & Handled)
{
  int Index = SessionsPageControl->IndexOfTabAt(MousePos.X, MousePos.Y);
  // no context menu for "New session tab"
  if ((Index >= 0) && (GetSessionTabTerminal(SessionsPageControl->Pages[Index]) != NULL))
  {
    SessionsPageControl->ActivePageIndex = Index;

    if (DebugAlwaysTrue(SessionTabSwitched()))
    {
      // copied from TControl.WMContextMenu
      SendCancelMode(SessionsPageControl);

      // explicit popup instead of using PopupMenu property
      // to avoid menu to popup somewhere within SessionTabSwitched above,
      // while connecting yet not-connected session and hence
      // allowing an access to commands over not-completelly connected session
      TPoint Point = SessionsPageControl->ClientToScreen(MousePos);
      TPopupMenu * PopupMenu = NonVisualDataModule->SessionsPopup;
      PopupMenu->PopupComponent = SessionsPageControl;
      PopupMenu->Popup(Point.x, Point.y);
    }
  }
  Handled = true;
}
//---------------------------------------------------------------------------
