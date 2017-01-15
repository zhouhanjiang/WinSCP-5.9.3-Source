//---------------------------------------------------------------------------
#include <vcl.h>
#pragma hdrstop

#include "Http.h"
#include "NeonIntf.h"
#include "Exceptions.h"
#include "ne_request.h"
#include "TextsCore.h"
#include <openssl/ssl.h>
//---------------------------------------------------------------------------
THttp::THttp()
{
  FProxyPort = 0;
  FOnDownload = NULL;
  FOnError = NULL;
  FResponseLimit = -1;

  FRequestHeaders = NULL;
  FResponseHeaders = new TStringList();
}
//---------------------------------------------------------------------------
THttp::~THttp()
{
  delete FResponseHeaders;
}
//---------------------------------------------------------------------------
void THttp::SendRequest(const char * Method, const UnicodeString & Request)
{
  std::unique_ptr<TStringList> AttemptedUrls(CreateSortedStringList());
  AttemptedUrls->Add(URL);
  UnicodeString RequestUrl = URL;
  bool WasTlsUri = false; // shut up

  bool Retry;

  do
  {
    ne_uri uri;
    NeonParseUrl(RequestUrl, uri);

    bool IsTls = IsTlsUri(uri);
    if (RequestUrl == URL)
    {
      WasTlsUri = IsTls;
    }
    else
    {
      if (!IsTls && WasTlsUri)
      {
        throw Exception(LoadStr(UNENCRYPTED_REDIRECT));
      }
    }

    FHostName = StrFromNeon(uri.host);

    UnicodeString Uri = StrFromNeon(uri.path);
    if (uri.query != NULL)
    {
      Uri += L"?" + StrFromNeon(uri.query);
    }

    FResponse.SetLength(0);
    FCertificateError.SetLength(0);
    FException.reset(NULL);

    TProxyMethod ProxyMethod = ProxyHost.IsEmpty() ? ::pmNone : pmHTTP;

    ne_session_s * NeonSession =
      CreateNeonSession(
        uri, ProxyMethod, ProxyHost, ProxyPort, UnicodeString(), UnicodeString());
    try
    {
      if (IsTls)
      {
        SetNeonTlsInit(NeonSession, InitSslSession);

        ne_ssl_set_verify(NeonSession, NeonServerSSLCallback, this);

        ne_ssl_trust_default_ca(NeonSession);
      }

      ne_request_s * NeonRequest = ne_request_create(NeonSession, Method, StrToNeon(Uri));
      try
      {
        if (FRequestHeaders != NULL)
        {
          for (int Index = 0; Index < FRequestHeaders->Count; Index++)
          {
            ne_add_request_header(
              NeonRequest, StrToNeon(FRequestHeaders->Names[Index]), StrToNeon(FRequestHeaders->ValueFromIndex[Index]));
          }
        }

        UTF8String RequestUtf;
        if (!Request.IsEmpty())
        {
          RequestUtf = UTF8String(Request);
          ne_set_request_body_buffer(NeonRequest, RequestUtf.c_str(), RequestUtf.Length());
        }

        ne_add_response_body_reader(NeonRequest, ne_accept_2xx, NeonBodyReader, this);

        int Status = ne_request_dispatch(NeonRequest);

        // Exception has precedence over status as status will always be NE_ERROR,
        // as we returned 1 from NeonBodyReader
        if (FException.get() != NULL)
        {
          RethrowException(FException.get());
        }

        if (Status == NE_REDIRECT)
        {
          Retry = true;
          RequestUrl = GetNeonRedirectUrl(NeonSession);
          CheckRedirectLoop(RequestUrl, AttemptedUrls.get());
        }
        else
        {
          Retry = false;
          CheckNeonStatus(NeonSession, Status, FHostName, FCertificateError);

          const ne_status * NeonStatus = ne_get_status(NeonRequest);
          if (NeonStatus->klass != 2)
          {
            int Status = NeonStatus->code;
            UnicodeString Message = StrFromNeon(NeonStatus->reason_phrase);
            if (OnError != NULL)
            {
              OnError(this, Status, Message);
            }
            throw Exception(FMTLOAD(HTTP_ERROR2, (Status, Message, FHostName)));
          }

          void * Cursor = NULL;
          const char * HeaderName;
          const char * HeaderValue;
          while ((Cursor = ne_response_header_iterate(NeonRequest, Cursor, &HeaderName, &HeaderValue)) != NULL)
          {
            FResponseHeaders->Values[StrFromNeon(HeaderName)] = StrFromNeon(HeaderValue);
          }
        }
      }
      __finally
      {
        ne_request_destroy(NeonRequest);
      }
    }
    __finally
    {
      DestroyNeonSession(NeonSession);
      ne_uri_free(&uri);
    }
  }
  while (Retry);
}
//---------------------------------------------------------------------------
void THttp::Get()
{
  SendRequest("GET", UnicodeString());
}
//---------------------------------------------------------------------------
void THttp::Post(const UnicodeString & Request)
{
  SendRequest("POST", Request);
}
//---------------------------------------------------------------------------
UnicodeString THttp::GetResponse()
{
  UTF8String UtfResponse(FResponse);
  return UnicodeString(UtfResponse);
}
//---------------------------------------------------------------------------
int THttp::NeonBodyReaderImpl(const char * Buf, size_t Len)
{
  bool Result = true;
  if ((FResponseLimit < 0) ||
      (FResponse.Length() + Len <= FResponseLimit))
  {
    FResponse += RawByteString(Buf, Len);

    if (FOnDownload != NULL)
    {
      bool Cancel = false;

      try
      {
        FOnDownload(this, ResponseLength, Cancel);
      }
      catch (Exception & E)
      {
        FException.reset(CloneException(&E));
        Result = false;
      }

      if (Cancel)
      {
        FException.reset(new EAbort(UnicodeString()));
        Result = false;
      }
    }
  }

  // neon wants 0 for success
  return Result ? 0 : 1;
}
//---------------------------------------------------------------------------
int THttp::NeonBodyReader(void * UserData, const char * Buf, size_t Len)
{
  THttp * Http = static_cast<THttp *>(UserData);
  return Http->NeonBodyReaderImpl(Buf, Len);
}
//---------------------------------------------------------------------------
__int64 THttp::GetResponseLength()
{
  return FResponse.Length();
}
//------------------------------------------------------------------------------
void THttp::InitSslSession(ssl_st * Ssl, ne_session * /*Session*/)
{
  int Options = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1;
  SSL_ctrl(Ssl, SSL_CTRL_OPTIONS, Options, NULL);
}
//---------------------------------------------------------------------------
int THttp::NeonServerSSLCallback(void * UserData, int Failures, const ne_ssl_certificate * Certificate)
{
  THttp * Http = static_cast<THttp *>(UserData);
  return Http->NeonServerSSLCallbackImpl(Failures, Certificate);
}
//---------------------------------------------------------------------------
int THttp::NeonServerSSLCallbackImpl(int Failures, const ne_ssl_certificate * Certificate)
{
  AnsiString AsciiCert = NeonExportCertificate(Certificate);

  UnicodeString WindowsCertificateError;
  if (Failures != 0)
  {
    NeonWindowsValidateCertificate(Failures, AsciiCert, WindowsCertificateError);
  }

  if (Failures != 0)
  {
    FCertificateError = NeonCertificateFailuresErrorStr(Failures, FHostName);
    AddToList(FCertificateError, WindowsCertificateError, L"\n");
  }

  return (Failures == 0) ? NE_OK : NE_ERROR;
}
//---------------------------------------------------------------------------
bool THttp::IsCertificateError()
{
  return !FCertificateError.IsEmpty();
}
//---------------------------------------------------------------------------
