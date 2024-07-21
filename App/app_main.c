// Program Description: This program controls a flood guard system using an STM32 microcontroller.

// Including necessary libraries
#include "main.h"                    // Include main header file
#include <stdio.h>                   // Include standard input/output library
#include <string.h>                  // Include string manipulation library
#include <stdbool.h>                 // Include boolean data type

// External peripheral handlers declaration
extern ADC_HandleTypeDef hadc;       // Declare ADC handler
extern TIM_HandleTypeDef htim2;      // Declare Timer 2 handler
extern UART_HandleTypeDef huart2;    // Declare UART handler
extern RTC_HandleTypeDef hrtc;       // Declare RTC handler
extern TIM_HandleTypeDef htim21;     // Declare Timer 21 handler
RTC_AlarmTypeDef sAlarm;             // Declare RTC Alarm structure

// Global variable declaration
char message[40];                     // Buffer to store messages

uint8_t wupFlag = 1;                  // Initialize wake-up flag
uint8_t rtcFlag = 0;				  // Initialize wake-up flag
static uint8_t Low_battery;           // Initialize low battery flag

volatile static uint8_t valve_open;   // Initialize valve open flag
volatile static uint8_t floodFlag = 0;// Initialize flood flag
volatile static uint8_t buttonState = 0; // Initialize button state
volatile static uint32_t holdTime = 0; // Initialize button hold time
volatile static uint32_t releaseTime = 0; // Initialize button release time
volatile static uint32_t pressDuration = 0; // Initialize button press duration

static uint32_t alert_time = 0;       // Initialize alert time
static uint32_t sleep_time = 0;       // Initialize sleep time

// Function prototypes
void openValve(void);                 // Function prototype for opening the valve
void closeValve(void);                // Function prototype for closing the valve
void alert(void);                     // Function prototype for activating the buzzer and warning LED
void resetFloodEvent(void);           // Function prototype for resetting the flood event
uint16_t measureBattery(void);        // Function prototype for measuring battery voltage
void monitorBattery(void);            // Function prototype for monitoring battery voltage
void statusled(void);                 // Function prototype for system status LED
void RTC_AlarmConfig(void);
void batteryled(void);				  // Function prototype for activating battery LED
void batteryAlarm(void);              // Function prototype for activating battery Alarm
void console(char *log);              // Function prototype for transmitting messages via UART

// Main application function
void app_main(void)
{
    // Initialize message buffer with default message
    strcpy(message, "EFloodGuardLP(v3.3)\r\n");
    // Send initialization message
    console(message);

    // Check if the flood flag is set
    if(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9) == GPIO_PIN_SET)
    {
        floodFlag = 0;
        HAL_Delay(100);
        openValve();
    }
    else
    {
        floodFlag = 1;
        HAL_Delay(100);
        closeValve();
    }
    alert();
    // Main loop
    while(1)
    {
        // Get current time
        uint32_t now = HAL_GetTick();

        // Test Mode activated by long pressing the button
        if(pressDuration >= 2000 && !floodFlag)
        {
            strcpy(message, "Test Mode\r\n");
            console(message);
            pressDuration = 0;
            statusled();
            closeValve();
            alert();
            HAL_Delay(500);
            statusled();
            openValve();
        }
        // Servicing the short button press during a flood event
        else if(floodFlag && pressDuration >= 1000)
        {
            strcpy(message, "Reset\r\n");
            console(message);
            pressDuration = 0;
            resetFloodEvent();
        }
        // Close the valve if the flood flag is set
        if (floodFlag)
        {
            if(now - alert_time > 5000)
            {
                alert_time = now;
                strcpy(message, "Flood\r\n");
                console(message);
                alert();
            }
            if(valve_open == 1)
            {
                strcpy(message, "Closing Valve\r\n");
                console(message);
                closeValve();
                strcpy(message, "Valve closed\r\n");
                console(message);
            }
        }
        if(rtcFlag)
        {
        	strcpy(message, "RTC Event\r\n");
        	console(message);
        	rtcFlag = 0;
        }
        if((now - sleep_time >= 5000) && !floodFlag && wupFlag)
        {
            monitorBattery();
            wupFlag = 0;
            strcpy(message, "Entering Sleep\r\n");
            console(message);
            HAL_SuspendTick(); //
            HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI); // Enable Stop mode
            strcpy(message, "After Sleep\r\n");
            console(message);
        }
    }
}

// Callback function for rising edge interrupt on GPIO EXTI line
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    SystemClock_Config();
    HAL_ResumeTick();
    sleep_time = HAL_GetTick();
    wupFlag = 1;
    if(GPIO_Pin == GPIO_PIN_15)
    {
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15) == GPIO_PIN_SET) // Rising edge
        {
            if (buttonState == 1)
            {
                releaseTime = HAL_GetTick();
                pressDuration = releaseTime - holdTime;
            }
            buttonState = 0;
        }
        else if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15) == GPIO_PIN_RESET) // Falling edge
        {
            buttonState = 1;
            holdTime = HAL_GetTick(); // Record button hold time
        }
    }
    else if(GPIO_Pin == GPIO_PIN_9)
    {
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_Pin) == GPIO_PIN_RESET) // Falling edge
        {
            HAL_TIM_Base_Start_IT(&htim21);
        }
    }
}

// Callback function for RTC Alarm A event
void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc)
{
    SystemClock_Config();
    HAL_ResumeTick();
    sleep_time = HAL_GetTick();
    wupFlag = 1;
    rtcFlag = 1;
}

// Callback function for TIM21 period elapsed interrupt
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if(htim == &htim21)
    {
        if(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9) == GPIO_PIN_RESET)
        {
            floodFlag = 1; // Set flood flag
        }
        HAL_TIM_Base_Stop_IT(&htim21);
    }
}

// Function to open the valve
void openValve(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_SET); // Activate valve
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1); // Start PWM signal for valve control
    HAL_Delay(50);
    for(uint16_t i = 1800; i >= 900; i -= 50)
    {
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, i); // Set PWM duty cycle for valve opening
        HAL_Delay(30);
    }

    HAL_Delay(50);
    HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1); // Stop PWM signal
    HAL_Delay(50);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET); // Deactivate valve
    valve_open = 1;
}

// Function to close the valve
void closeValve(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_SET); // Activate valve
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1); // Start PWM signal for valve control
    HAL_Delay(50);
    for(uint16_t i = 900; i <= 1800; i += 50)
    {
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, i); // Set PWM duty cycle for valve closing
        HAL_Delay(30);
    }
    HAL_Delay(50);
    HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1); // Stop PWM signal
    HAL_Delay(50);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET); // Deactivate valve
    valve_open = 0;
}

// Function to reset flood event
void resetFloodEvent(void)
{
    // Check if the button is pressed and the valve is open
    if ((HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15) == GPIO_PIN_SET) && HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9) == GPIO_PIN_SET)
    {
        if(valve_open == 0)
        {
            openValve(); // Open the valve
        }
        strcpy(message, "Valve open\r\n");
        console(message);
        floodFlag = 0; // Clear the flood flag
    }
}

// Function to measure battery voltage
uint16_t measureBattery(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_SET); // Enable battery voltage measurement
    HAL_ADC_Start(&hadc); // Start ADC conversion
    HAL_ADC_PollForConversion(&hadc, HAL_MAX_DELAY); // Wait for ADC conversion to complete
    uint16_t analogbatt = HAL_ADC_GetValue(&hadc); // Read ADC value
    HAL_Delay(5);
    HAL_ADC_Stop(&hadc); // Stop ADC conversion
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_RESET); // Disable battery voltage measurement

    // Check battery voltage threshold
    if(analogbatt < 2950 && analogbatt >= 2800)
    {
        Low_battery = 1; // Set low battery flag if voltage is below threshold
    }
    else if(analogbatt < 2800)
    {
        Low_battery = 2; // Set low battery flag flag if voltage is below critical threshold
    }
    else
    {
    	Low_battery = 0;
    }
    return analogbatt; // Return battery voltage reading
}

// Function to monitor battery voltage
void monitorBattery(void)
{
	uint16_t vBatt = measureBattery(); // Measure battery voltage
    if(Low_battery == 1)
    {
    	batteryled();
    	RTC_AlarmConfig();
    }
    else if(Low_battery == 2)
    {
    	RTC_AlarmConfig();
    	closeValve();	// Close Valve if Critically low Battery
    	batteryAlarm(); // Activate battery Alarm if critically low battery
    }
    sprintf(message, "Battery Voltage: %d\r\n", vBatt); // Format battery voltage message
    console(message); // Send battery voltage message via UART
}

// Function to control status LED
void statusled(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET); // Activate status LED
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET); // Activate status LED
    HAL_Delay(100);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET); // Deactivate status LED
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET); // Deactivate status LED
}

// Function to activate battery led
void batteryled(void)
{
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET); // Activate battery LED
	HAL_Delay(200); // Delay for LED indication
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET); // Deactivate battery LED
}

// Function to activate battery Alarm
void batteryAlarm(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET); // Activate battery LED
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET); // Activate battery LED
    HAL_Delay(200); // Delay for LED indication
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET); // Deactivate battery LED
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET); // Deactivate battery LED
}

void RTC_AlarmConfig(void)
{
	HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_A);
	sAlarm.AlarmTime.Hours = 0x0;
	sAlarm.AlarmTime.Minutes = 0x0;
	sAlarm.AlarmTime.Seconds = 0x0;
	sAlarm.AlarmTime.SubSeconds = 0x0;
	sAlarm.AlarmTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
	sAlarm.AlarmTime.StoreOperation = RTC_STOREOPERATION_RESET;
	sAlarm.AlarmMask = RTC_ALARMMASK_DATEWEEKDAY|RTC_ALARMMASK_HOURS
			|RTC_ALARMMASK_MINUTES;
	sAlarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
	sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
	sAlarm.AlarmDateWeekDay = 0x1;
	sAlarm.Alarm = RTC_ALARM_A;
	if (HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BCD) != HAL_OK)
	{
		Error_Handler();
	}

}
// Function to activate buzzer and warning LED
void alert(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET); // Activate buzzer
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET); // Activate warning LED
    HAL_Delay(1000); // Delay for alert indication
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET); // Deactivate buzzer
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET); // Deactivate warning LED
}
// Function to transmit messages via UART
void console(char *log)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)log, strlen(log), HAL_MAX_DELAY); // Transmit message via UART
    HAL_Delay(10);
    memset(log, '\0', strlen(log)); // Clear message buffer
}
