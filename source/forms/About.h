//----------------------------------------------------------------------------
#ifndef AboutH
#define AboutH
//----------------------------------------------------------------------------
#include <System.Classes.hpp>
#include <Vcl.Controls.hpp>
#include <Vcl.ExtCtrls.hpp>
#include <Vcl.Forms.hpp>
#include <Vcl.Graphics.hpp>
#include <Vcl.StdCtrls.hpp>
//----------------------------------------------------------------------------
#include <Configuration.h>
#include <GUITools.h>
//----------------------------------------------------------------------------
class TAboutDialog : public TForm
{
__published:
  TLabel *ApplicationLabel;
  TLabel *VersionLabel;
  TLabel *WinSCPCopyrightLabel;
  TStaticText *HomepageLabel;
  TLabel *ProductSpecificMessageLabel;
  TStaticText *ForumUrlLabel;
  TButton *OKButton;
  TButton *LicenseButton;
  TButton *HelpButton;
  TLabel *Label3;
  TLabel *RegistrationLabel;
  TPanel *RegistrationBox;
  TLabel *RegistrationLicensesLabel;
  TStaticText *RegistrationProductIdLabel;
  TLabel *RegistrationSubjectLabel;
  TPanel *ThirdPartyPanel;
  TPaintBox *IconPaintBox;
  void __fastcall LicenseButtonClick(TObject *Sender);
  void __fastcall HelpButtonClick(TObject *Sender);
  void __fastcall RegistrationProductIdLabelClick(TObject *Sender);
  void __fastcall OKButtonMouseDown(TObject *Sender, TMouseButton Button, TShiftState Shift,
          int X, int Y);
  void __fastcall IconPaintBoxPaint(TObject *Sender);
private:
  TConfiguration * FConfiguration;
  TNotifyEvent FOnRegistrationLink;
  HICON FIconHandle;

  void __fastcall LoadData();
  void __fastcall LoadThirdParty();
  void __fastcall AddPara(UnicodeString & Text, const UnicodeString & S);
  UnicodeString __fastcall CreateLink(const UnicodeString & URL, const UnicodeString & Title = L"");
  void __fastcall ExpatLicenceHandler(TObject * Sender);
  void __fastcall AccessViolationTest();
  void __fastcall LookupAddress();

public:
  virtual __fastcall TAboutDialog(TComponent * AOwner,
    TConfiguration * Configuration, bool AllowLicense, TRegistration * Registration,
    bool ALoadThirdParty);
  __fastcall ~TAboutDialog();
};
//----------------------------------------------------------------------------
#endif
