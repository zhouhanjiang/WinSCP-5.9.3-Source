//---------------------------------------------------------------------------
#include <vcl.h>
#pragma hdrstop

USEFORM("forms\CustomScpExplorer.cpp", CustomScpExplorerForm);
USEFORM("forms\NonVisual.cpp", NonVisualDataModule); /* TDataModule: File Type */
USEFORM("forms\ScpCommander.cpp", ScpCommanderForm);
USEFORM("forms\ScpExplorer.cpp", ScpExplorerForm);
//---------------------------------------------------------------------------
#include <CoreMain.h>
#include <WinInterface.h>
#include <ProgParams.h>
#include <VCLCommon.h>
#include <Setup.h>
//---------------------------------------------------------------------------
WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
  int Result = 0;
  try
  {
    WinInitialize();
    Application->Initialize();
    Application->MainFormOnTaskBar = true;
    Application->ModalPopupMode = pmAuto;
    SetEnvironmentVariable(L"WINSCP_PATH",
      ExcludeTrailingBackslash(ExtractFilePath(Application->ExeName)).c_str());
    CoreInitialize();
    InitializeWinHelp();
    InitializeSystemSettings();

    try
    {
      try
      {
        ConfigureInterface();
        SetupInitialize();

        Application->Title = AppName;
        Result = Execute();
      }
      catch (Exception & E)
      {
        // Capture most errors before Usage class is released,
        // so that we can count them
        Configuration->Usage->Inc(L"GlobalFailures");
        // After we get WM_QUIT (posted by Application->Terminate()), i.e once Application->Run() exits,
        // the message just blinks
        ShowExtendedException(&E);
      }
    }
    __finally
    {
      FinalizeSystemSettings();
      FinalizeWinHelp();
      CoreFinalize();
      WinFinalize();
    }
  }
  catch (Exception &E)
  {
    ShowExtendedException(&E);
  }
  return Result;
}
//---------------------------------------------------------------------------
