/*****************************************************************************
$Work file     : display.cpp $
Description    : This file contains the Initialization of display and sensor tasks.
Project(s)     : Smart Health Gear
Compiler       : Cross ARM GCC
OS			   : RTOS
Original Author: $ Aniket Dali
$Author        : $ Aniket Dali
$Date          : $ 26 May 2017
$Revision      : 1.0 $
*****************************************************************************/
/****************************************************************************/
/*                       INCLUDE FILES                                      */
/****************************************************************************/
#include "display.hpp"
#include "printf_lib.h"
#include "utilities.h"
#include "stdint.h"
#include "LPC17xx.h"
#include "stdlib.h"
#include "rtc.h"
#include <iostream>
#include <stdio.h>
#include <string.h>
#include "eint.h"
#include "semphr.h"
#include "stdio.h"
#include "timers.h"
#include "lpc_timers.h"
#include <sstream>
#include "uart3.hpp"
extern "C"
{
	#include "gpio.h"
}
using namespace std;


/****************************************************************************/
/*                        VARIABLES AND MACROS                              */
/****************************************************************************/
// global buffer to convert integers into ascii to be sent over SPI to Display
char buff[50] = {ZERO};
// acquire the UART3 instance to push the data out over to the Bluetooth Application
Uart3& uart_3 = Uart3::getInstance();
// pointer to 100 millisecond pointer
LPC_TIM_TypeDef *Hundred_millisec_timer_ptr    = NULL;
// Instance of RTC to load the current time.
rtc_t mytime;
// Global variable to hold the data acquired through Queues.
int32_t Queue_data              = ZERO;
// Display Screens
volatile screens watch_skins    = clock_screen;
volatile screens previous_skins = clock_screen;

/************************** Semaphores and Mutexs ****************************/
// Semaphores to signal events to update display clock
SemaphoreHandle_t triggerOneSecond	           = NULL;
SemaphoreHandle_t triggerOneMin  	           = NULL;
SemaphoreHandle_t triggerOneHour  	           = NULL;
SemaphoreHandle_t triggerOneDay  	  		   = NULL;
SemaphoreHandle_t triggerOneMonth	   		   = NULL;
SemaphoreHandle_t triggerOneYear	   		   = NULL;
// Semaphores to signal event to lock/ unlock the display
SemaphoreHandle_t lock_unlock_button	       = NULL;
// Semaphores to signal event to show sensor values over display
SemaphoreHandle_t senor_button	      		   = NULL;
// Semaphores to control display timeout and switch de-bounces.
SemaphoreHandle_t Timer_start				   = NULL;
SemaphoreHandle_t Timer_increment			   = NULL;
SemaphoreHandle_t sensor_debounce			   = NULL;

// Semaphores to refresh displayed sensor parameters on change in readings
SemaphoreHandle_t BS_REFRESH	               = NULL;
SemaphoreHandle_t O2_REFRESH	      		   = NULL;
SemaphoreHandle_t BT_REFRESH				   = NULL;
SemaphoreHandle_t ST_REFRESH     			   = NULL;

// Mutex for mutual exclusion of LCD Refresh.
SemaphoreHandle_t screen_change	     		   = NULL;

/****************************************************************************/
/*                       FUNCTION DECLARATAIONS                             */
/****************************************************************************/
// callback for navigation button
void callback_parameters();
// Function to update all the software timers every 100 msec
void Update_timer();
// Timer 3 Interrupt
void Timer3_100ms_init(void);
// Routine to configure Power to drive LCD
void LCDPower();


/**************** Character Array Operations ****************************/
// Routine to Trim the character array
char * trim(char * str,uint8_t start, uint8_t end);
// Routine to convert Integer to ASCII
char* itoa(int num, char* str, int base);
// Routine to reverse the character string
void reverse(char str[], int length);

/****************************************************************************/
/*                       FUNCTION DEFINITIONS                               */
/****************************************************************************/



/*----------------------------------------------------------------------------
Function    :  display_Task (Constructor)
Inputs      :  None
Processing  :  This function is Constructor for the display_Task. It configures
			   the SSP interface and port pins to interact with LCD module ILI9341
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
display_Task :: display_Task(uint8_t priority) :scheduler_task("display", 10240, priority)
{

	SSP0_power(ON);
	// clear the settings
	LPC_SC->PCLKSEL1 &= ~(0b11<<10);
	// set PCLK = CLK
	LPC_SC->PCLKSEL1 |= (0b01<<10);
	// SCK1
	GPIOSetMode(0,15,FUNC3);
	// MISO
	GPIOSetMode(0,17,FUNC3);
	// MOSI
	GPIOSetMode(0,18,FUNC3);
	// SCK speed = CPU / 2 = 24 Mhz
    LPC_SSP0->CPSR = 2;
	// set the data transfer to 8 bits.
	LPC_SSP0->CR0  =   0b0111;
	// Turn on SSP module
	SSP0_enable();
	// CS
	GPIOSetDir(0,29,OUTPUT);
	// RESET
	GPIOSetDir(0,30,OUTPUT);
	// DC/RS
	GPIOSetDir(1,19,OUTPUT);
	// LED pin
	GPIOSetDir(1,28,OUTPUT);
	// Turn on the power to LED driver circuit
	GPIOSetValue(1,28,1);
}
/*----------------------------------------------------------------------------
Function    :  display_Task (run)
Inputs      :  None
Processing  :  This function is run function for the display_Task. It de-queues
			   data from sensor queues, maintains the display state and refreshes
			   the display to display current time and sensor values.
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
bool display_Task::run(void* p)
{

	// Display initial  sensor values ( All 0's)
	xSemaphoreGive(BS_REFRESH);
	xSemaphoreGive(O2_REFRESH);
	xSemaphoreGive(BT_REFRESH);
	xSemaphoreGive(ST_REFRESH);


	while(1)
	{
			 // Check if the task has right to change the screen
			 if(xSemaphoreTake(screen_change,100))
			 {
				switch(watch_skins)
				{
				  // Screen has to be changed to Clock screen
				  case clock_screen:
					 if(previous_skins == sensor_screen )
					{
						 // Store the Previous screen state
						 previous_skins = clock_screen;
						 // Clear sensor Screen to display black background
						 clearScrn1();
						 // Display clock Screen
						 displayScrn2();
					}
					// if a second has elapsed,check whether minutes, hours,
					// days, months or years have changed along with it
					if( xSemaphoreTake(triggerOneSecond,0))
					{
						minute_check();
					}
					// Year has changed
					if(xSemaphoreTake(triggerOneYear,0))
					{
						event = YEAR_EVENT;
						displayScrn2();
					}
					// Month has changed
					else if( xSemaphoreTake(triggerOneMonth,0))
					{
						event = MONTH_EVENT;
						displayScrn2();
					}
					// Day has changed
					else if( xSemaphoreTake(triggerOneDay,0))
					{
						event = DAY_EVENT;
						displayScrn2();
					}
					// Hour has changed
					else if( xSemaphoreTake(triggerOneHour,0))
					{
						event = HOUR_EVENT;
						displayScrn2();
					}
					// Minute has changed
					else if( xSemaphoreTake(triggerOneMin,0))
					{
						event = MINUTE_EVENT;
						displayScrn2();
					}
					break;

					// Screen has to be changed to sensor screen
				case sensor_screen:
					if(previous_skins == clock_screen )
					{
						// Clear Watch Screen to display black background
						clearScrn2();
						// Store the Previous screen state
						previous_skins = sensor_screen;
					}
					// Display sensor Screen
					displayScrn1();
					 break;
					 // Future implementation
				case warning_screen:
					break;
				}
				// Release lock
				xSemaphoreGive(screen_change);

			// if Heart rate value has changed
			if(xQueueReceive(heart_data,&Queue_data,100))
			{
				// Update the value
				if(BS != Queue_data)
				{
					BS = Queue_data;
					// Signal to update it on next refresh
					xSemaphoreGive(BS_REFRESH);
				}
			}
			// if Oxygen value has changed
			if(xQueueReceive(oxygen_data,&Queue_data,100))
			{
				// Update the value
				if(OX != Queue_data)
				{
					OX = Queue_data;
					// Signal to update it on next refresh
					xSemaphoreGive(O2_REFRESH);
				}
			}
			// if Temperature value has changed
			if(xQueueReceive(temp_data,&Queue_data,50))
			{
				// Update the value
				if(BT != Queue_data)
				{
					BT = Queue_data;
					// Signal to update it on next refresh
					xSemaphoreGive(BT_REFRESH);
				}
			}
			// if Number of steps has changed
			if(xQueueReceive(step_data,&Queue_data,100))
			{
				// Update the value
				if(ST != Queue_data)
				{
					ST = Queue_data;
					// Signal to update it on next refresh
					xSemaphoreGive(ST_REFRESH);
				}
			}

			// Push the data out over Uart- HC05 for android application
			uart_3.printf("#%3d+%3d+%3d+%4d+~",BS, OX, BT, ST);
			// Sleep for 100 ticks so that lower priority tasks can run
			vTaskDelay(100);
		}
	}
	return 1;
}
/*----------------------------------------------------------------------------
Function    :  button_Task (constructor)
Inputs      :  None
Processing  :  This function is constructor for the button_Task. It configures
			   port pins to interact with LCD module ILI9341
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
button_Task :: button_Task(uint8_t priority) :scheduler_task("display", 1024, priority)
{
	   // Configure the interrupt for navigation Button to display Sensor Screen
	  eint3_enable_port2(5,eint_rising_edge,callback_parameters);
	  // Configure the interrupt to Lock/Unlock screen
	  eint3_enable_port2(6,eint_rising_edge,LCDPower);
}
/*----------------------------------------------------------------------------
Function    :  button_Task (Init)
Inputs      :  None
Processing  :  This function creates semaphores to signal events for LCD interaction
			   buttons
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
bool button_Task :: init()
{
	lock_unlock_button   = xSemaphoreCreateBinary();
	senor_button         = xSemaphoreCreateBinary();
	return 1;
}
/*----------------------------------------------------------------------------
Function    :  button_Task (run)
Inputs      :  None
Processing  :  This function creates is a run function for button tasks
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
bool button_Task :: run(void *p)
{
	while(1)
	{

		// Has Sensor screen button pressed?
		if(xSemaphoreTake(sensor_debounce,0))
		{
			// Acquire Lock to change the screen
			if(xSemaphoreTake(screen_change,portMAX_DELAY))
			{
				// Start the screen time_out
				xSemaphoreGive(Timer_start);
				previous_skins = watch_skins;
				// change state machine to display sensor screen
				watch_skins = sensor_screen;

				// refresh the sensor screen with the new values.
				xSemaphoreGive(BS_REFRESH);
				xSemaphoreGive(O2_REFRESH);
				xSemaphoreGive(BT_REFRESH);
				xSemaphoreGive(ST_REFRESH);
				// Release the Lock
				xSemaphoreGive(screen_change);
				// Sleep so that RTOS can schedule another sensor task.
				vTaskDelay(100);
			}
		}
		// Screen time has occurred?
		else if(xSemaphoreTake(Timer_increment,0))
		{
			// Acquire Lock to change the screen
			if(xSemaphoreTake(screen_change,portMAX_DELAY))
			{
				// change state machine to display clock screen
				previous_skins = watch_skins;
				watch_skins = clock_screen;
				// Release the Lock
				xSemaphoreGive(screen_change);
				// Sleep so that RTOS can schedule another sensor task.
				vTaskDelay(100);
			}

		}
		else
		{
			// No events, Sleep
			vTaskDelay(200);
		}
	}

	return 1;
}
/*----------------------------------------------------------------------------
Function    :  callback_parameters()
Inputs      :  None
Processing  :  This function signals debounce timer to start
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void callback_parameters()
{
	// Signal the Sensor Screen button pressed event to start the de-bounce timer
	// to verify the validity of the button pressed
	xSemaphoreGive(senor_button);
}
/*----------------------------------------------------------------------------
Function    :  LCDPower()
Inputs      :  None
Processing  :  This function controls the power to LCD
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void LCDPower()
{
	static bool Power = SET;
	if(Power)
	{
		// Turn Off the LCD
		GPIOSetValue(1,28,0);
		Power = 0;
	}
	else
	{
		// Turn ON the LCD
		GPIOSetValue(1,28,1);
		Power = 1;
	}
}
/*----------------------------------------------------------------------------
Function    :  display_Task(init)
Inputs      :  None
Processing  :  This function Initializes LCD ILI9341, Creates Queues, Semaphores
			   and mutex for signaling the sensor tasks, acquiring the data.
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
bool display_Task::init(void)
{

	// Give a nudge on Reset pin of LCD to initiate the setup process
	 display_RST_dessert();
	 display_RST_assert();
	 delay_ms(5);
	 display_RST_dessert();
	 delay_ms(20);
	 display_RST_assert();
	 delay_ms(150);

	/*********************** commands to configure ILI9340I **********************/
	  writecommand(0xEF);
	  writedata(0x03);
	  writedata(0x80);
	  writedata(0x02);

	  writecommand(0xCF);
	  writedata(0x00);
	  writedata(0XC1);
	  writedata(0X30);

	  writecommand(0xED);
	  writedata(0x64);
	  writedata(0x03);
	  writedata(0X12);
	  writedata(0X81);

	  writecommand(0xE8);
	  writedata(0x85);
	  writedata(0x00);
	  writedata(0x78);

	  writecommand(0xCB);
	  writedata(0x39);
	  writedata(0x2C);
	  writedata(0x00);
	  writedata(0x34);
	  writedata(0x02);

	  writecommand(0xF7);
	  writedata(0x20);

	  writecommand(0xEA);
	  writedata(0x00);
	  writedata(0x00);

	  // Power control
	  writecommand(ILI9340_PWCTR1);
	  // VRH[5:0]
	  writedata(0x23);

	  // Power control
	  writecommand(ILI9340_PWCTR2);
	  // SAP[2:0];BT[3:0]
	  writedata(0x10);

	  // VCM control
	  writecommand(ILI9340_VMCTR1);
	  writedata(0x3e);
	  writedata(0x28);

	  // VCM control2
	  writecommand(ILI9340_VMCTR2);
	  writedata(0x86);

	  // Memory Access Control
	  writecommand(ILI9340_MADCTL);
	  writedata(ILI9340_MADCTL_MX | ILI9340_MADCTL_BGR);

	  writecommand(ILI9340_PIXFMT);
	  writedata(0x55);

	  writecommand(ILI9340_FRMCTR1);
	  writedata(0x00);
	  writedata(0x18);

	  // Display Function Control
	  writecommand(ILI9340_DFUNCTR);
	  writedata(0x08);
	  writedata(0x82);
	  writedata(0x27);

	  // 3Gamma Function Disable
	  writecommand(0xF2);
	  writedata(0x00);

	  //Gamma curve selected
	  writecommand(ILI9340_GAMMASET);
	  writedata(0x01);

	  // Set Gamma
	  writecommand(ILI9340_GMCTRP1);
	  writedata(0x0F);
	  writedata(0x31);
	  writedata(0x2B);
	  writedata(0x0C);
	  writedata(0x0E);
	  writedata(0x08);
	  writedata(0x4E);
	  writedata(0xF1);
	  writedata(0x37);
	  writedata(0x07);
	  writedata(0x10);
	  writedata(0x03);
	  writedata(0x0E);
	  writedata(0x09);
	  writedata(0x00);

	  // Set Gamma
	  writecommand(ILI9340_GMCTRN1);
	  writedata(0x00);
	  writedata(0x0E);
	  writedata(0x14);
	  writedata(0x03);
	  writedata(0x11);
	  writedata(0x07);
	  writedata(0x31);
	  writedata(0xC1);
	  writedata(0x48);
	  writedata(0x08);
	  writedata(0x0F);
	  writedata(0x0C);
	  writedata(0x31);
	  writedata(0x36);
	  writedata(0x0F);

	  // Exit Sleep mode
	  writecommand(ILI9340_SLPOUT);
	  delay_ms(120);
	  // Turn the display on
	  writecommand(ILI9340_DISPON);

	  // Make the background black
	  clearScrn();
	  // Display Clock at the begining of the product.
	  displayScrn2();


	 // Create a binary semaphore to signal 1 second's delay
	 triggerOneSecond  = xSemaphoreCreateBinary();
	 // Create a binary semaphore to signal 1 minute's delay
	 triggerOneMin     = xSemaphoreCreateBinary();
	 // Create a binary semaphore to signal 1 hour's   delay
	 triggerOneHour    = xSemaphoreCreateBinary();
	 // Create a binary semaphore to signal 1 day's    delay
	 triggerOneDay     = xSemaphoreCreateBinary();
	 // Create a binary semaphore to signal 1 months's delay
	 triggerOneMonth   = xSemaphoreCreateBinary();
	 // Create a binary semaphore to signal 1 year's   delay
	 triggerOneYear    = xSemaphoreCreateBinary();
	 // Create a binary semaphore to start timer count down for sensor screen
	 Timer_start	   = xSemaphoreCreateBinary();
	 // Create a binary semaphore to signal increment timer by 1sec
	 Timer_increment   = xSemaphoreCreateBinary();
	 sensor_debounce   = xSemaphoreCreateBinary();

	 // Create a Mutex to provide mutually exclusive access to LCD refresh
	 screen_change     = xSemaphoreCreateMutex();

	 // Create a binary semaphores to signal sensor parameters display refresh
	 BS_REFRESH        = xSemaphoreCreateBinary();
	 O2_REFRESH        = xSemaphoreCreateBinary();
	 BT_REFRESH        = xSemaphoreCreateBinary();
	 ST_REFRESH        = xSemaphoreCreateBinary();

	 // Create a Queue of depth 1 to get data from Oxymeter sensor
	 oxygen_data = xQueueCreate(1,sizeof(int32_t));
	 // Create a Queue of depth 1 to get data from Heart rate sensor
	 heart_data  = xQueueCreate(1,sizeof(int32_t));
	 // Create a Queue of depth 1 to get data from Body Temperature sensor
	 temp_data   = xQueueCreate(1,sizeof(int32_t));
	 // Create a Queue of depth 1 to get data from Accelerometer sensor
	 step_data	 = xQueueCreate(1,sizeof(int32_t));

	 // Initialize 100ms timer
	 Timer3_100ms_init();

	 // Get the current time from RTC
	 minute = rtc_getmin();
	 hour   = rtc_gethour();
	 month  = rtc_getmonth();
	 day    = rtc_getday();
	 year   = rtc_getyear();

	 // Initialize UART 3 to communicate over UART with the bluetooth task
	 UART3_init();

     return 1;
}

/*----------------------------------------------------------------------------
Function    :  clearminute()
Inputs      :  None
Processing  :  This function clears minute field
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::clearminute(void)
{
	fillRect(130,70,70,47,ILI9340_BLACK);
}
/*----------------------------------------------------------------------------
Function    :  clearhour()
Inputs      :  None
Processing  :  This function clears hours field
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::clearhour(void)
{
	fillRect(40,70,160,47,ILI9340_BLACK);
}
/*----------------------------------------------------------------------------
Function    :  clearday()
Inputs      :  None
Processing  :  This function clears days field
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::clearday(void)
{
	fillRect(85,130,40,25,ILI9340_BLACK);
	clearhour();
}
/*----------------------------------------------------------------------------
Function    :  clearmonth()
Inputs      :  None
Processing  :  This function clears months field
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::clearmonth(void)
{
	fillRect(10,130,110,25,ILI9340_BLACK);
	clearhour();
}
/*----------------------------------------------------------------------------
Function    :  clearscrn1()
Inputs      :  None
Processing  :  This function clears sensor screen
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::clearScrn1(void)
{
	drawFastVLine(5,0,200,ILI9340_BLACK);
	drawFastVLine(142,0,200,ILI9340_BLACK);
	drawFastVLine(235,0,200,ILI9340_BLACK);

	drawFastHLine(5,0,231,ILI9340_BLACK);
	drawFastHLine(5,50,231,ILI9340_BLACK);
	drawFastHLine(5,100,231,ILI9340_BLACK);
	drawFastHLine(5,150,231,ILI9340_BLACK);
	drawFastHLine(5,200,231,ILI9340_BLACK);
	drawString("Heart Rate",12,20,2,ILI9340_BLACK);
	drawString("Blood Oxygen",12,70,2,ILI9340_BLACK);
	drawString("Body Temp",12,120,2,ILI9340_BLACK);
	drawString("step count",12,170,2,ILI9340_BLACK);
	// Clear the readings
	fillRect(148,20,235,190,ILI9340_BLACK);

}
/*----------------------------------------------------------------------------
Function    :  clearscrn2()
Inputs      :  None
Processing  :  This function clears entire screen
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::clearScrn2(void)
{
	fillRect(25,70,200,85,ILI9340_BLACK);
}
/*----------------------------------------------------------------------------
Function    :  clearOX()
Inputs      :  None
Processing  :  This function clears Oxygen parameters
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::clearOX(void)
{
	fillRect(145,55,86,35,ILI9340_BLACK);
}
/*----------------------------------------------------------------------------
Function    :  clearBS()
Inputs      :  None
Processing  :  This function clears Bits per second parameters
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::clearBS(void)
{
	fillRect(145,15,86,35,ILI9340_BLACK);
}
/*----------------------------------------------------------------------------
Function    :  clearBT()
Inputs      :  None
Processing  :  This function clears Body Temperature parameters
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::clearBT(void)
{
	fillRect(145,105,86,35,ILI9340_BLACK);
}
/*----------------------------------------------------------------------------
Function    :  clearST()
Inputs      :  None
Processing  :  This function clears Step parameters
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::clearST(void)
{
	fillRect(145,165,86,35,ILI9340_BLACK);
}
/*----------------------------------------------------------------------------
Function    :  clearScrn()
Inputs      :  None
Processing  :  This function clears entire the screen
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::clearScrn(void)
{
	fillRect(0,0,240,320,ILI9340_BLACK);
}
/*----------------------------------------------------------------------------
Function    :  clearScrn3()
Inputs      :  None
Processing  :  This function clears Step parameters
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::clearScrn3(void)
{
	drawTriangle(120,10,240,190,10,190,ILI9340_BLACK);
	drawString("!",110,50,4,ILI9340_BLACK);
	drawString("ALERT",90,90,3,ILI9340_BLACK);
	fillRect(75,125,115,55,ILI9340_BLACK);
}
/*----------------------------------------------------------------------------
Function    :  displayScrn3()
Inputs      :  None
Processing  :  This function displays Alert screen
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::displayScrn3(void)
{
	drawTriangle(120,10,240,190,10,190,ILI9340_RED);
	drawString("!",110,50,4,ILI9340_RED);
	drawString("ALERT",90,90,3,ILI9340_RED);
	drawString("FEVER",75,130,4,ILI9340_RED);
}
/*----------------------------------------------------------------------------
Function    :  displayScrn3()
Inputs      :  None
Processing  :  This function displays sensor screen
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::displayScrn1(void)
{


	drawFastVLine(5,0,200,ILI9340_WHITE);
	drawFastVLine(142,0,200,ILI9340_WHITE);
	drawFastVLine(235,0,200,ILI9340_WHITE);

	drawFastHLine(5,0,231,ILI9340_WHITE);
	drawFastHLine(5,50,231,ILI9340_WHITE);
	drawFastHLine(5,100,231,ILI9340_WHITE);
	drawFastHLine(5,150,231,ILI9340_WHITE);
	drawFastHLine(5,200,231,ILI9340_WHITE);
	drawString("Heart Rate",12,20,2,ILI9340_YELLOW);
	drawString("Blood Oxygen",12,70,2,ILI9340_YELLOW);
	drawString("Body Temp",12,120,2,ILI9340_YELLOW);
	drawString("step count",12,170,2,ILI9340_YELLOW);

	// Is there any change in BPS?
	if(xSemaphoreTake(BS_REFRESH,50))
	{
		itoa(BS,buff,10);
		clearBS();
		drawString(buff,148,20,2,ILI9340_YELLOW);
	}

	// Is there any change in Oxygen?
	if(xSemaphoreTake(O2_REFRESH,50))
	{
		itoa(OX,buff,10);
		clearOX();
		drawString(buff,148,70,2,ILI9340_YELLOW);
	}

	// Is there any change in Body Temp?
	if(xSemaphoreTake(BT_REFRESH,50))
	{
		itoa(BT,buff,10);
		clearBT();
		drawString(buff,148,120,2,ILI9340_YELLOW);
	}
	// Is there any change in Steps?
	if(xSemaphoreTake(ST_REFRESH,50))
	{
		itoa(ST,buff,10);
		clearST();
		drawString(buff,148,170,2,ILI9340_YELLOW);
	}
}
/*----------------------------------------------------------------------------
Function    :  displayScrn2()
Inputs      :  None
Processing  :  This function displays clock screen
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::displayScrn2(void)
{

	switch(event)
	{
	case YEAR_EVENT:  clearScrn2();
					  break;
	case MONTH_EVENT: clearmonth();
					  break;
	case DAY_EVENT:   clearday();
			          break;
	case HOUR_EVENT:  clearhour();
			          break;
	case MINUTE_EVENT:clearminute();
			          break;
	default: clearScrn();
	}

	// Display Hours and Minutes
	drawString(trim(rtc_get_date_time_str(),12,17),40,70,6,ILI9340_CYAN);
	// Display The Day and Month
	drawString(trim(rtc_get_date_time_str(),4,12),10,130,3,ILI9340_GREEN);

	drawString(",",120,130,3,ILI9340_GREEN);
	// Display the year
	drawString(trim(rtc_get_date_time_str(),21,25),150,130,3,ILI9340_MAGENTA);
}

/*----------------------------------------------------------------------------
Function    :  minute_check()
Inputs      :  None
Processing  :  This function checks whether a minute, hour, day or year
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::minute_check ()
{
	// Has minute expired?
	if(minute!=rtc_getmin())
	{

		// Has an year expired?
		if(year!=rtc_getyear())
		{
			year  = rtc_getyear();
			month = rtc_getmonth();
			day   = rtc_getday();
			hour  = rtc_gethour();
			minute = rtc_getmin();
		    xSemaphoreGive(triggerOneYear);
		}
		// Has a month expired?
		else if(month!=rtc_getmonth())
		{
			month = rtc_getmonth();
			day = 	rtc_getday();
			hour = 	rtc_gethour();
			minute = rtc_getmin();
		    xSemaphoreGive(triggerOneMonth);
		}
		// Has a day expired?
		else if(day!=rtc_getday())
		{
			day = 	rtc_getday();
			hour = 	rtc_gethour();
			minute = rtc_getmin();
			xSemaphoreGive(triggerOneDay);
		}
		// Has an hour expired?
		else if(hour!=rtc_gethour())
		{
			hour = 	rtc_gethour();
			minute = rtc_getmin();
			xSemaphoreGive(triggerOneHour);
		}
		// Has a minute expired?
		else
		{
			minute = rtc_getmin();
			xSemaphoreGive(triggerOneMin);
		}
	}
}
/*----------------------------------------------------------------------------
Function    :  Update_timer()
Inputs      :  None
Processing  :  This function updates software timers, de-bounce timers
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void Update_timer()
{
 	 // Timer count to increment 1 sec timer
 	 static uint16_t timer_count = ZERO;
     // Timer count to increment de-bounce timer
 	 static uint16_t debounce_count = ZERO;
 	 // Flag to set increment for debounce
 	 static volatile bool increment_debounce = ZERO;
 	 // Timer count to increment 1 minute timer
 	 static uint16_t  screen_timeout = ZERO;
 	 // Flag to set increment
 	 static volatile bool increment = ZERO;
 	 // Increment Timer count
 	 timer_count++;
 	 // Is it one second? Then we need to check if clock has changed.
	 if(xSemaphoreTake(senor_button,0))
	 {
		 increment_debounce = 1;
	 }
	 // Button is pressed increment
	 if(increment_debounce == 1)
	 {
		 debounce_count++;
	 }
	 // Its 300ms check the value of button again
	 if(debounce_count >=3)
	 {
		 if(GPIOGetValue(2,5)== 1)
		 {
			 xSemaphoreGive(sensor_debounce);
		 }
		 debounce_count = 0;
		 increment_debounce = 0;
	 }
	 // Second has expired signal display to update
 	 if(timer_count == SEC)
 	 {
 		 xSemaphoreGive(triggerOneSecond);
 		 timer_count = ZERO;
 	 }
 	 // Start 30 Seconds time out for sensor screen
 	 if(xSemaphoreTake(Timer_start,ZERO))
 	 {
 		 if(screen_timeout)
 		 {
 			screen_timeout = ZERO;
 		 }
 		increment = 1;
 	 }
 	 // if flag is der upate the timeout
 	 if(increment)
 	screen_timeout++;

 	// Timed out? Change screen back to Clock Screen
 	if(screen_timeout >= THIRTY_SEC)
 	{
			increment = ZERO;
			screen_timeout = ZERO;
			xSemaphoreGive(Timer_increment);
 	}

}
/*----------------------------------------------------------------------------
Function    :  Timer3_100ms_init()
Inputs      :  None
Processing  :  This function updates software timers, de-bounce timers
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void Timer3_100ms_init(void)
{
	 	 const lpc_timer_t hundred_ms_timer_source = (lpc_timer_t) Hundered_ms_timer;
	 	 // set timer resolution for 100 micro-second
	     const uint32_t Thousand_micro_second = 1000;
	     // Initialize the timer structure pointer
	     Hundred_millisec_timer_ptr = lpc_timer_get_struct(hundred_ms_timer_source);
	     // enable the timer for a precision of one milli second to increment TC count
	     lpc_timer_enable(hundred_ms_timer_source, Thousand_micro_second);
	     // Stop on Interrupt , No auto re-start
	     Hundred_millisec_timer_ptr->MCR = 0b11;
	     //  timeout is 100msec
	     Hundred_millisec_timer_ptr->MR0 = 100;
	     // enable LPC timer
	     lpc_timer_enable(hundred_ms_timer_source, Thousand_micro_second);
	     // Get the IRQ number for timer interrupt
	     const IRQn_Type timer_irq = lpc_timer_get_irq_num(hundred_ms_timer_source);
	     // Enable timer interrupt
	     NVIC_EnableIRQ(timer_irq);
}
/*----------------------------------------------------------------------------
Function    :  TIMER3_IRQHandler()
Inputs      :  None
Processing  :  This function is ISR for timer 3 interrupt, it calls update timers
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
extern "C"
{
	void TIMER3_IRQHandler()
	{
		// clear the interrupt
		Hundred_millisec_timer_ptr->IR =0b1;
		// update timers
		Update_timer();
	}
}

/*----------------------------------------------------------------------------
Function    :  UART3_init()
Inputs      :  None
Processing  :  This function Initializes UART3
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task :: UART3_init(void)
{
	 // Set UART3 Baudrate to 9600
	 uart_3.init(9600);
}

/*----------------------------------------------------------------------------
Function    :  UART3_init()
Inputs      :  None
Processing  :  This function Powers on SSP0 interface
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::SSP0_power(uint8_t mode)
{
	if(mode)
	{
		LPC_SC->PCONP |= (mode<<21);
	}
	else
	{
		LPC_SC->PCONP &= ~(mode<<21);
	}
}
/*----------------------------------------------------------------------------
Function    :  SSP0_byte_transfer()
Inputs      :  uint8_t send_byte
Processing  :  This function Powers on SSP0 interface
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
uint8_t display_Task::SSP0_byte_transfer (uint8_t send_byte)
{

	while (SSP0_TXfull());
    LPC_SSP0->DR = send_byte;

    while(SSP0_busy());

    return LPC_SSP0->DR;

}
/*----------------------------------------------------------------------------
Function    :  SSP0_enable()
Inputs      :  None
Processing  :  This function Enables SSP0 interface
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::SSP0_enable  (void)
{
	LPC_SSP0->CR1 |=   (0b1   << 1);
}
/*----------------------------------------------------------------------------
Function    :  SSP0_disable()
Inputs      :  None
Processing  :  This function Disables SSP0 interface
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::SSP0_disable (void)
{
	LPC_SSP0->CR1 &=  ~(0b1   << 1);
}
/*----------------------------------------------------------------------------
Function    :  SSP0_TXfull()
Inputs      :  None
Processing  :  This function checks whether the transmit queue is full?
Outputs     :  None
Returns     :  True, if busy, False if not full
Notes       :  None
----------------------------------------------------------------------------*/
bool display_Task::SSP0_TXfull(void)
{
	uint32_t mask = (0b1<<1);
	bool return_val = !(LPC_SSP0->SR & mask);
	return (return_val);
}
/*----------------------------------------------------------------------------
Function    :  SSP0_busy()
Inputs      :  None
Processing  :  This function checks whether the SSP interface is busy
Outputs     :  None
Returns     :  True, if busy, False if not full
Notes       :  None
----------------------------------------------------------------------------*/
bool display_Task::SSP0_busy(void)
{
	return (((LPC_SSP0->SR ) & (0b01 << 4)));
}
/*----------------------------------------------------------------------------
Function    :  display_CS_assert()
Inputs      :  None
Processing  :  This function asserts CS
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::display_CS_assert(void)
{
	GPIOSetValue(0,29,HIGH);
}
/*----------------------------------------------------------------------------
Function    :  display_CS_dessert()
Inputs      :  None
Processing  :  This function desserts CS
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::display_CS_dessert(void)
{
	GPIOSetValue(0,29,LOW);
}
/*----------------------------------------------------------------------------
Function    :  display_RST_assert()
Inputs      :  None
Processing  :  This function asserts RST
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::display_RST_assert(void)
{
	GPIOSetValue(0,30,HIGH);
}
/*----------------------------------------------------------------------------
Function    :  display_RST_dessert()
Inputs      :  None
Processing  :  This function desserts RST
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::display_RST_dessert(void)
{
	GPIOSetValue(0,30,LOW);
}
/*----------------------------------------------------------------------------
Function    :  display_DC_assert()
Inputs      :  None
Processing  :  This function makes D/C High
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::display_DC_assert(void)
{
	GPIOSetValue(1,19,HIGH);
}
/*----------------------------------------------------------------------------
Function    :  display_DC_dessert()
Inputs      :  None
Processing  :  This function makes D/C low
Outputs     :  None
Returns     :  None
Notes       :  None
----------------------------------------------------------------------------*/
void display_Task::display_DC_dessert(void)
{
	GPIOSetValue(1,19,LOW);
}

/*----------------------------------------------------------------------------
Function    :  reverse()
Inputs      :  char str[], int length
Processing  :  A utility function to reverse a string
Outputs     :  None
Returns     :  None
Notes       :  code sample taken from http://www.geeksforgeeks.org
----------------------------------------------------------------------------*/
 void reverse(char str[], int length)
 {
     int start = 0;
     int end = length -1;
     while (start < end)
     {
         swap(*(str+start), *(str+end));
         start++;
         end--;
     }
 }
 /*----------------------------------------------------------------------------
 Function    :  itoa()
 Inputs      :  int num, char* str, int base
 Processing  :  A utility function to covert integer into ascii
 Outputs     :  None
 Returns     :  char*
 Notes       :  code sample taken from http://www.geeksforgeeks.org
 ----------------------------------------------------------------------------*/
 char* itoa(int num, char* str, int base)
 {
     int i = 0;
     bool isNegative = false;

     /* Handle 0 explicitely, otherwise empty string is printed for 0 */
     if (num == 0)
     {
         str[i++] = '0';
         str[i] = '\0';
         return str;
     }

     // In standard itoa(), negative numbers are handled only with
     // base 10. Otherwise numbers are considered unsigned.
     if (num < 0 && base == 10)
     {
         isNegative = true;
         num = -num;
     }

     // Process individual digits
     while (num != 0)
     {
         int rem = num % base;
         str[i++] = (rem > 9)? (rem-10) + 'a' : rem + '0';
         num = num/base;
     }

     // If number is negative, append '-'
     if (isNegative)
         str[i++] = '-';

     // Append string terminator
     str[i] = '\0';

     // Reverse the string
     reverse(str, i);

     return str;
 }

 /****************************************************************************/
 /*                      ADA FRUIT GFX Library for ILI9430                   */
 /****************************************************************************/

void display_Task::setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{

  writecommand(ILI9340_CASET); // Column addr set
  writedata(x0 >> 8);
  writedata(x0 & 0xFF);     // XSTART
  writedata(x1 >> 8);
  writedata(x1 & 0xFF);     // XEND

  writecommand(ILI9340_PASET); // Row addr set
  writedata(y0>>8);
  writedata(y0);     // YSTART
  writedata(y1>>8);
  writedata(y1);     // YEND

  writecommand(ILI9340_RAMWR); // write to RAM
}

void display_Task::drawPixel(int16_t x, int16_t y, uint16_t color)
{

	  if((x < 0) ||(x >= 240) || (y < 0) || (y >= 320)) return;

	  setAddrWindow(x,y,x+1,y+1);

	  //digitalWrite(_dc, HIGH);
	  display_DC_assert();
	  //digitalWrite(_cs, LOW);
	  display_CS_dessert();

	  SSP0_byte_transfer(color >> 8);
	  SSP0_byte_transfer(color);

	  //digitalWrite(_cs, HIGH);
	  display_CS_assert();
	//  printf("pixel executed \n");
}

int display_Task::drawChar(int16_t x, int16_t y, unsigned char c,
		  uint16_t color, uint16_t bg, uint8_t size)
{

//		    if(!gfxFont) { // 'Classic' built-in font

		        if((x >= _width)            || // Clip right
		           (y >= _height)           || // Clip bottom
		           ((x + 6 * size - 1) < 0) || // Clip left
		           ((y + 8 * size - 1) < 0))   // Clip top
		            return 1;

		        if((c >= 176)) c++; // Handle 'classic' charset behavior

		    //    startWrite();
		        for(int8_t i=0; i<5; i++ ) { // Char bitmap = 5 columns
		            uint8_t line = pgm_read_byte(&font[c * 5 + i]);
		            for(int8_t j=0; j<8; j++, line >>= 1) {
		                if(line & 1) {
		                    if(size == 1)
		                    	drawPixel(x+i, y+j, color);
		                    else
		                        writeFillRect(x+i*size, y+j*size, size, size, color);
		                } else if(bg != color) {
		                    if(size == 1)
		                    	drawPixel(x+i, y+j, bg);
		                    else
		                        writeFillRect(x+i*size, y+j*size, size, size, bg);
		                }
		            }
		        }
		        if(bg != color) { // If opaque, draw vertical line for last column
		            if(size == 1) writeFastVLine(x+5, y, 8, bg);
		            else          writeFillRect(x+5*size, y, size, 8*size, bg);
		        }

		        return((5.0+0.4)*size);
		  //      endWrite();
}

void display_Task::writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{

    // Overwrite in subclasses if desired!
    fillRect(x,y,w,h,color);

}


void display_Task::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    for (int16_t i=x; i<x+w; i++) {
        writeFastVLine(i, y, h, color);
    }

}


void display_Task::writeFastVLine(int16_t x, int16_t y,
        int16_t h, uint16_t color)
{
	drawFastVLine(x, y, h, color);

}

void display_Task::drawFastVLine(int16_t x, int16_t y,
        int16_t h, uint16_t color)
{
	writeLine(x, y, x, y+h-1, color);
}

void display_Task::drawFastHLine(int16_t x, int16_t y,
        int16_t h, uint16_t color)
{
	writeLine(x, y, x+h-1, y, color);
}

void display_Task::writeLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
        uint16_t color) {
    int16_t steep = abs(y1 - y0) > abs(x1 - x0);
    if (steep) {
        _swap_int16_t(x0, y0);
        _swap_int16_t(x1, y1);
    }

    if (x0 > x1) {
        _swap_int16_t(x0, x1);
        _swap_int16_t(y0, y1);
    }

    int16_t dx, dy;
    dx = x1 - x0;
    dy = abs(y1 - y0);

    int16_t err = dx / 2;
    int16_t ystep;

    if (y0 < y1) {
        ystep = 1;
    } else {
        ystep = -1;
    }

    for (; x0<=x1; x0++) {
        if (steep) {
        	drawPixel(y0, x0, color);
        } else {
        	drawPixel(x0, y0, color);
        }
        err -= dy;
        if (err < 0) {
            y0 += ystep;
            err += dx;
        }
    }
}


void display_Task::drawString(const char *string, int poX, int poY, int size,uint16_t color)
{
    int sumX = 0;

    while(*string)
    {
        int xPlus = drawChar(poX, poY, *string,color, ILI9340_BLACK,  size);
        sumX += xPlus;
        *string++;
        poX += xPlus;
    }

}
void  display_Task::writecommand(uint8_t command_byte)
{
	// DC- low
	display_DC_dessert();
	//CS- low
	display_CS_dessert();
	// SSP exchange
    SSP0_byte_transfer(command_byte);
	//CS- HIGH
	display_CS_assert();

}
void  display_Task::writedata(uint8_t data_byte)
{

	// DC- HIGH
	display_DC_assert();
	//CS- low
	display_CS_dessert();
	// SSP exchange
	SSP0_byte_transfer(data_byte);
	//CS- HIGH
	display_CS_assert();

}

char * trim(char * str,uint8_t start, uint8_t end)
{

		int j = 0;
		str = str+start-1;

		for(int k=0;k<20;k++){buff[k]=0;}
		while(start!=end)
		{
			buff[j] = *str;
			str++;
			j++;
			start++;

		}
		buff[j]='\0';

		return &buff[0];

}

void display_Task::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
        uint16_t color)
{
    // Update in subclasses if desired!
    if(x0 == x1){
        if(y0 > y1) _swap_int16_t(y0, y1);
        drawFastVLine(x0, y0, y1 - y0 + 1, color);
    } else if(y0 == y1){
        if(x0 > x1) _swap_int16_t(x0, x1);
        drawFastHLine(x0, y0, x1 - x0 + 1, color);
    } else {

        writeLine(x0, y0, x1, y1, color);

    }
}
void display_Task::drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color)
{
    drawLine(x0, y0, x1, y1, color);
    drawLine(x1, y1, x2, y2, color);
    drawLine(x2, y2, x0, y0, color);
}

/*===================================================================
// $Log: $1.0 AVD:Added comments to increase the readability
//
//--------------------------------------------------------------------*/

