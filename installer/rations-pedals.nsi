; Rations Pedals Windows installer — built by makensis running natively on Linux.
;
; FIVE PLUG-INS, ONE INSTALLER, A COMPONENT PAGE. They are five separate VST3
; bundles that share nothing at run time, so the installer must not turn them
; back into one product: a pedal the user did not tick leaves NOTHING behind —
; no bundle, no registry value, no uninstall row — and a pedal they did tick can
; be removed on its own without disturbing the other four. That is why there are
; five sections, five uninstallers and five uninstall entries rather than one of
; each. Installing the Delay and then removing the Reverb is a thing a user will
; do, and it has to work.
;
; WHY AN INSTALLER AT ALL. A VST3 plug-in is installed by copying a folder, and
; the ZIP still contains all five folders for anyone who would rather do it by
; hand. What the installer buys is the two things copying by hand gets wrong:
; putting the bundles somewhere a host actually looks, and taking them away
; again. Windows has no equivalent of ~/.vst3 that everyone knows, the per-user
; path is buried four levels inside %LOCALAPPDATA%, and an old copy left in the
; other location shows up as a second entry in the plug-in list.
;
; NO WINDOWS MACHINE IS INVOLVED IN BUILDING THIS. makensis is a native Linux
; binary; it links one of NSIS's prebuilt PE stubs and appends the compressed
; payload, so producing the .exe needs neither Wine nor a cross compiler. Wine is
; used afterwards, to run it — a smoke test, not the gate.
;
; INVOKED BY scripts/makedist-windows.sh, which supplies every path:
;
;   makensis -DVERSION=0.1.0 -DVERSION4=0.1.0.0 \
;            -DSTAGE_DIR=<staged> -DDOC_DIR=<staged> \
;            -DOUTFILE=<staged>/RationsPedals-install.exe \
;            installer/rations-pedals.nsi
;
; THIS IS A 32-BIT INSTALLER INSTALLING 64-BIT PLUG-INS, deliberately. NSIS 3.11
; does ship amd64-unicode stubs, but the 32-bit stub is the path every audio
; plug-in installer on Windows has taken for twenty years, and the only thing the
; 64-bit one would save is the two lines below that spell out the view and the
; folder. Both of those have to be explicit anyway, or a 32-bit process silently
; gets WOW6432Node and \Program Files (x86):
;
;   SetRegView 64   for every registry access, and again in the uninstaller
;   $COMMONFILES64  rather than $COMMONFILES
;
; The installer is NOT code-signed, so Windows SmartScreen will warn about it.
; That is why the plain bundles stay in the ZIP beside it.

Unicode true
Target x86-unicode

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"
!include "Sections.nsh"

!ifndef VERSION
  !error "VERSION is not defined - pass -DVERSION=x.y.z"
!endif
!ifndef VERSION4
  !error "VERSION4 is not defined - pass -DVERSION4=x.y.z.0 (VIProductVersion needs four parts)"
!endif
!ifndef STAGE_DIR
  !error "STAGE_DIR is not defined - pass -DSTAGE_DIR=<path holding the five staged .vst3 folders>"
!endif
!ifndef DOC_DIR
  !error "DOC_DIR is not defined - pass -DDOC_DIR=<path holding LICENSE, NOTICE, INSTALL.txt>"
!endif
!ifndef OUTFILE
  !define OUTFILE "RationsPedals-install.exe"
!endif

!define APPNAME "Rations Pedals"
!define REGROOT "Software\RationsPedals"
!define PUBLISHER "rations"
!define ABOUTURL "https://github.com/rations/RationsPedals"
!define UNINSTROOT "Software\Microsoft\Windows\CurrentVersion\Uninstall"

Name "${APPNAME} ${VERSION}"
OutFile "${OUTFILE}"
BrandingText "${APPNAME} ${VERSION}"
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

; "highest" rather than "admin": an administrator is elevated and gets the
; machine-wide Common Files install, and everybody else still gets a working
; per-user install instead of a UAC prompt they cannot answer. Which of the two
; happened is decided in .onInit and shown on the directory page.
RequestExecutionLevel highest

VIProductVersion "${VERSION4}"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${APPNAME} ${VERSION} VST3 installer"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "LegalCopyright" "MIT. See NOTICE for third-party attribution."

Var Vst3Dir   ; the VST3 folder the user picks; each bundle goes inside it
Var AppDir    ; where the uninstallers and the licence files live, outside the bundles
Var OtherDir  ; the standard VST3 folder we are NOT installing into, checked for stale copies
Var PedalName ; uninstaller only: which of the five this copy is
Var Chosen    ; a bit per pedal named on the command line; 0 = none named, so all five

; One command-line flag per pedal, read in .onInit. A flag with no value rather
; than one /PEDALS= list, so that no string has to be split: ${GetOptions} sets
; the error flag when the switch is absent, and that is the whole test.
!macro CLI_PEDAL FLAG BIT
    ClearErrors
    ${GetOptions} "$CMDLINE" "${FLAG}" $R0
    ${IfNot} ${Errors}
        IntOp $Chosen $Chosen | ${BIT}
    ${EndIf}
!macroend

!macro APPLY_CHOICE SEC BIT
    IntOp $R0 $Chosen & ${BIT}
    ${If} $R0 = 0
        !insertmacro UnselectSection ${SEC}
    ${EndIf}
!macroend

;--------------------------------------------------------------------------
!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

!define MUI_WELCOMEPAGE_TITLE "${APPNAME} ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "This installs five VST3 stompboxes: Boost, Chorus, Flanger, Delay and Reverb.$\r$\n$\r$\nThey are five separate plug-ins, not one multi-effect. Each is its own entry in your host's plug-in list, each can be instantiated as many times as you like, and you can install any subset of them - the next page asks which.$\r$\n$\r$\nEach has its own enclosure, its own footswitch that crossfades rather than clicking, a bypass for your host's own automation, and one MIDI-learn row for a footswitch.$\r$\n$\r$\nThere is nothing else to install: cairo, FreeType, libpng, zlib and the GCC runtime are all linked into each plug-in."
!insertmacro MUI_PAGE_WELCOME

!insertmacro MUI_PAGE_LICENSE "${DOC_DIR}\LICENSE"

!define MUI_PAGE_CUSTOMFUNCTION_LEAVE OnComponentsLeave
!insertmacro MUI_PAGE_COMPONENTS

!define MUI_PAGE_HEADER_TEXT "Choose the VST3 folder"
!define MUI_PAGE_HEADER_SUBTEXT "One folder per pedal is created inside it."
!define MUI_DIRECTORYPAGE_TEXT_TOP "Hosts search these two folders, in this order:$\r$\n$\r$\n    %LOCALAPPDATA%\Programs\Common\VST3      (just you, no administrator rights)$\r$\n    C:\Program Files\Common Files\VST3      (every user, needs administrator rights)$\r$\n$\r$\nThe one below was chosen for you. Change it only if your host is set up to look somewhere else."
!define MUI_DIRECTORYPAGE_TEXT_DESTINATION "VST3 folder"
!define MUI_DIRECTORYPAGE_VARIABLE $Vst3Dir
!insertmacro MUI_PAGE_DIRECTORY

!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_TITLE "${APPNAME} is installed"
; Kept short deliberately: the finish page's text area is fixed, and MUI clips
; rather than scrolls - a longer version ran under the "show readme" checkbox.
!define MUI_FINISHPAGE_TEXT "Rescan plug-ins in your DAW to pick them up.$\r$\n$\r$\nEach pedal appears under its own name. To remove one, use Apps & features - each has its own entry, and removing one leaves the others alone."
!define MUI_FINISHPAGE_SHOWREADME "$AppDir\INSTALL.txt"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Open the notes on the pedals and MIDI learn"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

;--------------------------------------------------------------------------
; ONE SECTION BODY, FIVE PEDALS. Written once for the same reason the plug-ins
; share one processor template: five copies of this would drift, and the drift
; would be four correct installs and one that leaves something behind.
;
; NAME is the whole of what varies: the bundle is Rations<NAME>.vst3, the
; registry key and the uninstaller's folder are <NAME>, and <NAME> is what the
; component page and Apps & features show.
!macro PEDAL_SECTION NAME
Section "${NAME}" Sec${NAME}
    StrCpy $INSTDIR "$Vst3Dir\Rations${NAME}.vst3"

    ; A stale copy in the OTHER standard location is not harmless: hosts scan
    ; both, so it comes back as a second "Rations ${NAME}" in the plug-in
    ; list, and which of the two a project loads is not something the user
    ; controls. Offer to remove it. Both paths are fixed and known, and the
    ; folder is checked to actually be a bundle first, which is the only reason
    ; a recursive delete is acceptable here at all.
    ${If} ${FileExists} "$OtherDir\Rations${NAME}.vst3\Contents\*.*"
        ; /SD IDNO: an unattended install must never delete a folder on its own.
        MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON1 \
            "Another copy of Rations ${NAME} is already installed at:$\r$\n$\r$\n$OtherDir\Rations${NAME}.vst3$\r$\n$\r$\nYour host would list it twice. Remove that copy?" \
            /SD IDNO IDNO +2
        RMDir /r "$OtherDir\Rations${NAME}.vst3"
    ${EndIf}

    ; Replace rather than merge. An upgrade that only overwrote would leave
    ; behind art and fonts a later version had dropped, and the editor picks up
    ; whatever is on disk.
    ${If} ${FileExists} "$INSTDIR\Contents\*.*"
        DetailPrint "Removing the previous $INSTDIR"
        RMDir /r "$INSTDIR"
    ${EndIf}

    SetOutPath "$INSTDIR"
    File /r "${STAGE_DIR}\Rations${NAME}.vst3\*"

    ; Nothing goes inside a bundle that a hand-copied one does not also have, so
    ; an installed Rations${NAME}.vst3 and one dragged out of the ZIP are byte
    ; for byte the same folder. The uninstaller lives beside it, in a folder of
    ; this pedal's own - which is also how the uninstaller knows which pedal it
    ; is: $INSTDIR in an uninstaller is the folder it was written into.
    SetOutPath "$AppDir\${NAME}"
    WriteUninstaller "$AppDir\${NAME}\Uninstall Rations ${NAME}.exe"

    ; The licence files are shared by all five and are written by whichever
    ; section runs; the last one out removes them.
    SetOutPath "$AppDir"
    File "${DOC_DIR}\LICENSE"
    File "${DOC_DIR}\NOTICE"
    File "${DOC_DIR}\INSTALL.txt"

    WriteRegStr SHCTX "${REGROOT}\${NAME}" "BundlePath" "$INSTDIR"
    WriteRegStr SHCTX "${REGROOT}\${NAME}" "Version" "${VERSION}"

    WriteRegStr SHCTX "${UNINSTROOT}\Rations${NAME}" "DisplayName" "Rations ${NAME} ${VERSION}"
    WriteRegStr SHCTX "${UNINSTROOT}\Rations${NAME}" "DisplayVersion" "${VERSION}"
    WriteRegStr SHCTX "${UNINSTROOT}\Rations${NAME}" "Publisher" "${PUBLISHER}"
    WriteRegStr SHCTX "${UNINSTROOT}\Rations${NAME}" "URLInfoAbout" "${ABOUTURL}"
    WriteRegStr SHCTX "${UNINSTROOT}\Rations${NAME}" "InstallLocation" "$INSTDIR"
    WriteRegStr SHCTX "${UNINSTROOT}\Rations${NAME}" "UninstallString" "$\"$AppDir\${NAME}\Uninstall Rations ${NAME}.exe$\""
    WriteRegStr SHCTX "${UNINSTROOT}\Rations${NAME}" "QuietUninstallString" "$\"$AppDir\${NAME}\Uninstall Rations ${NAME}.exe$\" /S"
    WriteRegDWORD SHCTX "${UNINSTROOT}\Rations${NAME}" "NoModify" 1
    WriteRegDWORD SHCTX "${UNINSTROOT}\Rations${NAME}" "NoRepair" 1

    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD SHCTX "${UNINSTROOT}\Rations${NAME}" "EstimatedSize" "$0"

    DetailPrint "Installed $INSTDIR"
SectionEnd
!macroend

; All five, selected. A section that is selected in its declaration is also the
; one an unattended /S install performs, which is what a ZIP that is scripted
; into a machine image should do by default.
!insertmacro PEDAL_SECTION Boost
!insertmacro PEDAL_SECTION Chorus
!insertmacro PEDAL_SECTION Flanger
!insertmacro PEDAL_SECTION Delay
!insertmacro PEDAL_SECTION Reverb

;--------------------------------------------------------------------------
Function .onInit
    ${IfNot} ${RunningX64}
        ; /SD, here and below: without it a message box is shown even under /S,
        ; and an unattended install stops dead on a dialog nobody is watching.
        MessageBox MB_ICONSTOP|MB_OK "${APPNAME} is 64-bit only, and this is a 32-bit Windows.$\r$\n$\r$\nThere is no 32-bit build." /SD IDOK
        Abort
    ${EndIf}

    ; A SUBSET, WITHOUT THE PAGE. /S installs all five, which is what a component
    ; page's defaults say to do; naming one or more pedals on the command line
    ; installs those instead:
    ;
    ;     RationsPedals-install.exe /S /DELAY /REVERB
    ;
    ; This exists for two reasons. Somebody scripting a machine image has no way
    ; to answer a component page, and the rule that an unticked pedal leaves
    ; NOTHING behind has to be testable without a human clicking - which is how
    ; it is checked before every release.
    StrCpy $Chosen 0
    !insertmacro CLI_PEDAL "/BOOST" 1
    !insertmacro CLI_PEDAL "/CHORUS" 2
    !insertmacro CLI_PEDAL "/FLANGER" 4
    !insertmacro CLI_PEDAL "/DELAY" 8
    !insertmacro CLI_PEDAL "/REVERB" 16
    ${If} $Chosen <> 0
        !insertmacro APPLY_CHOICE ${SecBoost} 1
        !insertmacro APPLY_CHOICE ${SecChorus} 2
        !insertmacro APPLY_CHOICE ${SecFlanger} 4
        !insertmacro APPLY_CHOICE ${SecDelay} 8
        !insertmacro APPLY_CHOICE ${SecReverb} 16
    ${EndIf}

    ; SetRegView governs every registry access in this script, including the
    ; uninstall entries. Without it a 32-bit installer writes into WOW6432Node,
    ; where 64-bit "Apps & features" does not look and the entries never appear.
    SetRegView 64

    ClearErrors
    UserInfo::GetAccountType
    Pop $0
    ${If} ${Errors}
        ; Win9x-era fallback path; treat as unprivileged rather than guessing.
        StrCpy $0 "User"
    ${EndIf}

    ; $LOCALAPPDATA FOLLOWS SetShellVarContext, and in "all" context it is not a
    ; per-user folder at all: measured under Wine, current -> C:\users\<name>\
    ; AppData\Local but all -> C:\ProgramData. So the per-user paths are read
    ; here, in "current" context, BEFORE the elevated branch switches to "all" -
    ; otherwise the elevated install looks for the other copies under ProgramData,
    ; never finds them, and silently leaves the duplicates the check exists to
    ; catch. $COMMONFILES64 and $PROGRAMFILES64 do not depend on the context.
    SetShellVarContext current
    StrCpy $1 "$LOCALAPPDATA\Programs\Common\VST3"
    StrCpy $2 "$LOCALAPPDATA\Programs\${APPNAME}"

    ${If} $0 == "Admin"
        SetShellVarContext all
        StrCpy $Vst3Dir "$COMMONFILES64\VST3"
        StrCpy $AppDir "$PROGRAMFILES64\${APPNAME}"
        StrCpy $OtherDir "$1"
    ${Else}
        StrCpy $Vst3Dir "$1"
        StrCpy $AppDir "$2"
        StrCpy $OtherDir "$COMMONFILES64\VST3"
    ${EndIf}

    ; The directory page edits $Vst3Dir; $INSTDIR is set per pedal inside each
    ; section. It is given a value here only because MUI shows the free space of
    ; whatever $INSTDIR names while the directory page is open.
    StrCpy $INSTDIR "$Vst3Dir"
FunctionEnd

; An install of nothing is a user who has not understood the page, not a user
; who wants an empty install: it would run to the finish page having done
; nothing at all and said so nowhere.
;
; It sits BELOW the sections because a section's symbol - ${SecBoost} and the
; rest - exists only from the line its Section is compiled on. The page above
; names this function before it exists, which is fine: NSIS resolves function
; references at the end of the script.
Function OnComponentsLeave
    ${If} ${SectionIsSelected} ${SecBoost}
    ${OrIf} ${SectionIsSelected} ${SecChorus}
    ${OrIf} ${SectionIsSelected} ${SecFlanger}
    ${OrIf} ${SectionIsSelected} ${SecDelay}
    ${OrIf} ${SectionIsSelected} ${SecReverb}
    ${Else}
        MessageBox MB_ICONEXCLAMATION|MB_OK "Choose at least one pedal to install." /SD IDOK
        Abort
    ${EndIf}
FunctionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecBoost}   "An overdrive, one circuit, mono in. Drive, Tone, Level."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecChorus}  "A stereo chorus. Rate, Depth, Mix."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecFlanger} "A through-zero-ish flanger with feedback. Rate, Depth, Manual, Regen."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDelay}   "A delay that can follow your host's tempo, with a ping-pong. Time, Feedback, Tone, Mix."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecReverb}  "A room. Decay, Tone, Pre-delay, Mix."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

;--------------------------------------------------------------------------
; THE UNINSTALLER, WRITTEN FIVE TIMES AND IDENTICAL EVERY TIME. It works out
; which pedal it belongs to from where it was installed: an uninstaller's
; $INSTDIR is the folder it sits in, which is "$AppDir\<Name>", so the leaf of
; that path is the pedal. Nothing else in it branches on the pedal, and there is
; no fifth copy of this logic to keep in step.
Function un.onInit
    SetRegView 64

    ${un.GetFileName} "$INSTDIR" $PedalName
    ${If} $PedalName == ""
        MessageBox MB_ICONSTOP|MB_OK "This uninstaller has been moved out of the folder it was installed into, and can no longer tell which pedal it removes.$\r$\n$\r$\nDelete the Rations<name>.vst3 folder by hand instead." /SD IDOK
        Abort
    ${EndIf}

    ; Which of the two installs this is. HKLM is read first: a machine-wide
    ; install is the one that needs elevation, and getting it wrong that way
    ; fails loudly rather than silently leaving the bundle behind.
    SetShellVarContext all
    ReadRegStr $0 HKLM "${REGROOT}\$PedalName" "BundlePath"
    ${If} $0 == ""
        SetShellVarContext current
        ReadRegStr $0 HKCU "${REGROOT}\$PedalName" "BundlePath"
    ${EndIf}
    StrCpy $R0 $0

    ; $AppDir is the parent of this uninstaller's folder, not a registry value:
    ; it is where this binary actually is, which is the one thing that cannot be
    ; stale.
    ${un.GetParent} "$INSTDIR" $AppDir
FunctionEnd

Section "Uninstall"
    ; $R0 came out of the registry, so it is not trusted the way a path this
    ; script built would be: only remove it if it still looks like this pedal's
    ; bundle. A corrupted or hand-edited value must not turn this into rm -rf.
    ${If} $R0 != ""
    ${AndIf} ${FileExists} "$R0\Contents\x86_64-win\Rations$PedalName.vst3"
        RMDir /r "$R0"
        DetailPrint "Removed $R0"
    ${Else}
        DetailPrint "No Rations$PedalName.vst3 bundle found at $R0 - nothing to remove there"
    ${EndIf}

    DeleteRegKey SHCTX "${UNINSTROOT}\Rations$PedalName"
    DeleteRegKey SHCTX "${REGROOT}\$PedalName"

    ; This pedal's own folder, which holds nothing but this uninstaller.
    Delete "$INSTDIR\*.exe"
    RMDir "$INSTDIR"

    ; THE LAST ONE OUT TAKES THE LICENCE FILES WITH IT. They are shared by all
    ; five, so they can only go when no pedal is left registered - asked of the
    ; registry rather than of the disk, because the disk cannot distinguish
    ; "no pedals installed" from "the folder was emptied by hand".
    EnumRegKey $0 SHCTX "${REGROOT}" 0
    ${If} $0 == ""
        Delete "$AppDir\LICENSE"
        Delete "$AppDir\NOTICE"
        Delete "$AppDir\INSTALL.txt"
        RMDir "$AppDir"
        DeleteRegKey SHCTX "${REGROOT}"
        DetailPrint "That was the last Rations pedal; removed $AppDir"
    ${EndIf}
SectionEnd
