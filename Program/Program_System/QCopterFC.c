/*=====================================================================================================*/
/*=====================================================================================================*/
#include "stm32f4_system.h"

#include "QCopterFC.h"
#include "QCopterFC_ctrl.h"
#include "QCopterFC_ahrs.h"
#include "QCopterFC_transport.h"

#include "module_board.h"
#include "module_rs232.h"
#include "module_motor.h"
#include "module_sensor.h"
#include "module_nrf24l01.h"
#include "module_mpu9150.h"
#include "module_ms5611.h"

#include "test.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "algorithm_pid.h"
#include "algorithm_moveAve.h"
#include "algorithm_mathUnit.h"
#include "algorithm_quaternion.h"

#include "sys_manager.h"

xSemaphoreHandle TIM2_Semaphore = NULL;
xSemaphoreHandle TIM4_Semaphore = NULL;
xTaskHandle FlightControl_Handle = NULL;
xTaskHandle correction_task_handle = NULL;

#define NRF_NOT_EXIST
#define SENSOR_NOT_EXIST
#define SHELL_IS_EXIST

enum SYSTEM_STATUS {
	SYSTEM_UNINITIALIZED,
	SYSTEM_INITIALIZED
};

int System_Status = SYSTEM_UNINITIALIZED;

/*=====================================================================================================*/
#define PRINT_DEBUG(var1) printf("DEBUG PRINT"#var1"\r\n")
/*=====================================================================================================*/
void vApplicationTickHook(void)
{
}


void vApplicationStackOverflowHook(xTaskHandle pxTask, signed char *pcTaskName)
{
}

void vApplicationIdleHook(void)
{

}

void system_init(void)
{	
	LED_Config();
	KEY_Config();
	RS232_Config();
	Motor_Config();
	PWM_Capture_Config();
	Sensor_Config();
	nRF24L01_Config();


	PID_Init(&PID_Yaw);
	PID_Init(&PID_Roll);
	PID_Init(&PID_Pitch);

	PID_Pitch.Kp = +50.0f;
	PID_Pitch.Ki = 0;//0.002f;
	PID_Pitch.Kd = +11.5f;

	PID_Roll.Kp  = +50.0f;
	PID_Roll.Ki  = 0;//0.002f;
	PID_Roll.Kd  = 11.5f;

	PID_Yaw.Kp   = +10.0f;
	PID_Yaw.Ki   = +0.0f;
	PID_Yaw.Kd   = +0.0f;

	Delay_10ms(200);

	u8 Sta = ERROR;


	//while (remote_signal_check() == NO_SIGNAL);

	if (KEY == 1) {
		LED_B = 0;
		Motor_Control(PWM_MOTOR_MAX, PWM_MOTOR_MAX, PWM_MOTOR_MAX, PWM_MOTOR_MAX);
	}

	while (KEY == 1);

	LED_B = 1;
	Motor_Control(PWM_MOTOR_MIN, PWM_MOTOR_MIN, PWM_MOTOR_MIN, PWM_MOTOR_MIN);


#ifndef NRF_NOT_EXIST
	/* nRF Check */
	while (Sta == ERROR)
		Sta = nRF_Check();
#endif

#ifndef SENSOR_NOT_EXIST
	/* Sensor Init */
	if (Sensor_Init() == SUCCESS)
		LED_G = 0;
#endif

	Delay_10ms(10);


	/* Wait Correction */
	//while (SensorMode != Mode_Algorithm);

	/* Lock */
	LED_R = 1;
	LED_G = 1;
	LED_B = 1;

	System_Status = SYSTEM_INITIALIZED;

	/* Clear the screen */
	//putstr("\x1b[H\x1b[2J");

	/* Show the Initialization message */
	putstr("[System status]Initialized successfully!\n\r");	
	
	vTaskDelete(NULL);
}

vs16 Tmp_PID_KP;
vs16 Tmp_PID_KI;
vs16 Tmp_PID_KD;
vs16 Tmp_PID_Pitch;

Sensor_Mode SensorMode = Mode_GyrCorrect;
void correction_task()
{

	LED_B = ~LED_B; //task indicator
	u8 IMU_Buf[20] = {0};

	float Ellipse[5] = {0};

	static u8 BaroCnt = 0;

	static s16 ACC_FIFO[3][256] = {{0}};
	static s16 GYR_FIFO[3][256] = {{0}};

	static u32 Correction_Time = 0;

	/* 500Hz, Read Sensor ( Accelerometer, Gyroscope, Magnetometer ) */
	MPU9150_Read(IMU_Buf);

	/* 100Hz, Read Barometer */
	BaroCnt++;

	if (BaroCnt == SampleRateFreg / 100) {
		MS5611_Read(&Baro, MS5611_D1_OSR_4096);
		BaroCnt = 0;
	}

	Acc.X  = (s16)((IMU_Buf[0]  << 8) | IMU_Buf[1]);
	Acc.Y  = (s16)((IMU_Buf[2]  << 8) | IMU_Buf[3]);
	Acc.Z  = (s16)((IMU_Buf[4]  << 8) | IMU_Buf[5]);
	Temp.T = (s16)((IMU_Buf[6]  << 8) | IMU_Buf[7]);
	Gyr.X  = (s16)((IMU_Buf[8]  << 8) | IMU_Buf[9]);
	Gyr.Y  = (s16)((IMU_Buf[10] << 8) | IMU_Buf[11]);
	Gyr.Z  = (s16)((IMU_Buf[12] << 8) | IMU_Buf[13]);
	Mag.X  = (s16)((IMU_Buf[15] << 8) | IMU_Buf[14]);
	Mag.Y  = (s16)((IMU_Buf[17] << 8) | IMU_Buf[16]);
	Mag.Z  = (s16)((IMU_Buf[19] << 8) | IMU_Buf[18]);

	/* Offset */
	Acc.X -= Acc.OffsetX;
	Acc.Y -= Acc.OffsetY;
	Acc.Z -= Acc.OffsetZ;
	Gyr.X -= Gyr.OffsetX;
	Gyr.Y -= Gyr.OffsetY;
	Gyr.Z -= Gyr.OffsetZ;
	Mag.X *= Mag.AdjustX;
	Mag.Y *= Mag.AdjustY;
	Mag.Z *= Mag.AdjustZ;

#define MovegAveFIFO_Size 250


	LED_R = !LED_R;
	/* Simple Moving Average */
	Gyr.X = (s16)MoveAve_SMA(Gyr.X, GYR_FIFO[0], MovegAveFIFO_Size);
	Gyr.Y = (s16)MoveAve_SMA(Gyr.Y, GYR_FIFO[1], MovegAveFIFO_Size);
	Gyr.Z = (s16)MoveAve_SMA(Gyr.Z, GYR_FIFO[2], MovegAveFIFO_Size);

	Correction_Time++;  // 等待 FIFO 填滿空值 or 填滿靜態資料

	if (Correction_Time == SampleRateFreg) {
		Gyr.OffsetX += (Gyr.X - GYR_X_OFFSET);  // 角速度為 0dps
		Gyr.OffsetY += (Gyr.Y - GYR_Y_OFFSET);  // 角速度為 0dps
		Gyr.OffsetZ += (Gyr.Z - GYR_Z_OFFSET);  // 角速度為 0dps

		Correction_Time = 0;

	}

	LED_R = ~LED_R;
	/* Simple Moving Average */
	Acc.X = (s16)MoveAve_SMA(Acc.X, ACC_FIFO[0], MovegAveFIFO_Size);
	Acc.Y = (s16)MoveAve_SMA(Acc.Y, ACC_FIFO[1], MovegAveFIFO_Size);
	Acc.Z = (s16)MoveAve_SMA(Acc.Z, ACC_FIFO[2], MovegAveFIFO_Size);

	Correction_Time++;  // 等待 FIFO 填滿空值 or 填滿靜態資料

	if (Correction_Time == SampleRateFreg) {
		Acc.OffsetX += (Acc.X - ACC_X_OFFSET);  // 重力加速度為 0g
		Acc.OffsetY += (Acc.Y - ACC_Y_OFFSET);  // 重力加速度為 0g
		Acc.OffsetZ += (Acc.Z - ACC_Z_OFFSET);  // 重力加速度為 1g

		Correction_Time = 0;
	}


	LED_R = !LED_R;

	/* To Physical */
	Acc.TrueX = Acc.X * MPU9150A_4g;      // g/LSB
	Acc.TrueY = Acc.Y * MPU9150A_4g;      // g/LSB
	Acc.TrueZ = Acc.Z * MPU9150A_4g;      // g/LSB
	Gyr.TrueX = Gyr.X * MPU9150G_2000dps; // dps/LSB
	Gyr.TrueY = Gyr.Y * MPU9150G_2000dps; // dps/LSB
	Gyr.TrueZ = Gyr.Z * MPU9150G_2000dps; // dps/LSB
	Mag.TrueX = Mag.X * MPU9150M_1200uT;  // uT/LSB
	Mag.TrueY = Mag.Y * MPU9150M_1200uT;  // uT/LSB
	Mag.TrueZ = Mag.Z * MPU9150M_1200uT;  // uT/LSB
	Temp.TrueT = Temp.T * MPU9150T_85degC; // degC/LSB

	Ellipse[3] = (Mag.X * arm_cos_f32(Mag.EllipseSita) + Mag.Y * arm_sin_f32(Mag.EllipseSita)) / Mag.EllipseB;
	Ellipse[4] = (-Mag.X * arm_sin_f32(Mag.EllipseSita) + Mag.Y * arm_cos_f32(Mag.EllipseSita)) / Mag.EllipseA;

	AngE.Pitch = toDeg(atan2f(Acc.TrueY, Acc.TrueZ));
	AngE.Roll  = toDeg(-asinf(Acc.TrueX));
	AngE.Yaw   = toDeg(atan2f(Ellipse[3], Ellipse[4])) + 180.0f;

	Quaternion_ToNumQ(&NumQ, &AngE);
	vTaskDelay(10);
	vTaskResume(FlightControl_Handle);
	vTaskDelete(NULL);
}

void flightControl_task()
{
	//Waiting for system finish initialize
	while(System_Status == SYSTEM_UNINITIALIZED);

	while (1) {
		LED_B = ~LED_B; //task indicator
		u8 IMU_Buf[20] = {0};

		u16 Final_M1 = 0;
		u16 Final_M2 = 0;
		u16 Final_M3 = 0;
		u16 Final_M4 = 0;

		s16 Thr = 0, Pitch = 0, Roll = 0, Yaw = 0;
		s16 Exp_Thr = 0, Exp_Pitch = 0, Exp_Roll = 0, Exp_Yaw = 0;
		uint8_t safety = 0;
		float Ellipse[5] = {0};

		static u8 BaroCnt = 0;

		static s16 ACC_FIFO[3][256] = {{0}};
		static s16 GYR_FIFO[3][256] = {{0}};
		static s16 MAG_FIFO[3][256] = {{0}};

		static s16 MagDataX[8] = {0};
		static s16 MagDataY[8] = {0};

		static u32 Correction_Time = 0;

		/* 500Hz, Read Sensor ( Accelerometer, Gyroscope, Magnetometer ) */
		MPU9150_Read(IMU_Buf);

		/* 100Hz, Read Barometer */
		BaroCnt++;

		if (BaroCnt == SampleRateFreg / 100) {
			MS5611_Read(&Baro, MS5611_D1_OSR_4096);
			BaroCnt = 0;
		}

		Acc.X  = (s16)((IMU_Buf[0]  << 8) | IMU_Buf[1]);
		Acc.Y  = (s16)((IMU_Buf[2]  << 8) | IMU_Buf[3]);
		Acc.Z  = (s16)((IMU_Buf[4]  << 8) | IMU_Buf[5]);
		Temp.T = (s16)((IMU_Buf[6]  << 8) | IMU_Buf[7]);
		Gyr.X  = (s16)((IMU_Buf[8]  << 8) | IMU_Buf[9]);
		Gyr.Y  = (s16)((IMU_Buf[10] << 8) | IMU_Buf[11]);
		Gyr.Z  = (s16)((IMU_Buf[12] << 8) | IMU_Buf[13]);
		Mag.X  = (s16)((IMU_Buf[15] << 8) | IMU_Buf[14]);
		Mag.Y  = (s16)((IMU_Buf[17] << 8) | IMU_Buf[16]);
		Mag.Z  = (s16)((IMU_Buf[19] << 8) | IMU_Buf[18]);

		/* Offset */
		Acc.X -= Acc.OffsetX;
		Acc.Y -= Acc.OffsetY;
		Acc.Z -= Acc.OffsetZ;
		Gyr.X -= Gyr.OffsetX;
		Gyr.Y -= Gyr.OffsetY;
		Gyr.Z -= Gyr.OffsetZ;
		Mag.X *= Mag.AdjustX;
		Mag.Y *= Mag.AdjustY;
		Mag.Z *= Mag.AdjustZ;



		/* 加權移動平均法 Weighted Moving Average */
		Acc.X = (s16)MoveAve_WMA(Acc.X, ACC_FIFO[0], 8);
		Acc.Y = (s16)MoveAve_WMA(Acc.Y, ACC_FIFO[1], 8);
		Acc.Z = (s16)MoveAve_WMA(Acc.Z, ACC_FIFO[2], 8);
		Gyr.X = (s16)MoveAve_WMA(Gyr.X, GYR_FIFO[0], 8);
		Gyr.Y = (s16)MoveAve_WMA(Gyr.Y, GYR_FIFO[1], 8);
		Gyr.Z = (s16)MoveAve_WMA(Gyr.Z, GYR_FIFO[2], 8);
		Mag.X = (s16)MoveAve_WMA(Mag.X, MAG_FIFO[0], 64);
		Mag.Y = (s16)MoveAve_WMA(Mag.Y, MAG_FIFO[1], 64);
		Mag.Z = (s16)MoveAve_WMA(Mag.Z, MAG_FIFO[2], 64);

	/* To Physical */
		Acc.TrueX = Acc.X * MPU9150A_4g;      // g/LSB
		Acc.TrueY = Acc.Y * MPU9150A_4g;      // g/LSB
		Acc.TrueZ = Acc.Z * MPU9150A_4g;      // g/LSB
		Gyr.TrueX = Gyr.X * MPU9150G_2000dps; // dps/LSB
		Gyr.TrueY = Gyr.Y * MPU9150G_2000dps; // dps/LSB
		Gyr.TrueZ = Gyr.Z * MPU9150G_2000dps; // dps/LSB
		Mag.TrueX = Mag.X * MPU9150M_1200uT;  // uT/LSB
		Mag.TrueY = Mag.Y * MPU9150M_1200uT;  // uT/LSB
		Mag.TrueZ = Mag.Z * MPU9150M_1200uT;  // uT/LSB
		Temp.TrueT = Temp.T * MPU9150T_85degC; // degC/LSB

		/* Get Attitude Angle */
		AHRS_Update();


		/*Get RC Control*/
		Update_RC_Control(&Exp_Roll, &Exp_Pitch, &Exp_Yaw, &Exp_Thr, &safety);
		global_var[RC_EXP_THR].param  = Exp_Thr;
		global_var[RC_EXP_ROLL].param = Exp_Roll;
		global_var[RC_EXP_PITCH].param = Exp_Pitch;
		global_var[RC_EXP_YAW].param = Exp_Yaw;
		/* Get ZeroErr */
		PID_Pitch.ZeroErr = (float)((s16)Exp_Pitch);
		PID_Roll.ZeroErr  = (float)((s16)Exp_Roll);
		PID_Yaw.ZeroErr   = (float)((s16)Exp_Yaw) + 180.0f;

		/* PID */
		Roll  = (s16)PID_AHRS_Cal(&PID_Roll,   AngE.Roll,  Gyr.TrueX);
		Pitch = (s16)PID_AHRS_Cal(&PID_Pitch,  AngE.Pitch, Gyr.TrueY);
//      Yaw   = (s16)PID_AHRS_CalYaw(&PID_Yaw, AngE.Yaw,   Gyr.TrueZ);
		Yaw   = (s16)(PID_Yaw.Kd * Gyr.TrueZ) + 3 * (s16)Exp_Yaw;
		Thr   = (s16)Exp_Thr;

		/* Motor Ctrl */
		Final_M1 = Thr + Pitch - Roll - Yaw;
		Final_M2 = Thr + Pitch + Roll + Yaw;
		Final_M3 = Thr - Pitch + Roll - Yaw;
		Final_M4 = Thr - Pitch - Roll + Yaw;

		/* Check Connection */
#define NoSignal 1  // 1 sec

		if (safety == 1) {
			Motor_Control(PWM_MOTOR_MIN, PWM_MOTOR_MIN, PWM_MOTOR_MIN, PWM_MOTOR_MIN);

		} else {
			Motor_Control(Final_M1, Final_M2, Final_M3, Final_M4);
		}

		/* DeBug */
		Tmp_PID_KP = PID_Yaw.Kp * 1000;
		Tmp_PID_KI = PID_Yaw.Ki * 1000;
		Tmp_PID_KD = PID_Yaw.Kd * 1000;
		Tmp_PID_Pitch = Yaw;

		vTaskDelay(2);

	}
}


void statusReport_task()
{
	//Waiting for system finish initialize
	while(System_Status == SYSTEM_UNINITIALIZED);

	while (1) {
		printf("Roll = %f,Pitch = %f,Yaw = %f \r\n",
		       AngE.Roll, AngE.Pitch, AngE.Yaw);
		printf("CH1 %f(%f),CH2 %f(%f),CH3 %f(%f),CH4 %f(%f),CH5 %f()\r\n",
		       global_var[PWM1_CCR].param, global_var[RC_EXP_ROLL].param,
		       global_var[PWM2_CCR].param, global_var[RC_EXP_PITCH].param,
		       global_var[PWM3_CCR].param, global_var[RC_EXP_THR].param,
		       global_var[PWM4_CCR].param, global_var[RC_EXP_YAW].param,
		       global_var[PWM5_CCR].param);
		printf("\r\n");

		vTaskDelay(500);
	}
}

/*=====================================================================================================*/

void check_task()
{
	//Waiting for system finish initialize
	while(System_Status == SYSTEM_UNINITIALIZED);

	while (remote_signal_check() == NO_SIGNAL);
	vTaskResume(correction_task_handle);
	vTaskDelete(NULL);

}

void shell_task()
{
	while(System_Status == SYSTEM_UNINITIALIZED);

	char *shell_str;

	putstr("Please type \"help\" to get more informations\n\r");
	while(1) {
		shell_str = linenoise("User > ");
	}
}

int main(void)
{
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

	vSemaphoreCreateBinary(TIM2_Semaphore);
	vSemaphoreCreateBinary(TIM4_Semaphore);
	vSemaphoreCreateBinary(serial_tx_wait_sem);

	serial_rx_queue = xQueueCreate(1, sizeof(serial_msg));
#ifndef SHELL_IS_EXIST
	xTaskCreate(check_task,
		(signed portCHAR *) "Initial checking",
		512, NULL,
		tskIDLE_PRIORITY + 5, NULL);
	xTaskCreate(correction_task,
		(signed portCHAR *) "Initial checking",
		4096, NULL,
		tskIDLE_PRIORITY + 5, &correction_task_handle);


	xTaskCreate(statusReport_task,
		(signed portCHAR *) "Status report",
		512, NULL,
		tskIDLE_PRIORITY + 5, NULL);
#endif

	xTaskCreate(shell_task,
		(signed portCHAR *) "Shell",
		512, NULL,
		tskIDLE_PRIORITY + 5, NULL);
#ifndef SHELL_IS_EXIST
	xTaskCreate(flightControl_task,
		(signed portCHAR *) "Flight control",
		4096, NULL,
		tskIDLE_PRIORITY + 9, &FlightControl_Handle);
#endif
	xTaskCreate(system_init,
		(signed portCHAR *) "System Initialiation",
		512, NULL,
		tskIDLE_PRIORITY + 9, NULL);

	vTaskSuspend(FlightControl_Handle);
	vTaskSuspend(correction_task_handle);
	vTaskStartScheduler();

	return 0;
}
/*=====================================================================================================*/
/*=====================================================================================================*/
