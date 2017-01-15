//---------------------------------------------------------------------------
#include <vcl.h>
#pragma hdrstop

#include <io.h>
#include <fcntl.h>
#include <wincrypt.h>

#define NE_LFS
#define WINSCP
#include <ne_basic.h>
#include <ne_auth.h>
#include <ne_props.h>
#include <ne_uri.h>
#include <ne_session.h>
#include <ne_request.h>
#include <ne_xml.h>
#include <ne_redirect.h>
#include <ne_xmlreq.h>
#include <ne_locks.h>
#include <expat.h>

#include "WebDAVFileSystem.h"

#include "Interface.h"
#include "Common.h"
#include "Exceptions.h"
#include "Terminal.h"
#include "TextsCore.h"
#include "SecureShell.h"
#include "HelpCore.h"
#include "CoreMain.h"
#include "Security.h"
#include <StrUtils.hpp>
#include <NeonIntf.h>
#include <openssl/ssl.h>
//---------------------------------------------------------------------------
#pragma package(smart_init)
//---------------------------------------------------------------------------
#define FILE_OPERATION_LOOP_TERMINAL FTerminal
//---------------------------------------------------------------------------
const int tfFirstLevel = 0x01;
//---------------------------------------------------------------------------
struct TSinkFileParams
{
  UnicodeString TargetDir;
  const TCopyParamType * CopyParam;
  int Params;
  TFileOperationProgressType * OperationProgress;
  bool Skipped;
  unsigned int Flags;
};
//---------------------------------------------------------------------------
struct TWebDAVCertificateData
{
  UnicodeString Subject;
  UnicodeString Issuer;

  TDateTime ValidFrom;
  TDateTime ValidUntil;

  UnicodeString Fingerprint;
  AnsiString AsciiCert;

  int Failures;
};
//---------------------------------------------------------------------------
#define SESSION_FS_KEY "filesystem"
static const char CertificateStorageKey[] = "HttpsCertificates";
static const UnicodeString CONST_WEBDAV_PROTOCOL_BASE_NAME = L"WebDAV";
static const int HttpUnauthorized = 401;
//---------------------------------------------------------------------------
#define DAV_PROP_NAMESPACE "DAV:"
#define MODDAV_PROP_NAMESPACE "http://apache.org/dav/props/"
#define PROP_CONTENT_LENGTH "getcontentlength"
#define PROP_LAST_MODIFIED "getlastmodified"
#define PROP_RESOURCE_TYPE "resourcetype"
#define PROP_HIDDEN "ishidden"
#define PROP_QUOTA_AVAILABLE "quota-available-bytes"
#define PROP_QUOTA_USED "quota-used-bytes"
#define PROP_EXECUTABLE "executable"
#define PROP_OWNER "owner"
//---------------------------------------------------------------------------
static std::unique_ptr<TCriticalSection> DebugSection(new TCriticalSection);
static std::set<TWebDAVFileSystem *> FileSystems;
//---------------------------------------------------------------------------
extern "C"
{

void ne_debug(void * Context, int Channel, const char * Format, ...)
{
  bool DoLog;

  if (FLAGSET(Channel, NE_DBG_SOCKET) ||
      FLAGSET(Channel, NE_DBG_HTTP) ||
      FLAGSET(Channel, NE_DBG_HTTPAUTH) ||
      FLAGSET(Channel, NE_DBG_SSL))
  {
    DoLog = true;
  }
  else if (FLAGSET(Channel, NE_DBG_XML) ||
           FLAGSET(Channel, NE_DBG_WINSCP_HTTP_DETAIL))
  {
    DoLog = (Configuration->ActualLogProtocol >= 1);
  }
  else if (FLAGSET(Channel, NE_DBG_LOCKS) ||
           FLAGSET(Channel, NE_DBG_XMLPARSE) ||
           FLAGSET(Channel, NE_DBG_HTTPBODY))
  {
    DoLog = (Configuration->ActualLogProtocol >= 2);
  }
  else
  {
    DoLog = false;
    DebugFail();
  }

  #ifndef _DEBUG
  if (DoLog)
  #endif
  {
    va_list Args;
    va_start(Args, Format);
    UTF8String UTFMessage;
    UTFMessage.vprintf(Format, Args);
    va_end(Args);

    UnicodeString Message = TrimRight(UTFMessage);

    if (DoLog)
    {
      // Note that this gets called for THttp sessions too.
      // It does no harm atm.
      TWebDAVFileSystem * FileSystem = NULL;
      if (Context != NULL)
      {
        ne_session * Session = static_cast<ne_session *>(Context);

        FileSystem =
          static_cast<TWebDAVFileSystem *>(ne_get_session_private(Session, SESSION_FS_KEY));
      }
      else
      {
        TGuard Guard(DebugSection.get());

        if (FileSystems.size() == 1)
        {
          FileSystem = *FileSystems.begin();
        }
      }

      if (FileSystem != NULL)
      {
        FileSystem->NeonDebug(Message);
      }
    }
  }
}

} // extern "C"
//------------------------------------------------------------------------------
//---------------------------------------------------------------------------
// ne_path_escape returns 7-bit string, so it does not really matter if we use
// AnsiString or UTF8String here, though UTF8String might be more safe
static AnsiString PathEscape(const char * Path)
{
  char * EscapedPath = ne_path_escape(Path);
  AnsiString Result = EscapedPath;
  ne_free(EscapedPath);
  return Result;
}
//---------------------------------------------------------------------------
static UTF8String PathUnescape(const char * Path)
{
  char * UnescapedPath = ne_path_unescape(Path);
  UTF8String Result = UnescapedPath;
  ne_free(UnescapedPath);
  return Result;
}
//---------------------------------------------------------------------------
#define AbsolutePathToNeon(P) PathEscape(StrToNeon(P)).c_str()
#define PathToNeonStatic(THIS, P) AbsolutePathToNeon((THIS)->AbsolutePath(P, false))
#define PathToNeon(P) PathToNeonStatic(this, P)
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
static bool NeonInitialized = false;
static bool NeonSspiInitialized = false;
//---------------------------------------------------------------------------
void __fastcall NeonInitialize()
{
  // Even if this fails, we do not want to interrupt WinSCP starting for that.
  // Anyway, it can hardly fail.
  // Though it fails on Wine on Debian VM, because of ne_sspi_init():
  // sspi: QuerySecurityPackageInfo [failed] [80090305].
  // sspi: Unable to get negotiate maximum packet size
  int NeonResult = ne_sock_init();
  if (NeonResult == 0)
  {
    NeonInitialized = true;
    NeonSspiInitialized = true;
  }
  else if (NeonResult == -2)
  {
    NeonInitialized = true;
    NeonSspiInitialized = false;
  }
  else
  {
    NeonInitialized = false;
    NeonSspiInitialized = false;
  }
}
//---------------------------------------------------------------------------
void __fastcall NeonFinalize()
{
  if (NeonInitialized)
  {
    ne_sock_exit();
    NeonInitialized = false;
  }
}
//---------------------------------------------------------------------------
UnicodeString __fastcall NeonVersion()
{
  UnicodeString Str = StrFromNeon(ne_version_string());
  CutToChar(Str, L' ', true); // "neon"
  UnicodeString Result = CutToChar(Str, L':', true);
  return Result;
}
//---------------------------------------------------------------------------
UnicodeString __fastcall ExpatVersion()
{
  return FORMAT(L"%d.%d.%d", (XML_MAJOR_VERSION, XML_MINOR_VERSION, XML_MICRO_VERSION));
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
TWebDAVFileSystem::TWebDAVFileSystem(TTerminal * ATerminal) :
  TCustomFileSystem(ATerminal),
  FActive(false),
  FHasTrailingSlash(false),
  FNeonSession(NULL),
  FNeonLockStore(NULL),
  FNeonLockStoreSection(new TCriticalSection()),
  FUploading(false),
  FDownloading(false),
  FInitialHandshake(false),
  FIgnoreAuthenticationFailure(iafNo)
{
  FFileSystemInfo.ProtocolBaseName = CONST_WEBDAV_PROTOCOL_BASE_NAME;
  FFileSystemInfo.ProtocolName = FFileSystemInfo.ProtocolBaseName;
}
//---------------------------------------------------------------------------
__fastcall TWebDAVFileSystem::~TWebDAVFileSystem()
{
  UnregisterFromDebug();

  {
    TGuard Guard(FNeonLockStoreSection);
    if (FNeonLockStore != NULL)
    {
      ne_lockstore_destroy(FNeonLockStore);
      FNeonLockStore = NULL;
    }
  }

  delete FNeonLockStoreSection;
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::Open()
{

  if (!NeonInitialized)
  {
    throw Exception(LoadStr(NEON_INIT_FAILED));
  }

  if (!NeonSspiInitialized)
  {
    FTerminal->LogEvent(L"Warning: SSPI initialization failed.");
  }

  RegisterForDebug();

  FCurrentDirectory = L"";
  FHasTrailingSlash = true;
  FStoredPasswordTried = false;
  FTlsVersionStr = L"";
  FCapabilities = 0;

  TSessionData * Data = FTerminal->SessionData;

  FSessionInfo.LoginTime = Now();

  UnicodeString HostName = Data->HostNameExpanded;
  size_t Port = Data->PortNumber;
  UnicodeString ProtocolName = (FTerminal->SessionData->Ftps == ftpsNone) ? WebDAVProtocol : WebDAVSProtocol;
  UnicodeString Path = Data->RemoteDirectory;
  // PathToNeon is not used as we cannot call AbsolutePath here
  UnicodeString EscapedPath = StrFromNeon(PathEscape(StrToNeon(Path)).c_str());
  UnicodeString Url = FORMAT(L"%s://%s:%d%s", (ProtocolName, HostName, Port, EscapedPath));

  FTerminal->Information(LoadStr(STATUS_CONNECT), true);
  FActive = false;
  try
  {
    OpenUrl(Url);
  }
  catch (Exception & E)
  {
    CloseNeonSession();
    FTerminal->Closed();
    FTerminal->FatalError(&E, LoadStr(CONNECTION_FAILED));
  }
  FActive = true;
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TWebDAVFileSystem::ParsePathFromUrl(const UnicodeString & Url)
{
  UnicodeString Result;
  ne_uri ParsedUri;
  if (ne_uri_parse(StrToNeon(Url), &ParsedUri) == 0)
  {
    Result = StrFromNeon(PathUnescape(ParsedUri.path));
    ne_uri_free(&ParsedUri);
  }
  return Result;
}
//---------------------------------------------------------------------------
void TWebDAVFileSystem::OpenUrl(const UnicodeString & Url)
{
  UnicodeString CorrectedUrl;
  NeonClientOpenSessionInternal(CorrectedUrl, Url);

  if (CorrectedUrl.IsEmpty())
  {
    CorrectedUrl = Url;
  }
  UnicodeString ParsedPath = ParsePathFromUrl(CorrectedUrl);
  if (!ParsedPath.IsEmpty())
  {
    // this is most likely pointless as it get overwritten by
    // call to ChangeDirectory() from TTerminal::DoStartup
    FCurrentDirectory = ParsedPath;
  }
}
//---------------------------------------------------------------------------
void TWebDAVFileSystem::NeonClientOpenSessionInternal(UnicodeString & CorrectedUrl, UnicodeString Url)
{
  std::unique_ptr<TStringList> AttemptedUrls(CreateSortedStringList());
  AttemptedUrls->Add(Url);
  while (true)
  {
    CorrectedUrl = L"";
    NeonOpen(CorrectedUrl, Url);
    // No error and no corrected URL?  We're done here.
    if (CorrectedUrl.IsEmpty())
    {
      break;
    }
    CloseNeonSession();
    CheckRedirectLoop(CorrectedUrl, AttemptedUrls.get());
    // Our caller will want to know what our final corrected URL was.
    Url = CorrectedUrl;
  }

  CorrectedUrl = Url;
}
//---------------------------------------------------------------------------
void TWebDAVFileSystem::NeonOpen(UnicodeString & CorrectedUrl, const UnicodeString & Url)
{
  ne_uri uri;
  NeonParseUrl(Url, uri);

  FHostName = StrFromNeon(uri.host);
  FPortNumber = uri.port;

  FSessionInfo.CSCipher = UnicodeString();
  FSessionInfo.SCCipher = UnicodeString();
  bool Ssl = IsTlsUri(uri);
  FSessionInfo.SecurityProtocolName = Ssl ? LoadStr(FTPS_IMPLICIT) : UnicodeString();

  if (Ssl != (FTerminal->SessionData->Ftps != ftpsNone))
  {
    FTerminal->LogEvent(FORMAT(L"Warning: %s", (LoadStr(UNENCRYPTED_REDIRECT))));
  }

  TSessionData * Data = FTerminal->SessionData;

  DebugAssert(FNeonSession == NULL);
  FNeonSession =
    CreateNeonSession(
      uri, Data->ProxyMethod, Data->ProxyHost, Data->ProxyPort,
      Data->ProxyUsername, Data->ProxyPassword);

  UTF8String Path = uri.path;
  ne_uri_free(&uri);
  ne_set_session_private(FNeonSession, SESSION_FS_KEY, this);

  // Other flags:
  // NE_DBG_FLUSH - used only in native implementation of ne_debug
  // NE_DBG_HTTPPLAIN - log credentials in HTTP authentication

  ne_debug_mask =
    NE_DBG_SOCKET |
    NE_DBG_HTTP |
    NE_DBG_XML | // detail
    NE_DBG_HTTPAUTH |
    NE_DBG_LOCKS | // very details
    NE_DBG_XMLPARSE | // very details
    NE_DBG_HTTPBODY | // very details
    NE_DBG_SSL |
    FLAGMASK(Configuration->LogSensitive, NE_DBG_HTTPPLAIN);

  ne_set_read_timeout(FNeonSession, Data->Timeout);

  ne_set_connect_timeout(FNeonSession, Data->Timeout);

  NeonAddAuthentiation(Ssl);

  if (Ssl)
  {
    SetNeonTlsInit(FNeonSession, InitSslSession);

    // When the CA certificate or server certificate has
    // verification problems, neon will call our verify function before
    // outright rejection of the connection.
    ne_ssl_set_verify(FNeonSession, NeonServerSSLCallback, this);

    ne_ssl_trust_default_ca(FNeonSession);

    ne_ssl_provide_clicert(FNeonSession, NeonProvideClientCert, this);
  }

  ne_set_notifier(FNeonSession, NeonNotifier, this);
  ne_hook_create_request(FNeonSession, NeonCreateRequest, this);
  ne_hook_pre_send(FNeonSession, NeonPreSend, this);
  ne_hook_post_send(FNeonSession, NeonPostSend, this);
  ne_hook_post_headers(FNeonSession, NeonPostHeaders, this);

  TAutoFlag Flag(FInitialHandshake);
  ExchangeCapabilities(Path.c_str(), CorrectedUrl);
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::NeonAddAuthentiation(bool UseNegotiate)
{
  unsigned int NeonAuthTypes = NE_AUTH_BASIC | NE_AUTH_DIGEST;
  if (UseNegotiate)
  {
    NeonAuthTypes |= NE_AUTH_NEGOTIATE;
  }
  ne_add_server_auth(FNeonSession, NeonAuthTypes, NeonRequestAuth, this);
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TWebDAVFileSystem::GetRedirectUrl()
{
  UnicodeString Result = GetNeonRedirectUrl(FNeonSession);
  FTerminal->LogEvent(FORMAT(L"Redirected to \"%s\".", (Result)));
  return Result;
}
//---------------------------------------------------------------------------
void TWebDAVFileSystem::ExchangeCapabilities(const char * Path, UnicodeString & CorrectedUrl)
{
  ClearNeonError();

  int NeonStatus;
  FAuthenticationRetry = false;
  do
  {
    NeonStatus = ne_options2(FNeonSession, Path, &FCapabilities);
  }
  while ((NeonStatus == NE_AUTH) && FAuthenticationRetry);

  if (NeonStatus == NE_REDIRECT)
  {
    CorrectedUrl = GetRedirectUrl();
  }
  else if (NeonStatus == NE_OK)
  {
    if (FCapabilities > 0)
    {
      UnicodeString Str;
      unsigned int Capability = 0x01;
      unsigned int Capabilities = FCapabilities;
      while (Capabilities > 0)
      {
        if (FLAGSET(Capabilities, Capability))
        {
          AddToList(Str, StrFromNeon(ne_capability_name(Capability)), L", ");
          Capabilities -= Capability;
        }
        Capability <<= 1;
      }
      FTerminal->LogEvent(FORMAT(L"Server capabilities: %s", (Str)));
      FFileSystemInfo.AdditionalInfo +=
        LoadStr(WEBDAV_EXTENSION_INFO) + sLineBreak +
        L"  " + Str + sLineBreak;
    }
  }
  else
  {
    CheckStatus(NeonStatus);
  }

  FTerminal->SaveCapabilities(FFileSystemInfo);
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::CloseNeonSession()
{
  if (FNeonSession != NULL)
  {
    DestroyNeonSession(FNeonSession);
    FNeonSession = NULL;
  }
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::Close()
{
  DebugAssert(FActive);
  CloseNeonSession();
  FTerminal->Closed();
  FActive = false;
  UnregisterFromDebug();
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::RegisterForDebug()
{
  TGuard Guard(DebugSection.get());
  FileSystems.insert(this);
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::UnregisterFromDebug()
{
  TGuard Guard(DebugSection.get());
  FileSystems.erase(this);
}
//---------------------------------------------------------------------------
bool __fastcall TWebDAVFileSystem::GetActive()
{
  return FActive;
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::CollectUsage()
{
  if (!FTlsVersionStr.IsEmpty())
  {
    FTerminal->CollectTlsUsage(FTlsVersionStr);
  }

  if (!FTerminal->SessionData->TlsCertificateFile.IsEmpty())
  {
    Configuration->Usage->Inc(L"OpenedSessionsWebDAVSCertificate");
  }

  UnicodeString RemoteSystem = FFileSystemInfo.RemoteSystem;
  if (ContainsText(RemoteSystem, L"Microsoft-IIS"))
  {
    FTerminal->Configuration->Usage->Inc(L"OpenedSessionsWebDAVIIS");
  }
  else if (ContainsText(RemoteSystem, L"IT Hit WebDAV Server"))
  {
    FTerminal->Configuration->Usage->Inc(L"OpenedSessionsWebDAVITHit");
  }
  // e.g. brickftp.com
  else if (ContainsText(RemoteSystem, L"nginx"))
  {
    FTerminal->Configuration->Usage->Inc(L"OpenedSessionsWebDAVNginx");
  }
  else
  {
    // We also know OpenDrive, Yandex, iFiles (iOS), Swapper (iOS), SafeSync
    FTerminal->Configuration->Usage->Inc(L"OpenedSessionsWebDAVOther");
  }
}
//---------------------------------------------------------------------------
const TSessionInfo & __fastcall TWebDAVFileSystem::GetSessionInfo()
{
  return FSessionInfo;
}
//---------------------------------------------------------------------------
const TFileSystemInfo & __fastcall TWebDAVFileSystem::GetFileSystemInfo(bool /*Retrieve*/)
{
  return FFileSystemInfo;
}
//---------------------------------------------------------------------------
bool __fastcall TWebDAVFileSystem::TemporaryTransferFile(const UnicodeString & /*FileName*/)
{
  return false;
}
//---------------------------------------------------------------------------
bool __fastcall TWebDAVFileSystem::GetStoredCredentialsTried()
{
  return FStoredPasswordTried;
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TWebDAVFileSystem::GetUserName()
{
  return FUserName;
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::Idle()
{
  // noop
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TWebDAVFileSystem::AbsolutePath(const UnicodeString Path, bool /*Local*/)
{
  bool AddTrailingBackslash;

  if (Path == L"/")
  {
    // does not really matter as path "/" is still "/" when absolute,
    // no slash needed
    AddTrailingBackslash = FHasTrailingSlash;
  }
  else
  {
    AddTrailingBackslash = (Path[Path.Length()] == L'/');
  }

  UnicodeString Result = ::AbsolutePath(GetCurrentDirectory(), Path);
  // We must preserve trailing slash, because particularly for mod_dav,
  // it really matters if the slash in there or not
  if (AddTrailingBackslash)
  {
    Result = UnixIncludeTrailingBackslash(Result);
  }

  return Result;
}
//---------------------------------------------------------------------------
bool __fastcall TWebDAVFileSystem::IsCapable(int Capability) const
{
  DebugAssert(FTerminal);
  switch (Capability)
  {
    case fcRename:
    case fcRemoteMove:
    case fcMoveToQueue:
    case fcPreservingTimestampUpload:
    case fcCheckingSpaceAvailable:
    // Only to make double-click on file edit/open the file,
    // instead of trying to open it as directory
    case fcResolveSymlink:
      return true;

    case fcUserGroupListing:
    case fcModeChanging:
    case fcModeChangingUpload:
    case fcGroupChanging:
    case fcOwnerChanging:
    case fcAnyCommand:
    case fcShellAnyCommand:
    case fcHardLink:
    case fcSymbolicLink:
    case fcTextMode:
    case fcNativeTextMode:
    case fcNewerOnlyUpload:
    case fcTimestampChanging:
    case fcLoadingAdditionalProperties:
    case fcIgnorePermErrors:
    case fcCalculatingChecksum:
    case fcSecondaryShell:
    case fcGroupOwnerChangingByID:
    case fcRemoveCtrlZUpload:
    case fcRemoveBOMUpload:
    case fcRemoteCopy:
    case fcPreservingTimestampDirs:
    case fcResumeSupport:
      return false;

    case fcLocking:
      return FLAGSET(FCapabilities, NE_CAP_DAV_CLASS2);

    default:
      DebugFail();
      return false;
  }
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TWebDAVFileSystem::GetCurrentDirectory()
{
  return FCurrentDirectory;
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::DoStartup()
{
  FTerminal->SetExceptionOnFail(true);
  // retrieve initialize working directory to save it as home directory
  ReadCurrentDirectory();
  FTerminal->SetExceptionOnFail(false);
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::ClearNeonError()
{
  FCancelled = false;
  FAuthenticationRequested = false;
  ne_set_error(FNeonSession, "");
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TWebDAVFileSystem::GetNeonError()
{
  return ::GetNeonError(FNeonSession);
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::CheckStatus(int NeonStatus)
{
  if ((NeonStatus == NE_ERROR) && FCancelled)
  {
    FCancelled = false;
    Abort();
  }
  else
  {
    CheckNeonStatus(FNeonSession, NeonStatus, FHostName);
  }
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::LookupUsersGroups()
{
  DebugFail();
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::ReadCurrentDirectory()
{
  if (FCachedDirectoryChange.IsEmpty())
  {
    FCurrentDirectory = FCurrentDirectory.IsEmpty() ? UnicodeString(L"/") : FCurrentDirectory;
  }
  else
  {
    FCurrentDirectory = FCachedDirectoryChange;
    FCachedDirectoryChange = L"";
  }
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::HomeDirectory()
{
  ChangeDirectory(L"/");
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TWebDAVFileSystem::DirectoryPath(UnicodeString Path)
{
  if (FHasTrailingSlash)
  {
    Path = ::UnixIncludeTrailingBackslash(Path);
  }
  return Path;
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TWebDAVFileSystem::FilePath(const TRemoteFile * File)
{
  UnicodeString Result = File->FullFileName;
  if (File->IsDirectory)
  {
    Result = DirectoryPath(Result);
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::TryOpenDirectory(UnicodeString Directory)
{
  Directory = DirectoryPath(Directory);
  FTerminal->LogEvent(FORMAT(L"Trying to open directory \"%s\".", (Directory)));
  TRemoteFile * File;
  ReadFile(Directory, File);
  delete File;
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::AnnounceFileListOperation()
{
  // noop
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::ChangeDirectory(const UnicodeString ADirectory)
{
  UnicodeString Path = AbsolutePath(ADirectory, false);

  // to verify existence of directory try to open it
  TryOpenDirectory(Path);

  // if open dir did not fail, directory exists -> success.
  FCachedDirectoryChange = Path;
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::CachedChangeDirectory(const UnicodeString Directory)
{
  FCachedDirectoryChange = UnixExcludeTrailingBackslash(Directory);
}
//---------------------------------------------------------------------------
struct TReadFileData
{
  TWebDAVFileSystem * FileSystem;
  TRemoteFile * File;
  TRemoteFileList * FileList;
};
//---------------------------------------------------------------------------
int __fastcall TWebDAVFileSystem::ReadDirectoryInternal(
  const UnicodeString & Path, TRemoteFileList * FileList)
{
  TReadFileData Data;
  Data.FileSystem = this;
  Data.File = NULL;
  Data.FileList = FileList;
  ClearNeonError();
  ne_propfind_handler * PropFindHandler = ne_propfind_create(FNeonSession, PathToNeon(Path), NE_DEPTH_ONE);
  void * DiscoveryContext = ne_lock_register_discovery(PropFindHandler);
  int Result;
  try
  {
    Result = ne_propfind_allprop(PropFindHandler, NeonPropsResult, &Data);
  }
  __finally
  {
    ne_lock_discovery_free(DiscoveryContext);
    ne_propfind_destroy(PropFindHandler);
  }
  return Result;
}
//---------------------------------------------------------------------------
bool __fastcall TWebDAVFileSystem::IsValidRedirect(int NeonStatus, UnicodeString & Path)
{
  bool Result = (NeonStatus == NE_REDIRECT);
  if (Result)
  {
    // What PathToNeon does
    UnicodeString OriginalPath = AbsolutePath(Path, false);
    // Handle one-step redirect
    // (for more steps we would have to implement loop detection).
    // This is mainly to handle "folder" => "folder/" redirects of Apache/mod_dav.
    UnicodeString RedirectUrl = GetRedirectUrl();
    // We should test if the redirect is not for another server,
    // though not sure how to do this reliably (domain aliases, IP vs. domain, etc.)
    UnicodeString RedirectPath = ParsePathFromUrl(RedirectUrl);
    Result =
      !RedirectPath.IsEmpty() &&
      (RedirectPath != OriginalPath);

    if (Result)
    {
      Path = RedirectPath;
    }
  }

  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::ReadDirectory(TRemoteFileList * FileList)
{
  UnicodeString Path = DirectoryPath(FileList->Directory);
  TOperationVisualizer Visualizer(FTerminal->UseBusyCursor);

  int NeonStatus = ReadDirectoryInternal(Path, FileList);
  if (IsValidRedirect(NeonStatus, Path))
  {
    NeonStatus = ReadDirectoryInternal(Path, FileList);
  }
  CheckStatus(NeonStatus);
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::ReadSymlink(TRemoteFile * /*SymlinkFile*/,
  TRemoteFile *& /*File*/)
{
  // we never set SymLink flag, so we should never get here
  DebugFail();
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::ReadFile(const UnicodeString FileName,
  TRemoteFile *& File)
{
  CustomReadFile(FileName, File, NULL);
}
//---------------------------------------------------------------------------
void TWebDAVFileSystem::NeonPropsResult(
  void * UserData, const ne_uri * Uri, const ne_prop_result_set * Results)
{
  UnicodeString Path = StrFromNeon(PathUnescape(Uri->path).c_str());

  TReadFileData & Data = *static_cast<TReadFileData *>(UserData);
  if (Data.FileList != NULL)
  {
    UnicodeString FileListPath = Data.FileSystem->AbsolutePath(Data.FileList->Directory, false);
    if (UnixSamePath(Path, FileListPath))
    {
      Path = UnixIncludeTrailingBackslash(UnixIncludeTrailingBackslash(Path) + L"..");
    }
    std::unique_ptr<TRemoteFile> File(new TRemoteFile(NULL));
    File->Terminal = Data.FileSystem->FTerminal;
    Data.FileSystem->ParsePropResultSet(File.get(), Path, Results);
    Data.FileList->AddFile(File.release());
  }
  else
  {
    Data.FileSystem->ParsePropResultSet(Data.File, Path, Results);
  }
}
//---------------------------------------------------------------------------
const char * __fastcall TWebDAVFileSystem::GetProp(
  const ne_prop_result_set * Results, const char * Name, const char * NameSpace)
{
  ne_propname Prop;
  Prop.nspace = (NameSpace == NULL) ? DAV_PROP_NAMESPACE : NameSpace;
  Prop.name = Name;
  return ne_propset_value(Results, &Prop);
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::ParsePropResultSet(TRemoteFile * File,
  const UnicodeString & Path, const ne_prop_result_set * Results)
{
  File->FullFileName = UnixExcludeTrailingBackslash(Path);
  // Some servers do not use DAV:collection tag, but indicate the folder by trailing slash only.
  // It seems that all servers actually use the trailing slash, including IIS, mod_Dav, IT Hit, OpenDrive, etc.
  bool Collection = (File->FullFileName != Path);
  File->FileName = UnixExtractFileName(File->FullFileName);
  const char * ContentLength = GetProp(Results, PROP_CONTENT_LENGTH);
  // some servers, for example iFiles, do not provide "getcontentlength" for folders
  if (ContentLength != NULL)
  {
    File->Size = StrToInt64Def(ContentLength, 0);
  }
  const char * LastModified = GetProp(Results, PROP_LAST_MODIFIED);
  if (DebugAlwaysTrue(LastModified != NULL))
  {
    char WeekDay[4] = { L'\0' };
    int Year = 0;
    char MonthStr[4] = { L'\0' };
    int Day = 0;
    int Hour = 0;
    int Min = 0;
    int Sec = 0;
    #define RFC1123_FORMAT "%3s, %02d %3s %4d %02d:%02d:%02d GMT"
    int Filled =
      sscanf(LastModified, RFC1123_FORMAT, WeekDay, &Day, MonthStr, &Year, &Hour, &Min, &Sec);
    // we need at least a complete date
    if (Filled >= 4)
    {
      int Month = ParseShortEngMonthName(MonthStr);
      if (Month >= 1)
      {
        TDateTime Modification =
          EncodeDateVerbose((unsigned short)Year, (unsigned short)Month, (unsigned short)Day) +
          EncodeTimeVerbose((unsigned short)Hour, (unsigned short)Min, (unsigned short)Sec, 0);
        File->Modification = ConvertTimestampFromUTC(Modification);
        File->ModificationFmt = mfFull;
      }
    }
  }

  // optimization
  if (!Collection)
  {
    // This is possibly redundant code as all servers we know (see a comment above)
    // indicate the folder by trailing slash too
    const char * ResourceType = GetProp(Results, PROP_RESOURCE_TYPE);
    if (ResourceType != NULL)
    {
      // property has XML value
      UnicodeString AResourceType = ResourceType;
      // this is very poor parsing
      if (ContainsText(ResourceType, L"<DAV:collection"))
      {
        Collection = true;
      }
    }
  }

  File->Type = Collection ? FILETYPE_DIRECTORY : FILETYPE_DEFAULT;
  // this is MS extension (draft-hopmann-collection-props-00)
  const char * IsHidden = GetProp(Results, PROP_HIDDEN);
  if (IsHidden != NULL)
  {
    File->IsHidden = (StrToIntDef(IsHidden, 0) != 0);
  }

  const char * Owner = GetProp(Results, PROP_OWNER);
  if (Owner != NULL)
  {
    File->Owner.Name = Owner;
  }

  const UnicodeString RightsDelimiter(L", ");
  UnicodeString HumanRights;

  // Proprietary property of mod_dav
  // http://www.webdav.org/mod_dav/#imp
  const char * Executable = GetProp(Results, PROP_EXECUTABLE, MODDAV_PROP_NAMESPACE);
  if (Executable != NULL)
  {
    if (strcmp(Executable, "T") == NULL)
    {
      UnicodeString ExecutableRights;
      // The "gear" character is supported since Windows 8
      if (IsWin8())
      {
        ExecutableRights = L"\u2699";
      }
      else
      {
        ExecutableRights = LoadStr(EXECUTABLE);
      }
      AddToList(HumanRights, ExecutableRights, RightsDelimiter);
    }
  }

  struct ne_lock * Lock = static_cast<struct ne_lock *>(ne_propset_private(Results));
  if ((Lock != NULL) && (Lock->token != NULL))
  {
    UnicodeString Owner;
    if (Lock->owner != NULL)
    {
      Owner = StrFromNeon(Lock->owner).Trim();
    }
    UnicodeString LockRights;
    if (IsWin8())
    {
      // The "lock" character is supported since Windows 8
      LockRights = L"\uD83D\uDD12" + Owner;
    }
    else
    {
      LockRights = LoadStr(LOCKED);
      if (!Owner.IsEmpty())
      {
        LockRights = FORMAT(L"%s (%s)", (LockRights, Owner));
      }
    }

    AddToList(HumanRights, LockRights, RightsDelimiter);
  }

  File->HumanRights = HumanRights;
}
//---------------------------------------------------------------------------
int __fastcall TWebDAVFileSystem::CustomReadFileInternal(const UnicodeString FileName,
  TRemoteFile *& File, TRemoteFile * ALinkedByFile)
{
  std::unique_ptr<TRemoteFile> AFile(new TRemoteFile(ALinkedByFile));
  TReadFileData Data;
  Data.FileSystem = this;
  Data.File = AFile.get();
  Data.FileList = NULL;
  ClearNeonError();
  int Result =
    ne_simple_propfind(FNeonSession, PathToNeon(FileName), NE_DEPTH_ZERO, NULL,
      NeonPropsResult, &Data);
  if (Result == NE_OK)
  {
    File = AFile.release();
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::CustomReadFile(UnicodeString FileName,
  TRemoteFile *& File, TRemoteFile * ALinkedByFile)
{
  TOperationVisualizer Visualizer(FTerminal->UseBusyCursor);

  int NeonStatus = CustomReadFileInternal(FileName, File, ALinkedByFile);
  if (IsValidRedirect(NeonStatus, FileName))
  {
    NeonStatus = CustomReadFileInternal(FileName, File, ALinkedByFile);
  }
  CheckStatus(NeonStatus);
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::DeleteFile(const UnicodeString FileName,
  const TRemoteFile * File, int /*Params*/, TRmSessionAction & Action)
{
  Action.Recursive();
  ClearNeonError();
  TOperationVisualizer Visualizer(FTerminal->UseBusyCursor);
  RawByteString Path = PathToNeon(FilePath(File));
  // WebDAV does not allow non-recursive delete:
  // RFC 4918, section 9.6.1:
  // "A client MUST NOT submit a Depth header with a DELETE on a collection with any value but infinity."
  // We should check that folder is empty when called with FLAGSET(Params, dfNoRecursive)
  CheckStatus(ne_delete(FNeonSession, Path.c_str()));
  // The lock is removed with the file, but if a file with the same name gets created,
  // we would try to use obsoleted lock token with it, what the server would reject
  // (mod_dav returns "412 Precondition Failed")
  DiscardLock(Path);
}
//---------------------------------------------------------------------------
int __fastcall TWebDAVFileSystem::RenameFileInternal(const UnicodeString & FileName,
  const UnicodeString & NewName)
{
  // 0 = no overwrite
  return ne_move(FNeonSession, 0, PathToNeon(FileName), PathToNeon(NewName));
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::RenameFile(const UnicodeString FileName,
  const UnicodeString NewName)
{
  ClearNeonError();
  TOperationVisualizer Visualizer(FTerminal->UseBusyCursor);

  UnicodeString Path = FileName;
  int NeonStatus = RenameFileInternal(Path, NewName);
  if (IsValidRedirect(NeonStatus, Path))
  {
    NeonStatus = RenameFileInternal(Path, NewName);
  }
  CheckStatus(NeonStatus);
  // See a comment in DeleteFile
  DiscardLock(PathToNeon(Path));
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::CopyFile(const UnicodeString FileName,
    const UnicodeString NewName)
{
  DebugFail();
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::CreateDirectory(const UnicodeString DirName)
{
  ClearNeonError();
  TOperationVisualizer Visualizer(FTerminal->UseBusyCursor);
  CheckStatus(ne_mkcol(FNeonSession, PathToNeon(DirName)));
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::CreateLink(const UnicodeString FileName,
  const UnicodeString PointTo, bool /*Symbolic*/)
{
  DebugFail();
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::ChangeFileProperties(const UnicodeString FileName,
  const TRemoteFile * /*File*/, const TRemoteProperties * /*Properties*/,
  TChmodSessionAction & /*Action*/)
{
  DebugFail();
}
//---------------------------------------------------------------------------
bool __fastcall TWebDAVFileSystem::LoadFilesProperties(TStrings * /*FileList*/)
{
  DebugFail();
  return false;
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::CalculateFilesChecksum(const UnicodeString & /*Alg*/,
    TStrings * /*FileList*/, TStrings * /*Checksums*/,
    TCalculatedChecksumEvent /*OnCalculatedChecksum*/)
{
  DebugFail();
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::ConfirmOverwrite(
  const UnicodeString & SourceFullFileName, UnicodeString & TargetFileName,
  TFileOperationProgressType * OperationProgress,
  const TOverwriteFileParams * FileParams, const TCopyParamType * CopyParam,
  int Params)
{
  // all = "yes to newer"
  int Answers = qaYes | qaNo | qaCancel | qaYesToAll | qaNoToAll | qaAll;
  TQueryButtonAlias Aliases[3];
  Aliases[0].Button = qaAll;
  Aliases[0].Alias = LoadStr(YES_TO_NEWER_BUTTON);
  Aliases[0].GroupWith = qaYes;
  Aliases[0].GrouppedShiftState = TShiftState() << ssCtrl;
  Aliases[1].Button = qaYesToAll;
  Aliases[1].GroupWith = qaYes;
  Aliases[1].GrouppedShiftState = TShiftState() << ssShift;
  Aliases[2].Button = qaNoToAll;
  Aliases[2].GroupWith = qaNo;
  Aliases[2].GrouppedShiftState = TShiftState() << ssShift;
  TQueryParams QueryParams(qpNeverAskAgainCheck);
  QueryParams.Aliases = Aliases;
  QueryParams.AliasesCount = LENOF(Aliases);

  unsigned int Answer;

  {
    TSuspendFileOperationProgress Suspend(OperationProgress);
    Answer =
      FTerminal->ConfirmFileOverwrite(
        SourceFullFileName, TargetFileName, FileParams, Answers, &QueryParams,
        (OperationProgress->Side == osLocal) ? osRemote : osLocal,
        CopyParam, Params, OperationProgress);
  }

  switch (Answer)
  {
    case qaYes:
      // noop
      break;

    case qaNo:
      THROW_SKIP_FILE_NULL;

    default:
      DebugFail();
    case qaCancel:
      if (!OperationProgress->Cancel)
      {
        OperationProgress->Cancel = csCancel;
      }
      Abort();
      break;
  }
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::CustomCommandOnFile(const UnicodeString FileName,
  const TRemoteFile * /*File*/, UnicodeString Command, int /*Params*/, TCaptureOutputEvent /*OutputEvent*/)
{
  DebugFail();
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::AnyCommand(const UnicodeString Command,
  TCaptureOutputEvent /*OutputEvent*/)
{
  DebugFail();
}
//---------------------------------------------------------------------------
TStrings * __fastcall TWebDAVFileSystem::GetFixedPaths()
{
  return NULL;
}
//---------------------------------------------------------------------------
void TWebDAVFileSystem::NeonQuotaResult(
  void * UserData, const ne_uri * /*Uri*/, const ne_prop_result_set * Results)
{
  TSpaceAvailable & SpaceAvailable = *static_cast<TSpaceAvailable *>(UserData);

  const char * Value = GetProp(Results, PROP_QUOTA_AVAILABLE);
  if (Value != NULL)
  {
    SpaceAvailable.UnusedBytesAvailableToUser = StrToInt64(StrFromNeon(Value));

    const char * Value = GetProp(Results, PROP_QUOTA_USED);
    if (Value != NULL)
    {
      SpaceAvailable.BytesAvailableToUser =
        StrToInt64(StrFromNeon(Value)) + SpaceAvailable.UnusedBytesAvailableToUser;
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::SpaceAvailable(const UnicodeString Path,
  TSpaceAvailable & ASpaceAvailable)
{
  // RFC4331: http://tools.ietf.org/html/rfc4331

  // This is known to be supported by:

  // OpenDrive: for a root drive only (and contrary to the spec, it sends the properties
  // unconditionally, even when not explicitly requested)
  // Server: Apache/2.2.17 (Fedora)
  // X-Powered-By: PHP/5.5.7
  // X-DAV-Powered-By: OpenDrive
  // WWW-Authenticate: Basic realm="PHP WebDAV"

  // IT Hit WebDAV Server:
  // Server: Microsoft-HTTPAPI/1.0
  // X-Engine: IT Hit WebDAV Server .Net v3.8.1877.0 (Evaluation License)

  // Yandex disk:
  // WWW-Authenticate: Basic realm="Yandex.Disk"
  // Server: MochiWeb/1.0

  UnicodeString APath = DirectoryPath(Path);

  ne_propname QuotaProps[3];
  memset(QuotaProps, 0, sizeof(QuotaProps));
  QuotaProps[0].nspace = DAV_PROP_NAMESPACE;
  QuotaProps[0].name = PROP_QUOTA_AVAILABLE;
  QuotaProps[1].nspace = DAV_PROP_NAMESPACE;
  QuotaProps[1].name = PROP_QUOTA_USED;
  QuotaProps[2].nspace = NULL;
  QuotaProps[2].name = NULL;

  TOperationVisualizer Visualizer(FTerminal->UseBusyCursor);

  CheckStatus(
    ne_simple_propfind(FNeonSession, PathToNeon(APath), NE_DEPTH_ZERO, QuotaProps,
      NeonQuotaResult, &ASpaceAvailable));
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::CopyToRemote(TStrings * FilesToCopy,
  const UnicodeString ATargetDir, const TCopyParamType * CopyParam,
  int Params, TFileOperationProgressType * OperationProgress,
  TOnceDoneOperation & OnceDoneOperation)
{
  DebugAssert((FilesToCopy != NULL) && (OperationProgress != NULL));

  Params &= ~cpAppend;
  UnicodeString FileName, FileNameOnly;
  UnicodeString TargetDir = AbsolutePath(ATargetDir, false);
  UnicodeString FullTargetDir = UnixIncludeTrailingBackslash(TargetDir);
  intptr_t Index = 0;
  while ((Index < FilesToCopy->Count) && !OperationProgress->Cancel)
  {
    bool Success = false;
    FileName = FilesToCopy->Strings[Index];
    FileNameOnly = ExtractFileName(FileName, false);

    try
    {
      try
      {
        if (FTerminal->SessionData->CacheDirectories)
        {
          FTerminal->DirectoryModified(TargetDir, false);

          if (::DirectoryExists(ApiPath(::ExtractFilePath(FileName))))
          {
            FTerminal->DirectoryModified(FullTargetDir + FileNameOnly, true);
          }
        }
        SourceRobust(FileName, FullTargetDir, CopyParam, Params, OperationProgress,
          tfFirstLevel);
        Success = true;
      }
      catch (EScpSkipFile & E)
      {
        TSuspendFileOperationProgress Suspend(OperationProgress);
        if (!FTerminal->HandleException(&E))
        {
          throw;
        }
      }
    }
    __finally
    {
      OperationProgress->Finish(FileName, Success, OnceDoneOperation);
    }
    Index++;
  }
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::SourceRobust(const UnicodeString FileName,
  const UnicodeString TargetDir, const TCopyParamType * CopyParam, int Params,
  TFileOperationProgressType * OperationProgress, unsigned int Flags)
{
  // the same in TSFTPFileSystem

  TUploadSessionAction Action(FTerminal->ActionLog);
  TRobustOperationLoop RobustLoop(FTerminal, OperationProgress);

  do
  {
    bool ChildError = false;
    try
    {
      Source(FileName, TargetDir, CopyParam, Params, OperationProgress,
        Flags, Action, ChildError);
    }
    catch (Exception & E)
    {
      if (!RobustLoop.TryReopen(E))
      {
        if (!ChildError)
        {
          FTerminal->RollbackAction(Action, OperationProgress, &E);
        }
        throw;
      }
    }

    if (RobustLoop.ShouldRetry())
    {
      OperationProgress->RollbackTransfer();
      Action.Restart();
      // prevent overwrite confirmations
      // (should not be set for directories!)
      Params |= cpNoConfirmation;
    }
  }
  while (RobustLoop.Retry());
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::Source(const UnicodeString FileName,
  const UnicodeString TargetDir, const TCopyParamType * CopyParam, int Params,
  TFileOperationProgressType * OperationProgress, unsigned int Flags,
  TUploadSessionAction & Action, bool & ChildError)
{
  Action.FileName(ExpandUNCFileName(FileName));

  OperationProgress->SetFile(FileName, false);

  if (!FTerminal->AllowLocalFileTransfer(FileName, CopyParam, OperationProgress))
  {
    THROW_SKIP_FILE_NULL;
  }

  HANDLE File;
  __int64 MTime;
  __int64 Size;
  int Attrs;

  FTerminal->OpenLocalFile(FileName, GENERIC_READ, &Attrs,
    &File, NULL, &MTime, NULL, &Size);

  bool Dir = FLAGSET(Attrs, faDirectory);

  int FD = -1;
  try
  {
    OperationProgress->SetFileInProgress();

    if (Dir)
    {
      Action.Cancel();
      DirectorySource(IncludeTrailingBackslash(FileName), TargetDir,
        Attrs, CopyParam, Params, OperationProgress, Flags);
    }
    else
    {
      UnicodeString DestFileName =
        FTerminal->ChangeFileName(
          CopyParam, ExtractFileName(FileName), osLocal,
          FLAGSET(Flags, tfFirstLevel));

      FTerminal->LogEvent(FORMAT(L"Copying \"%s\" to remote directory started.", (FileName)));

      OperationProgress->SetLocalSize(Size);

      // Suppose same data size to transfer as to read
      // (not true with ASCII transfer)
      OperationProgress->SetTransferSize(OperationProgress->LocalSize);
      OperationProgress->TransferingFile = false;

      UnicodeString DestFullName = TargetDir + DestFileName;

      TRemoteFile * RemoteFile = NULL;
      try
      {
        TValueRestorer<TIgnoreAuthenticationFailure> IgnoreAuthenticationFailureRestorer(FIgnoreAuthenticationFailure);
        FIgnoreAuthenticationFailure = iafWaiting;

        // this should not throw
        CustomReadFileInternal(DestFullName, RemoteFile, NULL);
      }
      catch (...)
      {
        if (!FTerminal->Active)
        {
          throw;
        }
      }

      TDateTime Modification = UnixToDateTime(MTime, FTerminal->SessionData->DSTMode);

      if (RemoteFile != NULL)
      {
        TOverwriteFileParams FileParams;

        FileParams.SourceSize = Size;
        FileParams.SourceTimestamp = Modification;
        FileParams.DestSize = RemoteFile->Size;
        FileParams.DestTimestamp = RemoteFile->Modification;
        delete RemoteFile;

        ConfirmOverwrite(FileName, DestFileName, OperationProgress,
          &FileParams, CopyParam, Params);
      }

      DestFullName = TargetDir + DestFileName;
      // only now, we know the final destination
      // (not really true as we do not support changing file name on overwrite dialog)
      Action.Destination(DestFullName);

      FILE_OPERATION_LOOP_BEGIN
      {
        SetFilePointer(File, 0, NULL, FILE_BEGIN);

        FD = _open_osfhandle((intptr_t)File, O_BINARY);
        if (FD < 0)
        {
          THROW_SKIP_FILE_NULL;
        }

        TAutoFlag UploadingFlag(FUploading);

        ClearNeonError();
        CheckStatus(ne_put(FNeonSession, PathToNeon(DestFullName), FD));
      }
      FILE_OPERATION_LOOP_END(FMTLOAD(TRANSFER_ERROR, (FileName)));

      if (CopyParam->PreserveTime)
      {
        FTerminal->LogEvent(FORMAT(L"Preserving timestamp [%s]",
          (StandardTimestamp(Modification))));

        TTouchSessionAction TouchAction(FTerminal->ActionLog, DestFullName, Modification);
        try
        {
          TDateTime ModificationUTC = ConvertTimestampToUTC(Modification);
          TFormatSettings FormatSettings = GetEngFormatSettings();
          UnicodeString LastModified =
            FormatDateTime(L"ddd, d mmm yyyy hh:nn:ss 'GMT'", ModificationUTC, FormatSettings);
          UTF8String NeonLastModified(LastModified);
          // second element is "NULL-terminating"
          ne_proppatch_operation Operations[2];
          memset(Operations, 0, sizeof(Operations));
          ne_propname LastModifiedProp;
          LastModifiedProp.nspace = DAV_PROP_NAMESPACE;
          LastModifiedProp.name = PROP_LAST_MODIFIED;
          Operations[0].name = &LastModifiedProp;
          Operations[0].type = ne_propset;
          Operations[0].value = NeonLastModified.c_str();
          int Status = ne_proppatch(FNeonSession, PathToNeon(DestFullName), Operations);
          if (Status == NE_ERROR)
          {
            FTerminal->LogEvent(FORMAT(L"Preserving timestamp failed, ignoring: %s",
              (GetNeonError())));
            // Ignore errors as major WebDAV servers (like IIS), do not support
            // changing getlastmodified.
            // The only server we found that supports this is TradeMicro SafeSync.
            // But it announces itself as "Server: Apache",
            // so it's not reliable to autodetect the support.
            TouchAction.Cancel();
          }
          else
          {
            CheckStatus(Status);
          }
        }
        catch (Exception & E)
        {
          TouchAction.Rollback(&E);
          ChildError = true;
          throw;
        }
      }

      FTerminal->LogFileDone(OperationProgress);
    }
  }
  __finally
  {
    if (FD >= 0)
    {
      // _close calls CloseHandle internally (even doc states, we should not call CloseHandle),
      // but it crashes code guard
      _close(FD);
    }
    else if (File != NULL)
    {
      CloseHandle(File);
    }
  }

  // TODO : Delete also read-only files.
  if (FLAGSET(Params, cpDelete))
  {
    if (!Dir)
    {
      FILE_OPERATION_LOOP_BEGIN
      {
        THROWOSIFFALSE(::DeleteFile(ApiPath(FileName).c_str()));
      }
      FILE_OPERATION_LOOP_END(FMTLOAD(DELETE_LOCAL_FILE_ERROR, (FileName)));
    }
  }
  else if (CopyParam->ClearArchive && FLAGSET(Attrs, faArchive))
  {
    FILE_OPERATION_LOOP_BEGIN
    {
      THROWOSIFFALSE(FileSetAttr(ApiPath(FileName), Attrs & ~faArchive) == 0);
    }
    FILE_OPERATION_LOOP_END(FMTLOAD(CANT_SET_ATTRS, (FileName)));
  }
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::DirectorySource(const UnicodeString DirectoryName,
  const UnicodeString TargetDir, int Attrs, const TCopyParamType * CopyParam,
  int Params, TFileOperationProgressType * OperationProgress, unsigned int Flags)
{
  UnicodeString DestDirectoryName =
    FTerminal->ChangeFileName(
      CopyParam, ExtractFileName(ExcludeTrailingBackslash(DirectoryName)),
      osLocal, FLAGSET(Flags, tfFirstLevel));
  UnicodeString DestFullName = UnixIncludeTrailingBackslash(TargetDir + DestDirectoryName);
  // create DestFullName if it does not exist
  if (!FTerminal->FileExists(DestFullName))
  {
    TRemoteProperties Properties;
    if (CopyParam->PreserveRights)
    {
      Properties.Valid = TValidProperties() << vpRights;
      Properties.Rights = CopyParam->RemoteFileRights(Attrs);
    }
    FTerminal->CreateDirectory(DestFullName, &Properties);
  }

  OperationProgress->SetFile(DirectoryName);

  int FindAttrs = faReadOnly | faHidden | faSysFile | faDirectory | faArchive;
  TSearchRecChecked SearchRec;
  bool FindOK;

  FILE_OPERATION_LOOP_BEGIN
  {
    FindOK =
      (FindFirstChecked(DirectoryName + L"*.*", FindAttrs, SearchRec) == 0);
  }
  FILE_OPERATION_LOOP_END(FMTLOAD(LIST_DIR_ERROR, (DirectoryName)));

  try
  {
    while (FindOK && !OperationProgress->Cancel)
    {
      UnicodeString FileName = DirectoryName + SearchRec.Name;
      try
      {
        if ((SearchRec.Name != L".") && (SearchRec.Name != L".."))
        {
          SourceRobust(FileName, DestFullName, CopyParam, Params, OperationProgress,
            Flags & ~(tfFirstLevel));
        }
      }
      catch (EScpSkipFile & E)
      {
        // If ESkipFile occurs, just log it and continue with next file
        TSuspendFileOperationProgress Suspend(OperationProgress);
        // here a message to user was displayed, which was not appropriate
        // when user refused to overwrite the file in subdirectory.
        // hopefully it won't be missing in other situations.
        if (!FTerminal->HandleException(&E))
        {
          throw;
        }
      }

      FILE_OPERATION_LOOP_BEGIN
      {
        FindOK = (FindNextChecked(SearchRec) == 0);
      }
      FILE_OPERATION_LOOP_END(FMTLOAD(LIST_DIR_ERROR, (DirectoryName)));
    }
  }
  __finally
  {
    FindClose(SearchRec);
  }

  // TODO : Delete also read-only directories.
  // TODO : Show error message on failure.
  if (!OperationProgress->Cancel)
  {
    if (FLAGSET(Params, cpDelete))
    {
      RemoveDir(ApiPath(DirectoryName));
    }
    else if (CopyParam->ClearArchive && FLAGSET(Attrs, faArchive))
    {
      FILE_OPERATION_LOOP_BEGIN
      {
        THROWOSIFFALSE(FileSetAttr(ApiPath(DirectoryName), Attrs & ~faArchive) == 0);
      }
      FILE_OPERATION_LOOP_END(FMTLOAD(CANT_SET_ATTRS, (DirectoryName)));
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::CopyToLocal(TStrings * FilesToCopy,
  const UnicodeString TargetDir, const TCopyParamType * CopyParam,
  int Params, TFileOperationProgressType * OperationProgress,
  TOnceDoneOperation & OnceDoneOperation)
{
  Params &= ~cpAppend;
  UnicodeString FullTargetDir = ::IncludeTrailingBackslash(TargetDir);

  int Index = 0;
  while (Index < FilesToCopy->Count && !OperationProgress->Cancel)
  {
    UnicodeString FileName = FilesToCopy->Strings[Index];
    const TRemoteFile * File = dynamic_cast<const TRemoteFile *>(FilesToCopy->Objects[Index]);
    bool Success = false;
    try
    {
      try
      {
        SinkRobust(AbsolutePath(FileName, false), File, FullTargetDir, CopyParam, Params,
          OperationProgress, tfFirstLevel);
        Success = true;
      }
      catch (EScpSkipFile & E)
      {
        TSuspendFileOperationProgress Suspend(OperationProgress);
        if (!FTerminal->HandleException(&E))
        {
          throw;
        }
      }
    }
    __finally
    {
      OperationProgress->Finish(FileName, Success, OnceDoneOperation);
    }
    Index++;
  }
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::SinkRobust(const UnicodeString FileName,
  const TRemoteFile * File, const UnicodeString TargetDir,
  const TCopyParamType * CopyParam, int Params,
  TFileOperationProgressType * OperationProgress, unsigned int Flags)
{
  // the same in TSFTPFileSystem

  TDownloadSessionAction Action(FTerminal->ActionLog);
  TRobustOperationLoop RobustLoop(FTerminal, OperationProgress);

  do
  {
    bool ChildError = false;
    try
    {
      Sink(FileName, File, TargetDir, CopyParam, Params, OperationProgress,
        Flags, Action, ChildError);
    }
    catch (Exception & E)
    {
      if (!RobustLoop.TryReopen(E))
      {
        if (!ChildError)
        {
          FTerminal->RollbackAction(Action, OperationProgress, &E);
        }
        throw;
      }
    }

    if (RobustLoop.ShouldRetry())
    {
      OperationProgress->RollbackTransfer();
      Action.Restart();
      DebugAssert(File != NULL);
      if (!File->IsDirectory)
      {
        // prevent overwrite confirmations
        Params |= cpNoConfirmation;
      }
    }
  }
  while (RobustLoop.Retry());
}
//---------------------------------------------------------------------------
void TWebDAVFileSystem::NeonCreateRequest(
  ne_request * Request, void * UserData, const char * /*Method*/, const char * /*Uri*/)
{
  TWebDAVFileSystem * FileSystem = static_cast<TWebDAVFileSystem *>(UserData);
  ne_set_request_private(Request, SESSION_FS_KEY, FileSystem);
  ne_add_response_body_reader(Request, NeonBodyAccepter, NeonBodyReader, Request);
  FileSystem->FNtlmAuthenticationFailed = false;
}
//---------------------------------------------------------------------------
void TWebDAVFileSystem::NeonPreSend(
  ne_request * Request, void * UserData, ne_buffer * Header)
{
  TWebDAVFileSystem * FileSystem = static_cast<TWebDAVFileSystem *>(UserData);

  FileSystem->FAuthorizationProtocol = L"";
  UnicodeString HeaderBuf(StrFromNeon(AnsiString(Header->data, Header->used)));
  const UnicodeString AuthorizationHeaderName(L"Authorization:");
  int P = HeaderBuf.Pos(AuthorizationHeaderName);
  if (P > 0)
  {
    P += AuthorizationHeaderName.Length();
    int P2 = PosEx(L"\n", HeaderBuf, P);
    if (DebugAlwaysTrue(P2 > 0))
    {
      UnicodeString AuthorizationHeader = HeaderBuf.SubString(P, P2 - P).Trim();
      FileSystem->FAuthorizationProtocol = CutToChar(AuthorizationHeader, L' ', false);
    }
  }

  if (FileSystem->FDownloading)
  {
    // Needed by IIS server to make it download source code, not code output,
    // and mainly to even allow downloading file with unregistered extensions.
    // Without it files like .001 return 404 (Not found) HTTP code.
    // http://msdn.microsoft.com/en-us/library/cc250098.aspx
    // http://msdn.microsoft.com/en-us/library/cc250216.aspx
    // http://lists.manyfish.co.uk/pipermail/neon/2012-April/001452.html
    // It's also supported by Oracle server:
    // https://docs.oracle.com/cd/E19146-01/821-1828/gczya/index.html
    // We do not know yet of any server that fails when the header is used,
    // so it's added unconditionally.
    ne_buffer_zappend(Header, "Translate: f\r\n");
  }

  if (FileSystem->FTerminal->Log->Logging)
  {
    const char * Buffer;
    size_t Size;
    if (ne_get_request_body_buffer(Request, &Buffer, &Size))
    {
      // all neon request types that use ne_add_request_header
      // use XML content-type, so it's text-based
      DebugAssert(ContainsStr(HeaderBuf, L"Content-Type: " NE_XML_MEDIA_TYPE));
      FileSystem->FTerminal->Log->Add(llInput, UnicodeString(UTF8String(Buffer, Size)));
    }
  }

  if (FileSystem->FUploading)
  {
    ne_set_request_body_provider_pre(Request,
      FileSystem->NeonUploadBodyProvider, FileSystem);
  }

  FileSystem->FResponse = L"";
}
//---------------------------------------------------------------------------
int TWebDAVFileSystem::NeonPostSend(ne_request * /*Req*/, void * UserData,
  const ne_status * /*Status*/)
{
  TWebDAVFileSystem * FileSystem = static_cast<TWebDAVFileSystem *>(UserData);
  if (!FileSystem->FResponse.IsEmpty())
  {
    FileSystem->FTerminal->Log->Add(llOutput, FileSystem->FResponse);
  }
  return NE_OK;
}
//---------------------------------------------------------------------------
bool __fastcall TWebDAVFileSystem::IsNtlmAuthentication()
{
  return
    SameText(FAuthorizationProtocol, L"NTLM") ||
    SameText(FAuthorizationProtocol, L"Negotiate");
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::HttpAuthenticationFailed()
{
  // NTLM/GSSAPI failed
  if (IsNtlmAuthentication())
  {
    if (FNtlmAuthenticationFailed)
    {
      // Next time do not try Negotiate (NTLM/GSSAPI),
      // otherwise we end up in an endless loop.
      // If the server returns all other challenges in the response, removing the Negotiate
      // protocol will itself ensure that other protocols are tried (we haven't seen this behaviour).
      // IIS will return only Negotiate response if the request was Negotiate, so there's no fallback.
      // We have to retry with a fresh request. That's what FAuthenticationRetry does.
      FTerminal->LogEvent(FORMAT(L"%s challenge failed, will try different challenge", (FAuthorizationProtocol)));
      ne_remove_server_auth(FNeonSession);
      NeonAddAuthentiation(false);
      FAuthenticationRetry = true;
    }
    else
    {
      // The first 401 is expected, the server is using it to send WWW-Authenticate header with data.
      FNtlmAuthenticationFailed = true;
    }
  }
}
//---------------------------------------------------------------------------
void TWebDAVFileSystem::NeonPostHeaders(ne_request * /*Req*/, void * UserData, const ne_status * Status)
{
  TWebDAVFileSystem * FileSystem = static_cast<TWebDAVFileSystem *>(UserData);
  if (Status->code == HttpUnauthorized)
  {
    FileSystem->HttpAuthenticationFailed();
  }
}
//---------------------------------------------------------------------------
ssize_t TWebDAVFileSystem::NeonUploadBodyProvider(void * UserData, char * /*Buffer*/, size_t /*BufLen*/)
{
  TWebDAVFileSystem * FileSystem = static_cast<TWebDAVFileSystem *>(UserData);
  ssize_t Result;
  if (FileSystem->CancelTransfer())
  {
    Result = -1;
  }
  else
  {
    Result = 1;
  }
  return Result;
}
//---------------------------------------------------------------------------
static void __fastcall AddHeaderValueToList(UnicodeString & List, ne_request * Request, const char * Name)
{
  const char * Value = ne_get_response_header(Request, Name);
  if (Value != NULL)
  {
    AddToList(List, StrFromNeon(Value), L"; ");
  }
}
//---------------------------------------------------------------------------
int TWebDAVFileSystem::NeonBodyAccepter(void * UserData, ne_request * Request, const ne_status * Status)
{
  DebugAssert(UserData == Request);
  TWebDAVFileSystem * FileSystem =
    static_cast<TWebDAVFileSystem *>(ne_get_request_private(Request, SESSION_FS_KEY));

  bool AuthenticationFailureCode = (Status->code == HttpUnauthorized);
  bool PasswordAuthenticationFailed = AuthenticationFailureCode && FileSystem->FAuthenticationRequested;
  bool AuthenticationFailed = PasswordAuthenticationFailed || (AuthenticationFailureCode && FileSystem->IsNtlmAuthentication());
  bool AuthenticationNeeded = AuthenticationFailureCode && !AuthenticationFailed;

  if (FileSystem->FInitialHandshake)
  {
    UnicodeString Line;
    if (AuthenticationNeeded)
    {
      Line = LoadStr(STATUS_AUTHENTICATE);
    }
    else if (AuthenticationFailed)
    {
      Line = LoadStr(FTP_ACCESS_DENIED);
    }
    else if (Status->klass == 2)
    {
      Line = LoadStr(STATUS_AUTHENTICATED);
    }

    if (!Line.IsEmpty())
    {
      FileSystem->FTerminal->Information(Line, true);
    }

    UnicodeString RemoteSystem;
    // Used by IT Hit WebDAV Server:
    // Server: Microsoft-HTTPAPI/1.0
    // X-Engine: IT Hit WebDAV Server .Net v3.8.1877.0 (Evaluation License)
    AddHeaderValueToList(RemoteSystem, Request, "X-Engine");
    // Used by OpenDrive:
    // Server: Apache/2.2.17 (Fedora)
    // X-Powered-By: PHP/5.5.7
    // X-DAV-Powered-By: OpenDrive
    AddHeaderValueToList(RemoteSystem, Request, "X-DAV-Powered-By");
    // Used by IIS:
    // Server: Microsoft-IIS/8.5
    AddHeaderValueToList(RemoteSystem, Request, "Server");
    // Not really useful.
    // Can be e.g. "PleskLin"
    AddHeaderValueToList(RemoteSystem, Request, "X-Powered-By");
    FileSystem->FFileSystemInfo.RemoteSystem = RemoteSystem;
  }

  // When we explicitly fail authentication of request
  // with FIgnoreAuthenticationFailure flag (after it failed with password),
  // neon resets its internal password store and tries the next request
  // without calling our authentication hook first
  // (note AuthenticationFailed vs. AuthenticationNeeded)
  // what likely fails, but we do not want to reset out password
  // (as it was not even tried yet for this request).
  if (PasswordAuthenticationFailed)
  {
    if (FileSystem->FIgnoreAuthenticationFailure == iafNo)
    {
      FileSystem->FPassword = RawByteString();
    }
    else
    {
      FileSystem->FIgnoreAuthenticationFailure = iafPasswordFailed;
    }
  }

  return ne_accept_2xx(UserData, Request, Status);
}
//---------------------------------------------------------------------------
bool __fastcall TWebDAVFileSystem::CancelTransfer()
{
  bool Result = false;
  if ((FUploading || FDownloading) &&
      (FTerminal->OperationProgress != NULL) &&
      (FTerminal->OperationProgress->Cancel != csContinue))
  {
    FCancelled = true;
    Result = true;
  }
  return Result;
}
//---------------------------------------------------------------------------
int TWebDAVFileSystem::NeonBodyReader(void * UserData, const char * Buf, size_t Len)
{
  ne_request * Request = static_cast<ne_request *>(UserData);
  TWebDAVFileSystem * FileSystem =
    static_cast<TWebDAVFileSystem *>(ne_get_request_private(Request, SESSION_FS_KEY));

  if (FileSystem->FTerminal->Log->Logging)
  {
    ne_content_type ContentType;
    if (ne_get_content_type(Request, &ContentType) == 0)
    {
      // The main point of the content-type check was to exclude
      // GET responses (with file contents).
      // But this won't work when downloading text files that have text
      // content type on their own, hence the additional not-downloading test.
      if (!FileSystem->FDownloading &&
          ((ne_strcasecmp(ContentType.type, "text") == 0) ||
           media_type_is_xml(&ContentType)))
      {
        UnicodeString Content = UnicodeString(UTF8String(Buf, Len)).Trim();
        FileSystem->FResponse += Content;
      }
      ne_free(ContentType.value);
    }
  }

  int Result = FileSystem->CancelTransfer() ? 1 : 0;
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::Sink(const UnicodeString FileName,
  const TRemoteFile * File, const UnicodeString TargetDir,
  const TCopyParamType * CopyParam, int Params,
  TFileOperationProgressType * OperationProgress, unsigned int Flags,
  TDownloadSessionAction & Action, bool & ChildError)
{
  UnicodeString FileNameOnly = UnixExtractFileName(FileName);

  Action.FileName(FileName);

  DebugAssert(File);
  TFileMasks::TParams MaskParams;
  MaskParams.Size = File->Size;
  MaskParams.Modification = File->Modification;

  UnicodeString BaseFileName = FTerminal->GetBaseFileName(FileName);
  if (!CopyParam->AllowTransfer(BaseFileName, osRemote, File->IsDirectory, MaskParams))
  {
    FTerminal->LogEvent(FORMAT(L"File \"%s\" excluded from transfer", (FileName)));
    THROW_SKIP_FILE_NULL;
  }

  if (CopyParam->SkipTransfer(FileName, File->IsDirectory))
  {
    OperationProgress->AddSkippedFileSize(File->Size);
    THROW_SKIP_FILE_NULL;
  }

  FTerminal->LogFileDetails(FileName, TDateTime(), File->Size);

  OperationProgress->SetFile(FileName);

  UnicodeString DestFileName =
    FTerminal->ChangeFileName(
      CopyParam, FileNameOnly, osRemote, FLAGSET(Flags, tfFirstLevel));
  UnicodeString DestFullName = TargetDir + DestFileName;

  if (File->IsDirectory)
  {
    Action.Cancel();
    if (DebugAlwaysTrue(FTerminal->CanRecurseToDirectory(File)))
    {
      FILE_OPERATION_LOOP_BEGIN
      {
        int Attrs = FileGetAttrFix(ApiPath(DestFullName));
        if (FLAGCLEAR(Attrs, faDirectory)) { EXCEPTION; }
      }
      FILE_OPERATION_LOOP_END(FMTLOAD(NOT_DIRECTORY_ERROR, (DestFullName)));

      FILE_OPERATION_LOOP_BEGIN
      {
        THROWOSIFFALSE(ForceDirectories(ApiPath(DestFullName)));
      }
      FILE_OPERATION_LOOP_END(FMTLOAD(CREATE_DIR_ERROR, (DestFullName)));

      TSinkFileParams SinkFileParams;
      SinkFileParams.TargetDir = IncludeTrailingBackslash(DestFullName);
      SinkFileParams.CopyParam = CopyParam;
      SinkFileParams.Params = Params;
      SinkFileParams.OperationProgress = OperationProgress;
      SinkFileParams.Skipped = false;
      SinkFileParams.Flags = Flags & ~tfFirstLevel;

      FTerminal->ProcessDirectory(FileName, SinkFile, &SinkFileParams);

      // Do not delete directory if some of its files were skip.
      // Throw "skip file" for the directory to avoid attempt to deletion
      // of any parent directory
      if (FLAGSET(Params, cpDelete) && SinkFileParams.Skipped)
      {
        THROW_SKIP_FILE_NULL;
      }
    }
    else
    {
      // file is symlink to directory, currently do nothing, but it should be
      // reported to user
    }
  }
  else
  {
    FTerminal->LogEvent(FORMAT(L"Copying \"%s\" to local directory started.", (FileName)));
    if (FileExists(ApiPath(DestFullName)))
    {
      __int64 Size;
      __int64 MTime;
      FTerminal->OpenLocalFile(DestFullName, GENERIC_READ, NULL,
        NULL, NULL, &MTime, NULL, &Size);
      TOverwriteFileParams FileParams;

      FileParams.SourceSize = File->Size;
      FileParams.SourceTimestamp = File->Modification;
      FileParams.DestSize = Size;
      FileParams.DestTimestamp = UnixToDateTime(MTime,
        FTerminal->SessionData->DSTMode);

      ConfirmOverwrite(FileName, DestFileName, OperationProgress,
        &FileParams, CopyParam, Params);
    }

    // Suppose same data size to transfer as to write
    OperationProgress->SetTransferSize(File->Size);
    OperationProgress->SetLocalSize(OperationProgress->TransferSize);

    int Attrs = -1;
    FILE_OPERATION_LOOP_BEGIN
    {
      Attrs = FileGetAttrFix(ApiPath(DestFullName));
      if ((Attrs >= 0) && FLAGSET(Attrs, faDirectory)) { EXCEPTION; }
    }
    FILE_OPERATION_LOOP_END(FMTLOAD(NOT_FILE_ERROR, (DestFullName)));

    OperationProgress->TransferingFile = false; // not set with WebDAV protocol

    UnicodeString FilePath = ::UnixExtractFilePath(FileName);
    if (FilePath.IsEmpty())
    {
      FilePath = L"/";
    }

    Action.Destination(ExpandUNCFileName(DestFullName));

    FILE_OPERATION_LOOP_BEGIN
    {
      HANDLE LocalHandle;
      if (!FTerminal->CreateLocalFile(DestFullName, OperationProgress,
             &LocalHandle, FLAGSET(Params, cpNoConfirmation)))
      {
        THROW_SKIP_FILE_NULL;
      }

      bool DeleteLocalFile = true;

      int FD = -1;
      try
      {
        FD = _open_osfhandle((intptr_t)LocalHandle, O_BINARY);
        if (FD < 0)
        {
          THROW_SKIP_FILE_NULL;
        }

        TAutoFlag DownloadingFlag(FDownloading);

        ClearNeonError();
        CheckStatus(ne_get(FNeonSession, PathToNeon(FileName), FD));
        DeleteLocalFile = false;

        if (CopyParam->PreserveTime)
        {
          TDateTime Modification = File->Modification;
          FILETIME WrTime = DateTimeToFileTime(Modification, FTerminal->SessionData->DSTMode);
          FTerminal->LogEvent(FORMAT(L"Preserving timestamp [%s]",
            (StandardTimestamp(Modification))));
          SetFileTime(LocalHandle, NULL, NULL, &WrTime);
        }
      }
      __finally
      {
        if (FD >= 0)
        {
          // _close calls CloseHandle internally (even doc states, we should not call CloseHandle),
          // but it crashes code guard
          _close(FD);
        }
        else
        {
          CloseHandle(LocalHandle);
        }

        if (DeleteLocalFile)
        {
          FILE_OPERATION_LOOP_BEGIN
          {
            THROWOSIFFALSE(Sysutils::DeleteFile(ApiPath(DestFullName)));
          }
          FILE_OPERATION_LOOP_END(FMTLOAD(DELETE_LOCAL_FILE_ERROR, (DestFullName)));
        }
      }
    }
    FILE_OPERATION_LOOP_END(FMTLOAD(TRANSFER_ERROR, (FileName)));

    if (Attrs == -1)
    {
      Attrs = faArchive;
    }
    int NewAttrs = CopyParam->LocalFileAttrs(*File->Rights);
    if ((NewAttrs & Attrs) != NewAttrs)
    {
      FILE_OPERATION_LOOP_BEGIN
      {
        THROWOSIFFALSE(FileSetAttr(ApiPath(DestFullName), Attrs | NewAttrs) == 0);
      }
      FILE_OPERATION_LOOP_END(FMTLOAD(CANT_SET_ATTRS, (DestFullName)));
    }

    FTerminal->LogFileDone(OperationProgress);
  }

  if (FLAGSET(Params, cpDelete))
  {
    ChildError = true;
    // If file is directory, do not delete it recursively, because it should be
    // empty already. If not, it should not be deleted (some files were
    // skipped or some new files were copied to it, while we were downloading)
    int Params = dfNoRecursive;
    FTerminal->DeleteFile(FileName, File, &Params);
    ChildError = false;
  }
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::SinkFile(const UnicodeString FileName,
  const TRemoteFile * File, void * Param)
{
  TSinkFileParams * Params = static_cast<TSinkFileParams *>(Param);
  DebugAssert(Params->OperationProgress);
  try
  {
    SinkRobust(FileName, File, Params->TargetDir, Params->CopyParam,
      Params->Params, Params->OperationProgress, Params->Flags);
  }
  catch (EScpSkipFile & E)
  {
    TFileOperationProgressType * OperationProgress = Params->OperationProgress;

    Params->Skipped = true;

    {
      TSuspendFileOperationProgress Suspend(OperationProgress);
      if (!FTerminal->HandleException(&E))
      {
        throw;
      }
    }

    if (OperationProgress->Cancel)
    {
      Abort();
    }
  }
}
//---------------------------------------------------------------------------
bool TWebDAVFileSystem::VerifyCertificate(const TWebDAVCertificateData & Data)
{
  FSessionInfo.CertificateFingerprint = Data.Fingerprint;

  bool Result;
  if (FTerminal->SessionData->FingerprintScan)
  {
    Result = false;
  }
  else
  {
    FTerminal->LogEvent(
      FORMAT(L"Verifying certificate for \"%s\" with fingerprint %s and %2.2X failures",
        (Data.Subject, Data.Fingerprint, Data.Failures)));

    int Failures = Data.Failures;

    UnicodeString SiteKey = TSessionData::FormatSiteKey(FHostName, FPortNumber);
    Result =
      FTerminal->VerifyCertificate(CertificateStorageKey, SiteKey, Data.Fingerprint, Data.Subject, Failures);

    if (!Result)
    {
      UnicodeString WindowsCertificateError;
      if (NeonWindowsValidateCertificate(Failures, Data.AsciiCert, WindowsCertificateError))
      {
        FTerminal->LogEvent(L"Certificate verified against Windows certificate store");
        // There can be also other flags, not just the NE_SSL_UNTRUSTED.
        Result = (Failures == 0);
      }
      else
      {
        FTerminal->LogEvent(
          FORMAT(L"Certificate failed to verify against Windows certificate store: %s", (DefaultStr(WindowsCertificateError, L"no details"))));
      }
    }

    UnicodeString Summary;
    if (Failures == 0)
    {
      Summary = LoadStr(CERT_OK);
    }
    else
    {
      Summary = NeonCertificateFailuresErrorStr(Failures, FHostName);
    }

    UnicodeString ValidityTimeFormat = L"ddddd tt";
    FSessionInfo.Certificate =
      FMTLOAD(CERT_TEXT, (
        Data.Issuer + L"\n",
        Data.Subject + L"\n",
        FormatDateTime(ValidityTimeFormat, Data.ValidFrom),
        FormatDateTime(ValidityTimeFormat, Data.ValidUntil),
        Data.Fingerprint,
        Summary));

    if (!Result)
    {
      TClipboardHandler ClipboardHandler;
      ClipboardHandler.Text = Data.Fingerprint;

      TQueryButtonAlias Aliases[1];
      Aliases[0].Button = qaRetry;
      Aliases[0].Alias = LoadStr(COPY_KEY_BUTTON);
      Aliases[0].OnClick = &ClipboardHandler.Copy;

      TQueryParams Params;
      Params.HelpKeyword = HELP_VERIFY_CERTIFICATE;
      Params.NoBatchAnswers = qaYes | qaRetry;
      Params.Aliases = Aliases;
      Params.AliasesCount = LENOF(Aliases);
      unsigned int Answer = FTerminal->QueryUser(
        FMTLOAD(VERIFY_CERT_PROMPT3, (FSessionInfo.Certificate)),
        NULL, qaYes | qaNo | qaCancel | qaRetry, &Params, qtWarning);
      switch (Answer)
      {
        case qaYes:
          FTerminal->CacheCertificate(CertificateStorageKey, SiteKey, Data.Fingerprint, Failures);
          Result = true;
          break;

        case qaNo:
          Result = true;
          break;

        default:
          DebugFail();
        case qaCancel:
          FTerminal->Configuration->Usage->Inc(L"HostNotVerified");
          Result = false;
          break;
      }

      if (Result)
      {
        FTerminal->Configuration->RememberLastFingerprint(
          FTerminal->SessionData->SiteKey, TlsFingerprintType, FSessionInfo.CertificateFingerprint);
      }
    }

    if (Result)
    {
      CollectTLSSessionInfo();
    }
  }

  return Result;
}
//------------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::CollectTLSSessionInfo()
{
  // See also TFTPFileSystem::Open().
  // Have to cache the value as the connection (the neon HTTP session, not "our" session)
  // can be closed as the time we need it in CollectUsage().
  FTlsVersionStr = StrFromNeon(ne_ssl_get_version(FNeonSession));
  AddToList(FSessionInfo.SecurityProtocolName, FTlsVersionStr, L", ");

  UnicodeString Cipher = StrFromNeon(ne_ssl_get_cipher(FNeonSession));
  FSessionInfo.CSCipher = Cipher;
  FSessionInfo.SCCipher = Cipher;

  // see CAsyncSslSocketLayer::PrintSessionInfo()
  FTerminal->LogEvent(FORMAT(L"Using %s, cipher %s", (FTlsVersionStr, Cipher)));
}
//------------------------------------------------------------------------------
// A neon-session callback to validate the SSL certificate when the CA
// is unknown (e.g. a self-signed cert), or there are other SSL
// certificate problems.
int TWebDAVFileSystem::NeonServerSSLCallback(void * UserData, int Failures, const ne_ssl_certificate * Certificate)
{
  TWebDAVCertificateData Data;

  char Fingerprint[NE_SSL_DIGESTLEN] = {0};
  if (ne_ssl_cert_digest(Certificate, Fingerprint) != 0)
  {
    strcpy(Fingerprint, "<unknown>");
  }
  Data.Fingerprint = StrFromNeon(Fingerprint);
  Data.AsciiCert = NeonExportCertificate(Certificate);

  char * Subject = ne_ssl_readable_dname(ne_ssl_cert_subject(Certificate));
  Data.Subject = StrFromNeon(Subject);
  ne_free(Subject);
  char * Issuer = ne_ssl_readable_dname(ne_ssl_cert_issuer(Certificate));
  Data.Issuer = StrFromNeon(Issuer);
  ne_free(Issuer);

  Data.Failures = Failures;

  time_t ValidFrom;
  time_t ValidUntil;
  ne_ssl_cert_validity_time(Certificate, &ValidFrom, &ValidUntil);
  Data.ValidFrom = UnixToDateTime(ValidFrom, dstmWin);
  Data.ValidUntil = UnixToDateTime(ValidUntil, dstmWin);

  TWebDAVFileSystem * FileSystem = static_cast<TWebDAVFileSystem *>(UserData);

  return FileSystem->VerifyCertificate(Data) ? NE_OK : NE_ERROR;
}
//------------------------------------------------------------------------------
void TWebDAVFileSystem::NeonProvideClientCert(void * UserData, ne_session * Sess,
  const ne_ssl_dname * const * /*DNames*/, int /*DNCount*/)
{
  TWebDAVFileSystem * FileSystem = static_cast<TWebDAVFileSystem *>(UserData);

  FileSystem->FTerminal->LogEvent(LoadStr(NEED_CLIENT_CERTIFICATE));

  X509 * Certificate;
  EVP_PKEY * PrivateKey;
  if (FileSystem->FTerminal->LoadTlsCertificate(Certificate, PrivateKey))
  {
    ne_ssl_client_cert * NeonCertificate = ne_ssl_clicert_create(Certificate, PrivateKey);
    ne_ssl_set_clicert(Sess, NeonCertificate);
    ne_ssl_clicert_free(NeonCertificate);
  }
}
//------------------------------------------------------------------------------
int TWebDAVFileSystem::NeonRequestAuth(
  void * UserData, const char * Realm, int Attempt, char * UserName, char * Password)
{
  DebugUsedParam(Realm);
  DebugUsedParam(Attempt);
  TWebDAVFileSystem * FileSystem = static_cast<TWebDAVFileSystem *>(UserData);

  TTerminal * Terminal = FileSystem->FTerminal;
  TSessionData * SessionData = Terminal->SessionData;

  bool Result = true;

  // will ask for username only once
  if (FileSystem->FUserName.IsEmpty())
  {
    if (!SessionData->UserName.IsEmpty())
    {
      FileSystem->FUserName = SessionData->UserNameExpanded;
    }
    else
    {
      if (!Terminal->PromptUser(SessionData, pkUserName, LoadStr(USERNAME_TITLE), L"",
            LoadStr(USERNAME_PROMPT2), true, NE_ABUFSIZ, FileSystem->FUserName))
      {
        // note that we never get here actually
        Result = false;
      }
    }
  }

  UnicodeString APassword;
  if (Result)
  {
    // Some servers (Gallery2 on https://g2.pixi.me/w/webdav/)
    // return authentication error (401) on PROPFIND request for
    // non-existing files.
    // When we already tried password before, do not try anymore.
    // When we did not try password before (possible only when
    // server does not require authentication for any previous request,
    // such as when read access is not authenticated), try it now,
    // but use special flag for the try, because when it fails
    // we still want to try password for future requests (such as PUT).

    if (!FileSystem->FPassword.IsEmpty())
    {
      if (FileSystem->FIgnoreAuthenticationFailure == iafPasswordFailed)
      {
        // Fail PROPFIND /nonexising request...
        Result = false;
      }
      else
      {
        APassword = Terminal->DecryptPassword(FileSystem->FPassword);
      }
    }
    else
    {
      if (!SessionData->Password.IsEmpty() && !FileSystem->FStoredPasswordTried)
      {
        APassword = SessionData->Password;
        FileSystem->FStoredPasswordTried = true;
      }
      else
      {
        // Asking for password (or using configured password) the first time,
        // and asking for password.
        // Note that we never get false here actually
        Result =
          Terminal->PromptUser(
            SessionData, pkPassword, LoadStr(PASSWORD_TITLE), L"",
            LoadStr(PASSWORD_PROMPT), false, NE_ABUFSIZ, APassword);
      }

      if (Result)
      {
        // While neon remembers the password on its own,
        // we need to keep a copy in case neon store gets reset by
        // 401 response to PROPFIND /nonexisting on G2, see above.
        // Possibly we can do this for G2 servers only.
        FileSystem->FPassword = Terminal->EncryptPassword(APassword);
      }
    }
  }

  if (Result)
  {
    strncpy(UserName, StrToNeon(FileSystem->FUserName), NE_ABUFSIZ);
    strncpy(Password, StrToNeon(APassword), NE_ABUFSIZ);
  }

  FileSystem->FAuthenticationRequested = true;

  return Result ? 0 : -1;
}
//------------------------------------------------------------------------------
void TWebDAVFileSystem::NeonNotifier(void * UserData, ne_session_status Status, const ne_session_status_info * StatusInfo)
{
  TWebDAVFileSystem * FileSystem = static_cast<TWebDAVFileSystem *>(UserData);
  TFileOperationProgressType * OperationProgress = FileSystem->FTerminal->OperationProgress;

  // We particularly have to filter out response to "put" request,
  // handling that would reset the upload progress back to low number (response is small).
  if (((FileSystem->FUploading && (Status == ne_status_sending)) ||
       (FileSystem->FDownloading && (Status == ne_status_recving))) &&
      DebugAlwaysTrue(OperationProgress != NULL))
  {
    __int64 Progress = StatusInfo->sr.progress;
    __int64 Diff = Progress - OperationProgress->TransferedSize;

    if (Diff > 0)
    {
      OperationProgress->ThrottleToCPSLimit(static_cast<unsigned long>(Diff));
    }

    __int64 Total = StatusInfo->sr.total;

    // Total size unknown
    if (Total < 0)
    {
      if (Diff >= 0)
      {
        OperationProgress->AddTransfered(Diff);
      }
      else
      {
        // Session total has been reset. A new stream started
        OperationProgress->AddTransfered(Progress);
      }
    }
    else
    {
      OperationProgress->SetTransferSize(Total);
      OperationProgress->AddTransfered(Diff);
    }
  }
}
//------------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::NeonDebug(const UnicodeString & Message)
{
  FTerminal->LogEvent(Message);
}
//------------------------------------------------------------------------------
void TWebDAVFileSystem::InitSslSession(ssl_st * Ssl, ne_session * Session)
{
  TWebDAVFileSystem * FileSystem =
    static_cast<TWebDAVFileSystem *>(ne_get_session_private(Session, SESSION_FS_KEY));
  FileSystem->InitSslSessionImpl(Ssl);
}
//------------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::InitSslSessionImpl(ssl_st * Ssl)
{
  // See also CAsyncSslSocketLayer::InitSSLConnection
  TSessionData * Data = FTerminal->SessionData;
  #define MASK_TLS_VERSION(VERSION, FLAG) ((Data->MinTlsVersion > VERSION) || (Data->MaxTlsVersion < VERSION) ? FLAG : 0)
  int Options =
    MASK_TLS_VERSION(ssl2, SSL_OP_NO_SSLv2) |
    MASK_TLS_VERSION(ssl3, SSL_OP_NO_SSLv3) |
    MASK_TLS_VERSION(tls10, SSL_OP_NO_TLSv1) |
    MASK_TLS_VERSION(tls11, SSL_OP_NO_TLSv1_1) |
    MASK_TLS_VERSION(tls12, SSL_OP_NO_TLSv1_2);
  // SSL_ctrl() with SSL_CTRL_OPTIONS adds flags (not sets)
  SSL_ctrl(Ssl, SSL_CTRL_OPTIONS, Options, NULL);
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::GetSupportedChecksumAlgs(TStrings * /*Algs*/)
{
  // NOOP
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::LockFile(const UnicodeString & /*FileName*/, const TRemoteFile * File)
{
  ClearNeonError();
  struct ne_lock * Lock = ne_lock_create();
  try
  {
    Lock->uri.path = ne_strdup(PathToNeon(FilePath(File)));
    Lock->depth = NE_DEPTH_INFINITE;
    Lock->timeout = NE_TIMEOUT_INFINITE;
    Lock->owner = ne_strdup(StrToNeon(FTerminal->UserName));
    CheckStatus(ne_lock(FNeonSession, Lock));

    {
      TGuard Guard(FNeonLockStoreSection);

      RequireLockStore();

      ne_lockstore_add(FNeonLockStore, Lock);
    }
    // ownership passed
    Lock = NULL;
  }
  __finally
  {
    if (Lock != NULL)
    {
      ne_lock_destroy(Lock);
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::RequireLockStore()
{
  // Create store only when needed,
  // to limit the use of cross-thread code in UpdateFromMain
  if (FNeonLockStore == NULL)
  {
    FNeonLockStore = ne_lockstore_create();
    ne_lockstore_register(FNeonLockStore, FNeonSession);
  }
}
//---------------------------------------------------------------------------
void TWebDAVFileSystem::LockResult(void * UserData, const struct ne_lock * Lock,
  const ne_uri * /*Uri*/, const ne_status * /*Status*/)
{
  // Is NULL on failure (Status is not NULL then)
  if (Lock != NULL)
  {
    RawByteString & LockToken = *static_cast<RawByteString *>(UserData);
    LockToken = Lock->token;
  }
}
//---------------------------------------------------------------------------
struct ne_lock * __fastcall TWebDAVFileSystem::FindLock(const RawByteString & Path)
{
  ne_uri Uri = {0};
  Uri.path = Path.c_str();
  return ne_lockstore_findbyuri(FNeonLockStore, &Uri);
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::DiscardLock(const RawByteString & Path)
{
  TGuard Guard(FNeonLockStoreSection);
  if (FNeonLockStore != NULL)
  {
    struct ne_lock * Lock = FindLock(Path);
    if (Lock != NULL)
    {
      ne_lockstore_remove(FNeonLockStore, Lock);
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::UnlockFile(const UnicodeString & FileName, const TRemoteFile * File)
{
  ClearNeonError();
  struct ne_lock * Lock = ne_lock_create();
  try
  {
    RawByteString Path = PathToNeon(FilePath(File));
    RawByteString LockToken;

    struct ne_lock * Lock = NULL;

    {
      TGuard Guard(FNeonLockStoreSection);
      if (FNeonLockStore != NULL)
      {
        Lock = FindLock(Path);
      }
    }

    // we are not aware of the file being locked,
    // though it can be locked from another (previous and already closed)
    // session, so query the server.
    if (Lock == NULL)
    {
      CheckStatus(ne_lock_discover(FNeonSession, Path.c_str(), LockResult, &LockToken));
    }

    if ((Lock == NULL) && (LockToken.IsEmpty()))
    {
      throw Exception(FMTLOAD(NOT_LOCKED, (FileName)));
    }
    else
    {
      struct ne_lock * Unlock;
      if (Lock == NULL)
      {
        DebugAssert(!LockToken.IsEmpty());
        Unlock = ne_lock_create();
        Unlock->uri.path = ne_strdup(Path.c_str());
        Unlock->token = ne_strdup(LockToken.c_str());
      }
      else
      {
        Unlock = Lock;
      }
      CheckStatus(ne_unlock(FNeonSession, Unlock));

      DiscardLock(Path);
    }
  }
  __finally
  {
    ne_lock_destroy(Lock);
  }
}
//---------------------------------------------------------------------------
void __fastcall TWebDAVFileSystem::UpdateFromMain(TCustomFileSystem * AMainFileSystem)
{
  TWebDAVFileSystem * MainFileSystem = dynamic_cast<TWebDAVFileSystem *>(AMainFileSystem);
  if (DebugAlwaysTrue(MainFileSystem != NULL))
  {
    TGuard Guard(FNeonLockStoreSection);
    TGuard MainGuard(MainFileSystem->FNeonLockStoreSection);

    if (FNeonLockStore != NULL)
    {
      struct ne_lock * Lock;
      while ((Lock = ne_lockstore_first(FNeonLockStore)) != NULL)
      {
        ne_lockstore_remove(FNeonLockStore, Lock);
      }
    }

    if (DebugAlwaysTrue(MainFileSystem->FNeonLockStore != NULL))
    {
      RequireLockStore();
      struct ne_lock * Lock = ne_lockstore_first(MainFileSystem->FNeonLockStore);
      while (Lock != NULL)
      {
        ne_lockstore_add(FNeonLockStore, ne_lock_copy(Lock));
        Lock = ne_lockstore_next(MainFileSystem->FNeonLockStore);
      }
    }
  }
}
//------------------------------------------------------------------------------
