//---------------------------------------------------------------------------
#ifndef ServerPathH
#define ServerPathH
//---------------------------------------------------------------------------
class CServerPath
{
public:
  BOOL AddSubdir(CString subdir);
  CString GetSafePath() const;
  const BOOL IsEmpty() const;
  CServerPath GetParent() const;
  BOOL HasParent() const;
  CString GetLastSegment() const;
  CServerPath();
  CServerPath(CString path, bool trim = true);
  CServerPath(CString path, int nServerType, bool trim = true);
  CServerPath(const CServerPath & path);

  virtual ~CServerPath();

  void SetServer(const t_server & server);
  BOOL SetPath(CString & newpath, BOOL bIsFile);
  BOOL SetPath(CString newpath);
  const CString GetPath() const;

  CServerPath & operator=(const CServerPath & op);

  const bool operator==(const CServerPath & op) const;
  const bool operator!=(const CServerPath & op) const;

  CString FormatFilename(CString fn, bool omitPath = false) const;

protected:
  BOOL m_bEmpty;
  std::list<CString> m_Segments;
  typedef std::list<CString>::iterator tIter;
  typedef std::list<CString>::const_iterator tConstIter;
  typedef std::list<CString>::const_iterator tConstIter;
  CString m_Prefix;
  int m_nServerType;
};
//---------------------------------------------------------------------------
const BOOL operator==(const CServerPath & a, const CString & b);
//---------------------------------------------------------------------------
#endif // ServerPathH
