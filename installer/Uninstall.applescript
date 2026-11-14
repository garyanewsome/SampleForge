on run
	set scriptPath to POSIX path of (path to resource "uninstall.sh")

	display dialog "This will remove @PRODUCT@ (the plugin and the standalone app) from this Mac." & return & return & "Your saved presets are kept." buttons {"Cancel", "Uninstall"} default button "Cancel" with title "Uninstall @PRODUCT@" with icon caution

	-- Close the standalone app if it is open.
	try
		do shell script "/usr/bin/pkill -x @PRODUCT@"
	end try

	try
		do shell script "/bin/bash " & quoted form of scriptPath with administrator privileges
		display dialog "@PRODUCT@ has been removed." & return & return & "Please restart your music software." buttons {"OK"} default button "OK" with title "Uninstall @PRODUCT@"
	on error errMsg number errNum
		if errNum is not -128 then
			display dialog "Something went wrong:" & return & errMsg buttons {"OK"} default button "OK" with title "Uninstall @PRODUCT@" with icon stop
		end if
	end try
end run
