//---------------------------------------------------------------------------
#ifndef Animations192H
#define Animations192H
//---------------------------------------------------------------------------
#include <System.Classes.hpp>
#include "PngImageList.hpp"
#include <Vcl.Controls.hpp>
#include <Vcl.ImgList.hpp>
//---------------------------------------------------------------------------
class TAnimations192Module : public TDataModule
{
__published:
  TPngImageList *AnimationImages;

public:
  __fastcall TAnimations192Module(TComponent * Owner);
};
//---------------------------------------------------------------------------
#endif
