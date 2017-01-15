//---------------------------------------------------------------------------
#ifndef CustomScpExplorerH
#define CustomScpExplorerH
//---------------------------------------------------------------------------
#include <Classes.hpp>
#include <Controls.hpp>
#include <StdCtrls.hpp>
#include <Forms.hpp>
#include <CustomDirView.hpp>
#include <CustomUnixDirView.hpp>
#include <IEListView.hpp>
#include <NortonLikeListView.hpp>
#include <UnixDirView.h>
#include <ComCtrls.hpp>
#include <ExtCtrls.hpp>
#include <ToolWin.hpp>

#include <WinInterface.h>
#include <WinConfiguration.h>
#include <Terminal.h>
#include <Queue.h>
#include "QueueController.h"
#include "UnixDriveView.h"
#include "CustomDriveView.hpp"
#include "TBX.hpp"
#include "TB2Dock.hpp"
#include "TBXExtItems.hpp"
#include "TBXStatusBars.hpp"
#include "TB2Item.hpp"
#include "TB2Toolbar.hpp"
#include "TBXToolPals.hpp"
#include "PngImageList.hpp"
#include "ThemePageControl.h"
#include "PathLabel.hpp"
#include <Vcl.AppEvnts.hpp>
//---------------------------------------------------------------------------
class TProgressForm;
class TSynchronizeProgressForm;
class TTerminalQueue;
class TTerminalQueueStatus;
class TQueueItem;
class TQueueItemProxy;
class TQueueController;
class TSynchronizeController;
class TEditorManager;
class TEditorData;
class TTransferPresetNoteData;
struct TEditedFileData;
class ITaskbarList3;
//---------------------------------------------------------------------------
enum TActionAllowed { aaShortCut, aaUpdate, aaExecute };
enum TActionFlag { afLocal = 1, afRemote = 2, afExplorer = 4 , afCommander = 8 };
enum TExecuteFileBy { efShell = 1, efInternalEditor = 2, efExternalEditor = 3, efDefaultEditor = 100 };
enum TPanelExport { pePath, peFileList, peFullFileList };
enum TPanelExportDestination { pedClipboard, pedCommandLine };
enum TCopyOperationCommandFlag {
  cocNone = 0x00, cocShortCutHint = 0x01, cocQueue = 0x02, cocNonQueue = 0x04
};
enum TCustomCommandListType { ccltAll, ccltBoth, ccltNonFile, ccltFile };
//---------------------------------------------------------------------------
class TCustomScpExplorerForm : public TForm
{
__published:
  TPanel *RemotePanel;
  TTBXStatusBar *RemoteStatusBar;
  TUnixDirView *RemoteDirView;
  TTBXDock *TopDock;
  TListView *QueueView3;
  TPanel *QueuePanel;
  TSplitter *QueueSplitter;
  TTBXToolbar *QueueToolbar;
  TTBXDock *QueueDock;
  TTBXItem *QueueEnableItem;
  TTBXSeparatorItem *TBXSeparatorItem203;
  TTBXItem *TBXItem201;
  TTBXItem *TBXItem202;
  TTBXItem *TBXItem203;
  TTBXItem *TBXItem204;
  TTBXItem *TBXItem205;
  TTBXSeparatorItem *TBXSeparatorItem201;
  TTBXItem *TBXItem206;
  TTBXItem *TBXItem207;
  TTBXSeparatorItem *TBXSeparatorItem202;
  TTBXItem *TBXItem208;
  TUnixDriveView *RemoteDriveView;
  TSplitter *RemotePanelSplitter;
  TTBXItem *TBXItem194;
  TTBXItem *TBXItem195;
  TTBXSubmenuItem *TBXSubmenuItem27;
  TTBXItem *TBXItem211;
  TTBXItem *TBXItem225;
  TTBXItem *TBXItem226;
  TTabSheet *TabSheet1;
  TThemePageControl *SessionsPageControl;
  TPathLabel *QueueLabel;
  TTBXSeparatorItem *TBXSeparatorItem57;
  TTBXItem *QueueDeleteAllDoneQueueToolbarItem;
  TTBXItem *TBXItem173;
  TApplicationEvents *ApplicationEvents;
  void __fastcall ApplicationMinimize(TObject * Sender);
  void __fastcall ApplicationRestore(TObject * Sender);
  void __fastcall RemoteDirViewContextPopup(TObject *Sender,
    const TPoint &MousePos, bool &Handled);
  void __fastcall RemoteDirViewGetSelectFilter(
    TCustomDirView *Sender, bool Select, TFileFilter &Filter);
  void __fastcall FormCloseQuery(TObject *Sender, bool &CanClose);
  void __fastcall RemoteDirViewDisplayProperties(TObject *Sender);
  void __fastcall DirViewColumnRightClick(TObject *Sender,
    TListColumn *Column, TPoint &Point);
  void __fastcall DirViewExecFile(TObject *Sender, TListItem *Item, bool &AllowExec);
  void __fastcall ToolBarResize(TObject *Sender);
  void __fastcall FileControlDDDragEnter(TObject *Sender,
    _di_IDataObject DataObj, int grfKeyState, const TPoint &Point,
    int &dwEffect, bool &Accept);
  void __fastcall FileControlDDDragLeave(TObject *Sender);
  void __fastcall RemoteFileControlDDCreateDragFileList(TObject *Sender,
    TFileList *FileList, bool &Created);
  void __fastcall RemoteFileControlDDEnd(TObject *Sender);
  void __fastcall RemoteFileControlDDCreateDataObject(TObject *Sender,
    TDataObject *&DataObject);
  void __fastcall RemoteFileControlDDGiveFeedback(TObject *Sender,
    int dwEffect, HRESULT &Result);
  void __fastcall QueueSplitterCanResize(TObject *Sender, int &NewSize,
    bool &Accept);
  void __fastcall QueueView3ContextPopup(TObject *Sender, TPoint &MousePos,
    bool &Handled);
  void __fastcall QueueView3Deletion(TObject *Sender, TListItem *Item);
  void __fastcall QueueView3StartDrag(TObject *Sender,
    TDragObject *&DragObject);
  void __fastcall QueueView3DragOver(TObject *Sender, TObject *Source,
    int X, int Y, TDragState State, bool &Accept);
  void __fastcall QueueView3DragDrop(TObject *Sender, TObject *Source,
    int X, int Y);
  void __fastcall QueueView3Enter(TObject *Sender);
  void __fastcall QueueView3SelectItem(TObject *Sender, TListItem *Item,
    bool Selected);
  void __fastcall RemoteFileControlDDFileOperation(TObject * Sender,
    int Effect, UnicodeString SourcePath, UnicodeString TargetPath,
    bool & DoOperation);
  void __fastcall RemoteFileContolDDChooseEffect(TObject * Sender,
    int grfKeyState, int & dwEffect);
  void __fastcall RemoteFileControlDDDragFileName(TObject * Sender,
    TRemoteFile * File, UnicodeString & FileName);
  void __fastcall RemoteFileControlDDDragDetect(TObject * Sender,
    int grfKeyState, const TPoint & DetectStart, const TPoint & Point,
    TDragDetectStatus DragStatus);
  void __fastcall RemoteFileControlDDQueryContinueDrag(TObject *Sender,
          BOOL FEscapePressed, int grfKeyState, HRESULT &Result);
  void __fastcall RemoteDirViewEnter(TObject *Sender);
  void __fastcall RemoteDriveViewEnter(TObject *Sender);
  void __fastcall DirViewMatchMask(TObject *Sender, UnicodeString FileName,
    bool Directory, __int64 Size, TDateTime Modification,
    UnicodeString Masks, bool &Matches, bool AllowImplicitMatches);
  void __fastcall DirViewGetOverlay(TObject *Sender, TListItem *Item,
    WORD &Indexes);
  void __fastcall DirViewHistoryChange(TCustomDirView *Sender);
  void __fastcall RemoteStatusBarClick(TObject *Sender);
  void __fastcall DirViewLoaded(TObject *Sender);
  void __fastcall ToolbarGetBaseSize(TTBCustomToolbar * Toolbar, TPoint & ASize);
  void __fastcall FormConstrainedResize(TObject * Sender, int & MinWidth,
    int  &MinHeight, int  &MaxWidth, int  &MaxHeight);
  void __fastcall StatusBarPanelDblClick(TTBXCustomStatusBar * Sender,
    TTBXStatusPanel * Panel);
  void __fastcall RemotePathComboBoxAdjustImageIndex(
    TTBXComboBoxItem * Sender, const UnicodeString AText, int AIndex,
    int & ImageIndex);
  void __fastcall RemotePathComboBoxDrawItem(TTBXCustomList * Sender,
    TCanvas * ACanvas, TRect & ARect, int AIndex, int AHoverIndex,
    bool & DrawDefault);
  void __fastcall RemotePathComboBoxMeasureWidth(TTBXCustomList * Sender,
    TCanvas * ACanvas, int AIndex, int & AWidth);
  void __fastcall RemotePathComboBoxItemClick(TObject * Sender);
  void __fastcall RemotePathComboBoxCancel(TObject * Sender);
  void __fastcall DirViewEditing(TObject *Sender, TListItem *Item,
          bool &AllowEdit);
  void __fastcall FormShow(TObject *Sender);
  void __fastcall SessionsPageControlChange(TObject *Sender);
  void __fastcall SessionsPageControlMouseDown(TObject *Sender, TMouseButton Button,
          TShiftState Shift, int X, int Y);
  void __fastcall FormClose(TObject *Sender, TCloseAction &Action);
  void __fastcall RemoteDirViewRead(TObject *Sender);
  void __fastcall DirViewSelectItem(TObject *Sender, TListItem *Item, bool Selected);
  void __fastcall SessionsPageControlDragDrop(TObject *Sender, TObject *Source, int X,
    int Y);
  void __fastcall SessionsPageControlDragOver(TObject *Sender, TObject *Source, int X,
    int Y, TDragState State, bool &Accept);
  void __fastcall QueueView3Exit(TObject *Sender);
  void __fastcall EditMenuItemPopup(TTBCustomItem *Sender, bool FromLink);
  void __fastcall DirViewBusy(TObject *Sender, int Busy, bool & Allow);
  void __fastcall SessionsPageControlContextPopup(TObject *Sender, TPoint &MousePos, bool &Handled);

private:
  TTerminal * FTerminal;
  TTerminalQueue * FQueue;
  TTerminalQueueStatus * FQueueStatus;
  TCriticalSection * FQueueStatusSection;
  bool FQueueStatusInvalidated;
  bool FQueueItemInvalidated;
  bool FFormRestored;
  bool FAutoOperation;
  bool FForceExecution;
  unsigned short FIgnoreNextDialogChar;
  TStringList * FErrorList;
  HANDLE FDDExtMutex;
  UnicodeString FDragExtFakeDirectory;
  TStrings * FDelayedDeletionList;
  TTimer * FDelayedDeletionTimer;
  TStrings * FDDFileList;
  __int64 FDDTotalSize;
  UnicodeString FDragDropSshTerminate;
  TOnceDoneOperation FDragDropOnceDoneOperation;
  HINSTANCE FOle32Library;
  HCURSOR FDragMoveCursor;
  UnicodeString FDragTempDir;
  bool FRefreshLocalDirectory;
  bool FRefreshRemoteDirectory;
  TListItem * FQueueActedItem;
  TQueueController * FQueueController;
  int FLastDropEffect;
  bool FPendingTempSpaceWarn;
  TEditorManager * FEditorManager;
  TList * FLocalEditors;
  TStrings * FCapturedLog;
  bool FDragDropOperation;
  UnicodeString FCopyParamDefault;
  UnicodeString FCopyParamAutoSelected;
  bool FEditingFocusedAdHocCommand;
  TList * FDocks;
  TSynchronizeController * FSynchronizeController;
  UnicodeString FTransferDropDownHint;
  int FTransferListHoverIndex;
  TColor FSessionColor;
  TPngImageList * FSessionColors;
  int FNewSessionTabImageIndex;
  int FSessionTabImageIndex;
  int FSessionColorMaskImageIndex;
  ::TTrayIcon * FTrayIcon;
  TCustomCommandType FLastCustomCommand;
  TFileMasks FDirViewMatchMask;
  TTBXPopupMenu * FCustomCommandMenu;
  TStrings * FCustomCommandLocalFileList;
  TStrings * FCustomCommandRemoteFileList;
  ITaskbarList3 * FTaskbarList;
  bool FShowing;
  int FMaxQueueLength;
  TDateTime FSessionsPageControlNewSessionTime;
  bool FAppIdle;
  typedef std::set<TTBCustomItem *> TItemsWithTextDisplayMode;
  TItemsWithTextDisplayMode FItemsWithTextDisplayMode;
  HWND FHiddenWindow;
  TStrings * FTransferResumeList;
  bool FMoveToQueue;
  bool FStandaloneEditing;
  TFeedSynchronizeError FOnFeedSynchronizeError;
  bool FNeedSession;

  bool __fastcall GetEnableFocusedOperation(TOperationSide Side, int FilesOnly);
  bool __fastcall GetEnableSelectedOperation(TOperationSide Side, int FilesOnly);
  void __fastcall SetTerminal(TTerminal * value);
  void __fastcall SetQueue(TTerminalQueue * value);
  void __fastcall TransferListChange(TObject * Sender);
  void __fastcall TransferListDrawItem(TTBXCustomList * Sender, TCanvas * ACanvas,
    const TRect & ARect, int AIndex, int AHoverIndex, bool & DrawDefault);
  void __fastcall CloseInternalEditor(TObject * Sender);
  void __fastcall ForceCloseInternalEditor(TObject * Sender);
  void __fastcall ForceCloseLocalEditors();
  void __fastcall TerminalCaptureLog(const UnicodeString & AddedLine, TCaptureOutputType OutputType);
  void __fastcall HistoryItemClick(System::TObject* Sender);
  void __fastcall UpdateHistoryMenu(TOperationSide Side, bool Back);
  void __fastcall AdHocCustomCommandValidate(const TCustomCommandType & Command);
  void __fastcall SetDockAllowDrag(bool value);
  void __fastcall QueueSplitterDblClick(TObject * Sender);
  void __fastcall AddQueueItem(TTerminalQueue * Queue, TTransferDirection Direction,
    TStrings * FileList, const UnicodeString TargetDirectory,
    const TCopyParamType & CopyParam, int Params);
  void __fastcall AddQueueItem(TTerminalQueue * Queue, TQueueItem * QueueItem, TTerminal * Terminal);
  void __fastcall ClearTransferSourceSelection(TTransferDirection Direction);
  void __fastcall SessionsDDDragOver(int KeyState, const TPoint & Point, int & Effect);
  void __fastcall SessionsDDProcessDropped(TObject * Sender, int KeyState, const TPoint & Point, int Effect);
  void __fastcall RemoteFileControlDragDropFileOperation(
    TObject * Sender, int Effect, UnicodeString TargetPath, bool ForceQueue);
  void __fastcall SessionsDDDragEnter(_di_IDataObject DataObj, int KeyState,
    const TPoint & Point, int & Effect, bool & Accept);
  void __fastcall SessionsDDDragLeave();
  void __fastcall QueueDDProcessDropped(TObject * Sender, int KeyState, const TPoint & Point, int Effect);
  void __fastcall QueueDDDragEnter(_di_IDataObject DataObj, int KeyState,
    const TPoint & Point, int & Effect, bool & Accept);
  void __fastcall QueueDDDragLeave();
  void __fastcall EnableDDTransferConfirmation(TObject * Sender);
  void __fastcall CollectItemsWithTextDisplayMode(TWinControl * Control);
  void __fastcall CreateHiddenWindow();
  bool __fastcall IsQueueAutoPopup();
  void __fastcall UpdateSessionsPageControlHeight();
  TDragDropFilesEx * __fastcall CreateDragDropFilesEx();
  void __fastcall KeyProcessed(Word & Key, TShiftState Shift);
  void __fastcall CheckCustomCommandShortCut(TCustomCommandList * List, Word & Key, Classes::TShiftState Shift, TShortCut KeyShortCut);
  bool __fastcall CanPasteToDirViewFromClipBoard();
  void __fastcall CMShowingChanged(TMessage & Message);
  void __fastcall WMClose(TMessage & Message);

protected:
  TOperationSide FCurrentSide;
  bool FEverShown;
  TControl * FDDTargetControl;
  TProgressForm * FProgressForm;
  TSynchronizeProgressForm * FSynchronizeProgressForm;
  HANDLE FDDExtMapFile;
  bool FDDMoveSlipped;
  TTimer * FUserActionTimer;
  TQueueItemProxy * FPendingQueueActionItem;
  TTBXPopupMenu * FHistoryMenu[2][2];
  bool FAllowTransferPresetAutoSelect;
  TStrings * FNotes;
  TTimer * FNoteTimer;
  TDateTime FNoteShown;
  UnicodeString FNote;
  TObject * FNoteData;
  UnicodeString FNoteHints;
  TNotifyEvent FOnNoteClick;
  unsigned int FLockLevel;
  unsigned int FLockSuspendLevel;
  bool FDisabledOnLockSuspend;
  TImageList * FSystemImageList;
  bool FAlternativeDelete;
  TDragDropFilesEx * FSessionsDragDropFilesEx;
  TDragDropFilesEx * FQueueDragDropFilesEx;
  TPoint FLastContextPopupScreenPoint;
  bool FRemoteDirViewWasFocused;

  virtual bool __fastcall CopyParamDialog(TTransferDirection Direction,
    TTransferType Type, bool Temp, TStrings * FileList,
    UnicodeString & TargetDirectory, TGUICopyParamType & CopyParam, bool Confirm,
    bool DragDrop, int Options);
  virtual bool __fastcall RemoteTransferDialog(TTerminal *& Session,
    TStrings * FileList, UnicodeString & Target, UnicodeString & FileMask, bool & DirectCopy,
    bool NoConfirmation, bool Move);
  virtual void __fastcall CreateParams(TCreateParams & Params);
  void __fastcall DeleteFiles(TOperationSide Side, TStrings * FileList, bool Alternative);
  bool __fastcall RemoteTransferFiles(TStrings * FileList, bool NoConfirmation,
    bool Move, TTerminal * Session);
  virtual void __fastcall DoDirViewExecFile(TObject * Sender, TListItem * Item, bool & AllowExec);
  virtual TControl * __fastcall GetComponent(Byte Component);
  bool __fastcall GetComponentVisible(Byte Component);
  virtual Boolean __fastcall GetHasDirView(TOperationSide Side);
  DYNAMIC void __fastcall KeyDown(Word & Key, Classes::TShiftState Shift);
  virtual void __fastcall RestoreFormParams();
  virtual void __fastcall RestoreParams();
  virtual void __fastcall SetComponentVisible(Byte Component, bool value);
  virtual void __fastcall ComponentShowing(Byte Component, bool value);
  virtual void __fastcall FixControlsPlacement();
  bool __fastcall SetProperties(TOperationSide Side, TStrings * FileList);
  void __fastcall CustomCommand(TStrings * FileList,
    const TCustomCommandType & Command, TStrings * ALocalFileList);
  virtual void __fastcall TerminalChanging();
  virtual void __fastcall TerminalChanged();
  virtual void __fastcall QueueChanged();
  void __fastcall InitStatusBar();
  void __fastcall UpdateStatusBar();
  virtual void __fastcall UpdateStatusPanelText(TTBXStatusPanel * Panel);
  virtual void __fastcall DoOperationFinished(TFileOperation Operation,
    TOperationSide Side, bool Temp, const UnicodeString & FileName, bool Success,
    TOnceDoneOperation & OnceDoneOperation);
  virtual void __fastcall DoOpenDirectoryDialog(TOpenDirectoryMode Mode, TOperationSide Side);
  virtual void __fastcall FileOperationProgress(TFileOperationProgressType & ProgressData);
  void __fastcall OperationComplete(const TDateTime & StartTime);
  void __fastcall ExecutedFileChanged(const UnicodeString FileName,
    TEditedFileData * Data, HANDLE UploadCompleteEvent);
  void __fastcall ExecutedFileReload(const UnicodeString FileName,
    const TEditedFileData * Data);
  void __fastcall ExecutedFileEarlyClosed(const TEditedFileData * Data,
    bool & KeepOpen);
  void __fastcall ExecutedFileUploadComplete(TObject * Sender);
  void __fastcall CMDialogChar(TMessage & AMessage);
  inline void __fastcall WMAppCommand(TMessage & Message);
  inline void __fastcall WMSysCommand(TMessage & Message);
  void __fastcall WMQueryEndSession(TMessage & Message);
  void __fastcall WMEndSession(TWMEndSession & Message);
  void __fastcall WMCopyData(TMessage & Message);
  virtual void __fastcall SysResizing(unsigned int Cmd);
  DYNAMIC void __fastcall DoShow();
  TStrings * __fastcall CreateVisitedDirectories(TOperationSide Side);
  void __fastcall HandleErrorList(TStringList *& ErrorList);
  void __fastcall TerminalSynchronizeDirectory(const UnicodeString LocalDirectory,
    const UnicodeString RemoteDirectory, bool & Continue, bool Collect);
  void __fastcall DoSynchronize(TSynchronizeController * Sender,
    const UnicodeString LocalDirectory, const UnicodeString RemoteDirectory,
    const TCopyParamType & CopyParam, const TSynchronizeParamType & Params,
    TSynchronizeChecklist ** Checklist, TSynchronizeOptions * Options, bool Full);
  void __fastcall DoSynchronizeInvalid(TSynchronizeController * Sender,
    const UnicodeString Directory, const UnicodeString ErrorStr);
  void __fastcall DoSynchronizeTooManyDirectories(TSynchronizeController * Sender,
    int & MaxDirectories);
  void __fastcall Synchronize(const UnicodeString LocalDirectory,
    const UnicodeString RemoteDirectory, TSynchronizeMode Mode,
    const TCopyParamType & CopyParam, int Params, TSynchronizeChecklist ** Checklist,
    TSynchronizeOptions * Options);
  void __fastcall SynchronizeSessionLog(const UnicodeString & Message);
  void __fastcall GetSynchronizeOptions(int Params, TSynchronizeOptions & Options);
  bool __fastcall SynchronizeAllowSelectedOnly();
  virtual void __fastcall BatchStart(void *& Storage);
  virtual void __fastcall BatchEnd(void * Storage);
  bool __fastcall ExecuteFileOperation(TFileOperation Operation, TOperationSide Side,
    TStrings * FileList, bool NoConfirmation, void * Param);
  virtual bool __fastcall DDGetTarget(UnicodeString & Directory,
    bool & ForceQueue, bool & Internal);
  virtual void __fastcall DDExtInitDrag(TFileList * FileList, bool & Created);
  virtual void __fastcall SideEnter(TOperationSide Side);
  virtual TOperationSide __fastcall GetSide(TOperationSide Side);
  TStrings * __fastcall PanelExport(TOperationSide Side, TPanelExport Export);
  virtual void __fastcall PanelExportStore(TOperationSide Side,
    TPanelExport Export, TPanelExportDestination Destination,
    TStrings * ExportData);
  void __fastcall GenerateUrl(TStrings * Paths);
  void __fastcall QueueListUpdate(TTerminalQueue * Queue);
  void __fastcall QueueItemUpdate(TTerminalQueue * Queue, TQueueItem * Item);
  void __fastcall UpdateQueueStatus(bool QueueChanging);
  void __fastcall RefreshQueueItems();
  virtual int __fastcall GetStaticComponentsHeight();
  void __fastcall FillQueueViewItem(TListItem * Item,
    TQueueItemProxy * QueueItem, bool Detail);
  void __fastcall QueueViewDeleteItem(int Index);
  void __fastcall UserActionTimer(TObject * Sender);
  void __fastcall UpdateQueueView();
  bool __fastcall CanCloseQueue();
  virtual bool __fastcall IsFileControl(TObject * Control, TOperationSide Side);
  virtual void __fastcall ReloadLocalDirectory(const UnicodeString Directory = L"");
  virtual bool __fastcall PanelOperation(TOperationSide Side, bool DragDrop);
  void __fastcall DoWarnLackOfTempSpace(const UnicodeString Path,
    __int64 RequiredSpace, bool & Continue);
  void __fastcall AddDelayedDirectoryDeletion(const UnicodeString TempDir, int SecDelay);
  void __fastcall DoDelayedDeletion(TObject * Sender);
  TDragDropFilesEx * __fastcall DragDropFiles(TObject * Sender);
  void __fastcall RemoteFileControlDDTargetDrop();
  bool __fastcall RemoteFileControlFileOperation(TObject * Sender,
    TFileOperation Operation, bool NoConfirmation, void * Param);
  void __fastcall DDDownload(TStrings * FilesToCopy,
    const UnicodeString TargetDir, const TCopyParamType * CopyParam, int Params);
  bool __fastcall EnsureCommandSessionFallback(TFSCapability Capability);
  bool __fastcall CommandSessionFallback();
  void __fastcall FileTerminalRemoved(const UnicodeString FileName,
    TEditedFileData * Data, TObject * Token, void * Arg);
  void __fastcall FileConfigurationChanged(const UnicodeString FileName,
    TEditedFileData * Data, TObject * Token, void * Arg);
  void __fastcall CustomExecuteFile(TOperationSide Side,
    TExecuteFileBy ExecuteFileBy, UnicodeString FileName, UnicodeString OriginalFileName,
    const TEditorData * ExternalEditor, UnicodeString LocalRootDirectory,
    UnicodeString RemoteDirectory);
  void __fastcall ExecuteFile(TOperationSide Side,
    TExecuteFileBy ExecuteFileBy, const TEditorData * ExternalEditor,
    UnicodeString FullFileName, TObject * Object,
    const TFileMasks::TParams & MaskParams);
  bool __fastcall RemoteExecuteForceText(TExecuteFileBy ExecuteFileBy,
    const TEditorData * ExternalEditor);
  void __fastcall ExecuteFileNormalize(TExecuteFileBy & ExecuteFileBy,
    const TEditorData *& ExternalEditor, const UnicodeString & FileName,
    bool Local, const TFileMasks::TParams & MaskParams);
  void __fastcall ExecuteRemoteFile(
    const UnicodeString & FullFileName, TRemoteFile * File, TExecuteFileBy ExecuteFileBy);
  void __fastcall TemporaryFileCopyParam(TCopyParamType & CopyParam);
  void __fastcall TemporaryDirectoryForRemoteFiles(
    UnicodeString RemoteDirectory, TCopyParamType CopyParam,
    UnicodeString & Result, UnicodeString & RootDirectory);
  void __fastcall TemporarilyDownloadFiles(TStrings * FileList, bool ForceText,
    UnicodeString & RootTempDir, UnicodeString & TempDir, bool AllFiles, bool GetTargetNames,
    bool AutoOperation);
  void __fastcall LocalEditorClosed(TObject * Sender, bool Forced);
  TTBXPopupMenu * __fastcall HistoryMenu(TOperationSide Side, bool Back);
  UnicodeString __fastcall FileStatusBarText(const TStatusFileInfo & FileInfo, TOperationSide Side);
  void __fastcall UpdateFileStatusBar(TTBXStatusBar * StatusBar,
    const TStatusFileInfo & FileInfo, TOperationSide Side);
  void __fastcall UpdateFileStatusExtendedPanels(
    TTBXStatusBar * StatusBar, const TStatusFileInfo & FileInfo);
  void __fastcall FileStatusBarPanelClick(TTBXStatusPanel * Panel, TOperationSide Side);
  virtual void __fastcall DoDirViewLoaded(TCustomDirView * Sender);
  virtual void __fastcall UpdateControls();
  void __fastcall UpdateTransferList();
  void __fastcall UpdateTransferLabel();
  void __fastcall StartUpdates();
  void __fastcall TransferPresetAutoSelect();
  virtual void __fastcall GetTransferPresetAutoSelectData(TCopyParamRuleData & Data);
  inline bool __fastcall CustomCommandRemoteAllowed();
  void __fastcall CustomCommandMenu(
    TAction * Action, TStrings * LocalFileList, TStrings * RemoteFileList);
  void __fastcall LoadToolbarsLayoutStr(UnicodeString LayoutStr);
  UnicodeString __fastcall GetToolbarsLayoutStr();
  virtual void __fastcall Dispatch(void * Message);
  void __fastcall PostComponentHide(Byte Component);
  void __fastcall GetSpaceAvailable(const UnicodeString Path,
    TSpaceAvailable & ASpaceAvailable, bool & Close);
  void __fastcall CalculateSize(TStrings * FileList, __int64 & Size,
    TCalculateSizeStats & Stats, bool & Close);
  void __fastcall CalculateChecksum(const UnicodeString & Alg, TStrings * FileList,
    TCalculatedChecksumEvent OnCalculatedChecksum, bool & Close);
  void __fastcall UpdateCustomCommandsToolbar();
  virtual void __fastcall UpdateActions();
  void __fastcall SetSessionColor(TColor value);
  void __fastcall NoteTimer(TObject * Sender);
  void __fastcall AddNote(UnicodeString Note, bool UpdateNow = true);
  void __fastcall PostNote(UnicodeString Note, unsigned int Seconds,
    TNotifyEvent OnNoteClick, TObject * NoteData);
  bool __fastcall CancelNote(bool Force);
  void __fastcall UpdatesChecked();
  void __fastcall UpdatesNoteClicked(TObject * Sender);
  void __fastcall TransferPresetNoteClicked(TObject * Sender);
  void __fastcall TransferPresetNoteMessage(TTransferPresetNoteData * NoteData,
    bool AllowNeverAskAgain);
  void __fastcall UpdateTrayIcon();
  void __fastcall TrayIconClick(TObject * Sender);
  void __fastcall Notify(TTerminal * Terminal, UnicodeString Message,
    TQueryType Type, bool Important = false, TNotifyEvent OnClick = NULL,
    TObject * UserData = NULL, Exception * E = NULL);
  virtual void __fastcall UpdateSessionData(TSessionData * Data);
  virtual void __fastcall UpdateRemotePathComboBox(bool TextOnly);
  virtual void __fastcall ToolbarItemResize(TTBXCustomDropDownItem * Item, int Width);
  virtual void __fastcall CreateWnd();
  virtual void __fastcall DestroyWnd();
  virtual bool __fastcall OpenBookmark(UnicodeString Local, UnicodeString Remote);
  void __fastcall DoFindFiles(UnicodeString Directory, const TFileMasks & FileMask,
    TFileFoundEvent OnFileFound, TFindingFileEvent OnFindingFile);
  virtual void __fastcall DoFocusRemotePath(UnicodeString Path);
  bool __fastcall ExecuteFileOperation(TFileOperation Operation, TOperationSide Side,
    bool OnFocused, bool NoConfirmation = false, void * Param = NULL);
  void __fastcall UpdateCopyParamCounters(const TCopyParamType & CopyParam);
  int __fastcall AddSessionColor(TColor Color);
  void __fastcall UpdateSessionTab(TTabSheet * TabSheet);
  void __fastcall UpdateNewSessionTab();
  void __fastcall AddFixedSessionImages();
  int __fastcall AddFixedSessionImage(int GlyphsSourceIndex);
  TObjectList * __fastcall DoCollectWorkspace();
  void __fastcall DoSaveWorkspace(const UnicodeString & Name,
    TObjectList * DataList, bool SavePasswords);
  UnicodeString __fastcall WorkspaceName();
  virtual bool __fastcall EligibleForImageDisplayMode(TTBCustomItem * Item);
  virtual bool __fastcall UpdateToolbarDisplayMode();
  virtual void __fastcall QueueLabelUpdateStatus();
  void __fastcall EditorAutoConfig();
  void __fastcall DirViewContextPopupDefaultItem(
    TOperationSide Side, TTBXCustomItem * Item, TDoubleClickAction DoubleClickAction);
  void __fastcall DirViewContextPopup(
    TOperationSide Side, Byte PopupComponent, const TPoint & MousePos);
  bool __fastcall CommandLineFromAnotherInstance(const UnicodeString & CommandLine);
  bool __fastcall CanCommandLineFromAnotherInstance();
  void __fastcall SetQueueProgress();
  void __fastcall UpdateQueueLabel();
  void __fastcall SetTaskbarListProgressState(TBPFLAG Flags);
  void __fastcall SetTaskbarListProgressValue(TFileOperationProgressType * ProgressData);
  TTerminal * __fastcall GetSessionTabTerminal(TTabSheet * TabSheet);
  bool __fastcall SessionTabSwitched();
  void __fastcall RestoreApp();
  void __fastcall GoToQueue();
  virtual UnicodeString __fastcall DefaultDownloadTargetDirectory() = 0;
  void __fastcall LockFiles(TStrings * FileList, bool Lock);
  void __fastcall SaveInternalEditor(
    const UnicodeString FileName, TEditedFileData * Data, TObject * Token,
    void * Arg);
  void __fastcall SaveAllInternalEditors(TObject * Sender);
  void __fastcall InternalEditorModified(
    const UnicodeString FileName, TEditedFileData * Data, TObject * Token,
    void * Arg);
  void __fastcall AnyInternalEditorModified(TObject * Sender, bool & Modified);
  virtual void __fastcall StartingDisconnected();
  void __fastcall DoTerminalListChanged(bool Force);
  void __fastcall NeedSession(bool ReloadSessions);
  bool __fastcall DraggingAllFilesFromDirView(TOperationSide Side, TStrings * FileList);
  bool __fastcall SelectedAllFilesInDirView(TCustomDirView * DView);
  TSessionData * __fastcall SessionDataForCode();
  void __fastcall RefreshPanel(const UnicodeString & Session, const UnicodeString & Path);

public:
  virtual __fastcall ~TCustomScpExplorerForm();
  void __fastcall AddBookmark(TOperationSide Side);
  virtual void __fastcall AddEditLink(TOperationSide Side, bool Add);
  bool __fastcall CanAddEditLink(TOperationSide Side);
  bool __fastcall LinkFocused();
  virtual Boolean __fastcall AllowedAction(TAction * Action, TActionAllowed Allowed);
  bool __fastcall IsBusy();
  virtual void __fastcall ConfigurationChanged();
  void __fastcall CreateDirectory(TOperationSide Side);
  void __fastcall ExecuteFileOperationCommand(TFileOperation Operation, TOperationSide Side,
    bool OnFocused, bool NoConfirmation = false, void * Param = NULL);
  void __fastcall ExecuteCopyOperationCommand(
    TOperationSide Side, bool OnFocused, unsigned int Flags);
  void __fastcall AdHocCustomCommand(bool OnFocused);
  virtual TCustomDirView * __fastcall DirView(TOperationSide Side);
  virtual bool __fastcall DirViewEnabled(TOperationSide Side);
  virtual void __fastcall ChangePath(TOperationSide Side) = 0;
  virtual void __fastcall StoreParams();
  int __fastcall CustomCommandState(const TCustomCommandType & Command, bool OnFocused, TCustomCommandListType ListType);
  bool __fastcall GetLastCustomCommand(bool OnFocused,
    TCustomCommandType & CustomCommand, int & State);
  void __fastcall LastCustomCommand(bool OnFocused);
  void __fastcall BothCustomCommand(const TCustomCommandType & Command);
  void __fastcall LockWindow();
  void __fastcall UnlockWindow();
  void __fastcall SuspendWindowLock();
  void __fastcall ResumeWindowLock();

  void __fastcall NewSession(bool FromSite, const UnicodeString & SessionUrl = L"");
  void __fastcall DuplicateSession();
  void __fastcall CloseSession();
  void __fastcall OpenDirectory(TOperationSide Side);
  virtual void __fastcall HomeDirectory(TOperationSide Side);
  void __fastcall OpenStoredSession(TSessionData * Data);
  void __fastcall OpenFolderOrWorkspace(const UnicodeString & Name);
  void __fastcall Idle();
  __fastcall TCustomScpExplorerForm(TComponent* Owner);
  void __fastcall SaveCurrentSession();
  TSessionData * __fastcall CloneCurrentSessionData();
  bool __fastcall SaveWorkspace(bool EnableAutoSave);
  virtual void __fastcall CompareDirectories();
  void __fastcall ExecuteCurrentFile();
  virtual void __fastcall OpenConsole(UnicodeString Command = L"");
  virtual void __fastcall UpdateTerminal(TTerminal * Terminal);
  virtual void __fastcall SynchronizeDirectories();
  virtual void __fastcall FullSynchronizeDirectories() = 0;
  virtual void __fastcall ExploreLocalDirectory();
  virtual void __fastcall GoToCommandLine();
  virtual void __fastcall GoToTree();
  void __fastcall PanelExport(TOperationSide Side, TPanelExport Export,
    TPanelExportDestination Destination);
  void __fastcall Filter(TOperationSide Side);
  void __fastcall ExecuteFile(TOperationSide Side, TExecuteFileBy ExecuteFileBy,
    const TEditorData * ExternalEditor = NULL, bool AllSelected = false,
    bool OnFocused = false);
  void __fastcall ExecuteCurrentFileWith(bool OnFocused);
  void __fastcall EditNew(TOperationSide Side);
  bool __fastcall AllowQueueOperation(TQueueOperation Operation, void ** Param = NULL);
  void __fastcall ExecuteQueueOperation(TQueueOperation Operation, void * Param = NULL);
  TQueueOperation __fastcall DefaultQueueOperation();
  bool __fastcall GetQueueEnabled();
  void __fastcall ToggleQueueEnabled();
  UnicodeString __fastcall GetQueueProgressTitle();
  void __fastcall LastTerminalClosed(TObject * Sender);
  void __fastcall TerminalRemoved(TObject * Sender);
  void __fastcall TerminalListChanged(TObject * Sender);
  void __fastcall ApplicationTitleChanged();
  unsigned int __fastcall MoreMessageDialog(const UnicodeString Message,
    TStrings * MoreMessages, TQueryType Type, unsigned int Answers,
    UnicodeString HelpKeyword, const TMessageParams * Params = NULL,
    TTerminal * Terminal = NULL);
  void __fastcall OperationFinished(TFileOperation Operation, TOperationSide Side,
    bool Temp, const UnicodeString & FileName, bool Success, TOnceDoneOperation & OnceDoneOperation);
  void __fastcall OperationProgress(TFileOperationProgressType & ProgressData);
  void __fastcall ShowExtendedException(TTerminal * Terminal, Exception * E);
  void __fastcall InactiveTerminalException(TTerminal * Terminal, Exception * E);
  void __fastcall TerminalReady();
  void __fastcall QueueEvent(TTerminal * Terminal, TTerminalQueue * Queue, TQueueEvent Event);
  void __fastcall QueueEmptyNoteClicked(TObject * Sender);
  bool __fastcall DoSynchronizeDirectories(UnicodeString & LocalDirectory,
    UnicodeString & RemoteDirectory, bool UseDefaults);
  bool __fastcall DoFullSynchronizeDirectories(UnicodeString & LocalDirectory,
    UnicodeString & RemoteDirectory, TSynchronizeMode & Mode, bool & SaveMode,
    bool UseDefaults);
  void __fastcall StandaloneEdit(const UnicodeString & FileName);
  bool __fastcall CanPasteFromClipBoard();
  void __fastcall PasteFromClipBoard();
  void __fastcall ToggleQueueVisibility();
  virtual UnicodeString __fastcall PathForCaption();
  void __fastcall FileListFromClipboard();
  void __fastcall SelectSameExt(bool Select);
  void __fastcall PreferencesDialog(TPreferencesMode APreferencesMode);
  void __fastcall WhatsThis();
  virtual void __fastcall BeforeAction();
  void __fastcall FileSystemInfo();
  void __fastcall SessionGenerateUrl();
  void __fastcall FileGenerateUrl();
  void __fastcall ReadDirectoryCancelled();
  void __fastcall SynchronizeBrowsingChanged();
  void __fastcall ToggleShowHiddenFiles();
  void __fastcall SetFormatSizeBytes(TFormatBytesStyle Style);
  void __fastcall ToggleAutoReadDirectoryAfterOp();
  void __fastcall PopupTrayBalloon(TTerminal * Terminal, const UnicodeString & Str,
    TQueryType Type, Exception * E = NULL, unsigned int Seconds = 0,
    TNotifyEvent OnBalloonClick = NULL, TObject * UserData = NULL);
  void __fastcall RemoteFindFiles();
  virtual void __fastcall HistoryGo(TOperationSide Side, int Index);
  void __fastcall UpdateTaskbarList(ITaskbarList3 * TaskbarList);
  virtual void __fastcall DisplaySystemContextMenu();
  virtual void __fastcall GoToAddress() = 0;
  bool __fastcall CanConsole();

  __property bool ComponentVisible[Byte Component] = { read = GetComponentVisible, write = SetComponentVisible };
  __property bool EnableFocusedOperation[TOperationSide Side] = { read = GetEnableFocusedOperation, index = 0 };
  __property bool EnableSelectedOperation[TOperationSide Side] = { read = GetEnableSelectedOperation, index = 0 };
  __property bool EnableFocusedFileOperation[TOperationSide Side] = { read = GetEnableFocusedOperation, index = 1 };
  __property bool EnableSelectedFileOperation[TOperationSide Side] = { read = GetEnableSelectedOperation, index = 1 };
  __property bool HasDirView[TOperationSide Side] = { read = GetHasDirView };
  __property TTerminal * Terminal = { read = FTerminal, write = SetTerminal };
  __property TTerminalQueue * Queue = { read = FQueue, write = SetQueue };
  __property TColor SessionColor = { read = FSessionColor, write = SetSessionColor };
};
//---------------------------------------------------------------------------
#endif
