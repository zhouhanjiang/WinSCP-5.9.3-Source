//---------------------------------------------------------------------------
#ifndef CommonH
#define CommonH
//---------------------------------------------------------------------------
#include <vector>
//---------------------------------------------------------------------------
#define EXCEPTION throw ExtException(NULL, L"")
#define THROWOSIFFALSE(C) { if (!(C)) RaiseLastOSError(); }
#define SAFE_DESTROY_EX(CLASS, OBJ) { CLASS * PObj = OBJ; OBJ = NULL; delete PObj; }
#define SAFE_DESTROY(OBJ) SAFE_DESTROY_EX(TObject, OBJ)
#define NULL_TERMINATE(S) S[LENOF(S) - 1] = L'\0'
#define ASCOPY(dest, source) \
  { \
    AnsiString CopyBuf = source; \
    strncpy(dest, CopyBuf.c_str(), LENOF(dest)); \
    dest[LENOF(dest)-1] = '\0'; \
  }
#define SWAP(TYPE, FIRST, SECOND) \
  { TYPE __Backup = FIRST; FIRST = SECOND; SECOND = __Backup; }
//---------------------------------------------------------------------------
extern const wchar_t EngShortMonthNames[12][4];
extern const char Bom[3];
extern const wchar_t TokenPrefix;
extern const wchar_t NoReplacement;
extern const wchar_t TokenReplacement;
extern const UnicodeString LocalInvalidChars;
extern const UnicodeString PasswordMask;
extern const UnicodeString Ellipsis;
//---------------------------------------------------------------------------
extern const UnicodeString HttpProtocol;
extern const UnicodeString HttpsProtocol;
extern const UnicodeString ProtocolSeparator;
//---------------------------------------------------------------------------
UnicodeString ReplaceChar(UnicodeString Str, wchar_t A, wchar_t B);
UnicodeString DeleteChar(UnicodeString Str, wchar_t C);
void PackStr(UnicodeString & Str);
void PackStr(RawByteString & Str);
void PackStr(AnsiString & Str);
void __fastcall Shred(UnicodeString & Str);
void __fastcall Shred(UTF8String & Str);
void __fastcall Shred(AnsiString & Str);
UnicodeString AnsiToString(const RawByteString & S);
UnicodeString AnsiToString(const char * S, size_t Len);
UnicodeString MakeValidFileName(UnicodeString FileName);
UnicodeString RootKeyToStr(HKEY RootKey);
UnicodeString BooleanToStr(bool B);
UnicodeString BooleanToEngStr(bool B);
UnicodeString DefaultStr(const UnicodeString & Str, const UnicodeString & Default);
UnicodeString CutToChar(UnicodeString &Str, wchar_t Ch, bool Trim);
UnicodeString CopyToChars(const UnicodeString & Str, int & From, UnicodeString Chs, bool Trim,
  wchar_t * Delimiter = NULL, bool DoubleDelimiterEscapes = false);
UnicodeString CopyToChar(const UnicodeString & Str, wchar_t Ch, bool Trim);
UnicodeString DelimitStr(UnicodeString Str, UnicodeString Chars);
UnicodeString ShellDelimitStr(UnicodeString Str, wchar_t Quote);
UnicodeString ExceptionLogString(Exception *E);
UnicodeString __fastcall MainInstructions(const UnicodeString & S);
bool __fastcall HasParagraphs(const UnicodeString & S);
UnicodeString __fastcall MainInstructionsFirstParagraph(const UnicodeString & S);
bool ExtractMainInstructions(UnicodeString & S, UnicodeString & MainInstructions);
UnicodeString RemoveMainInstructionsTag(UnicodeString S);
UnicodeString UnformatMessage(UnicodeString S);
UnicodeString RemoveInteractiveMsgTag(UnicodeString S);
UnicodeString RemoveEmptyLines(const UnicodeString & S);
bool IsNumber(const UnicodeString Str);
UnicodeString __fastcall SystemTemporaryDirectory();
UnicodeString __fastcall GetShellFolderPath(int CSIdl);
UnicodeString __fastcall StripPathQuotes(const UnicodeString Path);
UnicodeString __fastcall AddQuotes(UnicodeString Str);
UnicodeString __fastcall AddPathQuotes(UnicodeString Path);
void __fastcall SplitCommand(UnicodeString Command, UnicodeString &Program,
  UnicodeString & Params, UnicodeString & Dir);
UnicodeString __fastcall ValidLocalFileName(UnicodeString FileName);
UnicodeString __fastcall ValidLocalFileName(
  UnicodeString FileName, wchar_t InvalidCharsReplacement,
  const UnicodeString & TokenizibleChars, const UnicodeString & LocalInvalidChars);
UnicodeString __fastcall ExtractProgram(UnicodeString Command);
UnicodeString __fastcall ExtractProgramName(UnicodeString Command);
UnicodeString __fastcall FormatCommand(UnicodeString Program, UnicodeString Params);
UnicodeString __fastcall ExpandFileNameCommand(const UnicodeString Command,
  const UnicodeString FileName);
void __fastcall ReformatFileNameCommand(UnicodeString & Command);
UnicodeString __fastcall EscapeParam(const UnicodeString & Param);
UnicodeString __fastcall EscapePuttyCommandParam(UnicodeString Param);
UnicodeString __fastcall ExpandEnvironmentVariables(const UnicodeString & Str);
bool __fastcall ComparePaths(const UnicodeString & Path1, const UnicodeString & Path2);
bool __fastcall CompareFileName(const UnicodeString & Path1, const UnicodeString & Path2);
int __fastcall CompareLogicalText(const UnicodeString & S1, const UnicodeString & S2);
bool __fastcall IsReservedName(UnicodeString FileName);
UnicodeString __fastcall ApiPath(UnicodeString Path);
UnicodeString __fastcall DisplayableStr(const RawByteString & Str);
UnicodeString __fastcall ByteToHex(unsigned char B, bool UpperCase = true);
UnicodeString __fastcall BytesToHex(const unsigned char * B, size_t Length, bool UpperCase = true, wchar_t Separator = L'\0');
UnicodeString __fastcall BytesToHex(RawByteString Str, bool UpperCase = true, wchar_t Separator = L'\0');
UnicodeString __fastcall CharToHex(wchar_t Ch, bool UpperCase = true);
RawByteString __fastcall HexToBytes(const UnicodeString Hex);
unsigned char __fastcall HexToByte(const UnicodeString Hex);
bool __fastcall IsLowerCaseLetter(wchar_t Ch);
bool __fastcall IsUpperCaseLetter(wchar_t Ch);
bool __fastcall IsLetter(wchar_t Ch);
bool __fastcall IsDigit(wchar_t Ch);
bool __fastcall IsHex(wchar_t Ch);
UnicodeString __fastcall DecodeUrlChars(UnicodeString S);
UnicodeString __fastcall EncodeUrlString(UnicodeString S);
UnicodeString __fastcall EncodeUrlPath(UnicodeString S);
UnicodeString __fastcall AppendUrlParams(UnicodeString URL, UnicodeString Params);
UnicodeString __fastcall ExtractFileNameFromUrl(const UnicodeString & Url);
bool __fastcall RecursiveDeleteFile(const UnicodeString & FileName, bool ToRecycleBin);
void __fastcall RecursiveDeleteFileChecked(const UnicodeString & FileName, bool ToRecycleBin);
void __fastcall DeleteFileChecked(const UnicodeString & FileName);
unsigned int __fastcall CancelAnswer(unsigned int Answers);
unsigned int __fastcall AbortAnswer(unsigned int Answers);
unsigned int __fastcall ContinueAnswer(unsigned int Answers);
UnicodeString __fastcall LoadStr(int Ident, unsigned int MaxLength);
UnicodeString __fastcall LoadStrPart(int Ident, int Part);
UnicodeString __fastcall EscapeHotkey(const UnicodeString & Caption);
bool __fastcall CutToken(UnicodeString & Str, UnicodeString & Token,
  UnicodeString * RawToken = NULL, UnicodeString * Separator = NULL);
bool __fastcall CutTokenEx(UnicodeString & Str, UnicodeString & Token,
  UnicodeString * RawToken = NULL, UnicodeString * Separator = NULL);
void __fastcall AddToList(UnicodeString & List, const UnicodeString & Value, const UnicodeString & Delimiter);
bool __fastcall IsWinVista();
bool __fastcall IsWin7();
bool __fastcall IsWin8();
bool __fastcall IsWin10();
bool __fastcall IsWine();
TLibModule * __fastcall FindModule(void * Instance);
__int64 __fastcall Round(double Number);
bool __fastcall TryRelativeStrToDateTime(UnicodeString S, TDateTime & DateTime, bool Add);
LCID __fastcall GetDefaultLCID();
UnicodeString __fastcall DefaultEncodingName();
UnicodeString __fastcall WindowsProductName();
bool _fastcall GetWindowsProductType(DWORD & Type);
UnicodeString __fastcall WindowsVersion();
UnicodeString __fastcall WindowsVersionLong();
bool __fastcall IsDirectoryWriteable(const UnicodeString & Path);
UnicodeString __fastcall FormatNumber(__int64 Size);
UnicodeString __fastcall FormatSize(__int64 Size);
UnicodeString __fastcall ExtractFileBaseName(const UnicodeString & Path);
TStringList * __fastcall TextToStringList(const UnicodeString & Text);
UnicodeString __fastcall StringsToText(TStrings * Strings);
TStrings * __fastcall CloneStrings(TStrings * Strings);
UnicodeString __fastcall TrimVersion(UnicodeString Version);
UnicodeString __fastcall FormatVersion(int MajovVersion, int MinorVersion, int Release);
TFormatSettings __fastcall GetEngFormatSettings();
int __fastcall ParseShortEngMonthName(const UnicodeString & MonthStr);
// The defaults are equal to defaults of TStringList class (except for Sorted)
TStringList * __fastcall CreateSortedStringList(bool CaseSensitive = false, System::Types::TDuplicates Duplicates = dupIgnore);
UnicodeString __fastcall FindIdent(const UnicodeString & Ident, TStrings * Idents);
void __fastcall CheckCertificate(const UnicodeString & Path);
typedef struct x509_st X509;
typedef struct evp_pkey_st EVP_PKEY;
void __fastcall ParseCertificate(const UnicodeString & Path,
  const UnicodeString & Passphrase, X509 *& Certificate, EVP_PKEY *& PrivateKey,
  bool & WrongPassphrase);
bool __fastcall IsHttpUrl(const UnicodeString & S);
bool __fastcall IsHttpOrHttpsUrl(const UnicodeString & S);
UnicodeString __fastcall ChangeUrlProtocol(const UnicodeString & S, const UnicodeString & Protocol);
void __fastcall LoadScriptFromFile(UnicodeString FileName, TStrings * Lines);
UnicodeString __fastcall StripEllipsis(const UnicodeString & S);
//---------------------------------------------------------------------------
typedef void __fastcall (__closure* TProcessLocalFileEvent)
  (const UnicodeString FileName, const TSearchRec Rec, void * Param);
bool __fastcall FileSearchRec(const UnicodeString FileName, TSearchRec & Rec);
struct TSearchRecChecked : public TSearchRec
{
  UnicodeString Path;
};
int __fastcall FindCheck(int Result, const UnicodeString & Path);
int __fastcall FindFirstUnchecked(const UnicodeString & Path, int Attr, TSearchRecChecked & F);
int __fastcall FindFirstChecked(const UnicodeString & Path, int Attr, TSearchRecChecked & F);
int __fastcall FindNextChecked(TSearchRecChecked & F);
void __fastcall ProcessLocalDirectory(UnicodeString DirName,
  TProcessLocalFileEvent CallBackFunc, void * Param = NULL, int FindAttrs = -1);
int __fastcall FileGetAttrFix(const UnicodeString FileName);
//---------------------------------------------------------------------------
extern const wchar_t * DSTModeNames;
enum TDSTMode
{
  dstmWin =  0, //
  dstmUnix = 1, // adjust UTC time to Windows "bug"
  dstmKeep = 2
};
bool __fastcall UsesDaylightHack();
TDateTime __fastcall EncodeDateVerbose(Word Year, Word Month, Word Day);
TDateTime __fastcall EncodeTimeVerbose(Word Hour, Word Min, Word Sec, Word MSec);
double __fastcall DSTDifferenceForTime(TDateTime DateTime);
TDateTime __fastcall SystemTimeToDateTimeVerbose(const SYSTEMTIME & SystemTime);
TDateTime __fastcall UnixToDateTime(__int64 TimeStamp, TDSTMode DSTMode);
TDateTime __fastcall ConvertTimestampToUTC(TDateTime DateTime);
TDateTime __fastcall ConvertTimestampFromUTC(TDateTime DateTime);
FILETIME __fastcall DateTimeToFileTime(const TDateTime DateTime, TDSTMode DSTMode);
TDateTime __fastcall AdjustDateTimeFromUnix(TDateTime DateTime, TDSTMode DSTMode);
void __fastcall UnifyDateTimePrecision(TDateTime & DateTime1, TDateTime & DateTime2);
TDateTime __fastcall FileTimeToDateTime(const FILETIME & FileTime);
__int64 __fastcall ConvertTimestampToUnix(const FILETIME & FileTime,
  TDSTMode DSTMode);
__int64 __fastcall ConvertTimestampToUnixSafe(const FILETIME & FileTime,
  TDSTMode DSTMode);
UnicodeString __fastcall FixedLenDateTimeFormat(const UnicodeString & Format);
UnicodeString __fastcall StandardTimestamp(const TDateTime & DateTime);
UnicodeString __fastcall StandardTimestamp();
UnicodeString __fastcall StandardDatestamp();
UnicodeString __fastcall FormatTimeZone(long Sec);
UnicodeString __fastcall GetTimeZoneLogString();
bool __fastcall AdjustClockForDSTEnabled();
int __fastcall CompareFileTime(TDateTime T1, TDateTime T2);
int __fastcall TimeToMSec(TDateTime T);
int __fastcall TimeToSeconds(TDateTime T);
int __fastcall TimeToMinutes(TDateTime T);
//---------------------------------------------------------------------------
template<class MethodT>
MethodT __fastcall MakeMethod(void * Data, void * Code)
{
  MethodT Method;
  ((TMethod*)&Method)->Data = Data;
  ((TMethod*)&Method)->Code = Code;
  return Method;
}
//---------------------------------------------------------------------------
enum TAssemblyLanguage { alCSharp, alVBNET, alPowerShell };
extern const UnicodeString RtfPara;
extern const UnicodeString AssemblyNamespace;
extern const UnicodeString SessionClassName;
extern const UnicodeString TransferOptionsClassName;
//---------------------------------------------------------------------
UnicodeString __fastcall RtfText(const UnicodeString & Text, bool Rtf = true);
UnicodeString __fastcall RtfColor(int Index);
UnicodeString __fastcall RtfOverrideColorText(const UnicodeString & Text);
UnicodeString __fastcall RtfColorItalicText(int Color, const UnicodeString & Text);
UnicodeString __fastcall RtfColorText(int Color, const UnicodeString & Text);
UnicodeString __fastcall RtfKeyword(const UnicodeString & Text);
UnicodeString __fastcall RtfParameter(const UnicodeString & Text);
UnicodeString __fastcall RtfString(const UnicodeString & Text);
UnicodeString __fastcall RtfLink(const UnicodeString & Link, const UnicodeString & RtfText);
UnicodeString __fastcall RtfSwitch(
  const UnicodeString & Name, const UnicodeString & Link, bool Rtf = true);
UnicodeString __fastcall RtfSwitchValue(
  const UnicodeString & Name, const UnicodeString & Link, const UnicodeString & Value, bool Rtf = true);
UnicodeString __fastcall RtfSwitch(
  const UnicodeString & Name, const UnicodeString & Link, const UnicodeString & Value, bool Rtf = true);
UnicodeString __fastcall RtfSwitch(
  const UnicodeString & Name, const UnicodeString & Link, int Value, bool Rtf = true);
UnicodeString __fastcall RtfEscapeParam(UnicodeString Param);
UnicodeString __fastcall RtfRemoveHyperlinks(UnicodeString Text);
UnicodeString __fastcall ScriptCommandLink(const UnicodeString & Command);
UnicodeString __fastcall AssemblyBoolean(TAssemblyLanguage Language, bool Value);
UnicodeString __fastcall AssemblyString(TAssemblyLanguage Language, UnicodeString S);
UnicodeString __fastcall AssemblyCommentLine(TAssemblyLanguage Language, const UnicodeString & Text);
UnicodeString __fastcall AssemblyPropertyRaw(
  TAssemblyLanguage Language, const UnicodeString & ClassName, const UnicodeString & Name,
  const UnicodeString & Value, bool Inline);
UnicodeString __fastcall AssemblyProperty(
  TAssemblyLanguage Language, const UnicodeString & ClassName, const UnicodeString & Name,
  const UnicodeString & Type, const UnicodeString & Member, bool Inline);
UnicodeString __fastcall AssemblyProperty(
  TAssemblyLanguage Language, const UnicodeString & ClassName, const UnicodeString & Name,
  const UnicodeString & Value, bool Inline);
UnicodeString __fastcall AssemblyProperty(
  TAssemblyLanguage Language, const UnicodeString & ClassName, const UnicodeString & Name, int Value, bool Inline);
UnicodeString __fastcall AssemblyProperty(
  TAssemblyLanguage Language, const UnicodeString & ClassName, const UnicodeString & Name, bool Value, bool Inline);
UnicodeString __fastcall RtfLibraryMethod(const UnicodeString & ClassName, const UnicodeString & MethodName, bool Inpage);
UnicodeString __fastcall RtfLibraryClass(const UnicodeString & ClassName);
UnicodeString __fastcall AssemblyVariableName(TAssemblyLanguage Language, const UnicodeString & ClassName);
UnicodeString __fastcall AssemblyStatementSeparator(TAssemblyLanguage Language);
UnicodeString __fastcall AssemblyNewClassInstance(
  TAssemblyLanguage Language, const UnicodeString & ClassName, bool Inline);
UnicodeString __fastcall AssemblyNewClassInstanceStart(
  TAssemblyLanguage Language, const UnicodeString & ClassName, bool Inline);
UnicodeString __fastcall AssemblyNewClassInstanceEnd(TAssemblyLanguage Language, bool Inline);
//---------------------------------------------------------------------------
#include "Global.h"
//---------------------------------------------------------------------------
template<class T>
class TValueRestorer
{
public:
  __fastcall TValueRestorer(T & Target, const T & Value) :
    FTarget(Target),
    FValue(Value),
    FArmed(true)
  {
  }

  __fastcall TValueRestorer(T & Target) :
    FTarget(Target),
    FValue(Target),
    FArmed(true)
  {
  }

  void Release()
  {
    if (FArmed)
    {
      FTarget = FValue;
      FArmed = false;
    }
  }

  __fastcall ~TValueRestorer()
  {
    Release();
  }

protected:
  T & FTarget;
  T FValue;
  bool FArmed;
};
//---------------------------------------------------------------------------
class TAutoNestingCounter : public TValueRestorer<int>
{
public:
  __fastcall TAutoNestingCounter(int & Target) :
    TValueRestorer<int>(Target)
  {
    DebugAssert(Target >= 0);
    ++Target;
  }

  __fastcall ~TAutoNestingCounter()
  {
    DebugAssert(!FArmed || (FTarget == (FValue + 1)));
  }
};
//---------------------------------------------------------------------------
class TAutoFlag : public TValueRestorer<bool>
{
public:
  __fastcall TAutoFlag(bool & Target) :
    TValueRestorer<bool>(Target)
  {
    DebugAssert(!Target);
    Target = true;
  }

  __fastcall ~TAutoFlag()
  {
    DebugAssert(!FArmed || FTarget);
  }
};
//---------------------------------------------------------------------------
#include <map>
//---------------------------------------------------------------------------
template<class T1, class T2>
class BiDiMap
{
public:
  typedef std::map<T1, T2> TFirstToSecond;
  typedef TFirstToSecond::const_iterator const_iterator;

  void Add(const T1 & Value1, const T2 & Value2)
  {
    FFirstToSecond.insert(std::make_pair(Value1, Value2));
    FSecondToFirst.insert(std::make_pair(Value2, Value1));
  }

  T1 LookupFirst(const T2 & Value2) const
  {
    TSecondToFirst::const_iterator Iterator = FSecondToFirst.find(Value2);
    DebugAssert(Iterator != FSecondToFirst.end());
    return Iterator->second;
  }

  T2 LookupSecond(const T1 & Value1) const
  {
    const_iterator Iterator = FFirstToSecond.find(Value1);
    DebugAssert(Iterator != FFirstToSecond.end());
    return Iterator->second;
  }

  const_iterator begin()
  {
    return FFirstToSecond.begin();
  }

  const_iterator end()
  {
    return FFirstToSecond.end();
  }

private:
  TFirstToSecond FFirstToSecond;
  typedef std::map<T2, T1> TSecondToFirst;
  TSecondToFirst FSecondToFirst;
};
//---------------------------------------------------------------------------
typedef std::vector<UnicodeString> TUnicodeStringVector;
//---------------------------------------------------------------------------
#endif
