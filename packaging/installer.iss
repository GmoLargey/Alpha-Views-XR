; Inno Setup script for Alpha Views XR. Built by package.ps1.
; AppVer is passed in by package.ps1 (/DAppVer=...), read from CMakeLists.txt.
#ifndef AppVer
  #error Build with package.ps1 so the version matches the DLL
#endif
#define Stage "..\dist\stage"

[Setup]
AppId={{3C9A7E21-5B4D-4E8F-A1C6-7D2E9B0F4A53}
AppName=Alpha Views XR
AppVersion={#AppVer}
AppVerName=Alpha Views XR {#AppVer}
AppPublisher=GmoLargey
AppPublisherURL=https://github.com/GmoLargey/Alpha-Views-XR
AppSupportURL=https://github.com/GmoLargey/Alpha-Views-XR
AppCopyright=Copyright (C) 2026 GmoLargey. GNU GPL v3.
VersionInfoVersion={#AppVer}
VersionInfoCompany=GmoLargey
VersionInfoDescription=Alpha Views XR installer
VersionInfoProductName=Alpha Views XR
DefaultDirName={autopf}\Alpha Views XR
DefaultGroupName=Alpha Views XR
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir=..\dist
OutputBaseFilename=AlphaViewsXR-Setup-{#AppVer}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Alpha Views XR
LicenseFile=LICENSE.txt
InfoBeforeFile=installer_info.txt
WizardSizePercent=150

[Files]
; Gesture pictures for the "How to use" page (not installed).
Source: "gestures\*.bmp"; Flags: dontcopy
Source: "{#Stage}\XR_APILAYER_GMO_alpha_views_xr.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\XR_APILAYER_GMO_alpha_views_xr.json"; DestDir: "{app}"; Flags: ignoreversion
; 32-bit layer for 32-bit games (registered separately, below).
Source: "{#Stage}\x86\XR_APILAYER_GMO_alpha_views_xr.dll"; DestDir: "{app}\x86"; Flags: ignoreversion
Source: "{#Stage}\x86\XR_APILAYER_GMO_alpha_views_xr.json"; DestDir: "{app}\x86"; Flags: ignoreversion
Source: "{#Stage}\AlphaViewsXRTest.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\README.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "gestures\Gestures.png"; DestDir: "{app}"; Flags: ignoreversion
Source: "gestures\Controllers.png"; DestDir: "{app}"; Flags: ignoreversion
Source: "gestures\Keyboard.png"; DestDir: "{app}"; Flags: ignoreversion
Source: "gestures\Shapes.png"; DestDir: "{app}"; Flags: ignoreversion
Source: "SteamVR-Settings.png"; DestDir: "{app}"; Flags: ignoreversion

[Registry]
; Implicit OpenXR API layer registration (64-bit view). Removed on uninstall.
Root: HKLM64; Subkey: "SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"; ValueType: dword; \
    ValueName: "{app}\XR_APILAYER_GMO_alpha_views_xr.json"; ValueData: 0; Flags: uninsdeletevalue
; 32-bit games use the 32-bit OpenXR loader, which reads the 32-bit registry view (WOW6432Node).
Root: HKLM32; Subkey: "SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"; ValueType: dword; \
    ValueName: "{app}\x86\XR_APILAYER_GMO_alpha_views_xr.json"; ValueData: 0; Flags: uninsdeletevalue

[Icons]
Name: "{group}\Alpha Views XR test scene"; Filename: "{app}\AlphaViewsXRTest.exe"
Name: "{group}\Alpha Views XR README"; Filename: "{app}\README.txt"
Name: "{group}\Alpha Views XR licence"; Filename: "{app}\LICENSE.txt"
Name: "{group}\Support Alpha Views XR (Ko-fi)"; Filename: "https://ko-fi.com/virtuallyreal"
Name: "{group}\Alpha Views XR gestures"; Filename: "{app}\Gestures.png"
Name: "{group}\Alpha Views XR controllers"; Filename: "{app}\Controllers.png"
Name: "{group}\Alpha Views XR keyboard shortcuts"; Filename: "{app}\Keyboard.png"
Name: "{group}\Alpha Views XR shapes and boxes"; Filename: "{app}\Shapes.png"
Name: "{group}\Uninstall Alpha Views XR"; Filename: "{uninstallexe}"

[Code]
// "How to use" pages (hands, controllers, keyboard): a picture of each input beside what it does.
// The text comes from gen_gestures.ps1 (gestures\tips.iss), which also makes Gestures.png,
// Controllers.png and Keyboard.png from the same lists.
var
  IconPx, ImgSize, KbBottom: Integer;

procedure AddFooter(Page: TWizardPage; S: String);
var
  T: TNewStaticText;
begin
  T := TNewStaticText.Create(Page);
  T.Parent := Page.Surface;
  T.Top := Page.SurfaceHeight - ScaleY(16);
  T.Caption := S;
end;

// Keyboard page: the keyboard picture across the top, then the shortcut list (AddKey).
procedure AddKeyboard(Page: TWizardPage);
var
  Img: TBitmapImage;
  H, W, Src: Integer;
  F: String;
begin
  H := Page.SurfaceHeight * 50 div 100;
  if H <= 120 then Src := 120
  else if H <= 150 then Src := 150
  else if H <= 180 then Src := 180
  else Src := 240;
  F := 'keyboard_' + IntToStr(Src) + '.bmp';
  ExtractTemporaryFile(F);
  Img := TBitmapImage.Create(Page);
  Img.Parent := Page.Surface;
  Img.Bitmap.LoadFromFile(ExpandConstant('{tmp}\') + F);
  W := Img.Bitmap.Width * H div Img.Bitmap.Height;
  if W > Page.SurfaceWidth then begin
    W := Page.SurfaceWidth;
    H := Img.Bitmap.Height * W div Img.Bitmap.Width;
  end;
  Img.SetBounds((Page.SurfaceWidth - W) div 2, 0, W, H);
  Img.Stretch := (W <> Img.Bitmap.Width);
  KbBottom := H + ScaleY(10);
end;

procedure AddKey(Page: TWizardPage; Col, Row: Integer; Key, Desc: String; Color: TColor);
var
  K, D: TNewStaticText;
  X, Y, RowH: Integer;
begin
  RowH := (Page.SurfaceHeight - KbBottom) div 7;   // up to 14 keys, two columns
  X := Col * (Page.SurfaceWidth div 2);
  Y := KbBottom + Row * RowH;
  K := TNewStaticText.Create(Page);
  K.Parent := Page.Surface;
  K.Caption := Key;
  K.Font.Style := [fsBold];
  K.Font.Color := Color;
  K.Left := X;
  K.Top := Y;
  D := TNewStaticText.Create(Page);
  D.Parent := Page.Surface;
  D.Caption := Desc;
  D.Left := X + ScaleX(40);
  D.Top := Y;
end;

procedure AddTip(Page: TWizardPage; Col, Row: Integer; Name, Title, Body: String);
var
  Img: TBitmapImage;
  T, B: TNewStaticText;
  CellW, CellH, X, Y: Integer;
  F: String;
begin
  CellW := Page.SurfaceWidth div 2;
  CellH := (Page.SurfaceHeight - ScaleY(22)) div 3;
  X := Col * CellW;
  Y := Row * CellH;
  F := Name + '_' + IntToStr(ImgSize) + '.bmp';
  ExtractTemporaryFile(F);
  Img := TBitmapImage.Create(Page);
  Img.Parent := Page.Surface;
  Img.SetBounds(X, Y, IconPx, IconPx);
  Img.Stretch := ImgSize <> IconPx;
  Img.Bitmap.LoadFromFile(ExpandConstant('{tmp}\') + F);

  T := TNewStaticText.Create(Page);
  T.Parent := Page.Surface;
  T.Caption := Title;
  T.Font.Style := [fsBold];
  T.Left := X + IconPx + ScaleX(8);
  T.Top := Y;

  B := TNewStaticText.Create(Page);
  B.Parent := Page.Surface;
  B.AutoSize := False;
  B.WordWrap := True;
  B.SetBounds(X + IconPx + ScaleX(8), Y + ScaleY(17), CellW - IconPx - ScaleX(16), CellH - ScaleY(19));
  B.Caption := Body;
end;

// "Before you play" page, shown after installing: runtime + layer checklist beside the SteamVR screenshot.
procedure AddPlayPage;
var
  Page: TWizardPage;
  Img: TBitmapImage;
  T: TNewStaticText;
  H, Src, W: Integer;
  F: String;
begin
  Page := CreateCustomPage(wpInstalling, 'Before you play',
    'Check these settings, or Alpha Views XR won''t show anything in your games.');
  // As tall as the page, but leave at least 40% of the width for the text.
  H := Page.SurfaceHeight;
  if H <= 200 then Src := 200
  else if H <= 250 then Src := 250
  else if H <= 300 then Src := 300
  else Src := 400;
  F := 'steamvr_' + IntToStr(Src) + '.bmp';
  ExtractTemporaryFile(F);
  Img := TBitmapImage.Create(Page);
  Img.Parent := Page.Surface;
  Img.Bitmap.LoadFromFile(ExpandConstant('{tmp}\') + F);
  W := Img.Bitmap.Width * H div Img.Bitmap.Height;
  if W > Page.SurfaceWidth * 60 div 100 then begin
    W := Page.SurfaceWidth * 60 div 100;
    H := Img.Bitmap.Height * W div Img.Bitmap.Width;
  end;
  Img.SetBounds(Page.SurfaceWidth - W, 0, W, H);
  Img.Stretch := (W <> Img.Bitmap.Width);

  T := TNewStaticText.Create(Page);
  T.Parent := Page.Surface;
  T.AutoSize := False;
  T.WordWrap := True;
  T.SetBounds(0, 0, Page.SurfaceWidth - W - ScaleX(14), Page.SurfaceHeight);
  T.Caption :=
    '1. Check the right OpenXR runtime is selected: the one for the app that streams to your ' +
    'headset. SteamVR Settings > OpenXR shows it as "Current OpenXR Runtime" (the picture is ' +
    'an example).' + #13#10#13#10 +
    '2. Same page, Manage OpenXR API Layers: Alpha Views XR must be On (also saved as ' +
    'SteamVR-Settings.png).' + #13#10#13#10 +
    '3. Passthrough: if your streamer or PC runtime offers an alpha channel (transparency), ' +
    'turn it on. If it doesn''t, use its colour-key passthrough instead and pick the same ' +
    'colour in Alpha Views XR (Ctrl+Alt+C). See README.txt.' + #13#10#13#10 +
    'The gesture, controller, keyboard and shapes pictures are saved in the install folder ' +
    '(Gestures.png, Controllers.png, Keyboard.png, Shapes.png) and in the Start menu.' + #13#10#13#10 +
    'Alpha Views XR by GmoLargey - https://github.com/GmoLargey/Alpha-Views-XR' + #13#10 +
    'If you want to support me: https://ko-fi.com/virtuallyreal';

end;

procedure InitializeWizard;
var
  Page: TWizardPage;
begin
  AddPlayPage;
  // Pick the drawn size closest to the display scale so pictures stay sharp.
  IconPx := ScaleX(80);
  if IconPx <= 80 then ImgSize := 80
  else if IconPx <= 100 then ImgSize := 100
  else if IconPx <= 120 then ImgSize := 120
  else ImgSize := 160;

#include "gestures\tips.iss"
end;
