//---------------------------------------------------------------------------
#include <vcl.h>
#pragma hdrstop

#include "SftpFileSystem.h"

#include "PuttyTools.h"
#include "Common.h"
#include "Exceptions.h"
#include "Interface.h"
#include "Terminal.h"
#include "TextsCore.h"
#include "HelpCore.h"
#include "SecureShell.h"
#include <WideStrUtils.hpp>
#include <limits>

#include <memory>
//---------------------------------------------------------------------------
#pragma package(smart_init)
//---------------------------------------------------------------------------
#define FILE_OPERATION_LOOP_TERMINAL FTerminal
//---------------------------------------------------------------------------
#define SSH_FX_OK                                 0
#define SSH_FX_EOF                                1
#define SSH_FX_NO_SUCH_FILE                       2
#define SSH_FX_PERMISSION_DENIED                  3
#define SSH_FX_FAILURE                            4
#define SSH_FX_OP_UNSUPPORTED                     8

#define SSH_FXP_INIT               1
#define SSH_FXP_VERSION            2
#define SSH_FXP_OPEN               3
#define SSH_FXP_CLOSE              4
#define SSH_FXP_READ               5
#define SSH_FXP_WRITE              6
#define SSH_FXP_LSTAT              7
#define SSH_FXP_FSTAT              8
#define SSH_FXP_SETSTAT            9
#define SSH_FXP_FSETSTAT           10
#define SSH_FXP_OPENDIR            11
#define SSH_FXP_READDIR            12
#define SSH_FXP_REMOVE             13
#define SSH_FXP_MKDIR              14
#define SSH_FXP_RMDIR              15
#define SSH_FXP_REALPATH           16
#define SSH_FXP_STAT               17
#define SSH_FXP_RENAME             18
#define SSH_FXP_READLINK           19
#define SSH_FXP_SYMLINK            20
#define SSH_FXP_LINK               21
#define SSH_FXP_STATUS             101
#define SSH_FXP_HANDLE             102
#define SSH_FXP_DATA               103
#define SSH_FXP_NAME               104
#define SSH_FXP_ATTRS              105
#define SSH_FXP_EXTENDED           200
#define SSH_FXP_EXTENDED_REPLY     201
#define SSH_FXP_ATTRS              105

#define SSH_FILEXFER_ATTR_SIZE              0x00000001
#define SSH_FILEXFER_ATTR_UIDGID            0x00000002
#define SSH_FILEXFER_ATTR_PERMISSIONS       0x00000004
#define SSH_FILEXFER_ATTR_ACMODTIME         0x00000008
#define SSH_FILEXFER_ATTR_EXTENDED          0x80000000
#define SSH_FILEXFER_ATTR_ACCESSTIME        0x00000008
#define SSH_FILEXFER_ATTR_CREATETIME        0x00000010
#define SSH_FILEXFER_ATTR_MODIFYTIME        0x00000020
#define SSH_FILEXFER_ATTR_ACL               0x00000040
#define SSH_FILEXFER_ATTR_OWNERGROUP        0x00000080
#define SSH_FILEXFER_ATTR_SUBSECOND_TIMES   0x00000100
#define SSH_FILEXFER_ATTR_BITS              0x00000200
#define SSH_FILEXFER_ATTR_ALLOCATION_SIZE   0x00000400
#define SSH_FILEXFER_ATTR_TEXT_HINT         0x00000800
#define SSH_FILEXFER_ATTR_MIME_TYPE         0x00001000
#define SSH_FILEXFER_ATTR_LINK_COUNT        0x00002000
#define SSH_FILEXFER_ATTR_UNTRANSLATED_NAME 0x00004000
#define SSH_FILEXFER_ATTR_CTIME             0x00008000
#define SSH_FILEXFER_ATTR_EXTENDED          0x80000000

#define SSH_FILEXFER_ATTR_COMMON \
  (SSH_FILEXFER_ATTR_SIZE | SSH_FILEXFER_ATTR_OWNERGROUP | \
   SSH_FILEXFER_ATTR_PERMISSIONS | SSH_FILEXFER_ATTR_ACCESSTIME | \
   SSH_FILEXFER_ATTR_MODIFYTIME)

#define SSH_FILEXFER_TYPE_REGULAR          1
#define SSH_FILEXFER_TYPE_DIRECTORY        2
#define SSH_FILEXFER_TYPE_SYMLINK          3
#define SSH_FILEXFER_TYPE_SPECIAL          4
#define SSH_FILEXFER_TYPE_UNKNOWN          5

#define SSH_FXF_READ            0x00000001
#define SSH_FXF_WRITE           0x00000002
#define SSH_FXF_APPEND          0x00000004
#define SSH_FXF_CREAT           0x00000008
#define SSH_FXF_TRUNC           0x00000010
#define SSH_FXF_EXCL            0x00000020
#define SSH_FXF_TEXT            0x00000040

#define SSH_FXF_ACCESS_DISPOSITION        0x00000007
#define     SSH_FXF_CREATE_NEW            0x00000000
#define     SSH_FXF_CREATE_TRUNCATE       0x00000001
#define     SSH_FXF_OPEN_EXISTING         0x00000002
#define     SSH_FXF_OPEN_OR_CREATE        0x00000003
#define     SSH_FXF_TRUNCATE_EXISTING     0x00000004
#define SSH_FXF_ACCESS_APPEND_DATA        0x00000008
#define SSH_FXF_ACCESS_APPEND_DATA_ATOMIC 0x00000010
#define SSH_FXF_ACCESS_TEXT_MODE          0x00000020

#define ACE4_READ_DATA         0x00000001
#define ACE4_LIST_DIRECTORY    0x00000001
#define ACE4_WRITE_DATA        0x00000002
#define ACE4_ADD_FILE          0x00000002
#define ACE4_APPEND_DATA       0x00000004
#define ACE4_ADD_SUBDIRECTORY  0x00000004
#define ACE4_READ_NAMED_ATTRS  0x00000008
#define ACE4_WRITE_NAMED_ATTRS 0x00000010
#define ACE4_EXECUTE           0x00000020
#define ACE4_DELETE_CHILD      0x00000040
#define ACE4_READ_ATTRIBUTES   0x00000080
#define ACE4_WRITE_ATTRIBUTES  0x00000100
#define ACE4_DELETE            0x00010000
#define ACE4_READ_ACL          0x00020000
#define ACE4_WRITE_ACL         0x00040000
#define ACE4_WRITE_OWNER       0x00080000
#define ACE4_SYNCHRONIZE       0x00100000

#define SSH_FILEXFER_ATTR_FLAGS_HIDDEN           0x00000004

#define SSH_FXP_REALPATH_NO_CHECK    0x00000001
#define SSH_FXP_REALPATH_STAT_IF     0x00000002
#define SSH_FXP_REALPATH_STAT_ALWAYS 0x00000003

#define SFTP_MAX_PACKET_LEN   1024000
//---------------------------------------------------------------------------
#define SFTP_EXT_OWNER_GROUP "owner-group-query@generic-extensions"
#define SFTP_EXT_OWNER_GROUP_REPLY "owner-group-query-reply@generic-extensions"
#define SFTP_EXT_NEWLINE "newline"
#define SFTP_EXT_SUPPORTED "supported"
#define SFTP_EXT_SUPPORTED2 "supported2"
#define SFTP_EXT_FSROOTS "fs-roots@vandyke.com"
#define SFTP_EXT_VENDOR_ID "vendor-id"
#define SFTP_EXT_VERSIONS "versions"
#define SFTP_EXT_SPACE_AVAILABLE "space-available"
#define SFTP_EXT_CHECK_FILE "check-file"
#define SFTP_EXT_CHECK_FILE_NAME "check-file-name"
#define SFTP_EXT_STATVFS "statvfs@openssh.com"
#define SFTP_EXT_STATVFS_VALUE_V2 L"2"
#define SFTP_EXT_STATVFS_ST_RDONLY 0x1
#define SFTP_EXT_STATVFS_ST_NOSUID 0x2
#define SFTP_EXT_HARDLINK "hardlink@openssh.com"
#define SFTP_EXT_HARDLINK_VALUE_V1 L"1"
#define SFTP_EXT_COPY_FILE "copy-file"
//---------------------------------------------------------------------------
#define OGQ_LIST_OWNERS 0x01
#define OGQ_LIST_GROUPS 0x02
//---------------------------------------------------------------------------
const int SFTPMinVersion = 0;
const int SFTPMaxVersion = 6;
const unsigned int SFTPNoMessageNumber = static_cast<unsigned int>(-1);

const int asNo =            0;
const int asOK =            1 << SSH_FX_OK;
const int asEOF =           1 << SSH_FX_EOF;
const int asPermDenied =    1 << SSH_FX_PERMISSION_DENIED;
const int asOpUnsupported = 1 << SSH_FX_OP_UNSUPPORTED;
const int asNoSuchFile =    1 << SSH_FX_NO_SUCH_FILE;
const int asAll = 0xFFFF;

const int tfFirstLevel =   0x01;
const int tfNewDirectory = 0x02;
//---------------------------------------------------------------------------
#define GET_32BIT(cp) \
    (((unsigned long)(unsigned char)(cp)[0] << 24) | \
    ((unsigned long)(unsigned char)(cp)[1] << 16) | \
    ((unsigned long)(unsigned char)(cp)[2] << 8) | \
    ((unsigned long)(unsigned char)(cp)[3]))

#define PUT_32BIT(cp, value) { \
    (cp)[0] = (unsigned char)((value) >> 24); \
    (cp)[1] = (unsigned char)((value) >> 16); \
    (cp)[2] = (unsigned char)((value) >> 8); \
    (cp)[3] = (unsigned char)(value); }
//---------------------------------------------------------------------------
#define SFTP_PACKET_ALLOC_DELTA 256
//---------------------------------------------------------------------------
#pragma warn -inl
//---------------------------------------------------------------------------
struct TSFTPSupport
{
  TSFTPSupport() :
    AttribExtensions(new TStringList()),
    Extensions(new TStringList())
  {
    Reset();
  }

  ~TSFTPSupport()
  {
    delete AttribExtensions;
    delete Extensions;
  }

  void Reset()
  {
    AttributeMask = 0;
    AttributeBits = 0;
    OpenFlags = 0;
    AccessMask = 0;
    MaxReadSize = 0;
    OpenBlockVector = 0;
    BlockVector = 0;
    AttribExtensions->Clear();
    Extensions->Clear();
    Loaded = false;
  }

  unsigned int AttributeMask;
  unsigned int AttributeBits;
  unsigned int OpenFlags;
  unsigned int AccessMask;
  unsigned int MaxReadSize;
  unsigned int OpenBlockVector;
  unsigned int BlockVector;
  TStrings * AttribExtensions;
  TStrings * Extensions;
  bool Loaded;
};
//---------------------------------------------------------------------------
class TSFTPPacket
{
public:
  TSFTPPacket()
  {
    Init();
  }

  TSFTPPacket(const TSFTPPacket & Source)
  {
    Init();
    *this = Source;
  }

  TSFTPPacket(unsigned char AType)
  {
    Init();
    ChangeType(AType);
  }

  TSFTPPacket(const unsigned char * Source, unsigned int Len)
  {
    Init();
    FLength = Len;
    Capacity = FLength;
    memcpy(Data, Source, Len);
  }

  TSFTPPacket(const RawByteString & Source)
  {
    Init();
    FLength = Source.Length();
    Capacity = FLength;
    memcpy(Data, Source.c_str(), Source.Length());
  }

  ~TSFTPPacket()
  {
    if (FData != NULL)
    {
      delete[] (FData - FSendPrefixLen);
    }
    if (FReservedBy) FReservedBy->UnreserveResponse(this);
  }

  void ChangeType(unsigned char AType)
  {
    FPosition = 0;
    FLength = 0;
    Capacity = 0;
    FType = AType;
    AddByte(FType);
    if ((FType != 1) && (FType != SSH_FXP_INIT))
    {
      AssignNumber();
      AddCardinal(FMessageNumber);
    }
  }

  void Reuse()
  {
    AssignNumber();

    DebugAssert(Length >= 5);

    // duplicated in AddCardinal()
    unsigned char Buf[4];
    PUT_32BIT(Buf, FMessageNumber);

    memcpy(FData + 1, Buf, sizeof(Buf));
  }

  void AddByte(unsigned char Value)
  {
    Add(&Value, sizeof(Value));
  }

  void AddBool(bool Value)
  {
    AddByte(Value ? 1 : 0);
  }

  void AddCardinal(unsigned long Value)
  {
    // duplicated in Reuse()
    unsigned char Buf[4];
    PUT_32BIT(Buf, Value);
    Add(&Buf, sizeof(Buf));
  }

  void AddInt64(__int64 Value)
  {
    AddCardinal((unsigned long)(Value >> 32));
    AddCardinal((unsigned long)(Value & 0xFFFFFFFF));
  }

  void AddData(const void * Data, int ALength)
  {
    AddCardinal(ALength);
    Add(Data, ALength);
  }

  void AddString(const RawByteString & Value)
  {
    AddCardinal(Value.Length());
    Add(Value.c_str(), Value.Length());
  }

  inline void AddUtfString(const UTF8String & Value)
  {
    AddString(Value);
  }

  inline void AddUtfString(const UnicodeString & Value)
  {
    AddUtfString(UTF8String(Value));
  }

  inline void AddString(const UnicodeString Value, TAutoSwitch Utf)
  {
    // asAuto: Using UTF until we receive non-UTF string from the server
    if ((Utf == asOn) || (Utf == asAuto))
    {
      AddUtfString(Value);
    }
    else
    {
      AddString(RawByteString(AnsiString(Value)));
    }
  }

  // now purposeless alias to AddString
  inline void AddPathString(const UnicodeString & Value, TAutoSwitch Utf)
  {
    AddString(Value, Utf);
  }

  unsigned int AllocationSizeAttribute(int Version)
  {
    return (Version >= 6) ? SSH_FILEXFER_ATTR_ALLOCATION_SIZE : SSH_FILEXFER_ATTR_SIZE;
  }

  void AddProperties(unsigned short * Rights, TRemoteToken * Owner,
    TRemoteToken * Group, __int64 * MTime, __int64 * ATime,
    __int64 * Size, bool IsDirectory, int Version, TAutoSwitch Utf)
  {
    int Flags = 0;
    if (Size != NULL)
    {
      Flags |= AllocationSizeAttribute(Version);
    }
    // both or neither
    DebugAssert((Owner != NULL) == (Group != NULL));
    if ((Owner != NULL) && (Group != NULL))
    {
      if (Version < 4)
      {
        DebugAssert(Owner->IDValid && Group->IDValid);
        Flags |= SSH_FILEXFER_ATTR_UIDGID;
      }
      else
      {
        DebugAssert(Owner->NameValid && Group->NameValid);
        Flags |= SSH_FILEXFER_ATTR_OWNERGROUP;
      }
    }
    if (Rights != NULL)
    {
      Flags |= SSH_FILEXFER_ATTR_PERMISSIONS;
    }
    if ((Version < 4) && ((MTime != NULL) || (ATime != NULL)))
    {
      Flags |= SSH_FILEXFER_ATTR_ACMODTIME;
    }
    if ((Version >= 4) && (ATime != NULL))
    {
      Flags |= SSH_FILEXFER_ATTR_ACCESSTIME;
    }
    if ((Version >= 4) && (MTime != NULL))
    {
      Flags |= SSH_FILEXFER_ATTR_MODIFYTIME;
    }
    AddCardinal(Flags);

    if (Version >= 4)
    {
      AddByte(static_cast<unsigned char>(IsDirectory ?
        SSH_FILEXFER_TYPE_DIRECTORY : SSH_FILEXFER_TYPE_REGULAR));
    }

    if (Size != NULL)
    {
      // this is SSH_FILEXFER_ATTR_SIZE for version <= 5, but
      // SSH_FILEXFER_ATTR_ALLOCATION_SIZE for version >= 6
      AddInt64(*Size);
    }

    if ((Owner != NULL) && (Group != NULL))
    {
      if (Version < 4)
      {
        DebugAssert(Owner->IDValid && Group->IDValid);
        AddCardinal(Owner->ID);
        AddCardinal(Group->ID);
      }
      else
      {
        DebugAssert(Owner->NameValid && Group->NameValid);
        AddString(Owner->Name, Utf);
        AddString(Group->Name, Utf);
      }
    }

    if (Rights != NULL)
    {
      AddCardinal(*Rights);
    }

    if ((Version < 4) && ((MTime != NULL) || (ATime != NULL)))
    {
      // any way to reflect sbSignedTS here?
      // (note that casting __int64 > 2^31 < 2^32 to unsigned long is wrapped,
      // thus we never can set time after 2038, even if the server supports it)
      AddCardinal(static_cast<unsigned long>(ATime != NULL ? *ATime : *MTime));
      AddCardinal(static_cast<unsigned long>(MTime != NULL ? *MTime : *ATime));
    }
    if ((Version >= 4) && (ATime != NULL))
    {
      AddInt64(*ATime);
    }
    if ((Version >= 4) && (MTime != NULL))
    {
      AddInt64(*MTime);
    }
  }

  void AddProperties(const TRemoteProperties * Properties,
    unsigned short BaseRights, bool IsDirectory, int Version, TAutoSwitch Utf,
    TChmodSessionAction * Action)
  {
    enum TValid { valNone = 0, valRights = 0x01, valOwner = 0x02, valGroup = 0x04,
      valMTime = 0x08, valATime = 0x10 } Valid = valNone;
    unsigned short RightsNum = 0;
    TRemoteToken Owner;
    TRemoteToken Group;
    __int64 MTime;
    __int64 ATime;

    if (Properties != NULL)
    {
      if (Properties->Valid.Contains(vpGroup))
      {
        Valid = (TValid)(Valid | valGroup);
        Group = Properties->Group;
      }

      if (Properties->Valid.Contains(vpOwner))
      {
        Valid = (TValid)(Valid | valOwner);
        Owner = Properties->Owner;
      }

      if (Properties->Valid.Contains(vpRights))
      {
        Valid = (TValid)(Valid | valRights);
        TRights Rights = BaseRights;
        Rights |= Properties->Rights.NumberSet;
        Rights &= (unsigned short)~Properties->Rights.NumberUnset;
        if (IsDirectory && Properties->AddXToDirectories)
        {
          Rights.AddExecute();
        }
        RightsNum = Rights;

        if (Action != NULL)
        {
          Action->Rights(Rights);
        }
      }

      if (Properties->Valid.Contains(vpLastAccess))
      {
        Valid = (TValid)(Valid | valATime);
        ATime = Properties->LastAccess;
      }

      if (Properties->Valid.Contains(vpModification))
      {
        Valid = (TValid)(Valid | valMTime);
        MTime = Properties->Modification;
      }
    }

    AddProperties(
      Valid & valRights ? &RightsNum : NULL,
      Valid & valOwner ? &Owner : NULL,
      Valid & valGroup ? &Group : NULL,
      Valid & valMTime ? &MTime : NULL,
      Valid & valATime ? &ATime : NULL,
      NULL, IsDirectory, Version, Utf);
  }

  unsigned char GetByte()
  {
    Need(sizeof(unsigned char));
    unsigned char Result = FData[FPosition];
    DataConsumed(sizeof(unsigned char));
    return Result;
  }

  bool GetBool()
  {
    return (GetByte() != 0);
  }

  bool CanGetBool()
  {
    return (RemainingLength >= sizeof(unsigned char));
  }

  unsigned long GetCardinal()
  {
    unsigned long Result = PeekCardinal();
    DataConsumed(sizeof(Result));
    return Result;
  }

  bool CanGetCardinal()
  {
    return (RemainingLength >= sizeof(unsigned long));
  }

  unsigned long GetSmallCardinal()
  {
    unsigned long Result;
    Need(2);
    Result = (FData[FPosition] << 8) + FData[FPosition + 1];
    DataConsumed(2);
    return Result;
  }

  bool CanGetSmallCardinal()
  {
    return (RemainingLength >= 2);
  }

  __int64 GetInt64()
  {
    __int64 Hi = GetCardinal();
    __int64 Lo = GetCardinal();
    return (Hi << 32) + Lo;
  }

  RawByteString GetRawByteString()
  {
    RawByteString Result;
    unsigned long Len = GetCardinal();
    Need(Len);
    // cannot happen anyway as Need() would raise exception
    DebugAssert(Len < SFTP_MAX_PACKET_LEN);
    Result.SetLength(Len);
    memcpy(Result.c_str(), FData + FPosition, Len);
    DataConsumed(Len);
    return Result;
  }

  bool CanGetString(unsigned int & Size)
  {
    bool Result = CanGetCardinal();
    if (Result)
    {
      unsigned long Len = PeekCardinal();
      Size = (sizeof(Len) + Len);
      Result = (Size <= RemainingLength);
    }
    return Result;
  }

  // For reading strings that are character strings (not byte strings as
  // as file handles), and SFTP spec does not say explicitly that they
  // are in UTF. For most of them it actually does not matter as
  // the content should be pure ASCII (e.g. extension names, etc.)
  inline UnicodeString GetAnsiString()
  {
    return AnsiToString(GetRawByteString());
  }

  inline RawByteString GetFileHandle()
  {
    return GetRawByteString();
  }

  inline UnicodeString GetString(TAutoSwitch & Utf)
  {
    if (Utf != asOff)
    {
      return GetUtfString(Utf);
    }
    else
    {
      return GetAnsiString();
    }
  }

  // now purposeless alias to GetString(bool)
  inline UnicodeString GetPathString(TAutoSwitch & Utf)
  {
    return GetString(Utf);
  }

  void GetFile(TRemoteFile * File, int Version, TDSTMode DSTMode, TAutoSwitch & Utf, bool SignedTS, bool Complete)
  {
    DebugAssert(File);
    unsigned int Flags;
    UnicodeString ListingStr;
    unsigned long Permissions = 0;
    bool ParsingFailed = false;
    if (Type != SSH_FXP_ATTRS)
    {
      File->FileName = GetPathString(Utf);
      if (Version < 4)
      {
        ListingStr = GetString(Utf);
      }
    }
    Flags = GetCardinal();
    if (Version >= 4)
    {
      unsigned char FXType = GetByte();
      // -:regular, D:directory, L:symlink, S:special, U:unknown
      // O:socket, C:char device, B:block device, F:fifo

      // SSH-2.0-cryptlib returns file type 0 in response to SSH_FXP_LSTAT,
      // handle this undefined value as "unknown"
      static wchar_t * Types = L"U-DLSUOCBF";
      if (FXType > (unsigned char)wcslen(Types))
      {
        throw Exception(FMTLOAD(SFTP_UNKNOWN_FILE_TYPE, (int(FXType))));
      }
      File->Type = Types[FXType];
    }
    if (Flags & SSH_FILEXFER_ATTR_SIZE)
    {
      File->Size = GetInt64();
    }
    // SFTP-6 only
    if (Flags & SSH_FILEXFER_ATTR_ALLOCATION_SIZE)
    {
      GetInt64(); // skip
    }
    // SSH-2.0-3.2.0 F-SECURE SSH - Process Software MultiNet
    // sets SSH_FILEXFER_ATTR_UIDGID for v4, but does not include the UID/GUID
    if ((Flags & SSH_FILEXFER_ATTR_UIDGID) && (Version < 4))
    {
      File->Owner.ID = GetCardinal();
      File->Group.ID = GetCardinal();
    }
    if (Flags & SSH_FILEXFER_ATTR_OWNERGROUP)
    {
      DebugAssert(Version >= 4);
      File->Owner.Name = GetString(Utf);
      File->Group.Name = GetString(Utf);
    }
    if (Flags & SSH_FILEXFER_ATTR_PERMISSIONS)
    {
      Permissions = GetCardinal();
    }
    if (Version < 4)
    {
      if (Flags & SSH_FILEXFER_ATTR_ACMODTIME)
      {
        File->LastAccess = UnixToDateTime(
          SignedTS ?
            static_cast<__int64>(static_cast<signed long>(GetCardinal())) :
            static_cast<__int64>(GetCardinal()),
          DSTMode);
        File->Modification = UnixToDateTime(
          SignedTS ?
            static_cast<__int64>(static_cast<signed long>(GetCardinal())) :
            static_cast<__int64>(GetCardinal()),
          DSTMode);
      }
    }
    else
    {
      if (Flags & SSH_FILEXFER_ATTR_ACCESSTIME)
      {
        File->LastAccess = UnixToDateTime(GetInt64(), DSTMode);
        if (Flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES)
        {
          GetCardinal(); // skip access time subseconds
        }
      }
      else
      {
        File->LastAccess = Now();
      }
      if (Flags & SSH_FILEXFER_ATTR_CREATETIME)
      {
        GetInt64(); // skip create time
        if (Flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES)
        {
          GetCardinal(); // skip create time subseconds
        }
      }
      if (Flags & SSH_FILEXFER_ATTR_MODIFYTIME)
      {
        File->Modification = UnixToDateTime(GetInt64(), DSTMode);
        if (Flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES)
        {
          GetCardinal(); // skip modification time subseconds
        }
      }
      else
      {
        File->Modification = Now();
      }
      // SFTP-6
      if (Flags & SSH_FILEXFER_ATTR_CTIME)
      {
        GetInt64(); // skip attribute modification time
        if (Flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES)
        {
          GetCardinal(); // skip attribute modification time subseconds
        }
      }
    }

    if (Flags & SSH_FILEXFER_ATTR_ACL)
    {
      GetRawByteString();
    }

    if (Flags & SSH_FILEXFER_ATTR_BITS)
    {
      // while SSH_FILEXFER_ATTR_BITS is defined for SFTP5 only, vandyke 2.3.3 sets it
      // for SFTP4 as well
      unsigned long Bits = GetCardinal();
      if (Version >= 6)
      {
        unsigned long BitsValid = GetCardinal();
        Bits = Bits & BitsValid;
      }
      if (FLAGSET(Bits, SSH_FILEXFER_ATTR_FLAGS_HIDDEN))
      {
        File->IsHidden = true;
      }
    }

    // skip some SFTP-6 only fields
    if (Flags & SSH_FILEXFER_ATTR_TEXT_HINT)
    {
      GetByte();
    }
    if (Flags & SSH_FILEXFER_ATTR_MIME_TYPE)
    {
      GetAnsiString();
    }
    if (Flags & SSH_FILEXFER_ATTR_LINK_COUNT)
    {
      GetCardinal();
    }
    if (Flags & SSH_FILEXFER_ATTR_UNTRANSLATED_NAME)
    {
      GetPathString(Utf);
    }

    if ((Version < 4) && (Type != SSH_FXP_ATTRS))
    {
      try
      {
        // update permissions and user/group name
        // modification time and filename is ignored
        File->ListingStr = ListingStr;
      }
      catch(...)
      {
        // ignore any error while parsing listing line,
        // SFTP specification do not recommend to parse it
        ParsingFailed = true;
      }
    }

    if (Type == SSH_FXP_ATTRS || Version >= 4 || ParsingFailed)
    {
      wchar_t Type = FILETYPE_DEFAULT;
      if (FLAGSET(Flags, SSH_FILEXFER_ATTR_PERMISSIONS))
      {
        File->Rights->Number = (unsigned short)(Permissions & TRights::rfAllSpecials);
        if (FLAGSET(Permissions, TRights::rfDirectory))
        {
          Type = FILETYPE_DIRECTORY;
        }
      }

      if (Version < 4)
      {
        File->Type = Type;
      }
    }

    if (Flags & SSH_FILEXFER_ATTR_EXTENDED)
    {
      unsigned int ExtendedCount = GetCardinal();
      for (unsigned int Index = 0; Index < ExtendedCount; Index++)
      {
        GetRawByteString(); // skip extended_type
        GetRawByteString(); // skip extended_data
      }
    }

    if (Complete)
    {
      File->Complete();
    }
  }

  unsigned char * GetNextData(unsigned int Size = 0)
  {
    if (Size > 0)
    {
      Need(Size);
    }
    return FPosition < FLength ? FData + FPosition : NULL;
  }

  void DataConsumed(unsigned int Size)
  {
    FPosition += Size;
  }

  void DataUpdated(int ALength)
  {
    FPosition = 0;
    FLength = ALength;
    FType = GetByte();
    if (FType != SSH_FXP_VERSION)
    {
      FMessageNumber = GetCardinal();
    }
    else
    {
      FMessageNumber = SFTPNoMessageNumber;
    }
  }

  void LoadFromFile(const UnicodeString FileName)
  {
    TStringList * DumpLines = new TStringList();
    RawByteString Dump;
    try
    {
      DumpLines->LoadFromFile(FileName);
      Dump = RawByteString(AnsiString(DumpLines->Text));
    }
    __finally
    {
      delete DumpLines;
    }

    Capacity = 20480;
    unsigned char Byte[3];
    memset(Byte, '\0', sizeof(Byte));
    int Index = 1;
    unsigned int Length = 0;
    while (Index < Dump.Length())
    {
      char C = Dump[Index];
      if (IsHex(C))
      {
        if (Byte[0] == '\0')
        {
          Byte[0] = C;
        }
        else
        {
          Byte[1] = C;
          DebugAssert(Length < Capacity);
          Data[Length] = HexToByte(UnicodeString(reinterpret_cast<char *>(Byte)));
          Length++;
          memset(Byte, '\0', sizeof(Byte));
        }
      }
      Index++;
    }
    DataUpdated(Length);
  }

  UnicodeString __fastcall Dump() const
  {
    UnicodeString Result;
    for (unsigned int Index = 0; Index < Length; Index++)
    {
      Result += ByteToHex(Data[Index]) + L",";
      if (((Index + 1) % 25) == 0)
      {
        Result += L"\n";
      }
    }
    return Result;
  }

  TSFTPPacket & operator = (const TSFTPPacket & Source)
  {
    Capacity = 0;
    Add(Source.Data, Source.Length);
    DataUpdated(Source.Length);
    FPosition = Source.FPosition;
    return *this;
  }

  __property unsigned int Length = { read = FLength };
  __property unsigned int RemainingLength = { read = GetRemainingLength };
  __property unsigned char * Data = { read = FData };
  __property unsigned char * SendData = { read = GetSendData };
  __property unsigned int SendLength = { read = GetSendLength };
  __property unsigned int Capacity = { read = FCapacity, write = SetCapacity };
  __property unsigned char Type = { read = FType };
  __property unsigned char RequestType = { read = GetRequestType };
  __property unsigned int MessageNumber = { read = FMessageNumber, write = FMessageNumber };
  __property TSFTPFileSystem * ReservedBy = { read = FReservedBy, write = FReservedBy };
  __property UnicodeString TypeName = { read = GetTypeName };

private:
  unsigned char * FData;
  unsigned int FLength;
  unsigned int FCapacity;
  unsigned int FPosition;
  unsigned char FType;
  unsigned int FMessageNumber;
  TSFTPFileSystem * FReservedBy;

  static int FMessageCounter;
  static const FSendPrefixLen = 4;

  void Init()
  {
    FData = NULL;
    FCapacity = 0;
    FLength = 0;
    FPosition = 0;
    FMessageNumber = SFTPNoMessageNumber;
    FType = -1;
    FReservedBy = NULL;
  }

  void AssignNumber()
  {
    // this is not strictly thread-safe, but as it is accessed from multiple
    // threads only for multiple connection, it is not problem if two threads get
    // the same number
    FMessageNumber = (FMessageCounter << 8) + FType;
    FMessageCounter++;
  }

  unsigned char GetRequestType()
  {
    if (FMessageNumber != SFTPNoMessageNumber)
    {
      return (unsigned char)(FMessageNumber & 0xFF);
    }
    else
    {
      DebugAssert(Type == SSH_FXP_VERSION);
      return SSH_FXP_INIT;
    }
  }

  inline void Add(const void * AData, int ALength)
  {
    if (Length + ALength > Capacity)
    {
      Capacity = Length + ALength + SFTP_PACKET_ALLOC_DELTA;
    }
    memcpy(FData + Length, AData, ALength);
    FLength += ALength;
  }

  void SetCapacity(unsigned int ACapacity)
  {
    if (ACapacity != Capacity)
    {
      FCapacity = ACapacity;
      if (FCapacity > 0)
      {
        unsigned char * NData = (new unsigned char[FCapacity + FSendPrefixLen]) + FSendPrefixLen;
        if (FData)
        {
          memcpy(NData - FSendPrefixLen, FData - FSendPrefixLen,
            (FLength < FCapacity ? FLength : FCapacity) + FSendPrefixLen);
          delete[] (FData - FSendPrefixLen);
        }
        FData = NData;
      }
      else
      {
        if (FData) delete[] (FData - FSendPrefixLen);
        FData = NULL;
      }
      if (FLength > FCapacity) FLength = FCapacity;
    }
  }

  UnicodeString GetTypeName() const
  {
    #define TYPE_CASE(TYPE) case TYPE: return TEXT(#TYPE)
    switch (Type) {
      TYPE_CASE(SSH_FXP_INIT);
      TYPE_CASE(SSH_FXP_VERSION);
      TYPE_CASE(SSH_FXP_OPEN);
      TYPE_CASE(SSH_FXP_CLOSE);
      TYPE_CASE(SSH_FXP_READ);
      TYPE_CASE(SSH_FXP_WRITE);
      TYPE_CASE(SSH_FXP_LSTAT);
      TYPE_CASE(SSH_FXP_FSTAT);
      TYPE_CASE(SSH_FXP_SETSTAT);
      TYPE_CASE(SSH_FXP_FSETSTAT);
      TYPE_CASE(SSH_FXP_OPENDIR);
      TYPE_CASE(SSH_FXP_READDIR);
      TYPE_CASE(SSH_FXP_REMOVE);
      TYPE_CASE(SSH_FXP_MKDIR);
      TYPE_CASE(SSH_FXP_RMDIR);
      TYPE_CASE(SSH_FXP_REALPATH);
      TYPE_CASE(SSH_FXP_STAT);
      TYPE_CASE(SSH_FXP_RENAME);
      TYPE_CASE(SSH_FXP_READLINK);
      TYPE_CASE(SSH_FXP_SYMLINK);
      TYPE_CASE(SSH_FXP_LINK);
      TYPE_CASE(SSH_FXP_STATUS);
      TYPE_CASE(SSH_FXP_HANDLE);
      TYPE_CASE(SSH_FXP_DATA);
      TYPE_CASE(SSH_FXP_NAME);
      TYPE_CASE(SSH_FXP_ATTRS);
      TYPE_CASE(SSH_FXP_EXTENDED);
      TYPE_CASE(SSH_FXP_EXTENDED_REPLY);
      default:
        return FORMAT(L"Unknown message (%d)", (int(Type)));
    }
  }

  unsigned char * GetSendData() const
  {
    unsigned char * Result = FData - FSendPrefixLen;
    // this is not strictly const-object operation
    PUT_32BIT(Result, Length);
    return Result;
  }

  unsigned int GetSendLength() const
  {
    return FSendPrefixLen + Length;
  }

  unsigned int GetRemainingLength() const
  {
    return Length - FPosition;
  }

  inline void Need(unsigned int Size)
  {
    if (Size > RemainingLength)
    {
      throw Exception(FMTLOAD(SFTP_PACKET_ERROR, (int(FPosition), int(Size), int(FLength))));
    }
  }

  unsigned long PeekCardinal()
  {
    unsigned long Result;
    Need(sizeof(Result));
    Result = GET_32BIT(FData + FPosition);
    return Result;
  }

  inline UnicodeString GetUtfString(TAutoSwitch & Utf)
  {
    DebugAssert(Utf != asOff);
    UnicodeString Result;
    RawByteString S = GetRawByteString();

    if (Utf == asAuto)
    {
      TEncodeType EncodeType = DetectUTF8Encoding(S);
      if (EncodeType == etANSI)
      {
        Utf = asOff;
        Result = AnsiToString(S);
      }
    }

    if (Utf != asOff)
    {
      Result = UTF8ToString(S);
    }

    return Result;
  }
};
//---------------------------------------------------------------------------
int TSFTPPacket::FMessageCounter = 0;
//---------------------------------------------------------------------------
class TSFTPQueue
{
public:
  __fastcall TSFTPQueue(TSFTPFileSystem * AFileSystem)
  {
    FFileSystem = AFileSystem;
    DebugAssert(FFileSystem);
    FRequests = new TList();
    FResponses = new TList();
  }

  virtual __fastcall ~TSFTPQueue()
  {
    TSFTPQueuePacket * Request;
    TSFTPPacket * Response;

    DebugAssert(FResponses->Count == FRequests->Count);
    for (int Index = 0; Index < FRequests->Count; Index++)
    {
      Request = static_cast<TSFTPQueuePacket*>(FRequests->Items[Index]);
      DebugAssert(Request);
      delete Request;

      Response = static_cast<TSFTPPacket*>(FResponses->Items[Index]);
      DebugAssert(Response);
      delete Response;
    }
    delete FRequests;
    delete FResponses;
  }

  bool __fastcall Init()
  {
    return SendRequests();
  }

  virtual void __fastcall Dispose()
  {
    DebugAssert(FFileSystem->FTerminal->Active);

    TSFTPQueuePacket * Request;
    TSFTPPacket * Response;

    while (FRequests->Count)
    {
      DebugAssert(FResponses->Count);

      Request = static_cast<TSFTPQueuePacket*>(FRequests->Items[0]);
      DebugAssert(Request);

      Response = static_cast<TSFTPPacket*>(FResponses->Items[0]);
      DebugAssert(Response);

      try
      {
        ReceiveResponse(Request, Response);
      }
      catch(Exception & E)
      {
        if (FFileSystem->FTerminal->Active)
        {
          FFileSystem->FTerminal->LogEvent(L"Error while disposing the SFTP queue.");
          FFileSystem->FTerminal->Log->AddException(&E);
        }
        else
        {
          FFileSystem->FTerminal->LogEvent(L"Fatal error while disposing the SFTP queue.");
          throw;
        }
      }

      FRequests->Delete(0);
      delete Request;
      FResponses->Delete(0);
      delete Response;
    }
  }

  void __fastcall DisposeSafe()
  {
    if (FFileSystem->FTerminal->Active)
    {
      Dispose();
    }
  }

  bool __fastcall ReceivePacket(TSFTPPacket * Packet,
    int ExpectedType = -1, int AllowStatus = -1, void ** Token = NULL, bool TryOnly = false)
  {
    DebugAssert(FRequests->Count);
    bool Result;
    TSFTPQueuePacket * Request = NULL;
    TSFTPPacket * Response = NULL;
    try
    {
      Request = static_cast<TSFTPQueuePacket*>(FRequests->Items[0]);
      FRequests->Delete(0);
      DebugAssert(Request);
      if (Token != NULL)
      {
        *Token = Request->Token;
      }

      Response = static_cast<TSFTPPacket*>(FResponses->Items[0]);
      FResponses->Delete(0);
      DebugAssert(Response);

      ReceiveResponse(Request, Response, ExpectedType, AllowStatus, TryOnly);

      if ((Response->Capacity == 0) && DebugAlwaysTrue(TryOnly))
      {
        FRequests->Insert(0, Request);
        Request = NULL;
        FResponses->Insert(0, Response);
        Response = NULL;
        Result = true;
      }
      else
      {
        if (Packet)
        {
          *Packet = *Response;
        }

        Result = !End(Response);
        if (Result)
        {
          SendRequests();
        }
      }
    }
    __finally
    {
      delete Request;
      delete Response;
    }

    return Result;
  }

  bool __fastcall Next(int ExpectedType = -1, int AllowStatus = -1)
  {
    return ReceivePacket(NULL, ExpectedType, AllowStatus);
  }

protected:
  TList * FRequests;
  TList * FResponses;
  TSFTPFileSystem * FFileSystem;

  class TSFTPQueuePacket : public TSFTPPacket
  {
  public:
    TSFTPQueuePacket() :
      TSFTPPacket()
    {
      Token = NULL;
    }

    void * Token;
  };

  virtual bool __fastcall InitRequest(TSFTPQueuePacket * Request) = 0;

  virtual bool __fastcall End(TSFTPPacket * Response) = 0;

  virtual void __fastcall SendPacket(TSFTPQueuePacket * Packet)
  {
    FFileSystem->SendPacket(Packet);
  }

  virtual void __fastcall ReceiveResponse(
    const TSFTPPacket * Packet, TSFTPPacket * Response, int ExpectedType = -1,
    int AllowStatus = -1, bool TryOnly = false)
  {
    FFileSystem->ReceiveResponse(Packet, Response, ExpectedType, AllowStatus, TryOnly);
  }

  // sends as many requests as allowed by implementation
  virtual bool SendRequests() = 0;

  virtual bool __fastcall SendRequest()
  {
    TSFTPQueuePacket * Request = NULL;
    try
    {
      Request = new TSFTPQueuePacket();
      if (!InitRequest(Request))
      {
        delete Request;
        Request = NULL;
      }
    }
    catch(...)
    {
      delete Request;
      throw;
    }

    if (Request != NULL)
    {
      TSFTPPacket * Response = new TSFTPPacket();
      FRequests->Add(Request);
      FResponses->Add(Response);

      // make sure the response is reserved before actually ending the message
      // as we may receive response asynchronously before SendPacket finishes
      FFileSystem->ReserveResponse(Request, Response);
      SendPacket(Request);
    }

    return (Request != NULL);
  }
};
//---------------------------------------------------------------------------
class TSFTPFixedLenQueue : public TSFTPQueue
{
public:
  __fastcall TSFTPFixedLenQueue(TSFTPFileSystem * AFileSystem) : TSFTPQueue(AFileSystem)
  {
    FMissedRequests = 0;
  }
  virtual __fastcall ~TSFTPFixedLenQueue(){}

  bool Init(int QueueLen)
  {
    FMissedRequests = QueueLen - 1;
    return TSFTPQueue::Init();
  }

protected:
  int FMissedRequests;

  // sends as many requests as allowed by implementation
  virtual bool SendRequests()
  {
    bool Result = false;
    FMissedRequests++;
    while ((FMissedRequests > 0) && SendRequest())
    {
      Result = true;
      FMissedRequests--;
    }
    return Result;
  }
};
//---------------------------------------------------------------------------
class TSFTPAsynchronousQueue : public TSFTPQueue
{
public:
  __fastcall TSFTPAsynchronousQueue(TSFTPFileSystem * AFileSystem) : TSFTPQueue(AFileSystem)
  {
    FFileSystem->FSecureShell->RegisterReceiveHandler(ReceiveHandler);
    FReceiveHandlerRegistered = true;
  }

  virtual __fastcall ~TSFTPAsynchronousQueue()
  {
    UnregisterReceiveHandler();
  }

  virtual void __fastcall Dispose()
  {
    // we do not want to receive asynchronous notifications anymore,
    // while waiting synchronously for pending responses
    UnregisterReceiveHandler();
    TSFTPQueue::Dispose();
  }

  bool __fastcall Continue()
  {
    return SendRequest();
  }

protected:

  // event handler for incoming data
  void __fastcall ReceiveHandler(TObject * /*Sender*/)
  {
    while (// optimization only as we call ReceivePacket with TryOnly anyway
           FFileSystem->PeekPacket() &&
           ReceivePacketAsynchronously())
    {
      // loop
    }
  }

  virtual bool __fastcall ReceivePacketAsynchronously() = 0;

  // sends as many requests as allowed by implementation
  virtual bool SendRequests()
  {
    // noop
    return true;
  }

  void __fastcall UnregisterReceiveHandler()
  {
    if (FReceiveHandlerRegistered)
    {
      FReceiveHandlerRegistered = false;
      FFileSystem->FSecureShell->UnregisterReceiveHandler(ReceiveHandler);
    }
  }

private:
  bool FReceiveHandlerRegistered;
};
//---------------------------------------------------------------------------
class TSFTPDownloadQueue : public TSFTPFixedLenQueue
{
public:
  TSFTPDownloadQueue(TSFTPFileSystem * AFileSystem) :
    TSFTPFixedLenQueue(AFileSystem)
  {
  }
  virtual __fastcall ~TSFTPDownloadQueue(){}

  bool __fastcall Init(int QueueLen, const RawByteString & AHandle,__int64 ATransfered,
    TFileOperationProgressType * AOperationProgress)
  {
    FHandle = AHandle;
    FTransfered = ATransfered;
    OperationProgress = AOperationProgress;

    return TSFTPFixedLenQueue::Init(QueueLen);
  }

  void __fastcall InitFillGapRequest(__int64 Offset, unsigned long Missing,
    TSFTPPacket * Packet)
  {
    InitRequest(Packet, Offset, Missing);
  }

  bool __fastcall ReceivePacket(TSFTPPacket * Packet, unsigned long & BlockSize)
  {
    void * Token;
    bool Result = TSFTPFixedLenQueue::ReceivePacket(Packet, SSH_FXP_DATA, asEOF, &Token);
    BlockSize = reinterpret_cast<unsigned long>(Token);
    return Result;
  }

protected:
  virtual bool __fastcall InitRequest(TSFTPQueuePacket * Request)
  {
    unsigned int BlockSize = FFileSystem->DownloadBlockSize(OperationProgress);
    InitRequest(Request, FTransfered, BlockSize);
    Request->Token = reinterpret_cast<void*>(BlockSize);
    FTransfered += BlockSize;
    return true;
  }

  void __fastcall InitRequest(TSFTPPacket * Request, __int64 Offset,
    unsigned long Size)
  {
    Request->ChangeType(SSH_FXP_READ);
    Request->AddString(FHandle);
    Request->AddInt64(Offset);
    Request->AddCardinal(Size);
  }

  virtual bool __fastcall End(TSFTPPacket * Response)
  {
    return (Response->Type != SSH_FXP_DATA);
  }

private:
  TFileOperationProgressType * OperationProgress;
  __int64 FTransfered;
  RawByteString FHandle;
};
//---------------------------------------------------------------------------
class TSFTPUploadQueue : public TSFTPAsynchronousQueue
{
public:
  TSFTPUploadQueue(TSFTPFileSystem * AFileSystem) :
    TSFTPAsynchronousQueue(AFileSystem)
  {
    FStream = NULL;
    OperationProgress = NULL;
    FLastBlockSize = 0;
    FEnd = false;
    FConvertToken = false;
  }

  virtual __fastcall ~TSFTPUploadQueue()
  {
    delete FStream;
  }

  bool __fastcall Init(const UnicodeString AFileName,
    HANDLE AFile, TFileOperationProgressType * AOperationProgress,
    const RawByteString AHandle, __int64 ATransfered,
    int ConvertParams)
  {
    FFileName = AFileName;
    FStream = new TSafeHandleStream((THandle)AFile);
    OperationProgress = AOperationProgress;
    FHandle = AHandle;
    FTransfered = ATransfered;
    FConvertParams = ConvertParams;

    return TSFTPAsynchronousQueue::Init();
  }

protected:
  virtual bool __fastcall InitRequest(TSFTPQueuePacket * Request)
  {
    TTerminal * FTerminal = FFileSystem->FTerminal;
    // Buffer for one block of data
    TFileBuffer BlockBuf;

    unsigned long BlockSize = GetBlockSize();
    bool Result = (BlockSize > 0);

    if (Result)
    {
      FILE_OPERATION_LOOP_BEGIN
      {
        BlockBuf.LoadStream(FStream, BlockSize, false);
      }
      FILE_OPERATION_LOOP_END(FMTLOAD(READ_ERROR, (FFileName)));

      FEnd = (BlockBuf.Size == 0);
      Result = !FEnd;
      if (Result)
      {
        OperationProgress->AddLocallyUsed(BlockBuf.Size);

        // We do ASCII transfer: convert EOL of current block
        if (OperationProgress->AsciiTransfer)
        {
          __int64 PrevBufSize = BlockBuf.Size;
          BlockBuf.Convert(FTerminal->Configuration->LocalEOLType,
            FFileSystem->GetEOL(), FConvertParams, FConvertToken);
          // update transfer size with difference arised from EOL conversion
          OperationProgress->ChangeTransferSize(OperationProgress->TransferSize -
            PrevBufSize + BlockBuf.Size);
        }

        if (FFileSystem->FTerminal->Configuration->ActualLogProtocol >= 1)
        {
          FFileSystem->FTerminal->LogEvent(FORMAT(L"Write request offset: %d, len: %d",
            (int(FTransfered), int(BlockBuf.Size))));
        }

        Request->ChangeType(SSH_FXP_WRITE);
        Request->AddString(FHandle);
        Request->AddInt64(FTransfered);
        Request->AddData(BlockBuf.Data, BlockBuf.Size);
        FLastBlockSize = BlockBuf.Size;

        FTransfered += BlockBuf.Size;
      }
    }

    return Result;
  }

  virtual void __fastcall SendPacket(TSFTPQueuePacket * Packet)
  {
    TSFTPAsynchronousQueue::SendPacket(Packet);
    OperationProgress->AddTransfered(FLastBlockSize);
  }

  virtual void __fastcall ReceiveResponse(
    const TSFTPPacket * Packet, TSFTPPacket * Response, int ExpectedType,
    int AllowStatus, bool TryOnly)
  {
    TSFTPAsynchronousQueue::ReceiveResponse(Packet, Response, ExpectedType, AllowStatus, TryOnly);

    if (Response->Capacity > 0)
    {
      // particularly when uploading a file that completelly fits into send buffer
      // over slow line, we may end up seemingly completing the transfer immediatelly
      // but hanging the application for a long time waiting for responses
      // (common is that the progress window would not even manage to draw itself,
      // showing that upload finished, before the application "hangs")
      FFileSystem->Progress(OperationProgress);
    }
    else
    {
      DebugAssert(TryOnly);
    }
  }

  virtual bool __fastcall ReceivePacketAsynchronously()
  {
    // do not read response to close request
    bool Result = (FRequests->Count > 0);
    if (Result)
    {
      // Try only: We cannot read from the socket here as we are already called
      // from TSecureShell::HandleNetworkEvents as it would cause a recursion
      // that would potentially make PuTTY code process the SSH packets in wrong order.
      ReceivePacket(NULL, SSH_FXP_STATUS, -1, NULL, true);
    }
    return Result;
  }

  inline int __fastcall GetBlockSize()
  {
    return FFileSystem->UploadBlockSize(FHandle, OperationProgress);
  }

  virtual bool __fastcall End(TSFTPPacket * /*Response*/)
  {
    return FEnd;
  }

private:
  TStream * FStream;
  TFileOperationProgressType * OperationProgress;
  UnicodeString FFileName;
  unsigned long FLastBlockSize;
  bool FEnd;
  __int64 FTransfered;
  RawByteString FHandle;
  bool FConvertToken;
  int FConvertParams;
};
//---------------------------------------------------------------------------
class TSFTPLoadFilesPropertiesQueue : public TSFTPFixedLenQueue
{
public:
  TSFTPLoadFilesPropertiesQueue(TSFTPFileSystem * AFileSystem) :
    TSFTPFixedLenQueue(AFileSystem)
  {
    FIndex = 0;
  }
  virtual __fastcall ~TSFTPLoadFilesPropertiesQueue(){}

  bool __fastcall Init(int QueueLen, TStrings * FileList)
  {
    FFileList = FileList;

    return TSFTPFixedLenQueue::Init(QueueLen);
  }

  bool __fastcall ReceivePacket(TSFTPPacket * Packet, TRemoteFile *& File)
  {
    void * Token;
    bool Result = TSFTPFixedLenQueue::ReceivePacket(Packet, SSH_FXP_ATTRS, asAll, &Token);
    File = static_cast<TRemoteFile *>(Token);
    return Result;
  }

protected:
  virtual bool __fastcall InitRequest(TSFTPQueuePacket * Request)
  {
    bool Result = false;
    while (!Result && (FIndex < FFileList->Count))
    {
      TRemoteFile * File = static_cast<TRemoteFile *>(FFileList->Objects[FIndex]);
      FIndex++;

      bool MissingRights =
        (FFileSystem->FSupport->Loaded &&
         FLAGSET(FFileSystem->FSupport->AttributeMask, SSH_FILEXFER_ATTR_PERMISSIONS) &&
         File->Rights->Unknown);
      bool MissingOwnerGroup =
        (FFileSystem->FSecureShell->SshImplementation == sshiBitvise) ||
        (FFileSystem->FSupport->Loaded &&
         FLAGSET(FFileSystem->FSupport->AttributeMask, SSH_FILEXFER_ATTR_OWNERGROUP) &&
         !File->Owner.IsSet || !File->Group.IsSet);

      Result = (MissingRights || MissingOwnerGroup);
      if (Result)
      {
        Request->ChangeType(SSH_FXP_LSTAT);
        Request->AddPathString(FFileSystem->LocalCanonify(File->FileName),
          FFileSystem->FUtfStrings);
        if (FFileSystem->FVersion >= 4)
        {
          Request->AddCardinal(
            FLAGMASK(MissingRights, SSH_FILEXFER_ATTR_PERMISSIONS) |
            FLAGMASK(MissingOwnerGroup, SSH_FILEXFER_ATTR_OWNERGROUP));
        }
        Request->Token = File;
      }
    }

    return Result;
  }

  virtual bool __fastcall SendRequest()
  {
    bool Result =
      (FIndex < FFileList->Count) &&
      TSFTPFixedLenQueue::SendRequest();
    return Result;
  }

  virtual bool __fastcall End(TSFTPPacket * /*Response*/)
  {
    return (FRequests->Count == 0);
  }

private:
  TStrings * FFileList;
  int FIndex;
};
//---------------------------------------------------------------------------
class TSFTPCalculateFilesChecksumQueue : public TSFTPFixedLenQueue
{
public:
  TSFTPCalculateFilesChecksumQueue(TSFTPFileSystem * AFileSystem) :
    TSFTPFixedLenQueue(AFileSystem)
  {
    FIndex = 0;
  }
  virtual __fastcall ~TSFTPCalculateFilesChecksumQueue(){}

  bool __fastcall Init(int QueueLen, const UnicodeString & Alg, TStrings * FileList)
  {
    FAlg = Alg;
    FFileList = FileList;

    return TSFTPFixedLenQueue::Init(QueueLen);
  }

  bool __fastcall ReceivePacket(TSFTPPacket * Packet, TRemoteFile *& File)
  {
    void * Token;
    bool Result;
    try
    {
      Result = TSFTPFixedLenQueue::ReceivePacket(Packet, SSH_FXP_EXTENDED_REPLY, asNo, &Token);
    }
    __finally
    {
      File = static_cast<TRemoteFile *>(Token);
    }
    return Result;
  }

protected:
  virtual bool __fastcall InitRequest(TSFTPQueuePacket * Request)
  {
    bool Result = false;
    while (!Result && (FIndex < FFileList->Count))
    {
      TRemoteFile * File = static_cast<TRemoteFile *>(FFileList->Objects[FIndex]);
      DebugAssert(File != NULL);
      FIndex++;

      Result = !File->IsDirectory;
      if (Result)
      {
        DebugAssert(!File->IsParentDirectory && !File->IsThisDirectory);

        Request->ChangeType(SSH_FXP_EXTENDED);
        Request->AddString(SFTP_EXT_CHECK_FILE_NAME);
        Request->AddPathString(FFileSystem->LocalCanonify(File->FullFileName),
          FFileSystem->FUtfStrings);
        Request->AddString(FAlg);
        Request->AddInt64(0); // offset
        Request->AddInt64(0); // length (0 = till end)
        Request->AddCardinal(0); // block size (0 = no blocks or "one block")

        Request->Token = File;
      }
    }

    return Result;
  }

  virtual bool __fastcall SendRequest()
  {
    bool Result =
      (FIndex < FFileList->Count) &&
      TSFTPFixedLenQueue::SendRequest();
    return Result;
  }

  virtual bool __fastcall End(TSFTPPacket * /*Response*/)
  {
    return (FRequests->Count == 0);
  }

private:
  UnicodeString FAlg;
  TStrings * FFileList;
  int FIndex;
};
//---------------------------------------------------------------------------
#pragma warn .inl
//---------------------------------------------------------------------------
class TSFTPBusy
{
public:
  __fastcall TSFTPBusy(TSFTPFileSystem * FileSystem)
  {
    FFileSystem = FileSystem;
    DebugAssert(FFileSystem != NULL);
    FFileSystem->BusyStart();
  }

  __fastcall ~TSFTPBusy()
  {
    FFileSystem->BusyEnd();
  }

private:
  TSFTPFileSystem * FFileSystem;
};
//===========================================================================
struct TOpenRemoteFileParams
{
  int LocalFileAttrs;
  UnicodeString FileName;
  UnicodeString RemoteFileName;
  TFileOperationProgressType * OperationProgress;
  const TCopyParamType * CopyParam;
  int Params;
  bool Resume;
  bool Resuming;
  TSFTPOverwriteMode OverwriteMode;
  __int64 DestFileSize; // output
  RawByteString RemoteFileHandle; // output
  TOverwriteFileParams * FileParams;
  bool Confirmed;
};
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
//===========================================================================
__fastcall TSFTPFileSystem::TSFTPFileSystem(TTerminal * ATerminal,
  TSecureShell * SecureShell):
  TCustomFileSystem(ATerminal)
{
  FSecureShell = SecureShell;
  FPacketReservations = new TList();
  FPacketNumbers = VarArrayCreate(OPENARRAY(int, (0, 1)), varLongWord);
  FPreviousLoggedPacket = 0;
  FNotLoggedPackets = 0;
  FBusy = 0;
  FAvoidBusy = false;
  FUtfStrings = asOff;
  FUtfDisablingAnnounced = true;
  FSignedTS = false;
  FSupport = new TSFTPSupport();
  FExtensions = new TStringList();
  FFixedPaths = NULL;
  FFileSystemInfoValid = false;

  FChecksumAlgs.reset(new TStringList());
  FChecksumSftpAlgs.reset(new TStringList());
  // List as defined by draft-ietf-secsh-filexfer-extensions-00
  // MD5 moved to the back
  RegisterChecksumAlg(Sha1ChecksumAlg, L"sha1");
  RegisterChecksumAlg(Sha224ChecksumAlg, L"sha224");
  RegisterChecksumAlg(Sha256ChecksumAlg, L"sha256");
  RegisterChecksumAlg(Sha384ChecksumAlg, L"sha384");
  RegisterChecksumAlg(Sha512ChecksumAlg, L"sha512");
  RegisterChecksumAlg(Md5ChecksumAlg, L"md5");
  RegisterChecksumAlg(Crc32ChecksumAlg, L"crc32");
}
//---------------------------------------------------------------------------
__fastcall TSFTPFileSystem::~TSFTPFileSystem()
{
  delete FSupport;
  ResetConnection();
  delete FPacketReservations;
  delete FExtensions;
  delete FFixedPaths;
  delete FSecureShell;
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::Open()
{
  // this is used for reconnects only
  FSecureShell->Open();
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::Close()
{
  FSecureShell->Close();
}
//---------------------------------------------------------------------------
bool __fastcall TSFTPFileSystem::GetActive()
{
  return FSecureShell->Active;
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::CollectUsage()
{
  FSecureShell->CollectUsage();

  UnicodeString VersionCounter;
  switch (FVersion)
  {
    case 0:
      VersionCounter = L"OpenedSessionsSFTP0";
      break;
    case 1:
      VersionCounter = L"OpenedSessionsSFTP1";
      break;
    case 2:
      VersionCounter = L"OpenedSessionsSFTP2";
      break;
    case 3:
      VersionCounter = L"OpenedSessionsSFTP3";
      break;
    case 4:
      VersionCounter = L"OpenedSessionsSFTP4";
      break;
    case 5:
      VersionCounter = L"OpenedSessionsSFTP5";
      break;
    case 6:
      VersionCounter = L"OpenedSessionsSFTP6";
      break;
    default:
      DebugFail();
  }
  FTerminal->Configuration->Usage->Inc(VersionCounter);
}
//---------------------------------------------------------------------------
const TSessionInfo & __fastcall TSFTPFileSystem::GetSessionInfo()
{
  return FSecureShell->GetSessionInfo();
}
//---------------------------------------------------------------------------
const TFileSystemInfo & __fastcall TSFTPFileSystem::GetFileSystemInfo(bool /*Retrieve*/)
{
  if (!FFileSystemInfoValid)
  {
    FFileSystemInfo.AdditionalInfo = L"";

    if (!IsCapable(fcRename))
    {
      FFileSystemInfo.AdditionalInfo += LoadStr(FS_RENAME_NOT_SUPPORTED) + L"\r\n\r\n";
    }

    if (FExtensions->Count > 0)
    {
      UnicodeString Name;
      UnicodeString Value;
      UnicodeString Line;
      FFileSystemInfo.AdditionalInfo += LoadStr(SFTP_EXTENSION_INFO) + L"\r\n";
      for (int Index = 0; Index < FExtensions->Count; Index++)
      {
        UnicodeString Name = FExtensions->Names[Index];
        UnicodeString Value = FExtensions->Values[Name];
        UnicodeString Line;
        if (Value.IsEmpty())
        {
          Line = Name;
        }
        else
        {
          Line = FORMAT(L"%s=%s", (Name, Value));
        }
        FFileSystemInfo.AdditionalInfo += FORMAT(L"  %s\r\n", (Line));
      }
    }
    else
    {
      FFileSystemInfo.AdditionalInfo += LoadStr(SFTP_NO_EXTENSION_INFO) + L"\r\n";
    }

    FFileSystemInfo.ProtocolBaseName = L"SFTP";
    FFileSystemInfo.ProtocolName = FMTLOAD(SFTP_PROTOCOL_NAME2, (FVersion));
    FTerminal->SaveCapabilities(FFileSystemInfo);

    FFileSystemInfoValid = true;
  }

  return FFileSystemInfo;
}
//---------------------------------------------------------------------------
bool __fastcall TSFTPFileSystem::TemporaryTransferFile(const UnicodeString & FileName)
{
  return SameText(UnixExtractFileExt(FileName), PARTIAL_EXT);
}
//---------------------------------------------------------------------------
bool __fastcall TSFTPFileSystem::GetStoredCredentialsTried()
{
  return FSecureShell->GetStoredCredentialsTried();
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TSFTPFileSystem::GetUserName()
{
  return FSecureShell->UserName;
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::Idle()
{
  // Keep session alive
  if ((FTerminal->SessionData->PingType != ptOff) &&
      (Now() - FSecureShell->LastDataSent > FTerminal->SessionData->PingIntervalDT))
  {
    if ((FTerminal->SessionData->PingType == ptDummyCommand) &&
        FSecureShell->Ready)
    {
      FTerminal->LogEvent(L"Sending dummy command to keep session alive.");
      TSFTPPacket Packet(SSH_FXP_REALPATH);
      Packet.AddPathString(L"/", FUtfStrings);
      SendPacketAndReceiveResponse(&Packet, &Packet);
    }
    else
    {
      FSecureShell->KeepAlive();
    }
  }

  FSecureShell->Idle();
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::ResetConnection()
{
  // there must be no valid packet reservation at the end
  for (int i = 0; i < FPacketReservations->Count; i++)
  {
    DebugAssert(FPacketReservations->Items[i] == NULL);
    delete (TSFTPPacket *)FPacketReservations->Items[i];
  }
  FPacketReservations->Clear();
  FPacketNumbers.Clear();
}
//---------------------------------------------------------------------------
bool __fastcall TSFTPFileSystem::IsCapable(int Capability) const
{
  DebugAssert(FTerminal);
  switch (Capability) {
    case fcAnyCommand:
    case fcShellAnyCommand:
      return false;

    case fcNewerOnlyUpload:
    case fcTimestampChanging:
    case fcIgnorePermErrors:
    case fcPreservingTimestampUpload:
    case fcSecondaryShell:
    case fcRemoveCtrlZUpload:
    case fcRemoveBOMUpload:
    case fcMoveToQueue:
    case fcPreservingTimestampDirs:
    case fcResumeSupport:
      return true;

    case fcRename:
    case fcRemoteMove:
      return (FVersion >= 2);

    case fcSymbolicLink:
    case fcResolveSymlink:
      return (FVersion >= 3);

    case fcModeChanging:
    case fcModeChangingUpload:
      return !FSupport->Loaded ||
        FLAGSET(FSupport->AttributeMask, SSH_FILEXFER_ATTR_PERMISSIONS);

    case fcGroupOwnerChangingByID:
      return (FVersion <= 3);

    case fcOwnerChanging:
    case fcGroupChanging:
      return
        (FVersion <= 3) ||
        ((FVersion >= 4) &&
         (!FSupport->Loaded ||
          FLAGSET(FSupport->AttributeMask, SSH_FILEXFER_ATTR_OWNERGROUP)));

    case fcNativeTextMode:
      return (FVersion >= 4);

    case fcTextMode:
      return (FVersion >= 4) ||
        strcmp(GetEOL(), EOLToStr(FTerminal->Configuration->LocalEOLType)) != 0;

    case fcUserGroupListing:
      return SupportsExtension(SFTP_EXT_OWNER_GROUP);

    case fcLoadingAdditionalProperties:
      // We allow loading properties only, if "supported" extension is supported and
      // the server supports "permissions" and/or "owner/group" attributes
      // (no other attributes are loaded).
      // This is here only because of VShell
      // (it supports owner/group, but does not include them into response to
      // SSH_FXP_READDIR)
      // and Bitwise (the same as VShell, but it does not even bother to provide "supported" extension until 6.21)
      // No other use is known.
      return
        (FSupport->Loaded &&
         ((FSupport->AttributeMask &
           (SSH_FILEXFER_ATTR_PERMISSIONS | SSH_FILEXFER_ATTR_OWNERGROUP)) != 0)) ||
        (FSecureShell->SshImplementation == sshiBitvise);

    case fcCheckingSpaceAvailable:
      return
        // extension announced in extension list of by
        // SFTP_EXT_SUPPORTED/SFTP_EXT_SUPPORTED2 extension
        // (SFTP version 5 and newer only)
        SupportsExtension(SFTP_EXT_SPACE_AVAILABLE) ||
        // extension announced by proprietary SFTP_EXT_STATVFS extension
        FSupportsStatVfsV2 ||
        // Bitwise (until 6.21) fails to report it's supported extensions.
        (FSecureShell->SshImplementation == sshiBitvise);

    case fcCalculatingChecksum:
      return
        // Specification says that "check-file" should be announced,
        // yet Vandyke VShell (as of 4.0.3) announce "check-file-name"
        // https://forums.vandyke.com/showthread.php?t=11597
        SupportsExtension(SFTP_EXT_CHECK_FILE) ||
        SupportsExtension(SFTP_EXT_CHECK_FILE_NAME) ||
        // see above
        (FSecureShell->SshImplementation == sshiBitvise);

    case fcRemoteCopy:
      return
        SupportsExtension(SFTP_EXT_COPY_FILE) ||
        // see above
        (FSecureShell->SshImplementation == sshiBitvise);

    case fcHardLink:
      return
        (FVersion >= 6) ||
        FSupportsHardlink;

    case fcLocking:
      return false;

    default:
      DebugFail();
      return false;
  }
}
//---------------------------------------------------------------------------
bool __fastcall TSFTPFileSystem::SupportsExtension(const UnicodeString & Extension) const
{
  return FSupport->Loaded && (FSupport->Extensions->IndexOf(Extension) >= 0);
}
//---------------------------------------------------------------------------
inline void __fastcall TSFTPFileSystem::BusyStart()
{
  if (FBusy == 0 && FTerminal->UseBusyCursor && !FAvoidBusy)
  {
    FBusyToken = ::BusyStart();
  }
  FBusy++;
  DebugAssert(FBusy < 10);
}
//---------------------------------------------------------------------------
inline void __fastcall TSFTPFileSystem::BusyEnd()
{
  DebugAssert(FBusy > 0);
  FBusy--;
  if (FBusy == 0 && FTerminal->UseBusyCursor && !FAvoidBusy)
  {
    ::BusyEnd(FBusyToken);
    FBusyToken = NULL;
  }
}
//---------------------------------------------------------------------------
unsigned long __fastcall TSFTPFileSystem::TransferBlockSize(unsigned long Overhead,
  TFileOperationProgressType * OperationProgress, unsigned long MaxPacketSize)
{
  const unsigned long MinPacketSize = 32768;
  // size + message number + type
  const unsigned long SFTPPacketOverhead = 4 + 4 + 1;
  unsigned long AMaxPacketSize = FSecureShell->MaxPacketSize();
  bool MaxPacketSizeValid = (AMaxPacketSize > 0);
  unsigned long Result = OperationProgress->CPS();

  if ((MaxPacketSize > 0) &&
      ((MaxPacketSize < AMaxPacketSize) || !MaxPacketSizeValid))
  {
    AMaxPacketSize = MaxPacketSize;
    MaxPacketSizeValid = true;
  }

  if ((FMaxPacketSize > 0) &&
      ((FMaxPacketSize < AMaxPacketSize) || !MaxPacketSizeValid))
  {
    AMaxPacketSize = FMaxPacketSize;
    MaxPacketSizeValid = true;
  }

  if (Result == 0)
  {
    Result = OperationProgress->StaticBlockSize();
  }

  if (Result < MinPacketSize)
  {
    Result = MinPacketSize;
  }

  if (MaxPacketSizeValid)
  {
    Overhead += SFTPPacketOverhead;
    if (AMaxPacketSize < Overhead)
    {
      // do not send another request
      // (generally should happen only if upload buffer is full)
      Result = 0;
    }
    else
    {
      AMaxPacketSize -= Overhead;
      if (Result > AMaxPacketSize)
      {
        Result = AMaxPacketSize;
      }
    }
  }

  Result = OperationProgress->AdjustToCPSLimit(Result);

  return Result;
}
//---------------------------------------------------------------------------
unsigned long __fastcall TSFTPFileSystem::UploadBlockSize(const RawByteString & Handle,
  TFileOperationProgressType * OperationProgress)
{
  // handle length + offset + data size
  const unsigned long UploadPacketOverhead =
    sizeof(unsigned long) + sizeof(__int64) + sizeof(unsigned long);
  return TransferBlockSize(UploadPacketOverhead + Handle.Length(), OperationProgress);
}
//---------------------------------------------------------------------------
unsigned long __fastcall TSFTPFileSystem::DownloadBlockSize(
  TFileOperationProgressType * OperationProgress)
{
  unsigned long Result = TransferBlockSize(sizeof(unsigned long), OperationProgress);
  if (FSupport->Loaded && (FSupport->MaxReadSize > 0) &&
      (Result > FSupport->MaxReadSize))
  {
    Result = FSupport->MaxReadSize;
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::Progress(TFileOperationProgressType * OperationProgress)
{
  FTerminal->Progress(OperationProgress);
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::SendPacket(const TSFTPPacket * Packet)
{
  // putting here for a lack of better place
  if (!FUtfDisablingAnnounced && (FUtfStrings == asOff))
  {
    FTerminal->LogEvent(L"Strings received in non-UTF-8 encoding in a previous packet, will not use UTF-8 anymore");
    FUtfDisablingAnnounced = true;
  }

  BusyStart();
  try
  {
    if (FTerminal->Log->Logging)
    {
      if ((FPreviousLoggedPacket != SSH_FXP_READ &&
           FPreviousLoggedPacket != SSH_FXP_WRITE) ||
          (Packet->Type != FPreviousLoggedPacket) ||
          (FTerminal->Configuration->ActualLogProtocol >= 1))
      {
        if (FNotLoggedPackets)
        {
          FTerminal->LogEvent(FORMAT(L"%d skipped SSH_FXP_WRITE, SSH_FXP_READ, SSH_FXP_DATA and SSH_FXP_STATUS packets.",
            (FNotLoggedPackets)));
          FNotLoggedPackets = 0;
        }
        FTerminal->Log->Add(llInput, FORMAT(L"Type: %s, Size: %d, Number: %d",
          (Packet->TypeName, (int)Packet->Length, (int)Packet->MessageNumber)));
        if (FTerminal->Configuration->ActualLogProtocol >= 2)
        {
          FTerminal->Log->Add(llInput, Packet->Dump());
        }
        FPreviousLoggedPacket = Packet->Type;
      }
      else
      {
        FNotLoggedPackets++;
      }
    }
    FSecureShell->Send(Packet->SendData, Packet->SendLength);
  }
  __finally
  {
    BusyEnd();
  }
}
//---------------------------------------------------------------------------
unsigned long __fastcall TSFTPFileSystem::GotStatusPacket(TSFTPPacket * Packet,
  int AllowStatus)
{
  unsigned long Code = Packet->GetCardinal();

  static int Messages[] = {
    SFTP_STATUS_OK,
    SFTP_STATUS_EOF,
    SFTP_STATUS_NO_SUCH_FILE,
    SFTP_STATUS_PERMISSION_DENIED,
    SFTP_STATUS_FAILURE,
    SFTP_STATUS_BAD_MESSAGE,
    SFTP_STATUS_NO_CONNECTION,
    SFTP_STATUS_CONNECTION_LOST,
    SFTP_STATUS_OP_UNSUPPORTED,
    SFTP_STATUS_INVALID_HANDLE,
    SFTP_STATUS_NO_SUCH_PATH,
    SFTP_STATUS_FILE_ALREADY_EXISTS,
    SFTP_STATUS_WRITE_PROTECT,
    SFTP_STATUS_NO_MEDIA,
    SFTP_STATUS_NO_SPACE_ON_FILESYSTEM,
    SFTP_STATUS_QUOTA_EXCEEDED,
    SFTP_STATUS_UNKNOWN_PRINCIPAL,
    SFTP_STATUS_LOCK_CONFLICT,
    SFTP_STATUS_DIR_NOT_EMPTY,
    SFTP_STATUS_NOT_A_DIRECTORY,
    SFTP_STATUS_INVALID_FILENAME,
    SFTP_STATUS_LINK_LOOP,
    SFTP_STATUS_CANNOT_DELETE,
    SFTP_STATUS_INVALID_PARAMETER,
    SFTP_STATUS_FILE_IS_A_DIRECTORY,
    SFTP_STATUS_BYTE_RANGE_LOCK_CONFLICT,
    SFTP_STATUS_BYTE_RANGE_LOCK_REFUSED,
    SFTP_STATUS_DELETE_PENDING,
    SFTP_STATUS_FILE_CORRUPT,
    SFTP_STATUS_OWNER_INVALID,
    SFTP_STATUS_GROUP_INVALID,
    SFTP_STATUS_NO_MATCHING_BYTE_RANGE_LOCK
  };
  int Message;
  if ((AllowStatus & (0x01 << Code)) == 0)
  {
    if (Code >= LENOF(Messages))
    {
      Message = SFTP_STATUS_UNKNOWN;
    }
    else
    {
      Message = Messages[Code];
    }
    UnicodeString MessageStr = LoadStr(Message);
    UnicodeString ServerMessage;
    UnicodeString LanguageTag;
    if ((FVersion >= 3) ||
        // if version is not decided yet (i.e. this is status response
        // to the init request), go on, only if there are any more data
        ((FVersion < 0) && (Packet->RemainingLength > 0)))
    {
      // message is in UTF only since SFTP specification 01 (specification 00
      // is also version 3)
      // (in other words, always use UTF unless server is known to be buggy)
      ServerMessage = Packet->GetString(FUtfStrings);
      // SSH-2.0-Maverick_SSHD and SSH-2.0-CIGNA SFTP Server Ready! omit the language tag
      // and I believe I've seen one more server doing the same.
      if (Packet->RemainingLength > 0)
      {
        LanguageTag = Packet->GetAnsiString();
        if ((FVersion >= 5) && (Message == SFTP_STATUS_UNKNOWN_PRINCIPAL))
        {
          UnicodeString Principals;
          while (Packet->GetNextData() != NULL)
          {
            if (!Principals.IsEmpty())
            {
              Principals += L", ";
            }
            Principals += Packet->GetAnsiString();
          }
          MessageStr = FORMAT(MessageStr, (Principals));
        }
      }
    }
    else
    {
      ServerMessage = LoadStr(SFTP_SERVER_MESSAGE_UNSUPPORTED);
    }
    if (FTerminal->Log->Logging)
    {
      FTerminal->Log->Add(llOutput, FORMAT(L"Status code: %d, Message: %d, Server: %s, Language: %s ",
        (int(Code), (int)Packet->MessageNumber, ServerMessage, LanguageTag)));
    }
    if (!LanguageTag.IsEmpty())
    {
      LanguageTag = FORMAT(L" (%s)", (LanguageTag));
    }
    UnicodeString HelpKeyword;
    switch (Code)
    {
      case SSH_FX_FAILURE:
        HelpKeyword = HELP_SFTP_STATUS_FAILURE;
        break;

      case SSH_FX_PERMISSION_DENIED:
        HelpKeyword = HELP_SFTP_STATUS_PERMISSION_DENIED;
        break;
    }
    UnicodeString Error = FMTLOAD(SFTP_ERROR_FORMAT3, (MessageStr,
      int(Code), LanguageTag, ServerMessage));
    if (Code == SSH_FX_FAILURE)
    {
      FTerminal->Configuration->Usage->Inc(L"SftpFailureErrors");
      Error += L"\n\n" + LoadStr(SFTP_STATUS_4);
    }
    FTerminal->TerminalError(NULL, Error, HelpKeyword);
    return 0;
  }
  else
  {
    if (!FNotLoggedPackets || Code)
    {
      FTerminal->Log->Add(llOutput, FORMAT(L"Status code: %d", ((int)Code)));
    }
    return Code;
  }
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::RemoveReservation(int Reservation)
{
  for (int Index = Reservation+1; Index < FPacketReservations->Count; Index++)
  {
    FPacketNumbers.PutElement(FPacketNumbers.GetElement(Index), Index-1);
  }
  TSFTPPacket * Packet = (TSFTPPacket *)FPacketReservations->Items[Reservation];
  if (Packet)
  {
    DebugAssert(Packet->ReservedBy == this);
    Packet->ReservedBy = NULL;
  }
  FPacketReservations->Delete(Reservation);
}
//---------------------------------------------------------------------------
inline int __fastcall TSFTPFileSystem::PacketLength(unsigned char * LenBuf, int ExpectedType)
{
  int Length = GET_32BIT(LenBuf);
  if (Length > SFTP_MAX_PACKET_LEN)
  {
    UnicodeString Message = FMTLOAD(SFTP_PACKET_TOO_BIG, (
      int(Length), SFTP_MAX_PACKET_LEN));
    if (ExpectedType == SSH_FXP_VERSION)
    {
      RawByteString LenString(reinterpret_cast<char *>(LenBuf), 4);
      Message = FMTLOAD(SFTP_PACKET_TOO_BIG_INIT_EXPLAIN,
        (Message, DisplayableStr(LenString)));
    }
    FTerminal->FatalError(NULL, Message, HELP_SFTP_PACKET_TOO_BIG);
  }
  return Length;
}
//---------------------------------------------------------------------------
bool __fastcall TSFTPFileSystem::PeekPacket()
{
  bool Result;
  unsigned char * Buf;
  Result = FSecureShell->Peek(Buf, 4);
  if (Result)
  {
    int Length = PacketLength(Buf, -1);
    Result = FSecureShell->Peek(Buf, 4 + Length);
  }
  return Result;
}
//---------------------------------------------------------------------------
int __fastcall TSFTPFileSystem::ReceivePacket(TSFTPPacket * Packet,
  int ExpectedType, int AllowStatus, bool TryOnly)
{
  TSFTPBusy Busy(this);

  int Result = SSH_FX_OK;
  int Reservation = FPacketReservations->IndexOf(Packet);

  if (Reservation < 0 || Packet->Capacity == 0)
  {
    bool IsReserved;
    do
    {
      IsReserved = false;

      DebugAssert(Packet);

      if (TryOnly && !PeekPacket())
      {
        // Reset packet in case it was filled by previous out-of-order
        // reserved packet
        *Packet = TSFTPPacket();
      }
      else
      {
        unsigned char LenBuf[4];
        FSecureShell->Receive(LenBuf, sizeof(LenBuf));
        int Length = PacketLength(LenBuf, ExpectedType);
        Packet->Capacity = Length;
        FSecureShell->Receive(Packet->Data, Length);
        Packet->DataUpdated(Length);

        if (FTerminal->Log->Logging)
        {
          if ((FPreviousLoggedPacket != SSH_FXP_READ &&
               FPreviousLoggedPacket != SSH_FXP_WRITE) ||
              (Packet->Type != SSH_FXP_STATUS && Packet->Type != SSH_FXP_DATA) ||
              (FTerminal->Configuration->ActualLogProtocol >= 1))
          {
            if (FNotLoggedPackets)
            {
              FTerminal->LogEvent(FORMAT(L"%d skipped SSH_FXP_WRITE, SSH_FXP_READ, SSH_FXP_DATA and SSH_FXP_STATUS packets.",
                (FNotLoggedPackets)));
              FNotLoggedPackets = 0;
            }
            FTerminal->Log->Add(llOutput, FORMAT(L"Type: %s, Size: %d, Number: %d",
              (Packet->TypeName, (int)Packet->Length, (int)Packet->MessageNumber)));
            if (FTerminal->Configuration->ActualLogProtocol >= 2)
            {
              FTerminal->Log->Add(llOutput, Packet->Dump());
            }
          }
          else
          {
            FNotLoggedPackets++;
          }
        }

        if (Reservation < 0 ||
            Packet->MessageNumber != (unsigned int)FPacketNumbers.GetElement(Reservation))
        {
          TSFTPPacket * ReservedPacket;
          unsigned int MessageNumber;
          for (int Index = 0; Index < FPacketReservations->Count; Index++)
          {
            MessageNumber = (unsigned int)FPacketNumbers.GetElement(Index);
            if (MessageNumber == Packet->MessageNumber)
            {
              ReservedPacket = (TSFTPPacket *)FPacketReservations->Items[Index];
              IsReserved = true;
              if (ReservedPacket)
              {
                FTerminal->LogEvent(L"Storing reserved response");
                *ReservedPacket = *Packet;
              }
              else
              {
                FTerminal->LogEvent(L"Discarding reserved response");
                RemoveReservation(Index);
                if ((Reservation >= 0) && (Reservation > Index))
                {
                  Reservation--;
                  DebugAssert(Reservation == FPacketReservations->IndexOf(Packet));
                }
              }
              break;
            }
          }
        }
      }
    }
    while (IsReserved);
  }

  if ((Packet->Capacity == 0) && DebugAlwaysTrue(TryOnly))
  {
    // noop
  }
  else
  {
    // before we removed the reservation after check for packet type,
    // but if it raises exception, removal is unnecessarily
    // postponed until the packet is removed
    // (and it have not worked anyway until recent fix to UnreserveResponse)
    if (Reservation >= 0)
    {
      DebugAssert(Packet->MessageNumber == (unsigned int)FPacketNumbers.GetElement(Reservation));
      RemoveReservation(Reservation);
    }

    if (ExpectedType >= 0)
    {
      if (Packet->Type == SSH_FXP_STATUS)
      {
        if (AllowStatus < 0)
        {
          AllowStatus = (ExpectedType == SSH_FXP_STATUS ? asOK : asNo);
        }
        Result = GotStatusPacket(Packet, AllowStatus);
      }
      else if (ExpectedType != Packet->Type)
      {
        FTerminal->FatalError(NULL, FMTLOAD(SFTP_INVALID_TYPE, ((int)Packet->Type)));
      }
    }
  }

  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::ReserveResponse(const TSFTPPacket * Packet,
  TSFTPPacket * Response)
{
  if (Response != NULL)
  {
    DebugAssert(FPacketReservations->IndexOf(Response) < 0);
    // mark response as not received yet
    Response->Capacity = 0;
    Response->ReservedBy = this;
  }
  FPacketReservations->Add(Response);
  if (FPacketReservations->Count >= FPacketNumbers.ArrayHighBound())
  {
    FPacketNumbers.ArrayRedim(FPacketReservations->Count + 10);
  }
  FPacketNumbers.PutElement(Packet->MessageNumber, FPacketReservations->Count - 1);
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::UnreserveResponse(TSFTPPacket * Response)
{
  int Reservation = FPacketReservations->IndexOf(Response);
  if (Response->Capacity != 0)
  {
    // added check for already received packet
    // (it happens when the reserved response is received out of order,
    // unexpectedly soon, and then receivepacket() on the packet
    // is not actually called, due to exception)
    RemoveReservation(Reservation);
  }
  else
  {
    if (Reservation >= 0)
    {
      // we probably do not remove the item at all, because
      // we must remember that the response was expected, so we skip it
      // in receivepacket()
      FPacketReservations->Items[Reservation] = NULL;
    }
  }
}
//---------------------------------------------------------------------------
int __fastcall TSFTPFileSystem::ReceiveResponse(
  const TSFTPPacket * Packet, TSFTPPacket * Response, int ExpectedType,
  int AllowStatus, bool TryOnly)
{
  int Result;
  unsigned int MessageNumber = Packet->MessageNumber;
  TSFTPPacket * AResponse = (Response ? Response : new TSFTPPacket());
  try
  {
    Result = ReceivePacket(AResponse, ExpectedType, AllowStatus, TryOnly);
    if (MessageNumber != AResponse->MessageNumber)
    {
      FTerminal->FatalError(NULL, FMTLOAD(SFTP_MESSAGE_NUMBER,
        ((int)AResponse->MessageNumber, (int)MessageNumber)));
    }
  }
  __finally
  {
    if (!Response)
    {
      delete AResponse;
    }
  }
  return Result;
}
//---------------------------------------------------------------------------
int __fastcall TSFTPFileSystem::SendPacketAndReceiveResponse(
  const TSFTPPacket * Packet, TSFTPPacket * Response, int ExpectedType,
  int AllowStatus)
{
  int Result;
  TSFTPBusy Busy(this);
  SendPacket(Packet);
  Result = ReceiveResponse(Packet, Response, ExpectedType, AllowStatus);
  return Result;
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TSFTPFileSystem::RealPath(const UnicodeString Path)
{
  try
  {
    FTerminal->LogEvent(FORMAT(L"Getting real path for '%s'",
      (Path)));

    TSFTPPacket Packet(SSH_FXP_REALPATH);
    Packet.AddPathString(Path, FUtfStrings);

    // In SFTP-6 new optional field control-byte is added that defaults to
    // SSH_FXP_REALPATH_NO_CHECK=0x01, meaning it won't fail, if the path does not exist.
    // That differs from SFTP-5 recommendation that
    // "The server SHOULD fail the request if the path is not present on the server."
    // Earlier versions had no recommendation, though canonical SFTP-3 implementation
    // in OpenSSH fails.

    // While we really do not care much, we anyway set the flag to ~ & 0x01 to make the request fail.
    // First for consistency.
    // Second to workaround a bug in ProFTPD/mod_sftp version 1.3.5rc1 through 1.3.5-stable
    // that sends a completelly malformed response for non-existing paths,
    // when SSH_FXP_REALPATH_NO_CHECK (even implicitly) is used.
    // See http://bugs.proftpd.org/show_bug.cgi?id=4160

    // Note that earlier drafts of SFTP-6 (filexfer-07 and -08) had optional compose-path field
    // before control-byte field. If we ever use this against a server conforming to those drafts,
    // if may cause trouble.
    if (FVersion >= 6)
    {
      if (FSecureShell->SshImplementation != sshiProFTPD)
      {
        Packet.AddByte(SSH_FXP_REALPATH_STAT_ALWAYS);
      }
      else
      {
        // Cannot use SSH_FXP_REALPATH_STAT_ALWAYS as ProFTPD does wrong bitwise test
        // so it incorrectly evaluates SSH_FXP_REALPATH_STAT_ALWAYS (0x03) as
        // SSH_FXP_REALPATH_NO_CHECK (0x01). The only value conforming to the
        // specification, yet working with ProFTPD is SSH_FXP_REALPATH_STAT_IF (0x02).
        Packet.AddByte(SSH_FXP_REALPATH_STAT_IF);
      }
    }
    SendPacketAndReceiveResponse(&Packet, &Packet, SSH_FXP_NAME);
    if (Packet.GetCardinal() != 1)
    {
      FTerminal->FatalError(NULL, LoadStr(SFTP_NON_ONE_FXP_NAME_PACKET));
    }

    UnicodeString RealDir = UnixExcludeTrailingBackslash(Packet.GetPathString(FUtfStrings));
    // ignore rest of SSH_FXP_NAME packet

    FTerminal->LogEvent(FORMAT(L"Real path is '%s'", (RealDir)));

    return RealDir;
  }
  catch(Exception & E)
  {
    if (FTerminal->Active)
    {
      throw ExtException(&E, FMTLOAD(SFTP_REALPATH_ERROR, (Path)));
    }
    else
    {
      throw;
    }
  }
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TSFTPFileSystem::RealPath(const UnicodeString Path,
  const UnicodeString BaseDir)
{
  UnicodeString APath;

  if (UnixIsAbsolutePath(Path))
  {
    APath = Path;
  }
  else
  {
    if (!Path.IsEmpty())
    {
      // this condition/block was outside (before) current block
      // but it did not work when Path was empty
      if (!BaseDir.IsEmpty())
      {
        APath = UnixIncludeTrailingBackslash(BaseDir);
      }
      APath = APath + Path;
    }
    if (APath.IsEmpty()) APath = UnixIncludeTrailingBackslash(L".");
  }
  return RealPath(APath);
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TSFTPFileSystem::LocalCanonify(const UnicodeString & Path)
{
  // TODO: improve (handle .. etc.)
  if (UnixIsAbsolutePath(Path) ||
      (!FCurrentDirectory.IsEmpty() && UnixSamePath(FCurrentDirectory, Path)))
  {
    return Path;
  }
  else
  {
    return ::AbsolutePath(FCurrentDirectory, Path);
  }
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TSFTPFileSystem::Canonify(UnicodeString Path)
{
  // inspired by canonify() from PSFTP.C
  UnicodeString Result;
  FTerminal->LogEvent(FORMAT(L"Canonifying: \"%s\"", (Path)));
  Path = LocalCanonify(Path);
  bool TryParent = false;
  try
  {
    Result = RealPath(Path);
  }
  catch(...)
  {
    if (FTerminal->Active)
    {
      TryParent = true;
    }
    else
    {
      throw;
    }
  }

  if (TryParent)
  {
    UnicodeString APath = UnixExcludeTrailingBackslash(Path);
    UnicodeString Name = UnixExtractFileName(APath);
    if (Name == L"." || Name == L"..")
    {
      Result = Path;
    }
    else
    {
      UnicodeString FPath = UnixExtractFilePath(APath);
      try
      {
        Result = RealPath(FPath);
        Result = UnixIncludeTrailingBackslash(Result) + Name;
      }
      catch(...)
      {
        if (FTerminal->Active)
        {
          Result = Path;
        }
        else
        {
          throw;
        }
      }
    }
  }

  FTerminal->LogEvent(FORMAT(L"Canonified: \"%s\"", (Result)));

  return Result;
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TSFTPFileSystem::AbsolutePath(UnicodeString Path, bool Local)
{
  if (Local)
  {
    return LocalCanonify(Path);
  }
  else
  {
    return RealPath(Path, CurrentDirectory);
  }
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TSFTPFileSystem::GetHomeDirectory()
{
  if (FHomeDirectory.IsEmpty())
  {
    FHomeDirectory = RealPath(L".");
  }
  return FHomeDirectory;
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::LoadFile(TRemoteFile * File, TSFTPPacket * Packet,
  bool Complete)
{
  Packet->GetFile(File, FVersion, FTerminal->SessionData->DSTMode,
    FUtfStrings, FSignedTS, Complete);
}
//---------------------------------------------------------------------------
TRemoteFile * __fastcall TSFTPFileSystem::LoadFile(TSFTPPacket * Packet,
  TRemoteFile * ALinkedByFile, const UnicodeString FileName,
  TRemoteFileList * TempFileList, bool Complete)
{
  TRemoteFile * File = new TRemoteFile(ALinkedByFile);
  try
  {
    File->Terminal = FTerminal;
    if (!FileName.IsEmpty())
    {
      File->FileName = FileName;
    }
    // to get full path for symlink completion
    File->Directory = TempFileList;
    LoadFile(File, Packet, Complete);
    File->Directory = NULL;
  }
  catch(...)
  {
    delete File;
    throw;
  }
  return File;
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TSFTPFileSystem::GetCurrentDirectory()
{
  return FCurrentDirectory;
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::DoStartup()
{
  // do not know yet
  FVersion = -1;
  FFileSystemInfoValid = false;
  TSFTPPacket Packet(SSH_FXP_INIT);
  int MaxVersion = FTerminal->SessionData->SFTPMaxVersion;
  if (MaxVersion > SFTPMaxVersion)
  {
    MaxVersion = SFTPMaxVersion;
  }
  Packet.AddCardinal(MaxVersion);

  try
  {
    SendPacketAndReceiveResponse(&Packet, &Packet, SSH_FXP_VERSION);
  }
  catch(Exception &E)
  {
    FTerminal->FatalError(&E, LoadStr(SFTP_INITIALIZE_ERROR), HELP_SFTP_INITIALIZE_ERROR);
  }

  FVersion = Packet.GetCardinal();
  FTerminal->LogEvent(FORMAT(L"SFTP version %d negotiated.", (FVersion)));
  if (FVersion < SFTPMinVersion || FVersion > SFTPMaxVersion)
  {
    FTerminal->FatalError(NULL, FMTLOAD(SFTP_VERSION_NOT_SUPPORTED,
      (FVersion, SFTPMinVersion, SFTPMaxVersion)));
  }

  FExtensions->Clear();
  FEOL = "\r\n";
  FSupport->Loaded = false;
  FSupportsStatVfsV2 = false;
  FSupportsHardlink = false;
  SAFE_DESTROY(FFixedPaths);

  if (FVersion >= 3)
  {
    while (Packet.GetNextData() != NULL)
    {
      UnicodeString ExtensionName = Packet.GetAnsiString();
      RawByteString ExtensionData = Packet.GetRawByteString();
      UnicodeString ExtensionDisplayData = DisplayableStr(ExtensionData);

      if (ExtensionName == SFTP_EXT_NEWLINE)
      {
        FEOL = AnsiString(ExtensionData);
        FTerminal->LogEvent(FORMAT(L"Server requests EOL sequence %s.",
          (ExtensionDisplayData)));
        if (FEOL.Length() < 1 || FEOL.Length() > 2)
        {
          FTerminal->FatalError(NULL, FMTLOAD(SFTP_INVALID_EOL, (ExtensionDisplayData)));
        }
      }
      // do not allow "supported" to override "supported2" if both are received
      else if (((ExtensionName == SFTP_EXT_SUPPORTED) && !FSupport->Loaded) ||
               (ExtensionName == SFTP_EXT_SUPPORTED2))
      {
        FSupport->Reset();
        TSFTPPacket SupportedStruct(ExtensionData);
        FSupport->Loaded = true;
        FSupport->AttributeMask = SupportedStruct.GetCardinal();
        FSupport->AttributeBits = SupportedStruct.GetCardinal();
        FSupport->OpenFlags = SupportedStruct.GetCardinal();
        FSupport->AccessMask = SupportedStruct.GetCardinal();
        FSupport->MaxReadSize = SupportedStruct.GetCardinal();
        if (ExtensionName == SFTP_EXT_SUPPORTED)
        {
          while (SupportedStruct.GetNextData() != NULL)
          {
            FSupport->Extensions->Add(SupportedStruct.GetAnsiString());
          }
        }
        else
        {
          // note that supported-open-block-vector, supported-block-vector,
          // attrib-extension-count and attrib-extension-names fields
          // were added only in rev 08, while "supported2" was defined in rev 07
          FSupport->OpenBlockVector = SupportedStruct.GetSmallCardinal();
          FSupport->BlockVector = SupportedStruct.GetSmallCardinal();
          unsigned int ExtensionCount;
          ExtensionCount = SupportedStruct.GetCardinal();
          for (unsigned int i = 0; i < ExtensionCount; i++)
          {
            FSupport->AttribExtensions->Add(SupportedStruct.GetAnsiString());
          }
          ExtensionCount = SupportedStruct.GetCardinal();
          for (unsigned int i = 0; i < ExtensionCount; i++)
          {
            FSupport->Extensions->Add(SupportedStruct.GetAnsiString());
          }
        }

        if (FTerminal->Log->Logging)
        {
          FTerminal->LogEvent(FORMAT(
            L"Server support information (%s):\n"
             "  Attribute mask: %x, Attribute bits: %x, Open flags: %x\n"
             "  Access mask: %x, Open block vector: %x, Block vector: %x, Max read size: %d\n",
            (ExtensionName,
             int(FSupport->AttributeMask),
             int(FSupport->AttributeBits),
             int(FSupport->OpenFlags),
             int(FSupport->AccessMask),
             int(FSupport->OpenBlockVector),
             int(FSupport->BlockVector),
             int(FSupport->MaxReadSize))));
          FTerminal->LogEvent(FORMAT(L"  Attribute extensions (%d)\n", (FSupport->AttribExtensions->Count)));
          for (int Index = 0; Index < FSupport->AttribExtensions->Count; Index++)
          {
            FTerminal->LogEvent(
              FORMAT(L"    %s", (FSupport->AttribExtensions->Strings[Index])));
          }
          FTerminal->LogEvent(FORMAT(L"  Extensions (%d)\n", (FSupport->Extensions->Count)));
          for (int Index = 0; Index < FSupport->Extensions->Count; Index++)
          {
            FTerminal->LogEvent(
              FORMAT(L"    %s", (FSupport->Extensions->Strings[Index])));
          }
        }
      }
      else if (ExtensionName == SFTP_EXT_VENDOR_ID)
      {
        TSFTPPacket VendorIdStruct(ExtensionData);
        UnicodeString VendorName(VendorIdStruct.GetAnsiString());
        UnicodeString ProductName(VendorIdStruct.GetAnsiString());
        UnicodeString ProductVersion(VendorIdStruct.GetAnsiString());
        __int64 ProductBuildNumber = VendorIdStruct.GetInt64();
        FTerminal->LogEvent(FORMAT(L"Server software: %s %s (%d) by %s",
          (ProductName, ProductVersion, int(ProductBuildNumber), VendorName)));
      }
      else if (ExtensionName == SFTP_EXT_FSROOTS)
      {
        FTerminal->LogEvent(L"File system roots:\n");
        DebugAssert(FFixedPaths == NULL);
        FFixedPaths = new TStringList();
        try
        {
          TSFTPPacket RootsPacket(ExtensionData);
          while (RootsPacket.GetNextData() != NULL)
          {
            unsigned long Dummy = RootsPacket.GetCardinal();
            if (Dummy != 1)
            {
              break;
            }
            else
            {
              unsigned char Drive = RootsPacket.GetByte();
              unsigned char MaybeType = RootsPacket.GetByte();
              FTerminal->LogEvent(FORMAT(L"  %s: (type %d)", (static_cast<char>(Drive), (int)MaybeType)));
              FFixedPaths->Add(FORMAT(L"%s:", (static_cast<char>(Drive))));
            }
          }
        }
        catch(Exception & E)
        {
          FFixedPaths->Clear();
          FTerminal->LogEvent(FORMAT(L"Failed to decode %s extension",
            (SFTP_EXT_FSROOTS)));
          FTerminal->HandleException(&E);
        }
      }
      else if (ExtensionName == SFTP_EXT_VERSIONS)
      {
        // first try legacy decoding according to incorrect encoding
        // (structure-like) as of VShell (bug no longer present as of 4.0.3).
        TSFTPPacket VersionsPacket(ExtensionData);
        unsigned int StringSize;
        if (VersionsPacket.CanGetString(StringSize) &&
            (StringSize == VersionsPacket.RemainingLength))
        {
          UnicodeString Versions = VersionsPacket.GetAnsiString();
          FTerminal->LogEvent(FORMAT(L"SFTP versions supported by the server (VShell format): %s",
            (Versions)));
        }
        else
        {
          // if that fails, fallback to proper decoding
          FTerminal->LogEvent(FORMAT(L"SFTP versions supported by the server: %s",
            (AnsiToString(ExtensionData))));
        }
      }
      else if (ExtensionName == SFTP_EXT_STATVFS)
      {
        UnicodeString StatVfsVersion = AnsiToString(ExtensionData);
        if (StatVfsVersion == SFTP_EXT_STATVFS_VALUE_V2)
        {
          FSupportsStatVfsV2 = true;
          FTerminal->LogEvent(FORMAT(L"Supports %s extension version %s", (ExtensionName, ExtensionDisplayData)));
        }
        else
        {
          FTerminal->LogEvent(FORMAT(L"Unsupported %s extension version %s", (ExtensionName, ExtensionDisplayData)));
        }
      }
      else if (ExtensionName == SFTP_EXT_HARDLINK)
      {
        UnicodeString HardlinkVersion = AnsiToString(ExtensionData);
        if (HardlinkVersion == SFTP_EXT_HARDLINK_VALUE_V1)
        {
          FSupportsHardlink = true;
          FTerminal->LogEvent(FORMAT(L"Supports %s extension version %s", (ExtensionName, ExtensionDisplayData)));
        }
        else
        {
          FTerminal->LogEvent(FORMAT(L"Unsupported %s extension version %s", (ExtensionName, ExtensionDisplayData)));
        }
      }
      else
      {
        FTerminal->LogEvent(FORMAT(L"Unknown server extension %s=%s",
          (ExtensionName, ExtensionDisplayData)));
      }
      FExtensions->Values[ExtensionName] = ExtensionDisplayData;
    }

    if (SupportsExtension(SFTP_EXT_VENDOR_ID))
    {
      TSFTPPacket Packet(SSH_FXP_EXTENDED);
      Packet.AddString(SFTP_EXT_VENDOR_ID);
      Packet.AddString(FTerminal->Configuration->CompanyName);
      Packet.AddString(FTerminal->Configuration->ProductName);
      Packet.AddString(FTerminal->Configuration->ProductVersion);
      Packet.AddInt64(LOWORD(FTerminal->Configuration->FixedApplicationInfo->dwFileVersionLS));
      SendPacket(&Packet);
      // we are not interested in the response, do not wait for it
      ReceiveResponse(&Packet, &Packet);
      //ReserveResponse(&Packet, NULL);
    }
  }

  if (FVersion < 4)
  {
    // currently enable the bug for all servers (really known on OpenSSH)
    FSignedTS = (FTerminal->SessionData->SFTPBug[sbSignedTS] == asOn) ||
      (FTerminal->SessionData->SFTPBug[sbSignedTS] == asAuto);
    if (FSignedTS)
    {
      FTerminal->LogEvent(L"We believe the server has signed timestamps bug");
    }
  }
  else
  {
    FSignedTS = false;
  }

  switch (FTerminal->SessionData->NotUtf)
  {
    case asOff:
      FUtfStrings = asOn;
      FTerminal->LogEvent(L"We will use UTF-8 strings as configured");
      break;

    default:
      DebugFail();
    case asAuto:
      // Nb, Foxit server does not exist anymore
      if (GetSessionInfo().SshImplementation.Pos(L"Foxit-WAC-Server") == 1)
      {
        FUtfStrings = asOff;
        FTerminal->LogEvent(L"We will not use UTF-8 strings as the server is known not to use them");
      }
      else
      {
        if (FVersion >= 4)
        {
          FTerminal->LogEvent(L"We will use UTF-8 strings as it is mandatory with SFTP version 4 and newer");
          FUtfStrings = asOn;
        }
        else
        {
          FTerminal->LogEvent(L"We will use UTF-8 strings until server sends an invalid UTF-8 string as with SFTP version 3 and older UTF-8 strings are not mandatory");
          FUtfStrings = asAuto;
          FUtfDisablingAnnounced = false;
        }
      }
      break;

    case asOn:
      FTerminal->LogEvent(L"We will not use UTF-8 strings as configured");
      FUtfStrings = asOff;
      break;
  }

  FMaxPacketSize = FTerminal->SessionData->SFTPMaxPacketSize;
  if (FMaxPacketSize == 0)
  {
    if ((FSecureShell->SshImplementation == sshiOpenSSH) && (FVersion == 3) && !FSupport->Loaded)
    {
      FMaxPacketSize = 4 + (256 * 1024); // len + 256kB payload
      FTerminal->LogEvent(FORMAT(L"Limiting packet size to OpenSSH sftp-server limit of %d bytes",
        (int(FMaxPacketSize))));
    }
    // full string is "1.77 sshlib: Momentum SSH Server",
    // possibly it is sshlib-related
    else if (GetSessionInfo().SshImplementation.Pos(L"Momentum SSH Server") != 0)
    {
      FMaxPacketSize = 4 + (32 * 1024);
      FTerminal->LogEvent(FORMAT(L"Limiting packet size to Momentum sftp-server limit of %d bytes",
        (int(FMaxPacketSize))));
    }
  }
}
//---------------------------------------------------------------------------
char * __fastcall TSFTPFileSystem::GetEOL() const
{
  if (FVersion >= 4)
  {
    DebugAssert(!FEOL.IsEmpty());
    return FEOL.c_str();
  }
  else
  {
    return EOLToStr(FTerminal->SessionData->EOLType);
  }
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::LookupUsersGroups()
{
  DebugAssert(SupportsExtension(SFTP_EXT_OWNER_GROUP));

  TSFTPPacket PacketOwners(SSH_FXP_EXTENDED);
  TSFTPPacket PacketGroups(SSH_FXP_EXTENDED);

  TSFTPPacket * Packets[] = { &PacketOwners, &PacketGroups };
  TRemoteTokenList * Lists[] = { &FTerminal->FUsers, &FTerminal->FGroups };
  wchar_t ListTypes[] = { OGQ_LIST_OWNERS, OGQ_LIST_GROUPS };

  for (size_t Index = 0; Index < LENOF(Packets); Index++)
  {
    TSFTPPacket * Packet = Packets[Index];
    Packet->AddString(SFTP_EXT_OWNER_GROUP);
    Packet->AddByte(ListTypes[Index]);
    SendPacket(Packet);
    ReserveResponse(Packet, Packet);
  }

  for (size_t Index = 0; Index < LENOF(Packets); Index++)
  {
    TSFTPPacket * Packet = Packets[Index];

    ReceiveResponse(Packet, Packet, SSH_FXP_EXTENDED_REPLY, asOpUnsupported);

    if ((Packet->Type != SSH_FXP_EXTENDED_REPLY) ||
        (Packet->GetAnsiString() != SFTP_EXT_OWNER_GROUP_REPLY))
    {
      FTerminal->LogEvent(FORMAT(L"Invalid response to %s", (SFTP_EXT_OWNER_GROUP)));
    }
    else
    {
      TRemoteTokenList & List = *Lists[Index];
      unsigned long Count = Packet->GetCardinal();

      List.Clear();
      for (unsigned long Item = 0; Item < Count; Item++)
      {
        TRemoteToken Token(Packet->GetString(FUtfStrings));
        List.Add(Token);
        if (&List == &FTerminal->FGroups)
        {
          FTerminal->FMembership.Add(Token);
        }
      }
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::ReadCurrentDirectory()
{
  if (!FDirectoryToChangeTo.IsEmpty())
  {
    FCurrentDirectory = FDirectoryToChangeTo;
    FDirectoryToChangeTo = L"";
  }
  else if (FCurrentDirectory.IsEmpty())
  {
    // this happens only after startup when default remote directory is not specified
    FCurrentDirectory = GetHomeDirectory();
  }
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::HomeDirectory()
{
  ChangeDirectory(GetHomeDirectory());
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::TryOpenDirectory(const UnicodeString Directory)
{
  FTerminal->LogEvent(FORMAT(L"Trying to open directory \"%s\".", (Directory)));
  TRemoteFile * File;
  CustomReadFile(Directory, File, SSH_FXP_LSTAT, NULL, asOpUnsupported);
  if (File == NULL)
  {
    // File can be NULL only when server does not support SSH_FXP_LSTAT.
    // Fallback to legacy solution, which in turn does not allow entering
    // traverse-only (chmod 110) directories.
    // This is workaround for http://www.ftpshell.com/
    TSFTPPacket Packet(SSH_FXP_OPENDIR);
    Packet.AddPathString(UnixExcludeTrailingBackslash(Directory), FUtfStrings);
    SendPacketAndReceiveResponse(&Packet, &Packet, SSH_FXP_HANDLE);
    RawByteString Handle = Packet.GetFileHandle();
    Packet.ChangeType(SSH_FXP_CLOSE);
    Packet.AddString(Handle);
    SendPacketAndReceiveResponse(&Packet, &Packet, SSH_FXP_STATUS, asAll);
  }
  else
  {
    delete File;
  }
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::AnnounceFileListOperation()
{
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::ChangeDirectory(const UnicodeString Directory)
{
  UnicodeString Path, Current;

  Current = !FDirectoryToChangeTo.IsEmpty() ? FDirectoryToChangeTo : FCurrentDirectory;
  Path = RealPath(Directory, Current);

  // to verify existence of directory try to open it (SSH_FXP_REALPATH succeeds
  // for invalid paths on some systems, like CygWin)
  TryOpenDirectory(Path);

  // if open dir did not fail, directory exists -> success.
  FDirectoryToChangeTo = Path;
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::CachedChangeDirectory(const UnicodeString Directory)
{
  FDirectoryToChangeTo = UnixExcludeTrailingBackslash(Directory);
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::ReadDirectory(TRemoteFileList * FileList)
{
  DebugAssert(FileList && !FileList->Directory.IsEmpty());

  UnicodeString Directory;
  Directory = UnixExcludeTrailingBackslash(LocalCanonify(FileList->Directory));
  FTerminal->LogEvent(FORMAT(L"Listing directory \"%s\".", (Directory)));

  // moved before SSH_FXP_OPENDIR, so directory listing does not retain
  // old data (e.g. parent directory) when reading fails
  FileList->Reset();

  TSFTPPacket Packet(SSH_FXP_OPENDIR);
  RawByteString Handle;

  try
  {
    Packet.AddPathString(Directory, FUtfStrings);

    SendPacketAndReceiveResponse(&Packet, &Packet, SSH_FXP_HANDLE);

    Handle = Packet.GetFileHandle();
  }
  catch(...)
  {
    if (FTerminal->Active)
    {
      FileList->AddFile(new TRemoteParentDirectory(FTerminal));
    }
    throw;
  }

  TSFTPPacket Response;
  try
  {
    bool isEOF = false;
    int Total = 0;
    bool HasParentDirectory = false;
    TRemoteFile * File;

    Packet.ChangeType(SSH_FXP_READDIR);
    Packet.AddString(Handle);

    SendPacket(&Packet);

    do
    {
      ReceiveResponse(&Packet, &Response);
      if (Response.Type == SSH_FXP_NAME)
      {
        TSFTPPacket ListingPacket = Response;

        Packet.ChangeType(SSH_FXP_READDIR);
        Packet.AddString(Handle);

        SendPacket(&Packet);
        ReserveResponse(&Packet, &Response);

        unsigned int Count = ListingPacket.GetCardinal();

        int ResolvedLinks = 0;
        for (unsigned long Index = 0; !isEOF && (Index < Count); Index++)
        {
          File = LoadFile(&ListingPacket, NULL, L"", FileList);
          if (FTerminal->Configuration->ActualLogProtocol >= 1)
          {
            FTerminal->LogEvent(FORMAT(L"Read file '%s' from listing", (File->FileName)));
          }
          if (File->LinkedFile != NULL)
          {
            ResolvedLinks++;
          }
          if (File->IsParentDirectory)
          {
            HasParentDirectory = true;
          }
          FileList->AddFile(File);
          Total++;

          if (Total % 10 == 0)
          {
            FTerminal->DoReadDirectoryProgress(Total, ResolvedLinks, isEOF);
            if (isEOF)
            {
              FTerminal->DoReadDirectoryProgress(-2, 0, isEOF);
            }
          }
        }

        if ((FVersion >= 6) &&
            // As of 7.0.9 the Cerberus SFTP server always sets the end-of-list to true.
            (FSecureShell->SshImplementation != sshiCerberus) &&
            ListingPacket.CanGetBool())
        {
          isEOF = ListingPacket.GetBool();
        }

        if (Count == 0)
        {
          FTerminal->LogEvent(L"Empty directory listing packet. Aborting directory reading.");
          isEOF = true;
        }
      }
      else if (Response.Type == SSH_FXP_STATUS)
      {
        isEOF = (GotStatusPacket(&Response, asEOF) == SSH_FX_EOF);
      }
      else
      {
        FTerminal->FatalError(NULL, FMTLOAD(SFTP_INVALID_TYPE, ((int)Response.Type)));
      }
    }
    while (!isEOF);

    if (Total == 0)
    {
      bool Failure = false;
      // no point reading parent of root directory,
      // moreover CompleteFTP terminates session upon attempt to do so
      if (IsUnixRootPath(FileList->Directory))
      {
        File = NULL;
      }
      else
      {
        // Empty file list -> probably "permission denied", we
        // at least get link to parent directory ("..")
        try
        {
          FTerminal->ExceptionOnFail = true;
          try
          {
            File = NULL;
            FTerminal->ReadFile(
              UnixIncludeTrailingBackslash(FileList->Directory) + PARENTDIRECTORY, File);
          }
          __finally
          {
            FTerminal->ExceptionOnFail = false;
          }
        }
        catch(Exception &E)
        {
          if (E.InheritsFrom(__classid(EFatal)))
          {
            throw;
          }
          else
          {
            File = NULL;
            Failure = true;
          }
        }
      }

      // on some systems even getting ".." fails, we create dummy ".." instead
      if (File == NULL)
      {
        File = new TRemoteParentDirectory(FTerminal);
      }

      DebugAssert(File && File->IsParentDirectory);
      FileList->AddFile(File);

      if (Failure)
      {
        throw ExtException(
          NULL, FMTLOAD(EMPTY_DIRECTORY, (FileList->Directory)),
          HELP_EMPTY_DIRECTORY);
      }
    }
    else
    {
      if (!HasParentDirectory)
      {
        FileList->AddFile(new TRemoteParentDirectory(FTerminal));
      }
    }
  }
  __finally
  {
    if (FTerminal->Active)
    {
      Packet.ChangeType(SSH_FXP_CLOSE);
      Packet.AddString(Handle);
      SendPacket(&Packet);
      // we are not interested in the response, do not wait for it
      ReserveResponse(&Packet, NULL);
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::ReadSymlink(TRemoteFile * SymlinkFile,
  TRemoteFile *& File)
{
  DebugAssert(SymlinkFile && SymlinkFile->IsSymLink);
  DebugAssert(FVersion >= 3); // symlinks are supported with SFTP version 3 and later

  // need to use full filename when resolving links within subdirectory
  // (i.e. for download)
  UnicodeString FileName = LocalCanonify(
    SymlinkFile->Directory != NULL ? SymlinkFile->FullFileName : SymlinkFile->FileName);

  TSFTPPacket ReadLinkPacket(SSH_FXP_READLINK);
  ReadLinkPacket.AddPathString(FileName, FUtfStrings);
  SendPacket(&ReadLinkPacket);
  ReserveResponse(&ReadLinkPacket, &ReadLinkPacket);

  // send second request before reading response to first one
  // (performance benefit)
  TSFTPPacket AttrsPacket(SSH_FXP_STAT);
  AttrsPacket.AddPathString(FileName, FUtfStrings);
  if (FVersion >= 4)
  {
    AttrsPacket.AddCardinal(SSH_FILEXFER_ATTR_COMMON);
  }
  SendPacket(&AttrsPacket);
  ReserveResponse(&AttrsPacket, &AttrsPacket);

  ReceiveResponse(&ReadLinkPacket, &ReadLinkPacket, SSH_FXP_NAME);
  if (ReadLinkPacket.GetCardinal() != 1)
  {
    FTerminal->FatalError(NULL, LoadStr(SFTP_NON_ONE_FXP_NAME_PACKET));
  }
  SymlinkFile->LinkTo = ReadLinkPacket.GetPathString(FUtfStrings);
  FTerminal->LogEvent(FORMAT(L"Link resolved to \"%s\".", (SymlinkFile->LinkTo)));

  ReceiveResponse(&AttrsPacket, &AttrsPacket, SSH_FXP_ATTRS);
  // SymlinkFile->FileName was used instead SymlinkFile->LinkTo before, why?
  File = LoadFile(&AttrsPacket, SymlinkFile,
    UnixExtractFileName(SymlinkFile->LinkTo));
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::ReadFile(const UnicodeString FileName,
  TRemoteFile *& File)
{
  CustomReadFile(FileName, File, SSH_FXP_LSTAT);
}
//---------------------------------------------------------------------------
bool __fastcall TSFTPFileSystem::RemoteFileExists(const UnicodeString FullPath,
  TRemoteFile ** File)
{
  bool Result;
  try
  {
    TRemoteFile * AFile;
    CustomReadFile(FullPath, AFile, SSH_FXP_LSTAT, NULL, asNoSuchFile);
    Result = (AFile != NULL);
    if (Result)
    {
      if (File)
      {
        *File = AFile;
      }
      else
      {
        delete AFile;
      }
    }
  }
  catch(...)
  {
    if (!FTerminal->Active)
    {
      throw;
    }
    Result = false;
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::SendCustomReadFile(TSFTPPacket * Packet,
  TSFTPPacket * Response, unsigned long Flags)
{
  if (FVersion >= 4)
  {
    Packet->AddCardinal(Flags);
  }
  SendPacket(Packet);
  ReserveResponse(Packet, Response);
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::CustomReadFile(const UnicodeString FileName,
  TRemoteFile *& File, unsigned char Type, TRemoteFile * ALinkedByFile,
  int AllowStatus)
{
  unsigned long Flags = SSH_FILEXFER_ATTR_SIZE | SSH_FILEXFER_ATTR_PERMISSIONS |
    SSH_FILEXFER_ATTR_ACCESSTIME | SSH_FILEXFER_ATTR_MODIFYTIME |
    SSH_FILEXFER_ATTR_OWNERGROUP;
  TSFTPPacket Packet(Type);
  Packet.AddPathString(LocalCanonify(FileName), FUtfStrings);
  SendCustomReadFile(&Packet, &Packet, Flags);
  ReceiveResponse(&Packet, &Packet, SSH_FXP_ATTRS, AllowStatus);

  if (Packet.Type == SSH_FXP_ATTRS)
  {
    File = LoadFile(&Packet, ALinkedByFile, UnixExtractFileName(FileName));
  }
  else
  {
    DebugAssert(AllowStatus > 0);
    File = NULL;
  }
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::DoDeleteFile(const UnicodeString FileName, unsigned char Type)
{
  TSFTPPacket Packet(Type);
  UnicodeString RealFileName = LocalCanonify(FileName);
  Packet.AddPathString(RealFileName, FUtfStrings);
  SendPacketAndReceiveResponse(&Packet, &Packet, SSH_FXP_STATUS);
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::DeleteFile(const UnicodeString FileName,
  const TRemoteFile * File, int Params, TRmSessionAction & Action)
{
  unsigned char Type;
  if (File && File->IsDirectory && FTerminal->CanRecurseToDirectory(File))
  {
    if (FLAGCLEAR(Params, dfNoRecursive))
    {
      try
      {
        FTerminal->ProcessDirectory(FileName, FTerminal->DeleteFile, &Params);
      }
      catch(...)
      {
        Action.Cancel();
        throw;
      }
    }
    Type = SSH_FXP_RMDIR;
  }
  else
  {
    Type = SSH_FXP_REMOVE;
  }

  DoDeleteFile(FileName, Type);
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::RenameFile(const UnicodeString FileName,
  const UnicodeString NewName)
{
  TSFTPPacket Packet(SSH_FXP_RENAME);
  UnicodeString RealName = LocalCanonify(FileName);
  Packet.AddPathString(RealName, FUtfStrings);
  UnicodeString TargetName;
  if (UnixExtractFilePath(NewName).IsEmpty())
  {
    // rename case (TTerminal::RenameFile)
    TargetName = UnixExtractFilePath(RealName) + NewName;
  }
  else
  {
    TargetName = LocalCanonify(NewName);
  }
  Packet.AddPathString(TargetName, FUtfStrings);
  if (FVersion >= 5)
  {
    Packet.AddCardinal(0);
  }
  SendPacketAndReceiveResponse(&Packet, &Packet, SSH_FXP_STATUS);
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::CopyFile(const UnicodeString FileName,
  const UnicodeString NewName)
{
  // Implemented by ProFTPD/mod_sftp and Bitvise WinSSHD (without announcing it)
  DebugAssert(SupportsExtension(SFTP_EXT_COPY_FILE) || (FSecureShell->SshImplementation == sshiBitvise));
  TSFTPPacket Packet(SSH_FXP_EXTENDED);
  Packet.AddString(SFTP_EXT_COPY_FILE);
  Packet.AddPathString(Canonify(FileName), FUtfStrings);
  Packet.AddPathString(Canonify(NewName), FUtfStrings);
  Packet.AddBool(false);
  SendPacketAndReceiveResponse(&Packet, &Packet, SSH_FXP_STATUS);
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::CreateDirectory(const UnicodeString DirName)
{
  TSFTPPacket Packet(SSH_FXP_MKDIR);
  UnicodeString CanonifiedName = Canonify(DirName);
  Packet.AddPathString(CanonifiedName, FUtfStrings);
  Packet.AddProperties(NULL, 0, true, FVersion, FUtfStrings, NULL);
  SendPacketAndReceiveResponse(&Packet, &Packet, SSH_FXP_STATUS);
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::CreateLink(const UnicodeString FileName,
  const UnicodeString PointTo, bool Symbolic)
{
  // Cerberus server does not even response to LINK or SYMLINK,
  // Although its log says:
  // Unrecognized SFTP client command: (20)
  // Unknown SFTP packet - Sending Unsupported OP response

  DebugAssert(FVersion >= 3); // links are supported with SFTP version 3 and later
  bool UseLink = (FVersion >= 6);
  bool UseHardlink = !Symbolic && !UseLink && FSupportsHardlink;
  TSFTPPacket Packet(UseHardlink ? SSH_FXP_EXTENDED : (UseLink ? SSH_FXP_LINK : SSH_FXP_SYMLINK));
  if (UseHardlink)
  {
    Packet.AddString(SFTP_EXT_HARDLINK);
  }

  bool Buggy;
  // OpenSSH hardlink extension always uses the "wrong" order
  // as it's defined as such to mimic OpenSSH symlink bug
  if (UseHardlink)
  {
    Buggy = true; //sic
  }
  else
  {
    if (FTerminal->SessionData->SFTPBug[sbSymlink] == asOn)
    {
      Buggy = true;
      FTerminal->LogEvent(L"Forcing workaround for SFTP link bug");
    }
    else if (FTerminal->SessionData->SFTPBug[sbSymlink] == asOff)
    {
      Buggy = false;
    }
    else
    {
      if (UseLink)
      {
        if (FSecureShell->SshImplementation == sshiProFTPD)
        {
          // ProFTPD/mod_sftp followed OpenSSH symlink bug even for link implementation.
          // This will be fixed with the next release with
          // SSH version string bumbed to "mod_sftp/1.0.0"
          // http://bugs.proftpd.org/show_bug.cgi?id=4080
          UnicodeString ProFTPDVerStr = GetSessionInfo().SshImplementation;
          CutToChar(ProFTPDVerStr, L'/', false);
          int ProFTPDMajorVer = StrToIntDef(CutToChar(ProFTPDVerStr, L'.', false), 0);
          Buggy = (ProFTPDMajorVer == 0);
          if (Buggy)
          {
            FTerminal->LogEvent(L"We believe the server has SFTP link bug");
          }
        }
        else
        {
          Buggy = false;
        }
      }
      else
      {
        // ProFTPD/mod_sftp deliberately follows OpenSSH bug.
        // Though we should get here with ProFTPD only when user forced
        // SFTP version < 6 or when connecting to an ancient version of ProFTPD.
        Buggy =
          (FSecureShell->SshImplementation == sshiOpenSSH) ||
          (FSecureShell->SshImplementation == sshiProFTPD);
        if (Buggy)
        {
          FTerminal->LogEvent(L"We believe the server has SFTP symlink bug");
        }
      }
    }
  }

  UnicodeString FinalPointTo = PointTo;
  UnicodeString FinalFileName = Canonify(FileName);

  if (!Symbolic)
  {
    FinalPointTo = Canonify(PointTo);
  }

  if (!Buggy)
  {
    Packet.AddPathString(FinalFileName, FUtfStrings);
    Packet.AddPathString(FinalPointTo, FUtfStrings);
  }
  else
  {
    Packet.AddPathString(FinalPointTo, FUtfStrings);
    Packet.AddPathString(FinalFileName, FUtfStrings);
  }

  if (UseLink)
  {
    Packet.AddBool(Symbolic);
  }
  SendPacketAndReceiveResponse(&Packet, &Packet, SSH_FXP_STATUS);
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::ChangeFileProperties(const UnicodeString FileName,
  const TRemoteFile * /*File*/, const TRemoteProperties * AProperties,
  TChmodSessionAction & Action)
{
  DebugAssert(AProperties != NULL);

  TRemoteFile * File;

  UnicodeString RealFileName = LocalCanonify(FileName);
  ReadFile(RealFileName, File);

  try
  {
    DebugAssert(File);

    if (File->IsDirectory && FTerminal->CanRecurseToDirectory(File) && AProperties->Recursive)
    {
      try
      {
        FTerminal->ProcessDirectory(FileName, FTerminal->ChangeFileProperties,
          (void*)AProperties);
      }
      catch(...)
      {
        Action.Cancel();
        throw;
      }
    }

    // SFTP can change owner and group at the same time only, not individually.
    // Fortunately we know current owner/group, so if only one is present,
    // we can supplement the other.
    TRemoteProperties Properties(*AProperties);
    if (Properties.Valid.Contains(vpGroup) &&
        !Properties.Valid.Contains(vpOwner))
    {
      Properties.Owner = File->Owner;
      Properties.Valid << vpOwner;
    }
    else if (Properties.Valid.Contains(vpOwner) &&
             !Properties.Valid.Contains(vpGroup))
    {
      Properties.Group = File->Group;
      Properties.Valid << vpGroup;
    }

    TSFTPPacket Packet(SSH_FXP_SETSTAT);
    Packet.AddPathString(RealFileName, FUtfStrings);
    Packet.AddProperties(&Properties, *File->Rights, File->IsDirectory, FVersion, FUtfStrings, &Action);
    SendPacketAndReceiveResponse(&Packet, &Packet, SSH_FXP_STATUS);
  }
  __finally
  {
    delete File;
  }
}
//---------------------------------------------------------------------------
bool __fastcall TSFTPFileSystem::LoadFilesProperties(TStrings * FileList)
{
  bool Result = false;
  // without knowledge of server's capabilities, this all make no sense
  if (FSupport->Loaded || (FSecureShell->SshImplementation == sshiBitvise))
  {
    TFileOperationProgressType Progress(&FTerminal->DoProgress, &FTerminal->DoFinished);
    Progress.Start(foGetProperties, osRemote, FileList->Count);

    FTerminal->FOperationProgress = &Progress;

    static int LoadFilesPropertiesQueueLen = 5;
    TSFTPLoadFilesPropertiesQueue Queue(this);
    try
    {
      if (Queue.Init(LoadFilesPropertiesQueueLen, FileList))
      {
        TRemoteFile * File;
        TSFTPPacket Packet;
        bool Next;
        do
        {
          Next = Queue.ReceivePacket(&Packet, File);
          DebugAssert((Packet.Type == SSH_FXP_ATTRS) || (Packet.Type == SSH_FXP_STATUS));
          if (Packet.Type == SSH_FXP_ATTRS)
          {
            DebugAssert(File != NULL);
            Progress.SetFile(File->FileName);
            LoadFile(File, &Packet);
            Result = true;
            TOnceDoneOperation OnceDoneOperation;
            Progress.Finish(File->FileName, true, OnceDoneOperation);
          }

          if (Progress.Cancel != csContinue)
          {
            Next = false;
          }
        }
        while (Next);
      }
    }
    __finally
    {
      Queue.DisposeSafe();
      FTerminal->FOperationProgress = NULL;
      Progress.Stop();
    }
    // queue is discarded here
  }

  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::DoCalculateFilesChecksum(
  const UnicodeString & Alg, const UnicodeString & SftpAlg,
  TStrings * FileList, TStrings * Checksums,
  TCalculatedChecksumEvent OnCalculatedChecksum,
  TFileOperationProgressType * OperationProgress, bool FirstLevel)
{
  TOnceDoneOperation OnceDoneOperation; // not used

  // recurse into subdirectories only if we have callback function
  if (OnCalculatedChecksum != NULL)
  {
    for (int Index = 0; Index < FileList->Count; Index++)
    {
      TRemoteFile * File = (TRemoteFile *)FileList->Objects[Index];
      DebugAssert(File != NULL);
      if (File->IsDirectory && FTerminal->CanRecurseToDirectory(File) &&
          !File->IsParentDirectory && !File->IsThisDirectory)
      {
        OperationProgress->SetFile(File->FileName);
        TRemoteFileList * SubFiles =
          FTerminal->CustomReadDirectoryListing(File->FullFileName, false);

        if (SubFiles != NULL)
        {
          TStrings * SubFileList = new TStringList();
          bool Success = false;
          try
          {
            OperationProgress->SetFile(File->FileName);

            for (int Index = 0; Index < SubFiles->Count; Index++)
            {
              TRemoteFile * SubFile = SubFiles->Files[Index];
              SubFileList->AddObject(SubFile->FullFileName, SubFile);
            }

            // do not collect checksums for files in subdirectories,
            // only send back checksums via callback
            DoCalculateFilesChecksum(Alg, SftpAlg, SubFileList, NULL,
              OnCalculatedChecksum, OperationProgress, false);

            Success = true;
          }
          __finally
          {
            delete SubFiles;
            delete SubFileList;

            if (FirstLevel)
            {
              OperationProgress->Finish(File->FileName, Success, OnceDoneOperation);
            }
          }
        }
      }
    }
  }

  static int CalculateFilesChecksumQueueLen = 5;
  TSFTPCalculateFilesChecksumQueue Queue(this);
  try
  {
    if (Queue.Init(CalculateFilesChecksumQueueLen, SftpAlg, FileList))
    {
      TSFTPPacket Packet;
      bool Next;
      do
      {
        bool Success = false;
        UnicodeString Checksum;
        TRemoteFile * File = NULL;

        try
        {
          TChecksumSessionAction Action(FTerminal->ActionLog);
          try
          {
            Next = Queue.ReceivePacket(&Packet, File);
            DebugAssert(Packet.Type == SSH_FXP_EXTENDED_REPLY);

            OperationProgress->SetFile(File->FileName);
            Action.FileName(FTerminal->AbsolutePath(File->FullFileName, true));

            // skip alg
            Packet.GetAnsiString();
            Checksum = BytesToHex(reinterpret_cast<const unsigned char*>(Packet.GetNextData(Packet.RemainingLength)), Packet.RemainingLength, false);
            if (OnCalculatedChecksum != NULL)
            {
              OnCalculatedChecksum(File->FileName, Alg, Checksum);
            }
            Action.Checksum(Alg, Checksum);

            Success = true;
          }
          catch (Exception & E)
          {
            FTerminal->RollbackAction(Action, OperationProgress, &E);

            // Error formatting expanded from inline to avoid strange exceptions
            UnicodeString Error =
              FMTLOAD(CHECKSUM_ERROR,
                (File != NULL ? File->FullFileName : UnicodeString(L"")));
            FTerminal->CommandError(&E, Error);
            // TODO: retries? resume?
            Next = false;
          }

          if (Checksums != NULL)
          {
            Checksums->Add(Checksum);
          }
        }
        __finally
        {
          if (FirstLevel)
          {
            OperationProgress->Finish(File->FileName, Success, OnceDoneOperation);
          }
        }

        if (OperationProgress->Cancel != csContinue)
        {
          Next = false;
        }
      }
      while (Next);
    }
  }
  __finally
  {
    Queue.DisposeSafe();
  }
  // queue is discarded here
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::CalculateFilesChecksum(const UnicodeString & Alg,
  TStrings * FileList, TStrings * Checksums,
  TCalculatedChecksumEvent OnCalculatedChecksum)
{
  TFileOperationProgressType Progress(&FTerminal->DoProgress, &FTerminal->DoFinished);
  Progress.Start(foCalculateChecksum, osRemote, FileList->Count);

  UnicodeString NormalizedAlg = FindIdent(Alg, FChecksumAlgs.get());
  UnicodeString SftpAlg;
  int Index = FChecksumAlgs->IndexOf(NormalizedAlg);
  if (Index >= 0)
  {
    SftpAlg = FChecksumSftpAlgs->Strings[Index];
  }
  else
  {
    // try user-specified alg
    SftpAlg = NormalizedAlg;
  }

  FTerminal->FOperationProgress = &Progress;

  try
  {
    DoCalculateFilesChecksum(NormalizedAlg, SftpAlg, FileList, Checksums, OnCalculatedChecksum,
      &Progress, true);
  }
  __finally
  {
    FTerminal->FOperationProgress = NULL;
    Progress.Stop();
  }
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::CustomCommandOnFile(const UnicodeString /*FileName*/,
    const TRemoteFile * /*File*/, UnicodeString /*Command*/, int /*Params*/,
    TCaptureOutputEvent /*OutputEvent*/)
{
  DebugFail();
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::AnyCommand(const UnicodeString /*Command*/,
  TCaptureOutputEvent /*OutputEvent*/)
{
  DebugFail();
}
//---------------------------------------------------------------------------
TStrings * __fastcall TSFTPFileSystem::GetFixedPaths()
{
  return FFixedPaths;
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::SpaceAvailable(const UnicodeString Path,
  TSpaceAvailable & ASpaceAvailable)
{
  if (SupportsExtension(SFTP_EXT_SPACE_AVAILABLE) ||
      // See comment in IsCapable
      (FSecureShell->SshImplementation == sshiBitvise))
  {
    TSFTPPacket Packet(SSH_FXP_EXTENDED);
    Packet.AddString(SFTP_EXT_SPACE_AVAILABLE);
    Packet.AddPathString(LocalCanonify(Path), FUtfStrings);

    SendPacketAndReceiveResponse(&Packet, &Packet, SSH_FXP_EXTENDED_REPLY);

    ASpaceAvailable.BytesOnDevice = Packet.GetInt64();
    ASpaceAvailable.UnusedBytesOnDevice = Packet.GetInt64();
    ASpaceAvailable.BytesAvailableToUser = Packet.GetInt64();
    ASpaceAvailable.UnusedBytesAvailableToUser = Packet.GetInt64();
    // bytes-per-allocation-unit was added later to the protocol
    // (revision 07, while the extension was defined already in rev 06),
    // be tolerant
    if (Packet.CanGetCardinal())
    {
      ASpaceAvailable.BytesPerAllocationUnit = Packet.GetCardinal();
    }
    else if (Packet.CanGetSmallCardinal())
    {
      // See http://bugs.proftpd.org/show_bug.cgi?id=4079
      FTerminal->LogEvent(L"Assuming ProFTPD/mod_sftp bug of 2-byte bytes-per-allocation-unit field");
      ASpaceAvailable.BytesPerAllocationUnit = Packet.GetSmallCardinal();
    }
    else
    {
      FTerminal->LogEvent(L"Missing bytes-per-allocation-unit field");
    }
  }
  else if (DebugAlwaysTrue(FSupportsStatVfsV2))
  {
    // http://www.openbsd.org/cgi-bin/cvsweb/src/usr.bin/ssh/PROTOCOL?rev=HEAD;content-type=text/plain
    TSFTPPacket Packet(SSH_FXP_EXTENDED);
    Packet.AddString(SFTP_EXT_STATVFS);
    Packet.AddPathString(LocalCanonify(Path), FUtfStrings);

    SendPacketAndReceiveResponse(&Packet, &Packet, SSH_FXP_EXTENDED_REPLY);

    __int64 BlockSize = Packet.GetInt64(); // file system block size
    __int64 FundamentalBlockSize = Packet.GetInt64(); // fundamental fs block size
    __int64 Blocks = Packet.GetInt64(); // number of blocks (unit f_frsize)
    __int64 FreeBlocks = Packet.GetInt64(); // free blocks in file system
    __int64 AvailableBlocks = Packet.GetInt64(); // free blocks for non-root
    __int64 FileINodes = Packet.GetInt64(); // total file inodes
    __int64 FreeFileINodes = Packet.GetInt64(); // free file inodes
    __int64 AvailableFileINodes = Packet.GetInt64(); // free file inodes for to non-root
    __int64 SID = Packet.GetInt64(); // file system id
    __int64 Flags = Packet.GetInt64(); // bit mask of f_flag values
    __int64 NameMax = Packet.GetInt64(); // maximum filename length

    FTerminal->LogEvent(FORMAT(L"Block size: %s", (IntToStr(BlockSize))));
    FTerminal->LogEvent(FORMAT(L"Fundamental block size: %s", (IntToStr(FundamentalBlockSize))));
    FTerminal->LogEvent(FORMAT(L"Total blocks: %s", (IntToStr(Blocks))));
    FTerminal->LogEvent(FORMAT(L"Free blocks: %s", (IntToStr(FreeBlocks))));
    FTerminal->LogEvent(FORMAT(L"Free blocks for non-root: %s", (IntToStr(AvailableBlocks))));
    FTerminal->LogEvent(FORMAT(L"Total file inodes: %s", (IntToStr(FileINodes))));
    FTerminal->LogEvent(FORMAT(L"Free file inodes: %s", (IntToStr(FreeFileINodes))));
    FTerminal->LogEvent(FORMAT(L"Free file inodes for non-root: %s", (IntToStr(AvailableFileINodes))));
    FTerminal->LogEvent(FORMAT(L"File system ID: %s", (BytesToHex(reinterpret_cast<const unsigned char *>(&SID), sizeof(SID)))));
    UnicodeString FlagStr;
    if (FLAGSET(Flags, SFTP_EXT_STATVFS_ST_RDONLY))
    {
      AddToList(FlagStr, L"read-only", L",");
      Flags -= SFTP_EXT_STATVFS_ST_RDONLY;
    }
    if (FLAGSET(Flags, SFTP_EXT_STATVFS_ST_NOSUID))
    {
      AddToList(FlagStr, L"no-setuid", L",");
      Flags -= SFTP_EXT_STATVFS_ST_NOSUID;
    }
    if (Flags != 0)
    {
      AddToList(FlagStr, UnicodeString(L"0x") + IntToHex(Flags, 2), L",");
    }
    if (FlagStr.IsEmpty())
    {
      FlagStr = L"none";
    }
    FTerminal->LogEvent(FORMAT(L"Flags: %s", (FlagStr)));
    FTerminal->LogEvent(FORMAT(L"Max name length: %s", (IntToStr(NameMax))));

    ASpaceAvailable.BytesOnDevice = BlockSize * Blocks;
    ASpaceAvailable.UnusedBytesOnDevice = BlockSize * FreeBlocks;
    ASpaceAvailable.BytesAvailableToUser = 0;
    ASpaceAvailable.UnusedBytesAvailableToUser = BlockSize * AvailableBlocks;
    ASpaceAvailable.BytesPerAllocationUnit =
      (BlockSize > std::numeric_limits<unsigned long>::max()) ? 0 : static_cast<unsigned long>(BlockSize);
  }
}
//---------------------------------------------------------------------------
// transfer protocol
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::CopyToRemote(TStrings * FilesToCopy,
  const UnicodeString TargetDir, const TCopyParamType * CopyParam,
  int Params, TFileOperationProgressType * OperationProgress,
  TOnceDoneOperation & OnceDoneOperation)
{
  DebugAssert(FilesToCopy && OperationProgress);

  UnicodeString FileName, FileNameOnly;
  UnicodeString FullTargetDir = UnixIncludeTrailingBackslash(TargetDir);
  int Index = 0;
  while (Index < FilesToCopy->Count && !OperationProgress->Cancel)
  {
    bool Success = false;
    FileName = FilesToCopy->Strings[Index];
    FileNameOnly = ExtractFileName(FileName);
    DebugAssert(!FAvoidBusy);
    FAvoidBusy = true;

    try
    {
      try
      {
        if (FTerminal->SessionData->CacheDirectories)
        {
          FTerminal->DirectoryModified(TargetDir, false);

          if (DirectoryExists(ApiPath(FileName)))
          {
            FTerminal->DirectoryModified(UnixIncludeTrailingBackslash(TargetDir)+
              FileNameOnly, true);
          }
        }
        SFTPSourceRobust(FileName, FullTargetDir, CopyParam, Params, OperationProgress,
          tfFirstLevel);
        Success = true;
      }
      catch(EScpSkipFile & E)
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
      FAvoidBusy = false;
      OperationProgress->Finish(FileName, Success, OnceDoneOperation);
    }
    Index++;
  }
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::SFTPConfirmOverwrite(
  const UnicodeString & SourceFullFileName, UnicodeString & TargetFileName,
  const TCopyParamType * CopyParam, int Params, TFileOperationProgressType * OperationProgress,
  TSFTPOverwriteMode & OverwriteMode, const TOverwriteFileParams * FileParams)
{
  bool CanAppend = (FVersion < 4) || !OperationProgress->AsciiTransfer;
  unsigned int Answer;

  {
    TSuspendFileOperationProgress Suspend(OperationProgress);
    int Answers = qaYes | qaNo | qaCancel | qaYesToAll | qaNoToAll | qaAll | qaIgnore;

    // possibly we can allow alternate resume at least in some cases
    if (CanAppend)
    {
      Answers |= qaRetry;
    }
    TQueryButtonAlias Aliases[5];
    Aliases[0].Button = qaRetry;
    Aliases[0].Alias = LoadStr(APPEND_BUTTON);
    Aliases[0].GroupWith = qaNo;
    Aliases[0].GrouppedShiftState = TShiftState() << ssAlt;
    Aliases[1].Button = qaAll;
    Aliases[1].Alias = LoadStr(YES_TO_NEWER_BUTTON);
    Aliases[1].GroupWith = qaYes;
    Aliases[1].GrouppedShiftState = TShiftState() << ssCtrl;
    Aliases[2].Button = qaIgnore;
    Aliases[2].Alias = LoadStr(RENAME_BUTTON);
    Aliases[2].GroupWith = qaNo;
    Aliases[2].GrouppedShiftState = TShiftState() << ssCtrl;
    Aliases[3].Button = qaYesToAll;
    Aliases[3].GroupWith = qaYes;
    Aliases[3].GrouppedShiftState = TShiftState() << ssShift;
    Aliases[4].Button = qaNoToAll;
    Aliases[4].GroupWith = qaNo;
    Aliases[4].GrouppedShiftState = TShiftState() << ssShift;
    TQueryParams QueryParams(qpNeverAskAgainCheck);
    QueryParams.NoBatchAnswers = qaIgnore | qaRetry | qaAll;
    QueryParams.Aliases = Aliases;
    QueryParams.AliasesCount = LENOF(Aliases);
    Answer = FTerminal->ConfirmFileOverwrite(
      SourceFullFileName, TargetFileName, FileParams,
      Answers, &QueryParams,
      OperationProgress->Side == osLocal ? osRemote : osLocal,
      CopyParam, Params, OperationProgress);
  }

  if (CanAppend &&
      ((Answer == qaRetry) || (Answer == qaSkip)))
  {
    // duplicated in TTerminal::ConfirmFileOverwrite
    bool CanAlternateResume =
      (FileParams->DestSize < FileParams->SourceSize) && !OperationProgress->AsciiTransfer;
    TBatchOverwrite BatchOverwrite =
      FTerminal->EffectiveBatchOverwrite(SourceFullFileName, CopyParam, Params, OperationProgress, true);
    // when mode is forced by batch, never query user
    if (BatchOverwrite == boAppend)
    {
      OverwriteMode = omAppend;
    }
    else if (CanAlternateResume &&
             ((BatchOverwrite == boResume) || (BatchOverwrite == boAlternateResume)))
    {
      OverwriteMode = omResume;
    }
    // no other option, but append
    else if (!CanAlternateResume)
    {
      OverwriteMode = omAppend;
    }
    else
    {
      TQueryParams Params(0, HELP_APPEND_OR_RESUME);

      {
        TSuspendFileOperationProgress Suspend(OperationProgress);
        Answer = FTerminal->QueryUser(FORMAT(LoadStr(APPEND_OR_RESUME2), (SourceFullFileName)),
          NULL, qaYes | qaNo | qaNoToAll | qaCancel, &Params);
      }

      switch (Answer)
      {
        case qaYes:
          OverwriteMode = omAppend;
          break;

        case qaNo:
          OverwriteMode = omResume;
          break;

        case qaNoToAll:
          OverwriteMode = omResume;
          OperationProgress->BatchOverwrite = boAlternateResume;
          break;

        default: DebugFail(); //fallthru
        case qaCancel:
          if (!OperationProgress->Cancel)
          {
            OperationProgress->Cancel = csCancel;
          }
          Abort();
          break;
      }
    }
  }
  else if (Answer == qaIgnore)
  {
    if (FTerminal->PromptUser(FTerminal->SessionData, pkFileName, LoadStr(RENAME_TITLE), L"",
          LoadStr(RENAME_PROMPT2), true, 0, TargetFileName))
    {
      OverwriteMode = omOverwrite;
    }
    else
    {
      if (!OperationProgress->Cancel)
      {
        OperationProgress->Cancel = csCancel;
      }
      Abort();
    }
  }
  else
  {
    OverwriteMode = omOverwrite;
    switch (Answer)
    {
      case qaCancel:
        if (!OperationProgress->Cancel)
        {
          OperationProgress->Cancel = csCancel;
        }
        Abort();
        break;

      case qaNo:
        THROW_SKIP_FILE_NULL;
    }
  }
}
//---------------------------------------------------------------------------
bool TSFTPFileSystem::SFTPConfirmResume(const UnicodeString DestFileName,
  bool PartialBiggerThanSource, TFileOperationProgressType * OperationProgress)
{
  bool ResumeTransfer;
  DebugAssert(OperationProgress);
  if (PartialBiggerThanSource)
  {
    unsigned int Answer;
    {
      TSuspendFileOperationProgress Suspend(OperationProgress);
      TQueryParams Params(qpAllowContinueOnError, HELP_PARTIAL_BIGGER_THAN_SOURCE);
      Answer = FTerminal->QueryUser(
        FMTLOAD(PARTIAL_BIGGER_THAN_SOURCE, (DestFileName)), NULL,
          qaOK | qaAbort, &Params, qtWarning);
    }

    if (Answer == qaAbort)
    {
      if (!OperationProgress->Cancel)
      {
        OperationProgress->Cancel = csCancel;
      }
      Abort();
    }
    ResumeTransfer = false;
  }
  else if (FTerminal->Configuration->ConfirmResume)
  {
    unsigned int Answer;

    {
      TSuspendFileOperationProgress Suspend(OperationProgress);
      TQueryParams Params(qpAllowContinueOnError | qpNeverAskAgainCheck,
        HELP_RESUME_TRANSFER);
      // "abort" replaced with "cancel" to unify with "append/resume" query
      Answer = FTerminal->QueryUser(
        FMTLOAD(RESUME_TRANSFER2, (DestFileName)), NULL, qaYes | qaNo | qaCancel,
        &Params);
    }

    switch (Answer) {
      case qaNeverAskAgain:
        FTerminal->Configuration->ConfirmResume = false;
      case qaYes:
        ResumeTransfer = true;
        break;

      case qaNo:
        ResumeTransfer = false;
        break;

      case qaCancel:
        if (!OperationProgress->Cancel)
        {
          OperationProgress->Cancel = csCancel;
        }
        Abort();
        break;
    }
  }
  else
  {
    ResumeTransfer = true;
  }
  return ResumeTransfer;
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::SFTPSourceRobust(const UnicodeString FileName,
  const UnicodeString TargetDir, const TCopyParamType * CopyParam, int Params,
  TFileOperationProgressType * OperationProgress, unsigned int Flags)
{
  // the same in TFTPFileSystem

  TUploadSessionAction Action(FTerminal->ActionLog);
  TRobustOperationLoop RobustLoop(FTerminal, OperationProgress);

  do
  {
    bool ChildError = false;
    try
    {
      SFTPSource(FileName, TargetDir, CopyParam, Params, OperationProgress,
        Flags, Action, ChildError);
    }
    catch(Exception & E)
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
      // prevent overwrite and resume confirmations
      // (should not be set for directories!)
      Params |= cpNoConfirmation;
      // enable resume even if we are uploading into new directory
      Flags &= ~tfNewDirectory;
    }
  }
  while (RobustLoop.Retry());
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::SFTPSource(const UnicodeString FileName,
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

  TOpenRemoteFileParams OpenParams;
  OpenParams.OverwriteMode = omOverwrite;

  HANDLE File;
  __int64 MTime;
  __int64 Size;

  FTerminal->OpenLocalFile(FileName, GENERIC_READ, &OpenParams.LocalFileAttrs,
    &File, NULL, &MTime, NULL, &Size);

  bool Dir = FLAGSET(OpenParams.LocalFileAttrs, faDirectory);

  try
  {
    OperationProgress->SetFileInProgress();

    if (Dir)
    {
      Action.Cancel();
      SFTPDirectorySource(IncludeTrailingBackslash(FileName), TargetDir,
        OpenParams.LocalFileAttrs, CopyParam, Params, OperationProgress, Flags);
    }
    else
    {
      // File is regular file (not directory)
      DebugAssert(File);

      UnicodeString DestFileName =
        FTerminal->ChangeFileName(
          CopyParam, ExtractFileName(FileName), osLocal,
          FLAGSET(Flags, tfFirstLevel));
      UnicodeString DestFullName = LocalCanonify(TargetDir + DestFileName);
      UnicodeString DestPartialFullName;
      bool ResumeAllowed;
      bool ResumeTransfer = false;
      bool DestFileExists = false;
      TRights DestRights;

      __int64 ResumeOffset;

      FTerminal->LogEvent(FORMAT(L"Copying \"%s\" to remote directory started.", (FileName)));

      OperationProgress->SetLocalSize(Size);

      // Suppose same data size to transfer as to read
      // (not true with ASCII transfer)
      OperationProgress->SetTransferSize(OperationProgress->LocalSize);
      OperationProgress->TransferingFile = false;

      TDateTime Modification = UnixToDateTime(MTime, FTerminal->SessionData->DSTMode);

      // Will we use ASCII of BINARY file transfer?
      TFileMasks::TParams MaskParams;
      MaskParams.Size = Size;
      MaskParams.Modification = Modification;
      UnicodeString BaseFileName = FTerminal->GetBaseFileName(FileName);
      OperationProgress->SetAsciiTransfer(
        CopyParam->UseAsciiTransfer(BaseFileName, osLocal, MaskParams));
      FTerminal->LogEvent(
        UnicodeString((OperationProgress->AsciiTransfer ? L"Ascii" : L"Binary")) +
          L" transfer mode selected.");

      // should we check for interrupted transfer?
      ResumeAllowed = !OperationProgress->AsciiTransfer &&
        CopyParam->AllowResume(OperationProgress->LocalSize) &&
        IsCapable(fcRename);

      TOverwriteFileParams FileParams;
      FileParams.SourceSize = OperationProgress->LocalSize;
      FileParams.SourceTimestamp = Modification;

      if (ResumeAllowed)
      {
        DestPartialFullName = DestFullName + FTerminal->Configuration->PartialExt;

        if (FLAGCLEAR(Flags, tfNewDirectory))
        {
          FTerminal->LogEvent(L"Checking existence of file.");
          TRemoteFile * File = NULL;
          DestFileExists = RemoteFileExists(DestFullName, &File);

          OperationProgress->Progress();

          if (DestFileExists)
          {
            FTerminal->LogEvent(FORMAT(L"File exists: %s", (FTerminal->GetRemoteFileInfo(File))));
            OpenParams.DestFileSize = File->Size;
            FileParams.DestSize = OpenParams.DestFileSize;
            FileParams.DestTimestamp = File->Modification;
            DestRights = *File->Rights;
            // If destination file is symlink, never do resumable transfer,
            // as it would delete the symlink.
            if (File->IsSymLink)
            {
              ResumeAllowed = false;
              FTerminal->LogEvent(L"Existing file is symbolic link, not doing resumable transfer.");
            }
            // Also bit of heuristics to detect symlink on SFTP-3 and older
            // (which does not indicate symlink in SSH_FXP_ATTRS).
            // if file has all permissions and is small, then it is likely symlink.
            // also it is not likely that such a small file (if it is not symlink)
            // gets overwritten by large file (that would trigger resumable transfer).
            else if ((FVersion < 4) &&
                     ((*File->Rights & TRights::rfAll) == TRights::rfAll) &&
                     (File->Size < 100))
            {
              ResumeAllowed = false;
              FTerminal->LogEvent(L"Existing file looks like a symbolic link, not doing resumable transfer.");
            }
            // Also never do resumable transfer for file owned by other user
            // as deleting and recreating the file would change ownership.
            // This won't for work for SFTP-3 (OpenSSH) as it does not provide
            // owner name (only UID) and we know only logged in user name (not UID)
            else if (!File->Owner.Name.IsEmpty() && !SameUserName(File->Owner.Name, FTerminal->UserName))
            {
              ResumeAllowed = false;
              FTerminal->LogEvent(
                FORMAT(L"Existing file is owned by another user [%s], not doing resumable transfer.", (File->Owner.Name)));
            }

            delete File;
            File = NULL;
          }

          if (ResumeAllowed)
          {
            FTerminal->LogEvent(L"Checking existence of partially transfered file.");
            if (RemoteFileExists(DestPartialFullName, &File))
            {
              ResumeOffset = File->Size;
              delete File;
              File = NULL;

              bool PartialBiggerThanSource = (ResumeOffset > OperationProgress->LocalSize);
              if (FLAGCLEAR(Params, cpNoConfirmation) &&
                  FLAGCLEAR(Params, cpResume) &&
                  !CopyParam->ResumeTransfer(FileName))
              {
                ResumeTransfer = SFTPConfirmResume(DestFileName,
                  PartialBiggerThanSource, OperationProgress);
              }
              else
              {
                ResumeTransfer = !PartialBiggerThanSource;
              }

              if (!ResumeTransfer)
              {
                DoDeleteFile(DestPartialFullName, SSH_FXP_REMOVE);
                OperationProgress->Progress();
              }
              else
              {
                FTerminal->LogEvent(L"Resuming file transfer.");
              }
            }
            else
            {
              // partial upload file does not exists, check for full file
              if (DestFileExists)
              {
                UnicodeString PrevDestFileName = DestFileName;
                SFTPConfirmOverwrite(FileName, DestFileName,
                  CopyParam, Params, OperationProgress, OpenParams.OverwriteMode, &FileParams);
                if (PrevDestFileName != DestFileName)
                {
                  // update paths in case user changes the file name
                  DestFullName = LocalCanonify(TargetDir + DestFileName);
                  DestPartialFullName = DestFullName + FTerminal->Configuration->PartialExt;
                  FTerminal->LogEvent(L"Checking existence of new file.");
                  DestFileExists = RemoteFileExists(DestFullName, NULL);
                }
              }
            }
          }
        }
      }

      // will the transfer be resumable?
      bool DoResume = (ResumeAllowed && (OpenParams.OverwriteMode == omOverwrite));

      UnicodeString RemoteFileName = DoResume ? DestPartialFullName : DestFullName;
      OpenParams.FileName = FileName;
      OpenParams.RemoteFileName = RemoteFileName;
      OpenParams.Resume = DoResume;
      OpenParams.Resuming = ResumeTransfer;
      OpenParams.OperationProgress = OperationProgress;
      OpenParams.CopyParam = CopyParam;
      OpenParams.Params = Params;
      OpenParams.FileParams = &FileParams;
      OpenParams.Confirmed = false;

      FTerminal->LogEvent(L"Opening remote file.");
      FTerminal->FileOperationLoop(SFTPOpenRemote, OperationProgress, true,
        FMTLOAD(SFTP_CREATE_FILE_ERROR, (OpenParams.RemoteFileName)),
        &OpenParams);
      OperationProgress->Progress();

      if (OpenParams.RemoteFileName != RemoteFileName)
      {
        DebugAssert(!DoResume);
        DebugAssert(UnixExtractFilePath(OpenParams.RemoteFileName) == UnixExtractFilePath(RemoteFileName));
        DestFullName = OpenParams.RemoteFileName;
        UnicodeString NewFileName = UnixExtractFileName(DestFullName);
        DebugAssert(DestFileName != NewFileName);
        DestFileName = NewFileName;
      }

      Action.Destination(DestFullName);

      bool TransferFinished = false;
      __int64 DestWriteOffset = 0;
      TSFTPPacket CloseRequest;
      bool SetRights = ((DoResume && DestFileExists) || CopyParam->PreserveRights);
      bool SetProperties = (CopyParam->PreserveTime || SetRights);
      TSFTPPacket PropertiesRequest(SSH_FXP_SETSTAT);
      TSFTPPacket PropertiesResponse;
      TRights Rights;
      if (SetProperties)
      {
        PropertiesRequest.AddPathString(DestFullName, FUtfStrings);
        if (CopyParam->PreserveRights)
        {
          Rights = CopyParam->RemoteFileRights(OpenParams.LocalFileAttrs);
        }
        else if (DoResume && DestFileExists)
        {
          Rights = DestRights;
        }
        else
        {
          DebugAssert(!SetRights);
        }

        unsigned short RightsNumber = Rights.NumberSet;
        PropertiesRequest.AddProperties(
          SetRights ? &RightsNumber : NULL, NULL, NULL,
          CopyParam->PreserveTime ? &MTime : NULL,
          NULL, NULL, false, FVersion, FUtfStrings);
      }

      try
      {
        if (OpenParams.OverwriteMode == omAppend)
        {
          FTerminal->LogEvent(L"Appending file.");
          DestWriteOffset = OpenParams.DestFileSize;
        }
        else if (ResumeTransfer || (OpenParams.OverwriteMode == omResume))
        {
          if (OpenParams.OverwriteMode == omResume)
          {
            FTerminal->LogEvent(L"Resuming file transfer (append style).");
            ResumeOffset = OpenParams.DestFileSize;
          }
          FileSeek((THandle)File, ResumeOffset, 0);
          OperationProgress->AddResumed(ResumeOffset);
        }

        TSFTPUploadQueue Queue(this);
        try
        {
          int ConvertParams =
            FLAGMASK(CopyParam->RemoveCtrlZ, cpRemoveCtrlZ) |
            FLAGMASK(CopyParam->RemoveBOM, cpRemoveBOM);
          Queue.Init(FileName, File, OperationProgress,
            OpenParams.RemoteFileHandle,
            DestWriteOffset + OperationProgress->TransferedSize,
            ConvertParams);

          while (Queue.Continue())
          {
            if (OperationProgress->Cancel)
            {
              Abort();
            }
          }

          // send close request before waiting for pending read responses
          SFTPCloseRemote(OpenParams.RemoteFileHandle, DestFileName,
            OperationProgress, false, true, &CloseRequest);
          OpenParams.RemoteFileHandle = L"";

          // when resuming is disabled, we can send "set properties"
          // request before waiting for pending read/close responses
          if (SetProperties && !DoResume)
          {
            SendPacket(&PropertiesRequest);
            ReserveResponse(&PropertiesRequest, &PropertiesResponse);
          }
        }
        __finally
        {
          Queue.DisposeSafe();
        }

        TransferFinished = true;
        // queue is discarded here
      }
      __finally
      {
        if (FTerminal->Active)
        {
          // if file transfer was finished, the close request was already sent
          if (!OpenParams.RemoteFileHandle.IsEmpty())
          {
            SFTPCloseRemote(OpenParams.RemoteFileHandle, DestFileName,
              OperationProgress, TransferFinished, true, &CloseRequest);
          }
          // wait for the response
          SFTPCloseRemote(OpenParams.RemoteFileHandle, DestFileName,
            OperationProgress, TransferFinished, false, &CloseRequest);

          // delete file if transfer was not completed, resuming was not allowed and
          // we were not appending (incl. alternate resume),
          // shortly after plain transfer completes (eq. !ResumeAllowed)
          if (!TransferFinished && !DoResume && (OpenParams.OverwriteMode == omOverwrite))
          {
            DoDeleteFile(OpenParams.RemoteFileName, SSH_FXP_REMOVE);
          }
        }
      }

      OperationProgress->Progress();

      if (DoResume)
      {
        if (DestFileExists)
        {
          FILE_OPERATION_LOOP_BEGIN
          {
            if (FTerminal->SessionData->OverwrittenToRecycleBin &&
                !FTerminal->SessionData->RecycleBinPath.IsEmpty())
            {
              FTerminal->RecycleFile(DestFullName, NULL);
            }
            else
            {
              DoDeleteFile(DestFullName, SSH_FXP_REMOVE);
            }
          }
          FILE_OPERATION_LOOP_END(
            FMTLOAD(DELETE_ON_RESUME_ERROR,
              (UnixExtractFileName(DestFullName), DestFullName)));
        }

        // originally this was before CLOSE (last __finally statement),
        // on VShell it failed
        FILE_OPERATION_LOOP_BEGIN
        {
          RenameFile(OpenParams.RemoteFileName, DestFileName);
        }
        FILE_OPERATION_LOOP_END_CUSTOM(
          FMTLOAD(RENAME_AFTER_RESUME_ERROR,
            (UnixExtractFileName(OpenParams.RemoteFileName), DestFileName)),
          true, HELP_RENAME_AFTER_RESUME_ERROR);
      }

      if (SetProperties)
      {
        std::unique_ptr<TTouchSessionAction> TouchAction;
        if (CopyParam->PreserveTime)
        {
          TDateTime MDateTime = UnixToDateTime(MTime, FTerminal->SessionData->DSTMode);
          FTerminal->LogEvent(FORMAT(L"Preserving timestamp [%s]",
            (StandardTimestamp(MDateTime))));
          TouchAction.reset(new TTouchSessionAction(FTerminal->ActionLog, DestFullName,
            MDateTime));
        }
        std::unique_ptr<TChmodSessionAction> ChmodAction;
        // do record chmod only if it was explicitly requested,
        // not when it was implicitly performed to apply timestamp
        // of overwritten file to new file
        if (CopyParam->PreserveRights)
        {
          ChmodAction.reset(new TChmodSessionAction(FTerminal->ActionLog, DestFullName, Rights));
        }
        try
        {
          // when resuming is enabled, the set properties request was not sent yet
          if (DoResume)
          {
            SendPacket(&PropertiesRequest);
          }
          bool Resend = false;
          FILE_OPERATION_LOOP_BEGIN
          {
            try
            {
              TSFTPPacket DummyResponse;
              TSFTPPacket * Response = &PropertiesResponse;
              if (Resend)
              {
                PropertiesRequest.Reuse();
                SendPacket(&PropertiesRequest);
                // ReceiveResponse currently cannot receive twice into same packet,
                // so DummyResponse is temporary workaround
                Response = &DummyResponse;
              }
              Resend = true;
              ReceiveResponse(&PropertiesRequest, Response, SSH_FXP_STATUS,
                asOK | FLAGMASK(CopyParam->IgnorePermErrors, asPermDenied));
            }
            catch (...)
            {
              if (FTerminal->Active &&
                  (!CopyParam->PreserveRights && !CopyParam->PreserveTime))
              {
                DebugAssert(DoResume);
                FTerminal->LogEvent(L"Ignoring error preserving permissions of overwritten file");
              }
              else
              {
                throw;
              }
            }
          }
          FILE_OPERATION_LOOP_END_CUSTOM(
            FMTLOAD(PRESERVE_TIME_PERM_ERROR3, (DestFileName)),
            true, HELP_PRESERVE_TIME_PERM_ERROR);
        }
        catch(Exception & E)
        {
          if (TouchAction.get() != NULL)
          {
            TouchAction->Rollback(&E);
          }
          if (ChmodAction.get() != NULL)
          {
            ChmodAction->Rollback(&E);
          }
          ChildError = true;
          throw;
        }
      }

      FTerminal->LogFileDone(OperationProgress);
    }
  }
  __finally
  {
    if (File != NULL)
    {
      CloseHandle(File);
    }
  }

  /* TODO : Delete also read-only files. */
  if (FLAGSET(Params, cpDelete))
  {
    if (!Dir)
    {
      FILE_OPERATION_LOOP_BEGIN
      {
        THROWOSIFFALSE(Sysutils::DeleteFile(ApiPath(FileName)));
      }
      FILE_OPERATION_LOOP_END(FMTLOAD(DELETE_LOCAL_FILE_ERROR, (FileName)));
    }
  }
  else if (CopyParam->ClearArchive && FLAGSET(OpenParams.LocalFileAttrs, faArchive))
  {
    FILE_OPERATION_LOOP_BEGIN
    {
      THROWOSIFFALSE(FileSetAttr(ApiPath(FileName), OpenParams.LocalFileAttrs & ~faArchive) == 0);
    }
    FILE_OPERATION_LOOP_END(FMTLOAD(CANT_SET_ATTRS, (FileName)));
  }
}
//---------------------------------------------------------------------------
RawByteString __fastcall TSFTPFileSystem::SFTPOpenRemoteFile(
  const UnicodeString & FileName, unsigned int OpenType, __int64 Size)
{
  TSFTPPacket Packet(SSH_FXP_OPEN);

  Packet.AddPathString(FileName, FUtfStrings);
  if (FVersion < 5)
  {
    Packet.AddCardinal(OpenType);
  }
  else
  {
    unsigned long Access =
      FLAGMASK(FLAGSET(OpenType, SSH_FXF_READ), ACE4_READ_DATA) |
      FLAGMASK(FLAGSET(OpenType, SSH_FXF_WRITE), ACE4_WRITE_DATA | ACE4_APPEND_DATA);

    unsigned long Flags;

    if (FLAGSET(OpenType, SSH_FXF_CREAT | SSH_FXF_EXCL))
    {
      Flags = SSH_FXF_CREATE_NEW;
    }
    else if (FLAGSET(OpenType, SSH_FXF_CREAT | SSH_FXF_TRUNC))
    {
      Flags = SSH_FXF_CREATE_TRUNCATE;
    }
    else if (FLAGSET(OpenType, SSH_FXF_CREAT))
    {
      Flags = SSH_FXF_OPEN_OR_CREATE;
    }
    else
    {
      Flags = SSH_FXF_OPEN_EXISTING;
    }

    Flags |=
      FLAGMASK(FLAGSET(OpenType, SSH_FXF_APPEND), SSH_FXF_ACCESS_APPEND_DATA) |
      FLAGMASK(FLAGSET(OpenType, SSH_FXF_TEXT), SSH_FXF_ACCESS_TEXT_MODE);

    Packet.AddCardinal(Access);
    Packet.AddCardinal(Flags);
  }

  bool SendSize =
    (Size >= 0) &&
    FLAGSET(OpenType, SSH_FXF_CREAT | SSH_FXF_TRUNC) &&
    // Particuarly VanDyke VShell (4.0.3) does not support SSH_FILEXFER_ATTR_ALLOCATION_SIZE
    // (it fails open request when the attribute is included).
    // It's SFTP-6 attribute, so support structure should be available.
    // It's actually not with VShell. But VShell supports the SSH_FILEXFER_ATTR_ALLOCATION_SIZE.
    // All servers should support SSH_FILEXFER_ATTR_SIZE (SFTP < 6)
    (!FSupport->Loaded || FLAGSET(FSupport->AttributeMask, Packet.AllocationSizeAttribute(FVersion)));
  Packet.AddProperties(NULL, NULL, NULL, NULL, NULL,
    SendSize ? &Size : NULL, false, FVersion, FUtfStrings);

  SendPacketAndReceiveResponse(&Packet, &Packet, SSH_FXP_HANDLE);

  return Packet.GetFileHandle();
}
//---------------------------------------------------------------------------
int __fastcall TSFTPFileSystem::SFTPOpenRemote(void * AOpenParams, void * /*Param2*/)
{
  TOpenRemoteFileParams * OpenParams = (TOpenRemoteFileParams *)AOpenParams;
  DebugAssert(OpenParams);
  TFileOperationProgressType * OperationProgress = OpenParams->OperationProgress;

  int OpenType;
  bool Success = false;
  bool ConfirmOverwriting;

  do
  {
    try
    {
      ConfirmOverwriting =
        !OpenParams->Confirmed && !OpenParams->Resume &&
        FTerminal->CheckRemoteFile(OpenParams->FileName, OpenParams->CopyParam, OpenParams->Params, OperationProgress);
      OpenType = SSH_FXF_WRITE | SSH_FXF_CREAT;
      // when we want to preserve overwritten files, we need to find out that
      // they exist first... even if overwrite confirmation is disabled.
      // but not when we already know we are not going to overwrite (but e.g. to append)
      if ((ConfirmOverwriting || FTerminal->SessionData->OverwrittenToRecycleBin) &&
          (OpenParams->OverwriteMode == omOverwrite))
      {
        OpenType |= SSH_FXF_EXCL;
      }
      if (!OpenParams->Resuming && (OpenParams->OverwriteMode == omOverwrite))
      {
        OpenType |= SSH_FXF_TRUNC;
      }
      if ((FVersion >= 4) && OpenParams->OperationProgress->AsciiTransfer)
      {
        OpenType |= SSH_FXF_TEXT;
      }

      OpenParams->RemoteFileHandle = SFTPOpenRemoteFile(
        OpenParams->RemoteFileName, OpenType, OperationProgress->LocalSize);

      Success = true;
    }
    catch(Exception & E)
    {
      if (!OpenParams->Confirmed && (OpenType & SSH_FXF_EXCL) && FTerminal->Active)
      {
        FTerminal->LogEvent(FORMAT(L"Cannot create new file \"%s\", checking if it exists already", (OpenParams->RemoteFileName)));

        bool ThrowOriginal = false;

        // When exclusive opening of file fails, try to detect if file exists.
        // When file does not exist, failure was probably caused by 'permission denied'
        // or similar error. In this case throw original exception.
        try
        {
          OperationProgress->Progress();
          TRemoteFile * File;
          UnicodeString RealFileName = LocalCanonify(OpenParams->RemoteFileName);
          ReadFile(RealFileName, File);
          DebugAssert(File);
          OpenParams->DestFileSize = File->Size;
          if (OpenParams->FileParams != NULL)
          {
            OpenParams->FileParams->DestTimestamp = File->Modification;
            OpenParams->FileParams->DestSize = OpenParams->DestFileSize;
          }
          // file exists (otherwise exception was thrown)
          SAFE_DESTROY(File);
        }
        catch(...)
        {
          if (!FTerminal->Active)
          {
            throw;
          }
          else
          {
            ThrowOriginal = true;
          }
        }

        if (ThrowOriginal)
        {
          throw;
        }

        // we may get here even if confirmation is disabled,
        // when we have preserving of overwritten files enabled
        if (ConfirmOverwriting)
        {
          OperationProgress->Progress();
          // confirmation duplicated in SFTPSource for resumable file transfers.
          UnicodeString RemoteFileNameOnly = UnixExtractFileName(OpenParams->RemoteFileName);
          SFTPConfirmOverwrite(OpenParams->FileName, RemoteFileNameOnly,
            OpenParams->CopyParam, OpenParams->Params, OperationProgress, OpenParams->OverwriteMode, OpenParams->FileParams);
          if (RemoteFileNameOnly != UnixExtractFileName(OpenParams->RemoteFileName))
          {
            OpenParams->RemoteFileName =
              UnixExtractFilePath(OpenParams->RemoteFileName) + RemoteFileNameOnly;
          }
          OpenParams->Confirmed = true;
        }
        else
        {
          DebugAssert(FTerminal->SessionData->OverwrittenToRecycleBin);
        }

        if ((OpenParams->OverwriteMode == omOverwrite) &&
            FTerminal->SessionData->OverwrittenToRecycleBin &&
            !FTerminal->SessionData->RecycleBinPath.IsEmpty())
        {
          OperationProgress->Progress();
          FTerminal->RecycleFile(OpenParams->RemoteFileName, NULL);
        }
      }
      else if (FTerminal->Active)
      {
        // if file overwriting was confirmed, it means that the file already exists,
        // if not, check now
        if (!OpenParams->Confirmed)
        {
          bool ThrowOriginal = false;

          // When file does not exist, failure was probably caused by 'permission denied'
          // or similar error. In this case throw original exception.
          try
          {
            TRemoteFile * File;
            UnicodeString RealFileName = LocalCanonify(OpenParams->RemoteFileName);
            ReadFile(RealFileName, File);
            SAFE_DESTROY(File);
          }
          catch(...)
          {
            if (!FTerminal->Active)
            {
              throw;
            }
            else
            {
              ThrowOriginal = true;
            }
          }

          if (ThrowOriginal)
          {
            throw;
          }
        }

        // now we know that the file exists

        if (FTerminal->FileOperationLoopQuery(E, OperationProgress,
              FMTLOAD(SFTP_OVERWRITE_FILE_ERROR2, (OpenParams->RemoteFileName)),
              true, LoadStr(SFTP_OVERWRITE_DELETE_BUTTON)))
        {
          OperationProgress->Progress();
          int Params = dfNoRecursive;
          FTerminal->DeleteFile(OpenParams->RemoteFileName, NULL, &Params);
        }
      }
      else
      {
        throw;
      }
    }
  }
  while (!Success);

  return 0;
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::SFTPCloseRemote(const RawByteString Handle,
  const UnicodeString FileName, TFileOperationProgressType * OperationProgress,
  bool TransferFinished, bool Request, TSFTPPacket * Packet)
{
  // Moving this out of SFTPSource() fixed external exception 0xC0000029 error
  FILE_OPERATION_LOOP_BEGIN
  {
    try
    {
      TSFTPPacket CloseRequest;
      TSFTPPacket * P = (Packet == NULL ? &CloseRequest : Packet);

      if (Request)
      {
        P->ChangeType(SSH_FXP_CLOSE);
        P->AddString(Handle);
        SendPacket(P);
        ReserveResponse(P, Packet);
      }
      else
      {
        DebugAssert(Packet != NULL);
        ReceiveResponse(P, Packet, SSH_FXP_STATUS);
      }
    }
    catch(...)
    {
      if (!FTerminal->Active || TransferFinished)
      {
        throw;
      }
    }
  }
  FILE_OPERATION_LOOP_END(FMTLOAD(SFTP_CLOSE_FILE_ERROR, (FileName)));
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::SFTPDirectorySource(const UnicodeString DirectoryName,
  const UnicodeString TargetDir, int Attrs, const TCopyParamType * CopyParam,
  int Params, TFileOperationProgressType * OperationProgress, unsigned int Flags)
{
  UnicodeString DestDirectoryName =
    FTerminal->ChangeFileName(
      CopyParam, ExtractFileName(ExcludeTrailingBackslash(DirectoryName)),
      osLocal, FLAGSET(Flags, tfFirstLevel));
  UnicodeString DestFullName = UnixIncludeTrailingBackslash(TargetDir + DestDirectoryName);

  OperationProgress->SetFile(DirectoryName);

  bool CreateDir = false;
  try
  {
    TryOpenDirectory(DestFullName);
  }
  catch(...)
  {
    if (FTerminal->Active)
    {
      // opening directory failed, it probably does not exists, try to
      // create it
      CreateDir = true;
    }
    else
    {
      throw;
    }
  }

  if (CreateDir)
  {
    TRemoteProperties Properties;
    if (CopyParam->PreserveRights)
    {
      Properties.Valid = TValidProperties() << vpRights;
      Properties.Rights = CopyParam->RemoteFileRights(Attrs);
    }
    FTerminal->CreateDirectory(DestFullName, &Properties);
    Flags |= tfNewDirectory;
  }

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
          SFTPSourceRobust(FileName, DestFullName, CopyParam, Params, OperationProgress,
            Flags & ~tfFirstLevel);
        }
      }
      catch (EScpSkipFile &E)
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
    };
  }
  __finally
  {
    FindClose(SearchRec);
  }

  /* TODO : Delete also read-only directories. */
  /* TODO : Show error message on failure. */
  if (!OperationProgress->Cancel)
  {
    if (CopyParam->PreserveTime && CopyParam->PreserveTimeDirs)
    {
      TRemoteProperties Properties;
      Properties.Valid << vpModification;

      FTerminal->OpenLocalFile(
        ExcludeTrailingBackslash(DirectoryName), GENERIC_READ, NULL, NULL, NULL,
        &Properties.Modification, &Properties.LastAccess, NULL);

      FTerminal->ChangeFileProperties(DestFullName, NULL, &Properties);
    }

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
void __fastcall TSFTPFileSystem::CopyToLocal(TStrings * FilesToCopy,
  const UnicodeString TargetDir, const TCopyParamType * CopyParam,
  int Params, TFileOperationProgressType * OperationProgress,
  TOnceDoneOperation & OnceDoneOperation)
{
  DebugAssert(FilesToCopy && OperationProgress);

  UnicodeString FileName;
  UnicodeString FullTargetDir = IncludeTrailingBackslash(TargetDir);
  const TRemoteFile * File;
  bool Success;
  int Index = 0;
  while (Index < FilesToCopy->Count && !OperationProgress->Cancel)
  {
    Success = false;
    FileName = FilesToCopy->Strings[Index];
    File = (TRemoteFile *)FilesToCopy->Objects[Index];

    DebugAssert(!FAvoidBusy);
    FAvoidBusy = true;

    try
    {
      try
      {
        SFTPSinkRobust(LocalCanonify(FileName), File, FullTargetDir, CopyParam,
          Params, OperationProgress, tfFirstLevel);
        Success = true;
      }
      catch(EScpSkipFile & E)
      {
        TSuspendFileOperationProgress Suspend(OperationProgress);
        if (!FTerminal->HandleException(&E))
        {
          throw;
        }
      }
      catch(...)
      {
        // TODO: remove the block?
        throw;
      }
    }
    __finally
    {
      FAvoidBusy = false;
      OperationProgress->Finish(FileName, Success, OnceDoneOperation);
    }
    Index++;
  }
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::SFTPSinkRobust(const UnicodeString FileName,
  const TRemoteFile * File, const UnicodeString TargetDir,
  const TCopyParamType * CopyParam, int Params,
  TFileOperationProgressType * OperationProgress, unsigned int Flags)
{
  // the same in TFTPFileSystem

  TDownloadSessionAction Action(FTerminal->ActionLog);
  TRobustOperationLoop RobustLoop(FTerminal, OperationProgress);

  do
  {
    bool ChildError = false;
    try
    {
      SFTPSink(FileName, File, TargetDir, CopyParam, Params, OperationProgress,
        Flags, Action, ChildError);
    }
    catch(Exception & E)
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
        // prevent overwrite and resume confirmations
        Params |= cpNoConfirmation;
      }
    }
  }
  while (RobustLoop.Retry());
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::SFTPSink(const UnicodeString FileName,
  const TRemoteFile * File, const UnicodeString TargetDir,
  const TCopyParamType * CopyParam, int Params,
  TFileOperationProgressType * OperationProgress, unsigned int Flags,
  TDownloadSessionAction & Action, bool & ChildError)
{

  Action.FileName(FileName);

  UnicodeString OnlyFileName = UnixExtractFileName(FileName);

  TFileMasks::TParams MaskParams;
  DebugAssert(File);
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

  FTerminal->LogFileDetails(FileName, File->Modification, File->Size);

  OperationProgress->SetFile(FileName);

  UnicodeString DestFileName =
    FTerminal->ChangeFileName(
      CopyParam, OnlyFileName, osRemote, FLAGSET(Flags, tfFirstLevel));
  UnicodeString DestFullName = TargetDir + DestFileName;

  if (File->IsDirectory)
  {
    Action.Cancel();
    if (FTerminal->CanRecurseToDirectory(File))
    {
      FILE_OPERATION_LOOP_BEGIN
      {
        int Attrs = FileGetAttrFix(ApiPath(DestFullName));
        if ((Attrs & faDirectory) == 0) EXCEPTION;
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

      FTerminal->ProcessDirectory(FileName, SFTPSinkFile, &SinkFileParams);

      if (CopyParam->PreserveTime && CopyParam->PreserveTimeDirs)
      {
        FTerminal->LogEvent(FORMAT(L"Preserving directory timestamp [%s]",
          (StandardTimestamp(File->Modification))));
        int SetFileTimeError = ERROR_SUCCESS;
        // FILE_FLAG_BACKUP_SEMANTICS is needed to "open" directory
        HANDLE LocalHandle = CreateFile(ApiPath(DestFullName).c_str(), GENERIC_WRITE,
          FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
        if (LocalHandle == INVALID_HANDLE_VALUE)
        {
          SetFileTimeError = GetLastError();
        }
        else
        {
          FILETIME AcTime = DateTimeToFileTime(File->LastAccess, FTerminal->SessionData->DSTMode);
          FILETIME WrTime = DateTimeToFileTime(File->Modification, FTerminal->SessionData->DSTMode);
          if (!SetFileTime(LocalHandle, NULL, &AcTime, &WrTime))
          {
            SetFileTimeError = GetLastError();
          }
          CloseHandle(LocalHandle);
        }

        if (SetFileTimeError != ERROR_SUCCESS)
        {
          FTerminal->LogEvent(FORMAT(L"Preserving timestamp failed, ignoring: %s",
            (SysErrorMessageForError(SetFileTimeError))));
        }
      }

      // Do not delete directory if some of its files were skip.
      // Throw "skip file" for the directory to avoid attempt to deletion
      // of any parent directory
      if ((Params & cpDelete) && SinkFileParams.Skipped)
      {
        THROW_SKIP_FILE_NULL;
      }
    }
    else
    {
      FTerminal->LogEvent(FORMAT(L"Skipping symlink to directory \"%s\".", (FileName)));
    }
  }
  else
  {
    FTerminal->LogEvent(FORMAT(L"Copying \"%s\" to local directory started.", (FileName)));

    UnicodeString DestPartialFullName;
    bool ResumeAllowed;
    bool ResumeTransfer = false;
    __int64 ResumeOffset;

    // Will we use ASCII of BINARY file transfer?
    OperationProgress->SetAsciiTransfer(
      CopyParam->UseAsciiTransfer(BaseFileName, osRemote, MaskParams));
    FTerminal->LogEvent(UnicodeString((OperationProgress->AsciiTransfer ? L"Ascii" : L"Binary")) +
      " transfer mode selected.");

    // Suppose same data size to transfer as to write
    // (not true with ASCII transfer)
    OperationProgress->SetTransferSize(File->Size);
    OperationProgress->SetLocalSize(OperationProgress->TransferSize);

    // resume has no sense for temporary downloads
    ResumeAllowed = ((Params & cpTemporary) == 0) &&
      !OperationProgress->AsciiTransfer &&
      CopyParam->AllowResume(OperationProgress->TransferSize);

    int Attrs;
    FILE_OPERATION_LOOP_BEGIN
    {
      Attrs = FileGetAttrFix(ApiPath(DestFullName));
      if ((Attrs >= 0) && (Attrs & faDirectory)) EXCEPTION;
    }
    FILE_OPERATION_LOOP_END(FMTLOAD(NOT_FILE_ERROR, (DestFullName)));

    OperationProgress->TransferingFile = false; // not set with SFTP protocol

    HANDLE LocalHandle = NULL;
    TStream * FileStream = NULL;
    bool DeleteLocalFile = false;
    RawByteString RemoteHandle;
    UnicodeString LocalFileName = DestFullName;
    TSFTPOverwriteMode OverwriteMode = omOverwrite;

    try
    {
      if (ResumeAllowed)
      {
        DestPartialFullName = DestFullName + FTerminal->Configuration->PartialExt;
        LocalFileName = DestPartialFullName;

        FTerminal->LogEvent(L"Checking existence of partially transfered file.");
        if (FileExists(ApiPath(DestPartialFullName)))
        {
          FTerminal->LogEvent(L"Partially transfered file exists.");
          FTerminal->OpenLocalFile(DestPartialFullName, GENERIC_WRITE,
            NULL, &LocalHandle, NULL, NULL, NULL, &ResumeOffset);

          bool PartialBiggerThanSource = (ResumeOffset > OperationProgress->TransferSize);
          if (FLAGCLEAR(Params, cpNoConfirmation))
          {
            ResumeTransfer = SFTPConfirmResume(DestFileName,
              PartialBiggerThanSource, OperationProgress);
          }
          else
          {
            ResumeTransfer = !PartialBiggerThanSource;
            if (!ResumeTransfer)
            {
              FTerminal->LogEvent(L"Partially transfered file is bigger that original file.");
            }
          }

          if (!ResumeTransfer)
          {
            CloseHandle(LocalHandle);
            LocalHandle = NULL;
            FILE_OPERATION_LOOP_BEGIN
            {
              THROWOSIFFALSE(Sysutils::DeleteFile(ApiPath(DestPartialFullName)));
            }
            FILE_OPERATION_LOOP_END(FMTLOAD(DELETE_LOCAL_FILE_ERROR, (DestPartialFullName)));
          }
          else
          {
            FTerminal->LogEvent(L"Resuming file transfer.");
            FileSeek((THandle)LocalHandle, ResumeOffset, 0);
            OperationProgress->AddResumed(ResumeOffset);
          }
        }

        OperationProgress->Progress();
      }

      // first open source file, not to loose the destination file,
      // if we cannot open the source one in the first place
      FTerminal->LogEvent(L"Opening remote file.");
      FILE_OPERATION_LOOP_BEGIN
      {
        int OpenType = SSH_FXF_READ;
        if ((FVersion >= 4) && OperationProgress->AsciiTransfer)
        {
          OpenType |= SSH_FXF_TEXT;
        }
        RemoteHandle = SFTPOpenRemoteFile(FileName, OpenType);
        OperationProgress->Progress();
      }
      FILE_OPERATION_LOOP_END(FMTLOAD(SFTP_OPEN_FILE_ERROR, (FileName)));

      TDateTime Modification;
      FILETIME AcTime;
      FILETIME WrTime;

      TSFTPPacket RemoteFilePacket(SSH_FXP_FSTAT);
      RemoteFilePacket.AddString(RemoteHandle);
      SendCustomReadFile(&RemoteFilePacket, &RemoteFilePacket,
        SSH_FILEXFER_ATTR_MODIFYTIME);
      ReceiveResponse(&RemoteFilePacket, &RemoteFilePacket);
      OperationProgress->Progress();

      const TRemoteFile * AFile = File;
      try
      {
        // ignore errors
        if (RemoteFilePacket.Type == SSH_FXP_ATTRS)
        {
          // load file, avoid completion (resolving symlinks) as we do not need that
          AFile = LoadFile(&RemoteFilePacket, NULL, UnixExtractFileName(FileName),
            NULL, false);
        }

        Modification = AFile->Modification;
        AcTime = DateTimeToFileTime(AFile->LastAccess,
          FTerminal->SessionData->DSTMode);
        WrTime = DateTimeToFileTime(Modification,
          FTerminal->SessionData->DSTMode);
      }
      __finally
      {
        if (AFile != File)
        {
          delete AFile;
        }
      }

      if ((Attrs >= 0) && !ResumeTransfer)
      {
        __int64 DestFileSize;
        __int64 MTime;
        FTerminal->OpenLocalFile(DestFullName, GENERIC_WRITE,
          NULL, &LocalHandle, NULL, &MTime, NULL, &DestFileSize, false);

        FTerminal->LogEvent(L"Confirming overwriting of file.");
        TOverwriteFileParams FileParams;
        FileParams.SourceSize = OperationProgress->TransferSize;
        FileParams.SourceTimestamp = Modification;
        FileParams.DestTimestamp = UnixToDateTime(MTime,
          FTerminal->SessionData->DSTMode);
        FileParams.DestSize = DestFileSize;
        UnicodeString PrevDestFileName = DestFileName;
        SFTPConfirmOverwrite(FileName, DestFileName, CopyParam, Params, OperationProgress, OverwriteMode, &FileParams);
        if (PrevDestFileName != DestFileName)
        {
          DestFullName = TargetDir + DestFileName;
          DestPartialFullName = DestFullName + FTerminal->Configuration->PartialExt;
          if (ResumeAllowed)
          {
            if (FileExists(ApiPath(DestPartialFullName)))
            {
              FILE_OPERATION_LOOP_BEGIN
              {
                THROWOSIFFALSE(Sysutils::DeleteFile(ApiPath(DestPartialFullName)));
              }
              FILE_OPERATION_LOOP_END(FMTLOAD(DELETE_LOCAL_FILE_ERROR, (DestPartialFullName)));
            }
            LocalFileName = DestPartialFullName;
          }
          else
          {
            LocalFileName = DestFullName;
          }
        }

        if (OverwriteMode == omOverwrite)
        {
          // is NULL when overwriting read-only file
          if (LocalHandle)
          {
            CloseHandle(LocalHandle);
            LocalHandle = NULL;
          }
        }
        else
        {
          // is NULL when overwriting read-only file, so following will
          // probably fail anyway
          if (LocalHandle == NULL)
          {
            FTerminal->OpenLocalFile(DestFullName, GENERIC_WRITE,
              NULL, &LocalHandle, NULL, NULL, NULL, NULL);
          }
          ResumeAllowed = false;
          FileSeek((THandle)LocalHandle, DestFileSize, 0);
          if (OverwriteMode == omAppend)
          {
            FTerminal->LogEvent(L"Appending to file.");
          }
          else
          {
            FTerminal->LogEvent(L"Resuming file transfer (append style).");
            DebugAssert(OverwriteMode == omResume);
            OperationProgress->AddResumed(DestFileSize);
          }
        }
      }

      Action.Destination(ExpandUNCFileName(DestFullName));

      // if not already opened (resume, append...), create new empty file
      if (!LocalHandle)
      {
        if (!FTerminal->CreateLocalFile(LocalFileName, OperationProgress,
               &LocalHandle, FLAGSET(Params, cpNoConfirmation)))
        {
          THROW_SKIP_FILE_NULL;
        }
      }
      DebugAssert(LocalHandle);

      DeleteLocalFile = true;

      FileStream = new TSafeHandleStream((THandle)LocalHandle);

      // at end of this block queue is discarded
      {
        TSFTPDownloadQueue Queue(this);
        try
        {
          TSFTPPacket DataPacket;

          int QueueLen = int(File->Size / DownloadBlockSize(OperationProgress)) + 1;
          if ((QueueLen > FTerminal->SessionData->SFTPDownloadQueue) ||
              (QueueLen < 0))
          {
            QueueLen = FTerminal->SessionData->SFTPDownloadQueue;
          }
          if (QueueLen < 1)
          {
            QueueLen = 1;
          }
          Queue.Init(QueueLen, RemoteHandle, OperationProgress->TransferedSize,
            OperationProgress);

          bool Eof = false;
          bool PrevIncomplete = false;
          int GapFillCount = 0;
          int GapCount = 0;
          unsigned long Missing = 0;
          unsigned long DataLen = 0;
          unsigned long BlockSize;
          bool ConvertToken = false;

          while (!Eof)
          {
            if (Missing > 0)
            {
              Queue.InitFillGapRequest(OperationProgress->TransferedSize, Missing,
                &DataPacket);
              GapFillCount++;
              SendPacketAndReceiveResponse(&DataPacket, &DataPacket,
                SSH_FXP_DATA, asEOF);
            }
            else
            {
              Queue.ReceivePacket(&DataPacket, BlockSize);
            }

            if (DataPacket.Type == SSH_FXP_STATUS)
            {
              // must be SSH_FX_EOF, any other status packet would raise exception
              Eof = true;
              // close file right away, before waiting for pending responses
              SFTPCloseRemote(RemoteHandle, DestFileName, OperationProgress,
                true, true, NULL);
              RemoteHandle = L""; // do not close file again in __finally block
            }

            if (!Eof)
            {
              if ((Missing == 0) && PrevIncomplete)
              {
                // This can happen only if last request returned less bytes
                // than expected, but exactly number of bytes missing to last
                // known file size, but actually EOF was not reached.
                // Can happen only when filesize has changed since directory
                // listing and server returns less bytes than requested and
                // file has some special file size.
                FTerminal->LogEvent(FORMAT(
                  L"Received incomplete data packet before end of file, "
                   "offset: %s, size: %d, requested: %d",
                  (IntToStr(OperationProgress->TransferedSize), int(DataLen),
                  int(BlockSize))));
                FTerminal->TerminalError(NULL, LoadStr(SFTP_INCOMPLETE_BEFORE_EOF));
              }

              // Buffer for one block of data
              TFileBuffer BlockBuf;

              DataLen = DataPacket.GetCardinal();

              PrevIncomplete = false;
              if (Missing > 0)
              {
                DebugAssert(DataLen <= Missing);
                Missing -= DataLen;
              }
              else if (DataLen < BlockSize)
              {
                if (OperationProgress->TransferedSize + DataLen !=
                      OperationProgress->TransferSize)
                {
                  // with native text transfer mode (SFTP>=4), do not bother about
                  // getting less than requested, read offset is ignored anyway
                  if ((FVersion < 4) || !OperationProgress->AsciiTransfer)
                  {
                    GapCount++;
                    Missing = BlockSize - DataLen;
                  }
                }
                else
                {
                  PrevIncomplete = true;
                }
              }

              DebugAssert(DataLen <= BlockSize);
              BlockBuf.Insert(0, reinterpret_cast<const char *>(DataPacket.GetNextData(DataLen)), DataLen);
              DataPacket.DataConsumed(DataLen);
              OperationProgress->AddTransfered(DataLen);

              if ((FVersion >= 6) && DataPacket.CanGetBool() && (Missing == 0))
              {
                Eof = DataPacket.GetBool();
              }

              if (OperationProgress->AsciiTransfer)
              {
                DebugAssert(!ResumeTransfer && !ResumeAllowed);

                unsigned int PrevBlockSize = BlockBuf.Size;
                BlockBuf.Convert(GetEOL(), FTerminal->Configuration->LocalEOLType, 0, ConvertToken);
                OperationProgress->SetLocalSize(
                  OperationProgress->LocalSize - PrevBlockSize + BlockBuf.Size);
              }

              FILE_OPERATION_LOOP_BEGIN
              {
                BlockBuf.WriteToStream(FileStream, BlockBuf.Size);
              }
              FILE_OPERATION_LOOP_END(FMTLOAD(WRITE_ERROR, (LocalFileName)));

              OperationProgress->AddLocallyUsed(BlockBuf.Size);
            }

            if (OperationProgress->Cancel == csCancel)
            {
              Abort();
            }
          };

          if (GapCount > 0)
          {
            FTerminal->LogEvent(FORMAT(
              L"%d requests to fill %d data gaps were issued.",
              (GapFillCount, GapCount)));
          }
        }
        __finally
        {
          Queue.DisposeSafe();
        }
        // queue is discarded here
      }

      if (CopyParam->PreserveTime)
      {
        FTerminal->LogEvent(FORMAT(L"Preserving timestamp [%s]",
          (StandardTimestamp(Modification))));
        SetFileTime(LocalHandle, NULL, &AcTime, &WrTime);
      }

      CloseHandle(LocalHandle);
      LocalHandle = NULL;

      if (ResumeAllowed)
      {
        FILE_OPERATION_LOOP_BEGIN
        {
          if (FileExists(ApiPath(DestFullName)))
          {
            DeleteFileChecked(DestFullName);
          }
          THROWOSIFFALSE(Sysutils::RenameFile(DestPartialFullName, DestFullName));
        }
        FILE_OPERATION_LOOP_END(
          FMTLOAD(RENAME_AFTER_RESUME_ERROR,
            (ExtractFileName(DestPartialFullName), DestFileName)));
      }

      DeleteLocalFile = false;

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

    }
    __finally
    {
      if (LocalHandle) CloseHandle(LocalHandle);
      if (FileStream) delete FileStream;
      if (DeleteLocalFile && (!ResumeAllowed || OperationProgress->LocallyUsed == 0) &&
          (OverwriteMode == omOverwrite))
      {
        FILE_OPERATION_LOOP_BEGIN
        {
          THROWOSIFFALSE(Sysutils::DeleteFile(ApiPath(LocalFileName)));
        }
        FILE_OPERATION_LOOP_END(FMTLOAD(DELETE_LOCAL_FILE_ERROR, (LocalFileName)));
      }

      // if the transfer was finished, the file is closed already
      if (FTerminal->Active && !RemoteHandle.IsEmpty())
      {
        // do not wait for response
        SFTPCloseRemote(RemoteHandle, DestFileName, OperationProgress,
          true, true, NULL);
      }
    }

    FTerminal->LogFileDone(OperationProgress);
  }

  if (Params & cpDelete)
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
void __fastcall TSFTPFileSystem::SFTPSinkFile(UnicodeString FileName,
  const TRemoteFile * File, void * Param)
{
  TSinkFileParams * Params = (TSinkFileParams *)Param;
  DebugAssert(Params->OperationProgress);
  try
  {
    SFTPSinkRobust(FileName, File, Params->TargetDir, Params->CopyParam,
      Params->Params, Params->OperationProgress, Params->Flags);
  }
  catch(EScpSkipFile & E)
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
void __fastcall TSFTPFileSystem::RegisterChecksumAlg(const UnicodeString & Alg, const UnicodeString & SftpAlg)
{
  FChecksumAlgs->Add(Alg);
  FChecksumSftpAlgs->Add(SftpAlg);
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::GetSupportedChecksumAlgs(TStrings * Algs)
{
  Algs->AddStrings(FChecksumAlgs.get());
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::LockFile(const UnicodeString & /*FileName*/, const TRemoteFile * /*File*/)
{
  DebugFail();
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::UnlockFile(const UnicodeString & /*FileName*/, const TRemoteFile * /*File*/)
{
  DebugFail();
}
//---------------------------------------------------------------------------
void __fastcall TSFTPFileSystem::UpdateFromMain(TCustomFileSystem * /*MainFileSystem*/)
{
  // noop
}
