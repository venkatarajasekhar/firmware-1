/* Standard C lib includes */
#include <stddef.h>
#include <string.h>

/* Linenoise and shell includes */
#include "shell.h"
#include "linenoise.h"
#include "parser.h"

#include "module_rs232.h"
#include "algorithm_quaternion.h"
#include "sys_manager.h"

#include "FreeRTOS.h"
#include "task.h"

/* Shell Command handlers */
void monitor_unknown_cmd(char parameter[][MAX_CMD_LEN], int par_cnt);

/* The identifier of the command */
enum MONITOR_CMD_ID {
	unknown_cmd_ID,
	MONITOR_CMD_CNT
};

//First string don't need to store anything for unknown commands
command_list monitorCmd_list[MONITOR_CMD_CNT] = {
	CMD_DEF(unknown_cmd, monitor)
};

/* Internal commands */
enum MONITOR_INTERNAL_CMD {
	MONITOR_UNKNOWN,
	MONITOR_QUIT,
	MONITOR_RESUME,
	MONITOR_IT_CMD_CNT
};

char monitor_cmd[MONITOR_IT_CMD_CNT - 1][MAX_CMD_LEN] = {"quit", "resume"};

int monitorInternalCmdIndentify(char *command)
{
	int i;
	for(i = 0; i < (MONITOR_IT_CMD_CNT - 1); i++) {
		if(strcmp(command ,monitor_cmd[i]) == 0) {
			return i + 1;
		}
	}

	return MONITOR_UNKNOWN;
}

void shell_monitor(char parameter[][MAX_CMD_LEN], int par_cnt)
{
	while(1) {	
		/* Clean the screen */
		printf("\x1b[H\x1b[2J");

		/* Welcome Messages */
		printf("QuadCopter Status Monitor\n\r");
		printf("Copyleft - NCKU Open Source Work of 2013 Embedded system class\n\r");
		printf("**************************************************************\n\r");

		printf("PID\tPitch\tRoll\tYow\n\r");
		printf("Kp\t%d\t%d\t%d\n\r", 0, 0, 0);
		printf("Ki\t%d\t%d\t%d\n\r", 0, 0, 0);
		printf("Kd\t%d\t%d\t%d\n\r", 0, 0, 0);

		printf("--------------------------------------------------------------\n\r");

		printf("Copter Attitudes <true value>\n\r");
		printf("Pitch\t: %d\n\rRoll\t: %d\n\rYaw\t: %d\n\r", AngE.Pitch, AngE.Roll, AngE.Yaw);

		printf("--------------------------------------------------------------\n\r");

		#define MOTOR_STATUS "Off"
		printf("RC Messages\tCurrent\tLast\n\r");
		printf("Pitch(expect)\t%d\t%d\n\r", global_var[RC_EXP_PITCH].param, 0);
		printf("Roll(expect)\t%d\t%d\n\r", global_var[RC_EXP_ROLL].param, 0);
		printf("Yaw(expect)\t%d\t%d\n\r", global_var[RC_EXP_YAW].param, 0);	

		printf("Throttle\t%d\n\r", global_var[RC_EXP_THR].param);
		printf("Engine\t\t%s\n\r", MOTOR_STATUS);

		printf("--------------------------------------------------------------\n\r");

		#define LED_STATUS "Off"
		printf("LED lights\n\r");
		printf("LED1\t: %s\n\rLED2\t: %s\n\rLED3\t: %s\n\rLED4\t: %s\n\r", LED_STATUS, LED_STATUS, LED_STATUS, LED_STATUS);

		printf("**************************************************************\n\r\n\r");

		printf("[Please press <Space> to refresh the status]\n\r");
		printf("[Please press <Enter> to enable the command line]");
	
		int monitor_cmd = 0;		
		char key_pressed = serial.getch();
	
		while(monitor_cmd != MONITOR_QUIT && monitor_cmd != MONITOR_RESUME) {
			if(key_pressed == ENTER) {
				/* Clean and move up two lines*/
				printf("\x1b[0G\x1b[0K\x1b[0A\x1b[0G\x1b[0K");
	
				while(monitor_cmd != MONITOR_QUIT && monitor_cmd != MONITOR_RESUME) {
					char *command_str;

					command_str = linenoise("(monitor) ");
				
					/* Check if it is internal command */
					/* The Internal command means that need not call
					   any functions but handle at here
					 */
					monitor_cmd = monitorInternalCmdIndentify(command_str);

					/* Check if it is not an Internal command */
					if(monitor_cmd == MONITOR_UNKNOWN) {
						/* FIXME:External Commands, need to call other functions */
						command_data monitor_cd = {.par_cnt = 0};			
						commandExec(command_str, &monitor_cd, monitorCmd_list, MONITOR_CMD_CNT);
					} else {
						printf("\x1b[0A");
					}
				}
			} else if(key_pressed == SPACE) {
				break;
			} else {
				key_pressed = serial.getch();
			}			
		}

		if(monitor_cmd == MONITOR_QUIT)
			break;
	}

	/* Clean the screen */
        printf("\x1b[H\x1b[2J");
}

/**** Customize command function ******************************************************/
void monitor_unknown_cmd(char parameter[][MAX_CMD_LEN], int par_cnt)
{
 	printf("\x1b[0A\x1b[0G\x1b[0K"); 
	printf("[Unknown command:Please press any key to resume...]");

	serial.getch();
	printf("\x1b[0G\x1b[0K");
}
