@PRODUCT@ - installing and removing
=============================

INSTALL
  1. Double-click "Install @PRODUCT@.pkg" and follow the steps.
  2. Quit and reopen your music software.

IF MACOS SAYS IT CAN'T BE OPENED
  This installer is not yet signed with an Apple developer certificate, so
  macOS is cautious the first time. This is expected, not a problem with
  your Mac. You only need to do this once per installer:

    1. Double-click "Install @PRODUCT@.pkg". macOS will say it can't be opened.
       Click Done (or OK).
    2. Open System Settings > Privacy & Security and scroll down to the
       Security section. You will see a message about "Install @PRODUCT@.pkg".
       Click "Open Anyway" and enter your Mac password.
    3. Double-click "Install @PRODUCT@.pkg" again and click Open.

  (On macOS 13 and 14 you can instead right-click the file, choose Open, then
  click Open.)

  The same applies to "Uninstall @PRODUCT@".

PASSWORD
  The installer asks for your Mac login password. That is normal: it puts
  the plugin where all your music software can find it. You need to be an
  administrator on the Mac.

REMOVE
  Double-click "Uninstall @PRODUCT@" and follow the prompts.
  Your saved presets are kept in ~/Library/Application Support/@PRODUCT@ and
  can be deleted by hand if you want them gone too.

REQUIREMENTS
  macOS 13 (Ventura) or newer. Works on Intel and Apple Silicon Macs.
