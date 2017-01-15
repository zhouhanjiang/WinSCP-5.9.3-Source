//---------------------------------------------------------------------
#include <vcl.h>
#pragma hdrstop

#include <StrUtils.hpp>
#include <CoreMain.h>
#include <Common.h>
#include <PuttyTools.h>
#include <TextsWin.h>
#include <TextsCore.h>
#include <HelpWin.h>
#include <VCLCommon.h>

#include "WinInterface.h"
#include "Login.h"
#include "GUITools.h"
#include "Tools.h"
#include "Setup.h"
#include "WinConfiguration.h"
#include "ProgParams.h"
#include "WinApi.h"
//---------------------------------------------------------------------
#pragma link "ComboEdit"
#pragma link "PasswordEdit"
#pragma link "UpDownEdit"
#ifndef NO_RESOURCES
#pragma resource "*.dfm"
#endif
//---------------------------------------------------------------------------
const int SiteImageIndex = 1;
const int OpenFolderImageIndex = 2;
const int ClosedFolderImageIndex = 3;
const int WorkspaceImageIndex = 4;
const int NewSiteImageIndex = 6;
const int SiteColorMaskImageIndex = 8;
//---------------------------------------------------------------------------
bool __fastcall DoLoginDialog(TStoredSessionList *SessionList, TList * DataList, TForm * LinkedForm)
{
  DebugAssert(DataList != NULL);
  TLoginDialog * LoginDialog = SafeFormCreate<TLoginDialog>();
  bool Result;
  try
  {
    LoginDialog->Init(SessionList, LinkedForm);
    Result = LoginDialog->Execute(DataList);
  }
  __finally
  {
    delete LoginDialog;
  }
  return Result;
}
//---------------------------------------------------------------------
static const TFSProtocol FSOrder[] = { fsSFTPonly, fsSCPonly, fsFTP, fsWebDAV };
//---------------------------------------------------------------------
__fastcall TLoginDialog::TLoginDialog(TComponent* AOwner)
        : TForm(AOwner)
{
  FNewSiteData = new TSessionData(L"");
  FInitialized = false;
  FHintNode = NULL;
  FScrollOnDragOver = new TTreeViewScrollOnDragOver(SessionTree, true);
  FDataList = NULL;
  FUpdatePortWithProtocol = true;
  FIncrementalSearching = 0;
  FSitesIncrementalSearchHaveNext = false;
  FEditing = false;
  FRenaming = false;
  FNewSiteKeepName = false;
  FForceNewSite = false;
  FLoading = false;
  FSortEnablePending = false;
  FSiteSearch = ssSiteName;
  FLinkedForm = NULL;

  // we need to make sure that window procedure is set asap
  // (so that CM_SHOWINGCHANGED handling is applied)
  UseSystemSettingsPre(this);

  FBasicGroupBaseHeight = BasicGroup->Height - BasicSshPanel->Height - BasicFtpPanel->Height;
  FNoteGroupOffset = NoteGroup->Top - (BasicGroup->Top + BasicGroup->Height);
  HideComponentsPanel(this);
}
//---------------------------------------------------------------------
__fastcall TLoginDialog::~TLoginDialog()
{
  delete FScrollOnDragOver;
  delete FNewSiteData;
  InvalidateSessionData();
}
//---------------------------------------------------------------------
void __fastcall TLoginDialog::InvalidateSessionData()
{
  delete FSessionData;
  FSessionData = NULL;
}
//---------------------------------------------------------------------
void __fastcall TLoginDialog::Init(TStoredSessionList *SessionList, TForm * LinkedForm)
{
  FStoredSessions = SessionList;
  FLinkedForm = LinkedForm;
  LoadSessions();
  UnicodeString Dummy;
  RunPageantAction->Visible = FindTool(PageantTool, Dummy);
  RunPuttygenAction->Visible = FindTool(PuttygenTool, Dummy);
  UpdateControls();
}
//---------------------------------------------------------------------
void __fastcall TLoginDialog::InitControls()
{
  if (SessionTree->WindowProc != SessionTreeProc)
  {
    FOldSessionTreeProc = SessionTree->WindowProc;
    SessionTree->WindowProc = SessionTreeProc;
  }

  int FtpsNoneIndex = FtpsToIndex(ftpsNone);
  int FtpsImplicitIndex = FtpsToIndex(ftpsImplicit);
  FtpsCombo->Items->Strings[FtpsImplicitIndex] = LoadStr(FTPS_IMPLICIT);
  FtpsCombo->Items->Strings[FtpsToIndex(ftpsExplicitTls)] = LoadStr(FTPS_EXPLICIT);
  WebDavsCombo->Items->Strings[FtpsNoneIndex] = FtpsCombo->Items->Strings[FtpsNoneIndex];
  WebDavsCombo->Items->Strings[FtpsImplicitIndex] = FtpsCombo->Items->Strings[FtpsImplicitIndex];

  BasicSshPanel->Top = BasicFtpPanel->Top;

  SitesIncrementalSearchLabel->AutoSize = false;
  SitesIncrementalSearchLabel->Left = SessionTree->Left;
  SitesIncrementalSearchLabel->Width = SessionTree->Width;
  SitesIncrementalSearchLabel->Top = SessionTree->BoundsRect.Bottom - SitesIncrementalSearchLabel->Height;
  SitesIncrementalSearchLabel->Visible = false;

  ReadOnlyControl(TransferProtocolView);
  ReadOnlyControl(EncryptionView);
  ReadOnlyControl(NoteMemo);

  MenuButton(ToolsMenuButton);
  MenuButton(ManageButton);

  FixButtonImage(LoginButton);
  CenterButtonImage(LoginButton);

  SelectScaledImageList(SessionImageList);
  SelectScaledImageList(ActionImageList);

  // Generate button images.
  // The button does not support alpha channel,
  // so we have to copy the PNG's to BMP's and use plain transparent color
  FButtonImageList.reset(new TImageList(this));
  FButtonImageList->SetSize(ActionImageList->Width, ActionImageList->Height);
  LoginButton->Images = FButtonImageList.get();

  LoginButton->ImageIndex = AddLoginButtonImage(true);
  LoginButton->DisabledImageIndex = AddLoginButtonImage(false);

  if (SessionTree->Items->Count > 0)
  {
    SetNewSiteNodeLabel();
  }
}
//---------------------------------------------------------------------
int __fastcall TLoginDialog::AddLoginButtonImage(bool Enabled)
{
  std::unique_ptr<TBitmap> Bitmap(new TBitmap());
  Bitmap->SetSize(ActionImageList->Width, ActionImageList->Height);

  ActionImageList->Draw(Bitmap->Canvas, 0, 0, LoginAction->ImageIndex, Enabled);

  const TColor TransparentColor = clFuchsia;

  // 16x16 version does not have any background
  if (Bitmap->Canvas->Pixels[0][0] == clWhite)
  {
    // A background is white, but there's also white used on the image itself.
    // So we first replace the background white with a unique color,
    // setting it as a transparent later.
    // This is obviously a hack specific to this particular image.
    Bitmap->Canvas->Brush->Color = TransparentColor;
    Bitmap->Canvas->FloodFill(0, 0, Bitmap->Canvas->Pixels[0][0], fsSurface);
  }

  return FButtonImageList->AddMasked(Bitmap.get(), TransparentColor);
}
//---------------------------------------------------------------------
void __fastcall TLoginDialog::Init()
{
  FInitialized = true;
  UseSystemSettingsPost(this);

  Caption = FormatFormCaption(this, Caption);

  InitControls();

  #ifdef NO_FILEZILLA
  DebugAssert(TransferProtocolCombo->Items->Count == FSPROTOCOL_COUNT - 2 - 1);
  TransferProtocolCombo->Items->Delete(TransferProtocolCombo->Items->Count - 1);
  #endif

  ReadOnlyControl(ContentsNameEdit);
  ReadOnlyControl(ContentsMemo);

  if (DebugAlwaysFalse(SessionTree->Items->Count == 0) ||
      ((SessionTree->Items->Count == 1) &&
       DebugAlwaysTrue(IsNewSiteNode(SessionTree->Items->GetFirstNode()))) ||
      FForceNewSite)
  {
    ActiveControl = HostNameEdit;
  }
  else
  {
    ActiveControl = SessionTree;
  }

  UpdateControls();
}
//---------------------------------------------------------------------
TTreeNode * __fastcall TLoginDialog::AddSessionPath(UnicodeString Path,
  bool CanCreate, bool IsWorkspace)
{
  TTreeNode * Parent = NULL;
  while (!Path.IsEmpty())
  {
    UnicodeString Folder = CutToChar(Path, L'/', false);
    TTreeNode * Node =
      ((Parent == NULL) ? SessionTree->Items->GetFirstNode() : Parent->getFirstChild());
    // note that we allow folder with the same name as existing session
    // on the same level (see also SessionTreeEdited)
    while ((Node != NULL) && (IsSessionNode(Node) || !AnsiSameText(Node->Text, Folder)))
    {
      Node = Node->getNextSibling();
    }

    if (Node == NULL)
    {
      if (!CanCreate)
      {
        return NULL;
      }
      else
      {
        TTreeNode * AParent = Parent;
        Parent = SessionTree->Items->AddChild(Parent, Folder);
        // once workspace, forever workspace
        if (!IsWorkspaceNode(Parent))
        {
          if (IsWorkspace)
          {
            SetNodeImage(Parent, WorkspaceImageIndex);
          }
          else
          {
            UpdateFolderNode(Parent);
          }
        }
        // optimization
        if (!FLoading)
        {
          // folders seem not to be sorted automatically (not having set the data property)
          if (AParent == NULL)
          {
            SessionTree->Items->AlphaSort();
          }
          else
          {
            AParent->AlphaSort();
          }
        }
      }
    }
    else
    {
      Parent = Node;
    }
  }
  return Parent;
}
//---------------------------------------------------------------------
bool __fastcall TLoginDialog::IsFolderNode(TTreeNode * Node)
{
  return (Node != NULL) && (Node->Data == NULL) && (Node->ImageIndex != WorkspaceImageIndex);
}
//---------------------------------------------------------------------
bool __fastcall TLoginDialog::IsWorkspaceNode(TTreeNode * Node)
{
  return (Node != NULL) && (Node->Data == NULL) && (Node->ImageIndex == WorkspaceImageIndex);
}
//---------------------------------------------------------------------
bool __fastcall TLoginDialog::IsFolderOrWorkspaceNode(TTreeNode * Node)
{
  return (Node != NULL) && (Node->Data == NULL);
}
//---------------------------------------------------------------------
bool __fastcall TLoginDialog::IsSiteNode(TTreeNode * Node)
{
  return (Node != NULL) && (Node->Data != NULL) && (Node->Data != FNewSiteData);
}
//---------------------------------------------------------------------
bool __fastcall TLoginDialog::IsNewSiteNode(TTreeNode * Node)
{
  return (Node != NULL) && (Node->Data != NULL) && (Node->Data == FNewSiteData);
}
//---------------------------------------------------------------------
bool __fastcall TLoginDialog::IsSessionNode(TTreeNode * Node)
{
  return (Node != NULL) && (Node->Data != NULL);
}
//---------------------------------------------------------------------
TSessionData * __fastcall TLoginDialog::GetNodeSession(TTreeNode * Node)
{
  return DebugNotNull(static_cast<TSessionData *>(Node->Data));
}
//---------------------------------------------------------------------
TTreeNode * __fastcall TLoginDialog::AddSession(TSessionData * Data)
{
  TTreeNode * Parent = AddSessionPath(UnixExtractFilePath(Data->Name), true, Data->IsWorkspace);
  TTreeNode * Node = SessionTree->Items->AddChild(Parent, UnixExtractFileName(Data->Name));
  Node->Data = Data;
  UpdateNodeImage(Node);

  return Node;
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::UpdateNodeImage(TTreeNode * Node)
{
  SetNodeImage(Node, GetSessionImageIndex(GetNodeSession(Node)));
}
//---------------------------------------------------------------------
int __fastcall TLoginDialog::GetSessionImageIndex(TSessionData * Data)
{
  int Result;
  if (Data->Color != 0)
  {
    AddSessionColorImage(SessionTree->Images, static_cast<TColor>(Data->Color), SiteColorMaskImageIndex);
    Result = SessionTree->Images->Count - 1;
  }
  else
  {
    Result = SiteImageIndex;
  }
  return Result;
}
//---------------------------------------------------------------------
void __fastcall TLoginDialog::SetNodeImage(TTreeNode * Node, int ImageIndex)
{
  Node->ImageIndex = ImageIndex;
  Node->SelectedIndex = ImageIndex;
}
//---------------------------------------------------------------------
void __fastcall TLoginDialog::DestroySession(TSessionData * Data)
{
  StoredSessions->Remove(Data);
}
//---------------------------------------------------------------------
TTreeNode * __fastcall TLoginDialog::GetNewSiteNode()
{
  TTreeNode * Result = SessionTree->Items->GetFirstNode();
  DebugAssert(IsNewSiteNode(Result));
  return Result;
}
//---------------------------------------------------------------------
void __fastcall TLoginDialog::SetNewSiteNodeLabel()
{
  GetNewSiteNode()->Text = LoadStr(LOGIN_NEW_SITE_NODE);
}
//---------------------------------------------------------------------
void __fastcall TLoginDialog::LoadSessions()
{
  TAutoFlag LoadingFlag(FLoading);
  SessionTree->Items->BeginUpdate();
  try
  {
    // optimization
    SessionTree->SortType = Comctrls::stNone;

    SessionTree->Items->Clear();

    TTreeNode * Node = SessionTree->Items->AddChild(NULL, L"");
    Node->Data = FNewSiteData;
    SetNewSiteNodeLabel();
    SetNodeImage(Node, NewSiteImageIndex);

    DebugAssert(StoredSessions != NULL);
    for (int Index = 0; Index < StoredSessions->Count; Index++)
    {
      AddSession(StoredSessions->Sessions[Index]);
    }
  }
  __finally
  {
    // Restore sorting. Moreover, folders would not be sorted automatically even when
    // SortType is set (not having set the data property), so we would have to
    // call AlphaSort here explicitly
    SessionTree->SortType = Comctrls::stBoth;
    SessionTree->Items->EndUpdate();
  }
  SessionTree->Selected = SessionTree->Items->GetFirstNode();
  UpdateControls();
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::UpdateFolderNode(TTreeNode * Node)
{
  DebugAssert((Node->ImageIndex == 0) ||
    (Node->ImageIndex == OpenFolderImageIndex) || (Node->ImageIndex == ClosedFolderImageIndex));
  SetNodeImage(Node, (Node->Expanded ? OpenFolderImageIndex : ClosedFolderImageIndex));
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::NewSite()
{
  TTreeNode * NewSiteNode = GetNewSiteNode();
  if (DebugAlwaysTrue(IsNewSiteNode(NewSiteNode)))
  {
    SessionTree->Selected = NewSiteNode;
  }

  LoadContents();
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::ResetNewSiteData()
{
  if (DebugAlwaysTrue(StoredSessions != NULL))
  {
    FNewSiteData->CopyData(StoredSessions->DefaultSettings);
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::Default()
{
  ResetNewSiteData();

  NewSite();
}
//---------------------------------------------------------------------
void __fastcall TLoginDialog::LoadContents()
{
  bool UseContentsPanel;
  TTreeNode * Node = SessionTree->Selected;
  if (IsSessionNode(Node))
  {
    LoadSession(SelectedSession);
    UseContentsPanel = false;
  }
  else if (DebugAlwaysTrue(IsFolderOrWorkspaceNode(Node)))
  {
    UnicodeString NodePath = SessionNodePath(Node);
    ContentsNameEdit->Text = NodePath;
    UnicodeString CommonRoot = IsFolderNode(Node) ? UnixIncludeTrailingBackslash(NodePath) : UnicodeString();
    ContentsMemo->Lines->Text =
      GetFolderOrWorkspaceContents(Node, L"", CommonRoot);
    UseContentsPanel = true;
    if (IsFolderNode(Node))
    {
      ContentsGroupBox->Caption = LoadStr(LOGIN_SITE_FOLDER_CAPTION);
    }
    else if (DebugAlwaysTrue(IsWorkspaceNode(Node)))
    {
      ContentsGroupBox->Caption = LoadStr(LOGIN_WORKSPACE_CAPTION);
    }
  }

  SitePanel->Visible = !UseContentsPanel;
  ContentsPanel->Visible = UseContentsPanel;
}
//---------------------------------------------------------------------
void __fastcall TLoginDialog::LoadSession(TSessionData * SessionData)
{
  WinConfiguration->BeginMasterPasswordSession();
  try
  {
    UserNameEdit->Text = SessionData->UserName;
    PortNumberEdit->AsInteger = SessionData->PortNumber;
    HostNameEdit->Text = SessionData->HostName;

    bool Editable = IsEditable();
    if (Editable)
    {
      PasswordEdit->Text = SessionData->Password;
    }
    else
    {
      PasswordEdit->Text =
        SessionData->HasPassword() ?
          UnicodeString::StringOfChar(L'?', 16) : UnicodeString();
    }

    int FtpsIndex = FtpsToIndex(SessionData->Ftps);
    FtpsCombo->ItemIndex = FtpsIndex;
    WebDavsCombo->ItemIndex = FtpsIndex;
    EncryptionView->Text =
      DebugAlwaysTrue(FtpsCombo->ItemIndex >= WebDavsCombo->ItemIndex) ? FtpsCombo->Text : WebDavsCombo->Text;

    bool AllowScpFallback;
    TransferProtocolCombo->ItemIndex = FSProtocolToIndex(SessionData->FSProtocol, AllowScpFallback);
    TransferProtocolView->Text = TransferProtocolCombo->Text;

    NoteGroup->Visible = !Trim(SessionData->Note).IsEmpty();
    NoteMemo->Lines->Text = SessionData->Note;

    // just in case TransferProtocolComboChange is not triggered
    FDefaultPort = DefaultPort();
    FUpdatePortWithProtocol = true;

    if (SessionData != FSessionData)
    {
      // advanced
      InvalidateSessionData();
      // clone advanced settings only when really needed,
      // see also note in SessionAdvancedActionExecute
      if (Editable)
      {
        FSessionData = new TSessionData(L"");
        FSessionData->Assign(SessionData);
      }
    }
    else
    {
      // we should get here only when called from SessionAdvancedActionExecute
      DebugAssert(Editable);
    }
  }
  __finally
  {
    WinConfiguration->EndMasterPasswordSession();
  }

  UpdateControls();
}
//---------------------------------------------------------------------
void __fastcall TLoginDialog::SaveSession(TSessionData * SessionData)
{
  // advanced
  if (DebugAlwaysTrue(FSessionData != NULL))
  {
    SessionData->Assign(FSessionData);
  }

  // Basic page
  SessionData->UserName = UserNameEdit->Text.Trim();
  SessionData->PortNumber = PortNumberEdit->AsInteger;
  // must be loaded after UserName, because HostName may be in format user@host
  SessionData->HostName = HostNameEdit->Text.Trim();
  SessionData->Password = PasswordEdit->Text;
  SessionData->Ftps = GetFtps();

  SessionData->FSProtocol =
    // requiring SCP fallback distinction
    GetFSProtocol(true);

  TSessionData * EditingSessionData = GetEditingSessionData();
  SessionData->Name =
    (EditingSessionData != NULL) ? EditingSessionData->Name :
        (FNewSiteKeepName ? SessionData->Name : SessionData->DefaultSessionName);
}
//---------------------------------------------------------------------
bool __fastcall TLoginDialog::IsEditable()
{
  return IsNewSiteNode(SessionTree->Selected) || FEditing;
}
//---------------------------------------------------------------------
void __fastcall TLoginDialog::UpdateControls()
{
  if (Visible && FInitialized)
  {
    bool Editable = IsEditable();

    TFSProtocol FSProtocol = GetFSProtocol(false);
    bool SshProtocol = IsSshProtocol(FSProtocol);
    bool FtpProtocol = (FSProtocol == fsFTP);
    bool WebDavProtocol = (FSProtocol == fsWebDAV);

    // session
    FtpsCombo->Visible = Editable && FtpProtocol;
    FtpsLabel->Visible = FtpProtocol;
    WebDavsCombo->Visible = Editable && WebDavProtocol;
    WebDavsLabel->Visible = WebDavProtocol;
    EncryptionView->Visible = !Editable && (FtpProtocol || WebDavProtocol);

    BasicSshPanel->Visible = SshProtocol;
    BasicFtpPanel->Visible = FtpProtocol && Editable;
    // we do not support both at the same time
    DebugAssert(!BasicSshPanel->Visible || !BasicFtpPanel->Visible);
    BasicGroup->Height =
      FBasicGroupBaseHeight +
      (BasicSshPanel->Visible ? BasicSshPanel->Height : 0) +
      (BasicFtpPanel->Visible ? BasicFtpPanel->Height : 0);
    int NoteGroupTop = (BasicGroup->Top + BasicGroup->Height) + FNoteGroupOffset;
    NoteGroup->SetBounds(
      NoteGroup->Left, (BasicGroup->Top + BasicGroup->Height) + FNoteGroupOffset,
      NoteGroup->Width, NoteGroup->Top + NoteGroup->Height - NoteGroupTop);
    AnonymousLoginCheck->Checked =
      SameText(UserNameEdit->Text, AnonymousUserName) &&
      SameText(PasswordEdit->Text, AnonymousPassword);
    TransferProtocolCombo->Visible = Editable;
    TransferProtocolView->Visible = !TransferProtocolCombo->Visible;
    ReadOnlyControl(HostNameEdit, !Editable);
    ReadOnlyControl(PortNumberEdit, !Editable);
    PortNumberEdit->ButtonsVisible = Editable;
    // FSessionData may be NULL temporary even when Editable while switching nodes
    bool NoAuth = Editable && SshProtocol && (FSessionData != NULL) && FSessionData->SshNoUserAuth;
    ReadOnlyAndEnabledControl(UserNameEdit, !Editable, !NoAuth);
    EnableControl(UserNameLabel, UserNameEdit->Enabled);
    ReadOnlyAndEnabledControl(PasswordEdit, !Editable, !NoAuth);
    EnableControl(PasswordLabel, PasswordEdit->Enabled);

    // sites
    if (SitesIncrementalSearchLabel->Visible != !FSitesIncrementalSearch.IsEmpty())
    {
      if (FSitesIncrementalSearch.IsEmpty())
      {
        SitesIncrementalSearchLabel->Visible = false;
        SessionTree->Height = SitesIncrementalSearchLabel->BoundsRect.Bottom - SessionTree->Top;
      }
      else
      {
        SitesIncrementalSearchLabel->Visible = true;
        SessionTree->Height = SitesIncrementalSearchLabel->BoundsRect.Top - SessionTree->Top;
      }
    }

    if (!FSitesIncrementalSearch.IsEmpty())
    {
      SitesIncrementalSearchLabel->Caption =
        L" " + FMTLOAD(LOGIN_SITES_INC_SEARCH, (FSitesIncrementalSearch)) +
        (FSitesIncrementalSearchHaveNext ? L" " + LoadStr(LOGIN_SITES_NEXT_SEARCH) : UnicodeString());
    }

    EnableControl(ManageButton, !FEditing);
    EnableControl(ToolsMenuButton, !FEditing);
    EnableControl(CloseButton, !FEditing);

    DefaultButton(LoginButton, !FEditing && !FRenaming && !IsCloneToNewSiteDefault());
    CloseButton->Cancel = !FEditing && !FRenaming;
    DefaultButton(SaveButton, FEditing);
    EditCancelButton->Cancel = FEditing;
    SiteClonetoNewSiteMenuItem->Default = IsCloneToNewSiteDefault();
    SiteLoginMenuItem->Default = LoginButton->Default;

    UpdateButtonVisibility(SaveButton);
    UpdateButtonVisibility(EditButton);
    UpdateButtonVisibility(EditCancelButton);

    SaveAsSessionMenuItem->Visible = FEditing;
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::UpdateButtonVisibility(TButton * Button)
{
  TAction * Action = DebugNotNull(dynamic_cast<TAction *>(Button->Action));
  // when all action targets are hidden, action does not get updated,
  // so we need to do it manually
  Action->Update();
  // button visibility cannot be bound to action visibility,
  // so we do not bother setting action visibility, instead we manually
  // bind visibility to enabled state
  Button->Visible = Action->Enabled;
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::DataChange(TObject * /*Sender*/)
{
  UpdateControls();
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::FormShow(TObject * /*Sender*/)
{
  // this is called twice on startup, first with ControlState = [csRecreating]
  // we should probably filter this out, it would avoid need for explicit
  // LoadContents call below
  bool NeedInitialize = !FInitialized;
  if (NeedInitialize)
  {
    Init();
  }

  // among other this makes the expanded nodes look like expanded,
  // because the LoadState call in Execute would be too early,
  // and some stray call to collapsed event during showing process,
  // make the image be set to collapsed.
  // Also LoadState calls RestoreFormSize that has to be
  // called only after DoFormWindowProc(CM_SHOWINGCHANGED).
  // See also comment about MakeVisible in LoadState().
  LoadState();
  if (NeedInitialize)
  {
    // Need to load contents only after state (as that selects initial node).
    // Explicit call is needed, as we get here during csRecreating phase,
    // when SessionTreeChange is not triggered, see initial method comment
    LoadContents();
  }
  UpdateControls();
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SessionTreeChange(TObject * /*Sender*/,
  TTreeNode * /*Node*/)
{
  if (FIncrementalSearching <= 0)
  {
    // Make sure UpdateControls is called here, no matter what,
    // now it is always called from ResetSitesIncrementalSearch.
    // For the "else" scenario, UpdateControls is called later from SitesIncrementalSearch.
    ResetSitesIncrementalSearch();
  }

  if (FInitialized)
  {
    LoadContents();
  }
}
//---------------------------------------------------------------------------
TSessionData * __fastcall TLoginDialog::GetSessionData()
{
  if (SelectedSession == FNewSiteData)
  {
    SaveSession(FNewSiteData);
  }
  return SelectedSession;
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SessionTreeDblClick(TObject * /*Sender*/)
{
  TPoint P = SessionTree->ScreenToClient(Mouse->CursorPos);
  TTreeNode * Node = SessionTree->GetNodeAt(P.x, P.y);

  // This may be false, when collapsed folder was double-clicked,
  // it got expanded, view was shifted to accommodate folder contents,
  // so that cursor now points to a different node (site).
  // This has to be evaluated before EnsureNotEditing,
  // as that may pop-up modal box.
  if (Node == SessionTree->Selected)
  {
    // EnsureNotEditing must be before CanLogin, as CanLogin checks for FEditing
    if (EnsureNotEditing())
    {
      if (IsCloneToNewSiteDefault())
      {
        CloneToNewSite();
      }
      // this can hardle be false
      // (after editing and clone tests above)
      // (except for empty folders, but those do not pass a condition below)
      else if (CanLogin())
      {
        if (IsSessionNode(Node) || IsWorkspaceNode(Node))
        {
          Login();
        }
      }
    }
  }
}
//---------------------------------------------------------------------------
TSessionData * __fastcall TLoginDialog::GetSelectedSession()
{
  // Selected can be temporarily NULL, e.g. while deleting selected node
  TTreeNode * Node = SessionTree->Selected;
  if ((Node != NULL) &&
      (IsSiteNode(Node) || IsNewSiteNode(Node)))
  {
    return GetNodeSession(Node);
  }
  else
  {
    return NULL;
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SessionTreeKeyDown(TObject * /*Sender*/,
  WORD & Key, TShiftState /*Shift*/)
{
  if (!SessionTree->IsEditing())
  {
    if (Key == VK_DELETE)
    {
      DeleteSessionAction->Execute();
      Key = 0;
    }
    else if (Key == VK_F2)
    {
      RenameSessionAction->Execute();
      Key = 0;
    }
    else if (Key == VK_BACK)
    {
      Key = 0;
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SessionTreeKeyPress(TObject * /*Sender*/, System::WideChar & Key)
{
  if (!SessionTree->IsEditing())
  {
    // filter control sequences
    if (Key >= VK_SPACE)
    {
      if (FSitesIncrementalSearch.IsEmpty())
      {
        Configuration->Usage->Inc(L"SiteIncrementalSearches");
      }
      if (!SitesIncrementalSearch(FSitesIncrementalSearch + Key, false, false))
      {
        MessageBeep(MB_ICONHAND);
      }
      Key = 0;
    }
    else if (Key == VK_BACK)
    {
      if (!FSitesIncrementalSearch.IsEmpty())
      {
        if (FSitesIncrementalSearch.Length() == 1)
        {
          ResetSitesIncrementalSearch();
        }
        else
        {
          UnicodeString NewText =
            FSitesIncrementalSearch.SubString(1, FSitesIncrementalSearch.Length() - 1);
          SitesIncrementalSearch(NewText, false, false);
        }
        Key = 0;
      }
    }
    else if ((Key == VK_RETURN) && IsCloneToNewSiteDefault())
    {
      CloneToNewSite();
      Key = 0;
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::EditSession()
{
  HostNameEdit->SetFocus();
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::EditSessionActionExecute(TObject * /*Sender*/)
{
  if (DebugAlwaysTrue(SelectedSession != NULL))
  {
    FEditing = true;
    EditSession();
    // reload session, to make sure we load decrypted password
    LoadContents();
    UpdateControls();
  }
}
//---------------------------------------------------------------------------
TTreeNode * __fastcall TLoginDialog::FindSessionNode(TSessionData * SessionData, bool ByName)
{
  TTreeNode * Node = SessionTree->Items->GetFirstNode();
  while (
    (Node != NULL) &&
    ((!ByName && (Node->Data != SessionData)) ||
     (ByName && (!IsSiteNode(Node) || (GetNodeSession(Node)->Name != SessionData->Name)))))
  {
    Node = Node->GetNext();
  }
  return Node;
}
//---------------------------------------------------------------------------
TSessionData * __fastcall TLoginDialog::GetEditingSessionData()
{
  return FEditing ? DebugNotNull(SelectedSession) : NULL;
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SaveAsSession(bool ForceDialog)
{
  // Parse hostname before saving
  // (HostNameEditExit is not triggered when child dialog pops up when it is invoked by accelerator)
  // We should better handle this automaticaly when focus is moved to another dialog.
  ParseHostName();

  std::unique_ptr<TSessionData> SessionData(new TSessionData(L""));
  SaveSession(SessionData.get());

  TSessionData * EditingSessionData = GetEditingSessionData();

  // collect list of empty folders (these are not persistent and known to login dialog only)
  std::unique_ptr<TStrings> NewFolders(new TStringList());
  TTreeNode * Node = SessionTree->Items->GetFirstNode();
  while (Node != NULL)
  {
    if (IsFolderNode(Node) && !Node->HasChildren)
    {
      NewFolders->Add(SessionNodePath(Node));
    }
    Node = Node->GetNext();
  }

  TSessionData * NewSession =
    DoSaveSession(SessionData.get(), EditingSessionData, ForceDialog, NewFolders.get());
  if (NewSession != NULL)
  {
    TTreeNode * ParentNode = AddSessionPath(UnixExtractFilePath(NewSession->SessionName), false, false);
    CheckIsSessionFolder(ParentNode);

    TTreeNode * Node = FindSessionNode(NewSession, false);

    if (Node == NULL)
    {
      Node = AddSession(NewSession);
    }

    if ((SessionTree->Selected != Node) &&
        IsSiteNode(SessionTree->Selected))
    {
      CancelEditing();
    }
    else
    {
      FEditing = false;
    }

    SessionTree->Selected = Node;
    SessionTree->SetFocus();

    // this
    // - updates TransferProtocolView and EncryptionView
    // - clears the password box, if user has not opted to save password
    // - reloads fake password
    LoadContents();

    UpdateControls();

    ResetNewSiteData();
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SaveSessionActionExecute(TObject * /*Sender*/)
{
  bool NewSiteSelected = IsNewSiteNode(SessionTree->Selected);
  // for new site, the "save" command is actually "save as"
  SaveAsSession(NewSiteSelected);
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SaveAsSessionActionExecute(TObject * /*Sender*/)
{
  SaveAsSession(true);
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TLoginDialog::SessionNodePath(TTreeNode * Node)
{
  UnicodeString Path;
  if ((Node != NULL) && !IsNewSiteNode(Node))
  {
    Path = Node->Text;
    Node = Node->Parent;
    while (Node != NULL)
    {
      Path.Insert(UnixIncludeTrailingBackslash(Node->Text), 1);
      Node = Node->Parent;
    }
  }

  return Path;
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::DeleteSessionActionExecute(TObject * /*Sender*/)
{
  DebugAssert(SessionTree->Selected != NULL);

  TMessageParams Params;
  Params.ImageName = L"Delete file";
  TTreeNode * Node = SessionTree->Selected;
  if (IsSiteNode(Node))
  {
    TSessionData * Session = SelectedSession;
    UnicodeString Message = MainInstructions(FMTLOAD(CONFIRM_DELETE_SESSION, (Session->SessionName)));
    if (MessageDialog(Message,
          qtConfirmation, qaOK | qaCancel, HELP_DELETE_SESSION, &Params) == qaOK)
    {
      WinConfiguration->DeleteSessionFromJumpList(Session->SessionName);
      Session->Remove();
      DestroySession(Session);
      SessionTree->Selected->Delete();
    }
  }
  else if (IsFolderNode(Node) || IsWorkspaceNode(Node))
  {
    int Sessions = 0;
    TTreeNode * ANode = Node->GetNext();
    while ((ANode != NULL) && ANode->HasAsParent(Node))
    {
      if (IsSessionNode(ANode))
      {
        TSessionData * Session = GetNodeSession(ANode);
        if (Session->Special)
        {
          SessionTree->Selected = ANode;
          ANode->MakeVisible();
          throw Exception(FMTLOAD(LOGIN_DELETE_SPECIAL_SESSION, (Session->SessionName)));
        }
        Sessions++;
      }
      ANode = ANode->GetNext();
    }

    UnicodeString Path = SessionNodePath(Node);

    int Prompt;
    UnicodeString HelpKeyword;
    if (IsFolderNode(Node))
    {
      Prompt = LOGIN_DELETE_SESSION_FOLDER;
      HelpKeyword = HELP_DELETE_SESSION_FOLDER;
    }
    else
    {
      Prompt = LOGIN_DELETE_WORKSPACE;
      HelpKeyword = HELP_DELETE_WORKSPACE;
    }

    if ((Sessions == 0) ||
        (MessageDialog(MainInstructions(FMTLOAD(Prompt, (Path, Sessions))),
          qtConfirmation, qaOK | qaCancel, HelpKeyword, &Params) == qaOK))
    {
      if (IsWorkspaceNode(Node))
      {
        WinConfiguration->DeleteWorkspaceFromJumpList(Path);
      }

      Node = SessionTree->Selected;
      TTreeNode * ANode = Node->GetNext();
      while ((ANode != NULL) && ANode->HasAsParent(Node))
      {
        if (IsSessionNode(ANode))
        {
          TSessionData * Session = GetNodeSession(ANode);
          Session->Remove();
          DestroySession(Session);
        }
        ANode = ANode->GetNext();
      }

      SessionTree->Selected->Delete();
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::ReloadSessions(const UnicodeString & SelectSite)
{
  SaveState();
  if (!SelectSite.IsEmpty())
  {
    WinConfiguration->LastStoredSession = SelectSite;
  }
  LoadSessions();
  LoadState();
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::ImportSessionsActionExecute(TObject * /*Sender*/)
{
  std::unique_ptr<TList> Imported(new TList());
  if (DoImportSessionsDialog(Imported.get()))
  {
    UnicodeString SelectSite;
    if (DebugAlwaysTrue(Imported->Count > 0))
    {
      // Focus the first imported session.
      // We should also consider expanding all newly created folders
      SelectSite = static_cast<TSessionData *>(Imported->Items[0])->Name;
    }

    ReloadSessions(SelectSite);

    // Focus the tree with focused imported session(s).
    SessionTree->SetFocus();
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::CleanUpActionExecute(TObject * /*Sender*/)
{
  if (DoCleanupDialog(StoredSessions, Configuration))
  {
    SaveState();
    LoadSessions();
    LoadState();
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::AboutActionExecute(TObject * /*Sender*/)
{
  DoAboutDialog(Configuration);
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::ActionListUpdate(TBasicAction * BasicAction,
      bool &Handled)
{
  bool NewSiteSelected = IsNewSiteNode(SessionTree->Selected);
  bool SiteSelected = IsSiteNode(SessionTree->Selected);
  bool FolderOrWorkspaceSelected = IsFolderOrWorkspaceNode(SessionTree->Selected);

  TAction * Action = DebugNotNull(dynamic_cast<TAction *>(BasicAction));
  bool PrevEnabled = Action->Enabled;
  bool Editable = IsEditable();

  if ((Action == EditSessionAction) ||
      (Action == CloneToNewSiteAction))
  {
    Action->Enabled = SiteSelected && !FEditing;
  }
  else if (Action == EditCancelAction)
  {
    EditCancelAction->Enabled = FEditing;
  }
  else if (Action == DeleteSessionAction)
  {
    DeleteSessionAction->Enabled =
      ((SiteSelected && !SelectedSession->Special && !FEditing) ||
       FolderOrWorkspaceSelected);
  }
  else if (Action == RenameSessionAction)
  {
    RenameSessionAction->Enabled =
      ((SiteSelected && !SelectedSession->Special && !FEditing) ||
       FolderOrWorkspaceSelected);
  }
  else if (Action == DesktopIconAction)
  {
    DesktopIconAction->Enabled =
      (SiteSelected && !FEditing) ||
      (FolderOrWorkspaceSelected && HasNodeAnySession(SessionTree->Selected));
  }
  else if (Action == SendToHookAction)
  {
    SendToHookAction->Enabled = SiteSelected && !FEditing;
  }
  else if (Action == LoginAction)
  {
    LoginAction->Enabled = CanLogin();
  }
  else if (Action == PuttyAction)
  {
    Action->Enabled = (NewSiteSelected || SiteSelected) && CanLogin();
  }
  else if (Action == SaveSessionAction)
  {
    SaveSessionAction->Enabled = Editable;
  }
  else if (Action == SessionAdvancedAction)
  {
    SessionAdvancedAction->Enabled = Editable;
  }
  else if (Action == SaveAsSessionAction)
  {
    // Save as is needed for new site only when !SupportsSplitButton()
    SaveAsSessionAction->Enabled = Editable;
  }
  else if (Action == NewSessionFolderAction)
  {
    NewSessionFolderAction->Enabled = !FEditing;
  }
  else if (Action == PasteUrlAction)
  {
    UnicodeString ClipboardUrl;
    Action->Enabled =
      NonEmptyTextFromClipboard(ClipboardUrl) &&
      StoredSessions->IsUrl(ClipboardUrl);
  }
  else if (Action == GenerateUrlAction2)
  {
    TSessionData * Data = GetSessionData();
    // URL without hostname is pointless
    Action->Enabled = (Data != NULL) && !Data->HostName.IsEmpty() && !FEditing;
  }
  else if (Action == CopyParamRuleAction)
  {
    TSessionData * Data = GetSessionData();
    // without hostname it's pointless
    Action->Enabled = (Data != NULL) && !Data->HostName.IsEmpty();
  }
  else if (Action == SearchSiteNameStartOnlyAction)
  {
    Action->Checked = (FSiteSearch == ssSiteNameStartOnly);
  }
  else if (Action == SearchSiteNameAction)
  {
    Action->Checked = (FSiteSearch == ssSiteName);
  }
  else if (Action == SearchSiteAction)
  {
    Action->Checked = (FSiteSearch == ssSite);
  }
  Handled = true;

  // to update buttons visibility
  if (PrevEnabled != Action->Enabled)
  {
    UpdateControls();
  }

  Idle();
}
//---------------------------------------------------------------------------
bool __fastcall TLoginDialog::IsCloneToNewSiteDefault()
{
  return !FEditing && !FRenaming && IsSiteNode(SessionTree->Selected) && !FStoredSessions->CanLogin(GetSessionData());
}
//---------------------------------------------------------------------------
bool __fastcall TLoginDialog::CanLogin()
{
  TSessionData * Data = GetSessionData();
  return
    ((Data != NULL) && FStoredSessions->CanLogin(Data) && !FEditing) ||
    (IsFolderOrWorkspaceNode(SessionTree->Selected) && HasNodeAnySession(SessionTree->Selected, true));
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::Idle()
{
  if (SessionTree->IsEditing() != FRenaming)
  {
    FRenaming = SessionTree->IsEditing();
    UpdateControls();
  }
}
//---------------------------------------------------------------------------
bool __fastcall TLoginDialog::Execute(TList * DataList)
{
  FDataList = DataList;
  if (DataList->Count > 0)
  {
    TSessionData * SessionData = reinterpret_cast<TSessionData * >(DataList->Items[0]);
    TTreeNode * Node = FindSessionNode(SessionData, true);
    if (Node != NULL)
    {
      SessionTree->Selected = Node;
      ActiveControl = SessionTree;
    }
    else
    {
      FNewSiteData->CopyData(SessionData);
      FNewSiteData->Special = false;

      // This is actualy bit pointless, as we focus the last selected site anyway
      // in LoadState(). As of now, we hardly get any useful data
      // in ad-hoc DataList anyway, so it is not a big deal
      // (this was implemented for support taking session url from clipboard instead
      // of command-line, but without autoconnect, but this functionality was cancelled)
      if (!FNewSiteData->IsSame(StoredSessions->DefaultSettings, false))
      {
        // we want to start with new site page
        FForceNewSite = true;
      }

      LoadContents();
    }
  }
  else
  {
    Default();
  }
  // Optimization. List view is recreated while showing the form,
  // causing nodes repopulation and in a consequence a huge number of
  // nodes comparison
  SessionTree->SortType = Comctrls::stNone;
  FSortEnablePending = true;
  // Not calling LoadState here.
  // It's redundant and does not work anyway, see comment in the method.
  int AResult = ShowModal();
  // When CanLogin is false, the DefaultResult() will fail finding a default button.
  bool Result = CanLogin() && (AResult == DefaultResult());
  SaveState();
  if (Result)
  {
    SaveConfiguration();
    // DataList saved already from FormCloseQuery
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SaveDataList(TList * DataList)
{
  // Normally we would call this from Execute,
  // but at that point the windows is already hidden.
  // Cloning session data may pop up master password dialog:
  // - if it happens between closing and destroyiong login dialog
  //   the next window will appear in background for some reason
  // - and its actually even nicer when master password dialog pops up over
  //   the login dialog

  DataList->Clear();

  TTreeNode * Node = SessionTree->Selected;
  if (IsFolderOrWorkspaceNode(Node))
  {
    UnicodeString Name = SessionNodePath(Node);

    if (IsWorkspaceNode(Node))
    {
      WinConfiguration->AddWorkspaceToJumpList(Name);
    }

    StoredSessions->GetFolderOrWorkspace(Name, DataList);
  }
  else
  {
    DataList->Add(CloneSelectedSession());
  }
}
//---------------------------------------------------------------------------
TSessionData * __fastcall TLoginDialog::CloneSelectedSession()
{
  TTreeNode * Node = SessionTree->Selected;
  std::unique_ptr<TSessionData> Data2(new TSessionData(L""));
  if (IsSiteNode(Node))
  {
    Data2->Assign(GetNodeSession(Node));
  }
  else if (DebugAlwaysTrue(IsNewSiteNode(Node)))
  {
    TSessionData * Data = GetSessionData();
    Data2->Assign(Data);
    // we carry the name of the edited stored session around while on the dialog,
    // but we do not want it to leave the dialog, so that we can distinguish
    // stored and ad-hoc sessions
    Data2->Name = L"";
  }
  return Data2.release();
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SaveState()
{
  DebugAssert(WinConfiguration != NULL);

  WinConfiguration->BeginUpdate();
  try
  {
    std::unique_ptr<TStringList> OpenedStoredSessionFolders(CreateSortedStringList());
    for (int Index = 0; Index < SessionTree->Items->Count; Index++)
    {
      TTreeNode * Node = SessionTree->Items->Item[Index];
      if (IsFolderNode(Node))
      {
        if (Node->Expanded)
        {
          OpenedStoredSessionFolders->Add(SessionNodePath(Node));
        }
      }
    }

    WinConfiguration->OpenedStoredSessionFolders = OpenedStoredSessionFolders->CommaText;

    WinConfiguration->LastStoredSession = SessionNodePath(SessionTree->Selected);

    TLoginDialogConfiguration DialogConfiguration = CustomWinConfiguration->LoginDialog;
    DialogConfiguration.WindowSize = StoreFormSize(this);
    DialogConfiguration.SiteSearch = FSiteSearch;
    CustomWinConfiguration->LoginDialog = DialogConfiguration;
  }
  __finally
  {
    WinConfiguration->EndUpdate();
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::LoadOpenedStoredSessionFolders(
  TTreeNode * Node, TStrings * OpenedStoredSessionFolders)
{
  if (IsFolderNode(Node))
  {
    UnicodeString Path = SessionNodePath(Node);
    if (OpenedStoredSessionFolders->IndexOf(Path) >= 0)
    {
      Node->Expand(false);
      UpdateFolderNode(Node);
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::LoadState()
{
  // it does not make any sense to call this before
  // DoFormWindowProc(CM_SHOWINGCHANGED), we would end up on wrong monitor
  if (DebugAlwaysTrue(Visible))
  {
    RestoreFormSize(CustomWinConfiguration->LoginDialog.WindowSize, this);
  }

  FSiteSearch = CustomWinConfiguration->LoginDialog.SiteSearch;

  TStringList * OpenedStoredSessionFolders = CreateSortedStringList();
  try
  {
    OpenedStoredSessionFolders->CommaText = WinConfiguration->OpenedStoredSessionFolders;

    for (int Index = 0; Index < SessionTree->Items->Count; Index++)
    {
      LoadOpenedStoredSessionFolders(
        SessionTree->Items->Item[Index], OpenedStoredSessionFolders);
    }

    // tree view tries to make expanded node children all visible, what
    // may scroll the selected node (what should be the first one here),
    // out of the view
    if (SessionTree->Selected != NULL)
    {
      // see comment for LastStoredSession branch below
      DebugAssert(Visible);
      SessionTree->Selected->MakeVisible();
    }
  }
  __finally
  {
    delete OpenedStoredSessionFolders;
  }

  // calling TTreeNode::MakeVisible() when tree view is not visible yet,
  // sometimes scrolls view horizontally when not needed
  // (seems like it happens for sites that are at the same level
  // as site folders, e.g. for the very last root-level site, at long as
  // there are any folders)
  if (!FForceNewSite &&
      !WinConfiguration->LastStoredSession.IsEmpty() && DebugAlwaysTrue(Visible))
  {
    UnicodeString Path = WinConfiguration->LastStoredSession;

    UnicodeString ParentPath = UnixExtractFilePath(Path);
    TTreeNode * Node;
    if (ParentPath.IsEmpty())
    {
      Node = SessionTree->Items->GetFirstNode();
    }
    else
    {
      TTreeNode * Parent = AddSessionPath(ParentPath, false, false);
      Node = (Parent != NULL) ? Parent->getFirstChild() : NULL;
    }

    if (Node != NULL)
    {
      UnicodeString Name = UnixExtractFileName(Path);
      // actually we cannot distinguish folder and session here
      // (note that we allow folder and session with the same name),
      // this is pending for future improvements
      while ((Node != NULL) && !AnsiSameText(Node->Text, Name))
      {
        Node = Node->getNextSibling();
      }

      if (Node != NULL)
      {
        SessionTree->Selected = Node;
        SessionTree->Selected->MakeVisible();
      }
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SaveConfiguration()
{
  DebugAssert(CustomWinConfiguration);
  TTreeNode * Node = SessionTree->Selected;
  if (IsWorkspaceNode(Node))
  {
    WinConfiguration->LastWorkspace = SessionNodePath(Node);
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::PreferencesActionExecute(TObject * /*Sender*/)
{
  ShowPreferencesDialog(::pmDefault);
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::PreferencesLoggingActionExecute(TObject * /*Sender*/)
{
  ShowPreferencesDialog(pmLogging);
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::MasterPasswordRecrypt(TObject * /*Sender*/)
{
  FNewSiteData->RecryptPasswords();
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::ShowPreferencesDialog(TPreferencesMode PreferencesMode)
{
  DebugAssert(CustomWinConfiguration->OnMasterPasswordRecrypt == NULL);
  CustomWinConfiguration->OnMasterPasswordRecrypt = MasterPasswordRecrypt;
  try
  {
    DoPreferencesDialog(PreferencesMode);
  }
  __finally
  {
    DebugAssert(CustomWinConfiguration->OnMasterPasswordRecrypt == MasterPasswordRecrypt);
    CustomWinConfiguration->OnMasterPasswordRecrypt = NULL;
  }
  UpdateControls();
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::ResetNewSessionActionExecute(TObject * /*Sender*/)
{
  Default();
  EditSession();
  FNewSiteKeepName = false;
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::CMDialogKey(TWMKeyDown & Message)
{
  if (Message.CharCode == VK_TAB)
  {
    if (!FSitesIncrementalSearch.IsEmpty())
    {
      TShiftState Shift = KeyDataToShiftState(Message.KeyData);
      bool Reverse = Shift.Contains(ssShift);
      if (!SitesIncrementalSearch(FSitesIncrementalSearch, true, Reverse))
      {
        MessageBeep(MB_ICONHAND);
      }
      Message.Result = 1;
      return;
    }
  }
  else if (Message.CharCode == VK_ESCAPE)
  {
    if (!FSitesIncrementalSearch.IsEmpty())
    {
      ResetSitesIncrementalSearch();
      Message.Result = 1;
      return;
    }
  }
  TForm::Dispatch(&Message);
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::WMMoving(TMessage & Message)
{
  TForm::Dispatch(&Message);

  if (FLinkedForm != NULL)
  {
    RECT & Rect = *reinterpret_cast<RECT*>(Message.LParam);
    FLinkedForm->SetBounds(
      FLinkedForm->Left + (Rect.left - Left),
      FLinkedForm->Top + (Rect.top - Top),
      FLinkedForm->Width, FLinkedForm->Height);
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::Dispatch(void * Message)
{
  TMessage * M = reinterpret_cast<TMessage*>(Message);
  DebugAssert(M);
  if (M->Msg == CM_DIALOGKEY)
  {
    CMDialogKey(*((TWMKeyDown *)Message));
  }
  else if (M->Msg == WM_MANAGES_CAPTION)
  {
    // caption managed in TLoginDialog::Init()
    M->Result = 1;
  }
  else if (M->Msg == WM_WANTS_MOUSEWHEEL)
  {
    M->Result = 1;
  }
  else if (M->Msg == CM_ACTIVATE)
  {
    // Called from TCustomForm.ShowModal
    if (FSortEnablePending)
    {
      FSortEnablePending = false;
      SessionTree->SortType = Comctrls::stBoth;
    }
    TForm::Dispatch(Message);
  }
  else if (M->Msg == WM_SYSCOMMAND)
  {
    if (!HandleMinimizeSysCommand(*M))
    {
      TForm::Dispatch(Message);
    }
  }
  else if (M->Msg == WM_MOVING)
  {
    WMMoving(*M);
  }
  else
  {
    TForm::Dispatch(Message);
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SetDefaultSessionActionExecute(
      TObject * /*Sender*/)
{
  UnicodeString Message = MainInstructions(LoadStr(SET_DEFAULT_SESSION_SETTINGS));
  if (MessageDialog(Message, qtConfirmation,
        qaOK | qaCancel, HELP_SESSION_SAVE_DEFAULT) == qaOK)
  {
    std::unique_ptr<TSessionData> SessionData(new TSessionData(L""));
    SaveSession(SessionData.get());
    CustomWinConfiguration->AskForMasterPasswordIfNotSetAndNeededToPersistSessionData(SessionData.get());
    StoredSessions->DefaultSettings = SessionData.get();

    Default();
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::ToolsMenuButtonClick(TObject * /*Sender*/)
{
  MenuPopup(ToolsPopupMenu, ToolsMenuButton);
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::DesktopIconActionExecute(TObject * /*Sender*/)
{
  TTreeNode * Node = SessionTree->Selected;

  UnicodeString Message;
  UnicodeString Name;
  UnicodeString AdditionalParams = TProgramParams::FormatSwitch(DESKTOP_SWITCH);
  int IconIndex = 0;
  if (IsSiteNode(Node))
  {
    Name = GetNodeSession(Node)->Name;
    Message = FMTLOAD(CONFIRM_CREATE_SHORTCUT, (Name));
    AddToList(AdditionalParams, TProgramParams::FormatSwitch(UPLOAD_IF_ANY_SWITCH), L" ");
    IconIndex = SITE_ICON;
  }
  else if (IsFolderNode(Node))
  {
    Name = SessionNodePath(SessionTree->Selected);
    Message = FMTLOAD(CONFIRM_CREATE_SHORTCUT_FOLDER, (Name));
    IconIndex = SITE_FOLDER_ICON;
  }
  else if (IsWorkspaceNode(Node))
  {
    Name = SessionNodePath(SessionTree->Selected);
    Message = FMTLOAD(CONFIRM_CREATE_SHORTCUT_WORKSPACE, (Name));
    IconIndex = WORKSPACE_ICON;
  }
  else
  {
    DebugFail();
  }

  Message = MainInstructions(Message);
  if (MessageDialog(Message, qtConfirmation, qaYes | qaNo, HELP_CREATE_SHORTCUT) == qaYes)
  {
    TInstantOperationVisualizer Visualizer;
    CreateDesktopSessionShortCut(Name, L"", AdditionalParams, -1, IconIndex);
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SendToHookActionExecute(TObject * /*Sender*/)
{
  DebugAssert(IsSiteNode(SessionTree->Selected));
  DebugAssert(SelectedSession != NULL);
  UnicodeString Message = MainInstructions(FMTLOAD(CONFIRM_CREATE_SENDTO, (SelectedSession->Name)));
  if (MessageDialog(Message,
        qtConfirmation, qaYes | qaNo, HELP_CREATE_SENDTO) == qaYes)
  {
    TInstantOperationVisualizer Visualizer;
    UnicodeString AdditionalParams =
      TProgramParams::FormatSwitch(SEND_TO_HOOK_SWITCH) + L" " +
      TProgramParams::FormatSwitch(UPLOAD_SWITCH);
    CreateDesktopSessionShortCut(SelectedSession->Name,
      FMTLOAD(SESSION_SENDTO_HOOK_NAME2, (SelectedSession->LocalName, AppName)),
      AdditionalParams,
      CSIDL_SENDTO, SITE_ICON);
  }
}
//---------------------------------------------------------------------------
bool __fastcall TLoginDialog::HasNodeAnySession(TTreeNode * Node, bool NeedCanLogin)
{
  bool Result = false;
  TTreeNode * ANode = Node->GetNext();
  while (!Result && (ANode != NULL) && ANode->HasAsParent(Node))
  {
    Result =
      IsSessionNode(ANode) &&
      (!NeedCanLogin || FStoredSessions->CanLogin(GetNodeSession(ANode)));
    ANode = ANode->GetNext();
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SessionTreeCustomDrawItem(
  TCustomTreeView * Sender, TTreeNode * Node, TCustomDrawState State,
  bool & DefaultDraw)
{
  TFontStyles Styles = Sender->Canvas->Font->Style;
  if (IsSessionNode(Node) && GetNodeSession(Node)->Special)
  {
    Styles = Styles << fsBold << fsUnderline;
  }
  else
  {
    Styles = Styles >> fsBold >> fsUnderline;
  }

  if (State.Empty() && !Node->DropTarget)
  {
    if (IsFolderOrWorkspaceNode(Node))
    {
      if (!HasNodeAnySession(Node))
      {
        Sender->Canvas->Font->Color = clGrayText;
      }
    }
  }

  Sender->Canvas->Font->Style = Styles;
  DefaultDraw = true;
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::CheckForUpdatesActionExecute(TObject * /*Sender*/)
{
  CheckForUpdates(false);
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::HelpButtonClick(TObject * /*Sender*/)
{
  FormHelp(this);
}
//---------------------------------------------------------------------------
TModalResult __fastcall TLoginDialog::DefaultResult()
{
  return ::DefaultResult(this, LoginButton);
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::FormCloseQuery(TObject * /*Sender*/,
  bool & CanClose)
{
  // CanClose test is now probably redundant,
  // once we have a fallback to LoginButton in DefaultResult
  CanClose = EnsureNotEditing();
  // When CanLogin is false, the DefaultResult() will fail finding a default button
  if (CanClose && CanLogin() && (ModalResult == DefaultResult()))
  {
    SaveDataList(FDataList);
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SessionTreeEditing(TObject * /*Sender*/,
  TTreeNode * Node, bool & AllowEdit)
{
  DebugAssert(!FRenaming);
  AllowEdit =
    IsFolderOrWorkspaceNode(Node) ||
    (DebugAlwaysTrue(IsSiteNode(Node)) && !GetNodeSession(Node)->Special);
  FRenaming = AllowEdit;
  UpdateControls();
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::RenameSessionActionExecute(TObject * /*Sender*/)
{
  if (SessionTree->Selected != NULL)
  {
    // would be more appropriate in SessionTreeEditing, but it does not work there
    ResetSitesIncrementalSearch();
    SessionTree->Selected->EditText();
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::CheckDuplicateFolder(TTreeNode * Parent,
  UnicodeString Text, TTreeNode * Node)
{
  TTreeNode * ANode =
    ((Parent == NULL) ? SessionTree->Items->GetFirstNode() :
     Parent->getFirstChild());
  // note that we allow folder with the same name as existing session
  // on the same level (see also AddSession)
  while ((ANode != NULL) &&
    ((ANode == Node) || IsSessionNode(ANode) || !AnsiSameText(ANode->Text, Text)))
  {
    ANode = ANode->getNextSibling();
  }

  if (ANode != NULL)
  {
    throw Exception(MainInstructions(FMTLOAD(LOGIN_DUPLICATE_SESSION_FOLDER_WORKSPACE, (Text))));
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::CheckIsSessionFolder(TTreeNode * Node)
{
  if ((Node != NULL) && (Node->Parent != NULL))
  {
    CheckIsSessionFolder(Node->Parent);
  }

  if (IsWorkspaceNode(Node))
  {
    throw Exception(FMTLOAD(WORKSPACE_NOT_FOLDER, (SessionNodePath(Node))));
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SessionTreeEdited(TObject * /*Sender*/,
  TTreeNode * Node, UnicodeString & S)
{
  if ((Node->Text != S) && !S.IsEmpty())
  {
    TSessionData * Session = SelectedSession;

    TSessionData::ValidateName(S);
    if (Session != NULL)
    {
      UnicodeString Path = UnixExtractFilePath(Session->Name) + S;

      SessionNameValidate(Path, Session->Name);

      // remove from storage
      Session->Remove();

      TSessionData * NewSession = StoredSessions->NewSession(Path, Session);
      // modified only, explicit
      StoredSessions->Save(false, true);
      // the session may be the same, if only letter case has changed
      if (Session != NewSession)
      {
        // if we overwrite existing session, remove the original item
        // (we must not delete the node we are editing)
        TTreeNode * ANode =
          ((Node->Parent == NULL) ? SessionTree->Items->GetFirstNode() :
           Node->Parent->getFirstChild());
        while ((ANode != NULL) && (ANode->Data != NewSession))
        {
          ANode = ANode->getNextSibling();
        }

        if (ANode != NULL)
        {
          ANode->Delete();
        }

        Node->Data = NewSession;

        DestroySession(Session);
      }
    }
    else
    {
      CheckDuplicateFolder(Node->Parent, S, Node);

      UnicodeString ParentPath = UnixIncludeTrailingBackslash(SessionNodePath(Node->Parent));
      UnicodeString OldRoot = ParentPath + Node->Text;
      UnicodeString NewRoot = ParentPath + S;

      bool AnySession = false;

      TSortType PrevSortType = SessionTree->SortType;
      // temporarily disable automatic sorting, so that nodes are kept in order
      // while we traverse them. otherwise it may happen that we omit some.
      SessionTree->SortType = Comctrls::stNone;
      try
      {
        TTreeNode * ANode = Node->GetNext();
        while ((ANode != NULL) && ANode->HasAsParent(Node))
        {
          if (IsSessionNode(ANode))
          {
            AnySession = true;
            TSessionData * Session = GetNodeSession(ANode);

            // remove from storage
            Session->Remove();

            UnicodeString Path = Session->Name;
            DebugAssert(Path.SubString(1, OldRoot.Length()) == OldRoot);
            Path.Delete(1, OldRoot.Length());
            Path.Insert(NewRoot, 1);

            TSessionData * NewSession = StoredSessions->NewSession(Path, Session);

            // the session may be the same, if only letter case has changed
            if (NewSession != Session)
            {
              ANode->Data = NewSession;
              DestroySession(Session);
            }
          }

          ANode = ANode->GetNext();
        }
      }
      __finally
      {
        SessionTree->SortType = PrevSortType;
      }

      if (AnySession)
      {
        // modified only, explicit
        StoredSessions->Save(false, true);
      }
    }
  }
}
//---------------------------------------------------------------------------
int __fastcall TLoginDialog::FSProtocolToIndex(TFSProtocol FSProtocol,
  bool & AllowScpFallback)
{
  if (FSProtocol == fsSFTP)
  {
    AllowScpFallback = true;
    bool Dummy;
    return FSProtocolToIndex(fsSFTPonly, Dummy);
  }
  else
  {
    AllowScpFallback = false;
    for (int Index = 0; Index < TransferProtocolCombo->Items->Count; Index++)
    {
      if (FSOrder[Index] == FSProtocol)
      {
        return Index;
      }
    }
    // SFTP is always present
    return FSProtocolToIndex(fsSFTP, AllowScpFallback);
  }
}
//---------------------------------------------------------------------------
TFSProtocol __fastcall TLoginDialog::IndexToFSProtocol(int Index, bool AllowScpFallback)
{
  bool InBounds = (Index >= 0) && (Index < static_cast<int>(LENOF(FSOrder)));
  // can be temporary "unselected" while new language is being loaded
  DebugAssert(InBounds || (Index == -1));
  TFSProtocol Result = fsSFTP;
  if (InBounds)
  {
    Result = FSOrder[Index];
    if ((Result == fsSFTPonly) && AllowScpFallback)
    {
      Result = fsSFTP;
    }
  }
  return Result;
}
//---------------------------------------------------------------------------
int __fastcall TLoginDialog::FtpsToIndex(TFtps Ftps)
{
  switch (Ftps)
  {
    default:
      DebugFail();
    case ftpsNone:
      return 0;

    case ftpsImplicit:
      return 1;

    case ftpsExplicitTls:
    case ftpsExplicitSsl:
      return 2;
  }
}
//---------------------------------------------------------------------------
TFtps __fastcall TLoginDialog::GetFtps()
{
  int Index = ((GetFSProtocol(false) == fsWebDAV) ? WebDavsCombo->ItemIndex : FtpsCombo->ItemIndex);
  TFtps Ftps;
  switch (Index)
  {
    default:
      DebugFail();
    case 0:
      Ftps = ftpsNone;
      break;

    case 1:
      Ftps = ftpsImplicit;
      break;

    case 2:
      Ftps = ftpsExplicitTls;
      break;
  }
  return Ftps;
}
//---------------------------------------------------------------------------
TFSProtocol __fastcall TLoginDialog::GetFSProtocol(bool RequireScpFallbackDistinction)
{
  bool AllowScpFallback = false;
  if (RequireScpFallbackDistinction && DebugAlwaysTrue(FSessionData != NULL))
  {
    FSProtocolToIndex(FSessionData->FSProtocol, AllowScpFallback);
  }
  return IndexToFSProtocol(TransferProtocolCombo->ItemIndex, AllowScpFallback);
}
//---------------------------------------------------------------------------
int __fastcall TLoginDialog::DefaultPort()
{
  return ::DefaultPort(GetFSProtocol(false), GetFtps());
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::TransferProtocolComboChange(TObject * Sender)
{
  int ADefaultPort = DefaultPort();
  if (!NoUpdate && FUpdatePortWithProtocol)
  {
    NoUpdate++;
    try
    {
      if (PortNumberEdit->AsInteger == FDefaultPort)
      {
        PortNumberEdit->AsInteger = ADefaultPort;
      }
    }
    __finally
    {
      NoUpdate--;
    }
  }
  FDefaultPort = ADefaultPort;
  DataChange(Sender);
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::NavigationTreeCollapsing(
  TObject * /*Sender*/, TTreeNode * /*Node*/, bool & AllowCollapse)
{
  AllowCollapse = false;
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SessionTreeExpandedCollapsed(TObject * /*Sender*/,
  TTreeNode * Node)
{
  UpdateFolderNode(Node);
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SessionTreeCompare(TObject * /*Sender*/,
  TTreeNode * Node1, TTreeNode * Node2, int /*Data*/, int & Compare)
{
  bool Node1IsNewSite = IsNewSiteNode(Node1);
  bool Node2IsNewSite = IsNewSiteNode(Node2);
  bool Node1IsWorkspace = IsWorkspaceNode(Node1);
  bool Node2IsWorkspace = IsWorkspaceNode(Node2);
  bool Node1IsFolder = IsFolderNode(Node1);
  bool Node2IsFolder = IsFolderNode(Node2);

  if (Node1IsNewSite && !Node2IsNewSite)
  {
    Compare = -1;
  }
  else if (!Node1IsNewSite && Node2IsNewSite)
  {
    Compare = 1;
  }
  else if (Node1IsWorkspace && !Node2IsWorkspace)
  {
    Compare = -1;
  }
  else if (!Node1IsWorkspace && Node2IsWorkspace)
  {
    Compare = 1;
  }
  else if (Node1IsFolder && !Node2IsFolder)
  {
    Compare = -1;
  }
  else if (!Node1IsFolder && Node2IsFolder)
  {
    Compare = 1;
  }
  else if (Node1IsWorkspace || Node1IsFolder)
  {
    Compare = CompareLogicalText(Node1->Text, Node2->Text);
  }
  else
  {
    Compare = NamedObjectSortProc(Node1->Data, Node2->Data);
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::NewSessionFolderInputDialogInitialize(
  TObject * /*Sender*/, TInputDialogData * Data)
{
  TCustomEdit * Edit = Data->Edit;
  int P = Edit->Text.LastDelimiter(L"/");
  if (P > 0)
  {
    Edit->SetFocus();
    Edit->SelStart = P;
    Edit->SelLength = Edit->Text.Length() - P;
  }
}
//---------------------------------------------------------------------------
TTreeNode * __fastcall TLoginDialog::SessionFolderNode(TTreeNode * Node)
{
  TTreeNode * Parent;
  if (IsSessionNode(Node))
  {
    Parent = Node->Parent;
  }
  else if (IsFolderNode(Node))
  {
    Parent = Node;
  }
  else if (IsWorkspaceNode(Node))
  {
    Parent = NULL;
  }
  else
  {
    DebugAssert(Node == NULL);
    Parent = NULL;
  }
  return Parent;
}
//---------------------------------------------------------------------------
TTreeNode * __fastcall TLoginDialog::CurrentSessionFolderNode()
{
  return SessionFolderNode(SessionTree->Selected);
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::NewSessionFolderActionExecute(
  TObject * /*Sender*/)
{
  UnicodeString Name =
    UnixIncludeTrailingBackslash(SessionNodePath(CurrentSessionFolderNode())) +
    LoadStr(NEW_FOLDER);
  if (InputDialog(LoadStr(LOGIN_NEW_SESSION_FOLDER_CAPTION),
        LoadStr(LOGIN_NEW_SESSION_FOLDER_PROMPT), Name, HELP_NEW_SESSION_FOLDER,
        NULL, true, NewSessionFolderInputDialogInitialize))
  {
    Name = UnixExcludeTrailingBackslash(Name);
    if (!Name.IsEmpty())
    {
      TTreeNode * Parent = AddSessionPath(UnixExtractFilePath(Name), true, false);
      // this does not prevent creation of subfolder under workspace,
      // if user creates more levels at once (but it does not show up anyway)
      CheckIsSessionFolder(Parent);
      CheckDuplicateFolder(Parent, UnixExtractFileName(Name), NULL);

      TTreeNode * Node = AddSessionPath(Name, true, false);
      SessionTree->Selected = Node;
      Node->MakeVisible();
    }
  }
}
//---------------------------------------------------------------------------
TTreeNode * __fastcall TLoginDialog::NormalizeDropTarget(TTreeNode * DropTarget)
{
  return IsWorkspaceNode(DropTarget) ? DropTarget : SessionFolderNode(DropTarget);
}
//---------------------------------------------------------------------------
bool __fastcall TLoginDialog::SessionAllowDrop(TTreeNode * DropTarget)
{
  DropTarget = NormalizeDropTarget(DropTarget);
  return
    (SessionTree->Selected != NULL) &&
    (SessionTree->Selected->Parent != DropTarget) &&
    ((DropTarget == NULL) || IsFolderNode(DropTarget));
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SessionTreeProc(TMessage & AMessage)
{
  if (AMessage.Msg == CM_DRAG)
  {
    TCMDrag & Message = reinterpret_cast<TCMDrag &>(AMessage);
    // reimplement dmDragMove to avoid TCustomTreeView.DoDragOver,
    // which resets DropTarget to pointed-to node
    // (note that this disables OnDragOver event handler)
    if ((Message.DragMessage == dmDragMove) ||
        (Message.DragMessage == dmDragEnter) ||
        (Message.DragMessage == dmDragLeave))
    {
      if (Message.DragMessage != dmDragMove)
      {
        // must call it at least for dmDragLeave, because it does some cleanup,
        // but we need to override result below, as it defaults to "not accepted"
        FOldSessionTreeProc(AMessage);
      }

      TDragControlObject * DragObject = dynamic_cast<TDragControlObject *>(Message.DragRec->Source);
      if ((DragObject != NULL) && (DragObject->Control == SessionTree))
      {
        TPoint P = SessionTree->ScreenToClient(Message.DragRec->Pos);
        TTreeNode * Node = SessionTree->GetNodeAt(P.x, P.y);
        if (!SessionAllowDrop(Node))
        {
          DropTarget = NULL;
          Message.Result = 0;
        }
        else
        {
          Message.Result = 1;
        }

        if (Message.DragMessage == dmDragMove)
        {
          SessionTree->DropTarget = NormalizeDropTarget(Node);
        }
        FScrollOnDragOver->DragOver(P);
      }
      else
      {
        Message.Result = 0;
      }
    }
    else
    {
      FOldSessionTreeProc(AMessage);
    }
  }
  else
  {
    FOldSessionTreeProc(AMessage);
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SessionTreeStartDrag(TObject * /*Sender*/,
  TDragObject *& /*DragObject*/)
{
  DebugAssert(SessionTree->Selected != NULL);
  // neither session folders/workspaces, nor special sessions can be dragged
  if ((SessionTree->Selected == NULL) ||
      IsFolderOrWorkspaceNode(SessionTree->Selected) ||
      IsNewSiteNode(SessionTree->Selected) ||
      (IsSiteNode(SessionTree->Selected) && SelectedSession->Special))
  {
    Abort();
  }

  FScrollOnDragOver->StartDrag();
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SessionTreeDragDrop(TObject * Sender,
  TObject * Source, int /*X*/, int /*Y*/)
{
  TTreeNode * DropTarget = SessionTree->DropTarget;
  if (DebugAlwaysTrue((Sender == Source) && SessionAllowDrop(DropTarget)) &&
      // calling EnsureNotEditing only on drop, not on drag start,
      // to avoid getting popup during unintended micro-dragging
      EnsureNotEditing())
  {
    TSessionData * Session = SelectedSession;
    UnicodeString Path =
      UnixIncludeTrailingBackslash(SessionNodePath(DropTarget)) +
      UnixExtractFileName(Session->SessionName);

    SessionNameValidate(Path, Session->SessionName);

    // remove from storage
    Session->Remove();

    TSessionData * NewSession = StoredSessions->NewSession(Path, Session);
    // modified only, explicit
    StoredSessions->Save(false, true);
    // this should aways be the case
    if (DebugAlwaysTrue(Session != NewSession))
    {
      TTreeNode * Node = SessionTree->Selected;

      // look for overwritten node (if any)
      TTreeNode * ANode = SessionTree->Items->GetFirstNode();
      while (ANode != NULL)
      {
        if (ANode->Data == NewSession)
        {
          ANode->Delete();
          break;
        }
        ANode = ANode->GetNext();
      }

      Node->MoveTo(DropTarget, naAddChild);
      Node->Data = NewSession;
      // try to make both visible
      if (DropTarget != NULL)
      {
        DropTarget->MakeVisible();
      }
      Node->MakeVisible();

      DestroySession(Session);
    }
  }
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TLoginDialog::GetFolderOrWorkspaceContents(
  TTreeNode * Node, const UnicodeString & Indent, const UnicodeString & CommonRoot)
{
  UnicodeString Contents;

  UnicodeString Path = SessionNodePath(Node);
  std::unique_ptr<TStrings> Names(FStoredSessions->GetFolderOrWorkspaceList(Path));
  for (int Index = 0; Index < Names->Count; Index++)
  {
    UnicodeString Name = Names->Strings[Index];
    if (StartsStr(CommonRoot, Name))
    {
      Name.Delete(1, CommonRoot.Length());
    }
    Contents += Indent + Name + L"\n";
  }

  return Contents;
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SessionTreeMouseMove(TObject * /*Sender*/,
  TShiftState /*Shift*/, int X, int Y)
{
  TTreeNode * Node = SessionTree->GetNodeAt(X, Y);
  THitTests HitTest = SessionTree->GetHitTestInfoAt(X, Y);

  if (Node != FHintNode)
  {
    Application->CancelHint();

    UnicodeString Hint;
    if (HitTest.Contains(htOnItem) || HitTest.Contains(htOnIcon) ||
        HitTest.Contains(htOnLabel) || HitTest.Contains(htOnStateIcon))
    {
      FHintNode = Node;
      if (IsSiteNode(Node))
      {
        Hint = GetNodeSession(Node)->InfoTip;
      }
      else if (IsWorkspaceNode(Node))
      {
        UnicodeString Path = SessionNodePath(Node);
        Hint =
          FMTLOAD(WORKSPACE_INFO_TIP, (Path)) + L"\n" +
          // trim the trailing new line
          TrimRight(GetFolderOrWorkspaceContents(Node, L"  ", UnicodeString()));
      }
      else
      {
        Hint = L"";
      }
    }
    else
    {
      FHintNode = NULL;
      Hint = L"";
    }

    SessionTree->Hint = Hint;
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SessionTreeEndDrag(TObject * /*Sender*/,
  TObject * /*Target*/, int /*X*/, int /*Y*/)
{
  FScrollOnDragOver->EndDrag();
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::AnonymousLoginCheckClick(TObject * /*Sender*/)
{
  if (AnonymousLoginCheck->Checked)
  {
    UserNameEdit->Text = AnonymousUserName;
    PasswordEdit->Text = AnonymousPassword;
  }
  UpdateControls();
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SaveButtonDropDownClick(TObject * /*Sender*/)
{
  MenuPopup(SaveDropDownMenu, SaveButton);
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SessionTreeExpanding(TObject * /*Sender*/,
  TTreeNode * Node, bool & AllowExpansion)
{
  // to prevent workspace expansion
  AllowExpansion = IsFolderNode(Node);
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::ExecuteTool(const UnicodeString & Name)
{
  UnicodeString Path;
  if (!FindTool(Name, Path) ||
      !ExecuteShell(Path, L""))
  {
    throw Exception(FMTLOAD(EXECUTE_APP_ERROR, (Name)));
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::RunPageantActionExecute(TObject * /*Sender*/)
{
  ExecuteTool(PageantTool);
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::RunPuttygenActionExecute(TObject * /*Sender*/)
{
  ExecuteTool(PuttygenTool);
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::PortNumberEditChange(TObject * Sender)
{
  if (!NoUpdate)
  {
    bool WellKnownPort = false;
    TFSProtocol FSProtocol;
    TFtps Ftps = ftpsNone;

    int PortNumber = PortNumberEdit->AsInteger;
    if (PortNumber == SshPortNumber)
    {
      FSProtocol = fsSFTP;
      WellKnownPort = true;
    }
    else if (PortNumber == FtpPortNumber)
    {
      FSProtocol = fsFTP;
      WellKnownPort = true;
    }
    else if (PortNumber == FtpsImplicitPortNumber)
    {
      FSProtocol = fsFTP;
      Ftps = ftpsImplicit;
      WellKnownPort = true;
    }
    else if (PortNumber == HTTPPortNumber)
    {
      FSProtocol = fsWebDAV;
      WellKnownPort = true;
    }
    else if (PortNumber == HTTPSPortNumber)
    {
      FSProtocol = fsWebDAV;
      Ftps = ftpsImplicit;
      WellKnownPort = true;
    }

    if (WellKnownPort)
    {
      bool AllowScpFallback;
      int ProtocolIndex = FSProtocolToIndex(FSProtocol, AllowScpFallback);
      if ((TransferProtocolCombo->ItemIndex == ProtocolIndex) &&
          (GetFtps() == Ftps))
      {
        FUpdatePortWithProtocol = true;
      }
      else
      {
        FUpdatePortWithProtocol = false;

        NoUpdate++;
        try
        {
          TransferProtocolCombo->ItemIndex = ProtocolIndex;

          int FtpsIndex = FtpsToIndex(Ftps);
          FtpsCombo->ItemIndex = FtpsIndex;
          WebDavsCombo->ItemIndex = FtpsIndex;
        }
        __finally
        {
          NoUpdate--;
        }
      }
    }
  }

  DataChange(Sender);
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TLoginDialog::ImportExportIniFilePath()
{
  UnicodeString PersonalDirectory = GetPersonalFolder();
  UnicodeString FileName = IncludeTrailingBackslash(PersonalDirectory) +
    ExtractFileName(ExpandEnvironmentVariables(Configuration->IniFileStorageName));
  return FileName;
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::ExportActionExecute(TObject * /*Sender*/)
{
  UnicodeString FileName = ImportExportIniFilePath();
  if (SaveDialog(LoadStr(EXPORT_CONF_TITLE), LoadStr(EXPORT_CONF_FILTER), L"ini", FileName))
  {
    Configuration->Export(FileName);
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::ImportActionExecute(TObject * /*Sender*/)
{
  if (MessageDialog(MainInstructions(LoadStr(IMPORT_CONFIGURATION)),
        qtWarning, qaOK | qaCancel, HELP_IMPORT_CONFIGURATION) == qaOK)
  {
    std::unique_ptr<TOpenDialog> OpenDialog(new TOpenDialog(Application));
    OpenDialog->Title = LoadStr(IMPORT_CONF_TITLE);
    OpenDialog->Filter = LoadStr(EXPORT_CONF_FILTER);
    OpenDialog->DefaultExt = L"ini";
    OpenDialog->FileName = ImportExportIniFilePath();

    if (OpenDialog->Execute())
    {
      // before the session list gets destroyed
      SessionTree->Items->Clear();
      Configuration->Import(OpenDialog->FileName);
      ReloadSessions(L"");

      if (SessionTree->Items->Count > 0)
      {
        SessionTree->Items->GetFirstNode()->MakeVisible();
      }
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::ResetSitesIncrementalSearch()
{
  FSitesIncrementalSearch = L"";
  // this is to prevent active tree node being set back to Sites tab
  // (from UpdateNavigationTree) when we are called from SessionTreeExit,
  // while tab is changing
  if (NoUpdate == 0)
  {
    UpdateControls();
  }
}
//---------------------------------------------------------------------------
bool __fastcall TLoginDialog::SitesIncrementalSearch(const UnicodeString & Text,
  bool SkipCurrent, bool Reverse)
{
  TTreeNode * Node = SearchSite(Text, false, SkipCurrent, Reverse);
  if (Node == NULL)
  {
    Node = SearchSite(Text, true, SkipCurrent, Reverse);
    if (Node != NULL)
    {
      TTreeNode * Parent = Node->Parent;
      while (Parent != NULL)
      {
        Parent->Expand(false);
        Parent = Parent->Parent;
      }
    }
  }

  bool Result = (Node != NULL);
  if (Result)
  {
    {
      TAutoNestingCounter Guard(FIncrementalSearching);
      SessionTree->Selected = Node;
    }
    FSitesIncrementalSearch = Text;

    // Tab always searches even in collapsed nodes
    TTreeNode * NextNode = SearchSite(Text, true, true, Reverse);
    FSitesIncrementalSearchHaveNext =
      (NextNode != NULL) && (NextNode != Node);

    UpdateControls();

    // make visible only after search panel is shown, what may obscure the node
    Node->MakeVisible();
  }
  return Result;
}
//---------------------------------------------------------------------------
TTreeNode * __fastcall TLoginDialog::GetNextNode(TTreeNode * Node, bool Reverse)
{
  if (Reverse)
  {
    Node = Node->GetPrev();
    if (Node == NULL)
    {
      // GetLastNode
      // http://stackoverflow.com/questions/6257348/how-should-i-implement-getlastnode-for-ttreenodes
      Node = SessionTree->Items->GetFirstNode();
      TTreeNode * Node2 = Node;
      if (Node2 != NULL)
      {
        do
        {
          Node2 = Node;
          if (Node != NULL)
          {
            Node = Node2->getNextSibling();
          }
          if (Node == NULL)
          {
            Node = Node2->getFirstChild();
          }
        }
        while (Node != NULL);
      }

      Node = Node2;
    }
  }
  else
  {
    Node = Node->GetNext();
    if (Node == NULL)
    {
      Node = SessionTree->Items->GetFirstNode();
    }
  }

  return Node;
}
//---------------------------------------------------------------------------
static bool __fastcall ContainsTextSemiCaseSensitive(
  const UnicodeString & Text, const UnicodeString & SubText)
{
  bool Result;
  if (AnsiLowerCase(SubText) == SubText)
  {
    Result = ContainsText(Text, SubText);
  }
  else
  {
    Result = ContainsStr(Text, SubText);
  }
  return Result;
}
//---------------------------------------------------------------------------
TTreeNode * __fastcall TLoginDialog::SearchSite(const UnicodeString & Text,
  bool AllowExpanding, bool SkipCurrent, bool Reverse)
{
  TTreeNode * CurrentNode =
    (SessionTree->Selected != NULL) ? SessionTree->Selected : SessionTree->Items->GetFirstNode();
  if (CurrentNode == NULL)
  {
    return NULL;
  }
  else
  {
    TTreeNode * Node = CurrentNode;
    if (SkipCurrent)
    {
      Node = GetNextNode(Node, Reverse);
      if (Node == NULL)
      {
        return NULL;
      }
    }

    while (true)
    {
      bool Eligible = true;
      TTreeNode * Parent = Node->Parent;
      while (Eligible && (Parent != NULL))
      {
        Eligible =
          IsFolderNode(Parent) &&
          (Parent->Expanded || AllowExpanding);
        Parent = Parent->Parent;
      }
      if (Eligible)
      {
        bool Matches = false;

        switch (FSiteSearch)
        {
          case ssSiteNameStartOnly:
            Matches = ContainsTextSemiCaseSensitive(Node->Text.SubString(1, Text.Length()), Text);
            break;
          case ssSiteName:
            Matches = ContainsTextSemiCaseSensitive(Node->Text, Text);
            break;
          case ssSite:
            Matches = ContainsTextSemiCaseSensitive(Node->Text, Text);
            if (!Matches && IsSiteNode(Node))
            {
              TSessionData * Data = GetNodeSession(Node);
              Matches =
                ContainsTextSemiCaseSensitive(Data->HostName, Text) ||
                ContainsTextSemiCaseSensitive(Data->UserName, Text) ||
                ContainsTextSemiCaseSensitive(Data->Note, Text);
            }
            break;
        }

        if (Matches)
        {
          return Node;
        }
      }

      Node = GetNextNode(Node, Reverse);

      if (Node == CurrentNode)
      {
        return NULL;
      }
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SessionTreeExit(TObject * /*Sender*/)
{
  ResetSitesIncrementalSearch();
}
//---------------------------------------------------------------------------
bool __fastcall TLoginDialog::EnsureNotEditing()
{
  bool Result = !FEditing;
  if (!Result)
  {
    UnicodeString Message = MainInstructions(LoadStr(LOGIN_SAVE_EDITING));
    unsigned int Answer = MessageDialog(Message, qtConfirmation, qaYes |qaNo | qaCancel);
    switch (Answer)
    {
      case qaYes:
        SaveAsSession(false);
        Result = true;
        break;

      case qaNo:
        CancelEditing();
        // Make sure OK button gets enabled
        UpdateControls();
        Result = true;
        break;

      default:
        // noop;
        break;
    }
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SessionTreeChanging(TObject * /*Sender*/,
  TTreeNode * /*Node*/, bool & AllowChange)
{
  if (!EnsureNotEditing())
  {
    AllowChange = false;
  }
  else
  {
    PersistNewSiteIfNeeded();
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::PersistNewSiteIfNeeded()
{
  // Visible: Never try to save data if we did not load any yet,
  // otherwise we get "HostNameEdit" and such
  if (Visible && IsNewSiteNode(SessionTree->Selected))
  {
    SaveSession(FNewSiteData);
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SessionAdvancedActionExecute(TObject * /*Sender*/)
{
  // If we ever allow showing advanced settings, while read-only,
  // we must make sure that FSessionData actually holds the advanced settings,
  // what it currently does not, in order to avoid master password prompt,
  // while cloning the session data in LoadSession.
  // To implement this, we may delegate the cloning to TWinConfiguration and
  // make use of FDontDecryptPasswords
  if (DebugAlwaysTrue(FSessionData != NULL))
  {
    // parse hostname (it may change protocol particularly) before opening advanced settings
    // (HostNameEditExit is not triggered when child dialog pops up when it is invoked by accelerator)
    // We should better handle this automaticaly when focus is moved to another dialog.
    ParseHostName();

    SaveSession(FSessionData);
    DoSiteAdvancedDialog(FSessionData);
    // Needed only for Note.
    // The only other property visible on Login dialog that Advanced site dialog
    // can change is protocol (between fsSFTP and fsSFTPonly),
    // difference of the two not being visible on Login dialog anyway.
    LoadSession(FSessionData);
    if (DebugAlwaysTrue(SessionTree->Selected != NULL) &&
        IsSiteNode(SessionTree->Selected))
    {
      SetNodeImage(SessionTree->Selected, GetSessionImageIndex(FSessionData));
    }
  }
}
//---------------------------------------------------------------------------
TPopupMenu * __fastcall TLoginDialog::GetSelectedNodePopupMenu()
{
  TPopupMenu * PopupMenu = NULL;

  TTreeNode * Selected = SessionTree->Selected;
  if (IsNewSiteNode(Selected))
  {
    PopupMenu = ManageNewSitePopupMenu;
  }
  else if (IsSiteNode(Selected))
  {
    PopupMenu = ManageSitePopupMenu;
  }
  else if (IsFolderNode(Selected))
  {
    PopupMenu = ManageFolderPopupMenu;
  }
  else if (IsWorkspaceNode(Selected))
  {
    PopupMenu = ManageWorkspacePopupMenu;
  }

  return DebugNotNull(PopupMenu);
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::ManageButtonClick(TObject * /*Sender*/)
{
  MenuPopup(GetSelectedNodePopupMenu(), ManageButton);
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SessionTreeMouseDown(TObject * /*Sender*/,
  TMouseButton Button, TShiftState /*Shift*/, int X, int Y)
{
  TTreeNode * Node = SessionTree->GetNodeAt(X, Y);
  if ((Button == mbRight) && (Node != NULL))
  {
    SessionTree->Selected = Node;
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SessionTreeContextPopup(TObject * /*Sender*/,
  TPoint & MousePos, bool & Handled)
{
  ResetSitesIncrementalSearch();
  TTreeNode * Node = SessionTree->GetNodeAt(MousePos.X, MousePos.Y);
  // This is mostly to prevent context menu from poping up,
  // while there is prompt to confirm cancelling session edit,
  // when right mouse is clicked on non-selected node
  if (Node != SessionTree->Selected)
  {
    Handled = true;
  }
  else
  {
    if (SessionTree->Selected != NULL)
    {
      SessionTree->PopupMenu = GetSelectedNodePopupMenu();
      if (DebugNotNull(SessionTree->PopupMenu))
      {
        MenuPopup(SessionTree, MousePos, Handled);
      }
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::EditCancelActionExecute(TObject * /*Sender*/)
{
  CancelEditing();
  // reset back to saved settings
  LoadContents();
  UpdateControls();
  // we do not want to see blinking cursor in read-only edit box
  SessionTree->SetFocus();
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::AdvancedButtonDropDownClick(TObject * /*Sender*/)
{
  MenuPopup(SessionAdvancedPopupMenu, AdvancedButton);
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::CancelEditing()
{
  FEditing = false;
  // reset back the color
  UpdateNodeImage(SessionTree->Selected);
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::CloneToNewSite()
{
  FNewSiteData->CopyData(SelectedSession);
  FNewSiteData->MakeUniqueIn(FStoredSessions);
  FNewSiteKeepName = true;
  NewSite();
  EditSession();
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::CloneToNewSiteActionExecute(TObject * /*Sender*/)
{
  CloneToNewSite();
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::Login()
{
  if (OpenInNewWindow() && !IsNewSiteNode(SessionTree->Selected))
  {
    UnicodeString Path = SessionNodePath(SessionTree->Selected);
    ExecuteNewInstance(EncodeUrlString(Path));
    // prevent closing the window, see below
    ModalResult = mrNone;
  }
  else
  {
    // this is not needed when used from LoginButton,
    // but is needed when used from popup menus
    ModalResult = LoginButton->ModalResult;
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::LoginActionExecute(TObject * /*Sender*/)
{
  Login();
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::PuttyActionExecute(TObject * /*Sender*/)
{
  // following may take some time, so cache the shift key state,
  // in case user manages to release it before following finishes
  bool Close = !OpenInNewWindow();

  std::unique_ptr<TSessionData> Data(CloneSelectedSession());
  // putty does not support resolving environment variables in session settings
  Data->ExpandEnvironmentVariables();
  OpenSessionInPutty(GUIConfiguration->PuttyPath, Data.get());

  if (Close)
  {
    ModalResult = CloseButton->ModalResult;
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::LoginButtonDropDownClick(TObject * /*Sender*/)
{
  MenuPopup(LoginDropDownMenu, LoginButton);
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::ParseUrl(const UnicodeString & Url)
{
  std::unique_ptr<TSessionData> SessionData(new TSessionData(L""));

  SaveSession(SessionData.get());

  // We do not want to pass in StoredSessions as we do not want the URL be
  // parsed as pointing to a stored site.
  // It also prevents resetting to defaults (do we want this?)
  bool DefaultsOnly; // unused
  SessionData->ParseUrl(Url, NULL, NULL, DefaultsOnly, NULL, NULL, NULL);

  LoadSession(SessionData.get());
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::PasteUrlActionExecute(TObject * /*Sender*/)
{
  UnicodeString ClipboardUrl;
  if (NonEmptyTextFromClipboard(ClipboardUrl))
  {
    if (!IsEditable())
    {
      // select new site node, when other node is selected and not in editing mode
      SessionTree->Selected = GetNewSiteNode();
    }

    // sanity check
    if (DebugAlwaysTrue(IsEditable()))
    {
      ParseUrl(ClipboardUrl);
    }

    // visualize the pasting
    HostNameEdit->SetFocus();
    HostNameEdit->SelectAll();
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::ParseHostName()
{
  UnicodeString HostName = HostNameEdit->Text;
  if (!HostName.IsEmpty() &&
      (StoredSessions->IsUrl(HostName) || (HostName.Pos(L"@") > 0)))
  {
    ParseUrl(HostName);
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::HostNameEditExit(TObject * /*Sender*/)
{
  ParseHostName();
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::GenerateUrlAction2Execute(TObject * /*Sender*/)
{
  if (DebugAlwaysTrue(SelectedSession != NULL))
  {
    PersistNewSiteIfNeeded();

    std::unique_ptr<TSessionData> Data(SelectedSession->Clone());
    Data->LookupLastFingerprint();

    DoGenerateUrlDialog(Data.get(), NULL);
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::CopyParamRuleActionExecute(TObject * /*Sender*/)
{
  TSessionData * Data = GetSessionData();
  std::unique_ptr<TCopyParamList> CopyParamList(new TCopyParamList());
  (*CopyParamList) = *GUIConfiguration->CopyParamList;

  TCopyParamRuleData RuleData;
  RuleData.HostName = Data->HostNameExpanded;
  RuleData.UserName = Data->UserNameExpanded;
  int CopyParamIndex = CopyParamList->Find(RuleData);
  if (CopyParamIndex < 0)
  {
    TCopyParamRuleData RuleDataHostNameOnly;
    RuleDataHostNameOnly.HostName = Data->HostNameExpanded;
    CopyParamIndex = CopyParamList->Find(RuleDataHostNameOnly);
  }

  TCopyParamPresetMode Mode;
  TCopyParamRuleData * CurrentRuleData = NULL;

  if (CopyParamIndex < 0)
  {
    Mode = cpmAddCurrent;
    CurrentRuleData = &RuleData;
  }
  else
  {
    Mode = cpmEdit;
  }

  TCopyParamType DummyDefaultCopyParams;

  if (DoCopyParamPresetDialog(
        CopyParamList.get(), CopyParamIndex, Mode, CurrentRuleData, DummyDefaultCopyParams))
  {
    GUIConfiguration->CopyParamList = CopyParamList.get();
  }
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SearchSiteNameStartOnlyActionExecute(TObject * /*Sender*/)
{
  FSiteSearch = ssSiteNameStartOnly;
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SearchSiteNameActionExecute(TObject * /*Sender*/)
{
  FSiteSearch = ssSiteName;
}
//---------------------------------------------------------------------------
void __fastcall TLoginDialog::SearchSiteActionExecute(TObject * /*Sender*/)
{
  FSiteSearch = ssSite;
}
//---------------------------------------------------------------------------
