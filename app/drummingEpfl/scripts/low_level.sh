				

#!/bin/bash

MYSHELL="bash -c"

case "$1" in
	kill)
		killall -9 drumGeneratorModule
		sleep 0.2
		killall -9 clockModule
		sleep 0.2
		killall -9 velocityControl
		sleep 0.2
		;;
    stop)
		killall drumGeneratorModule
		sleep 0.2
		killall clockModule
		sleep 0.2
		killall velocityControl
		sleep 0.2
		;;
	start)
		$MYSHELL "velocityControl --period 20 --part right_arm &"
		sleep 1
		$MYSHELL "velocityControl --period 20 --part left_arm &"
		sleep 1
		$MYSHELL "velocityControl --period 20 --part head &"
		sleep 1
		$MYSHELL "velocityControl --period 20 --part right_leg &"
		sleep 1
		$MYSHELL "velocityControl --period 20 --part left_leg &"
		sleep 1
		$MYSHELL "clockModule --period 20 --part head &"
		sleep 1
		$MYSHELL "drumGeneratorModule --period 20 --part head &"
		sleep 1
		$MYSHELL "drumGeneratorModule --period 20 --part right_arm &"
		sleep 1
		$MYSHELL "drumGeneratorModule --period 20 --part left_arm  &"
		sleep 1
		$MYSHELL "drumGeneratorModule --period 20 --part right_leg &"
		sleep 1
		$MYSHELL "drumGeneratorModule --period 20 --part left_leg &"
		sleep 1
		;;

    *)
		echo "stop, start, kill"
		;;
esac
