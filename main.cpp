#include "mbed.h"
#include "LCD_DISCO_F429ZI.h"
#include "TS_DISCO_F429ZI.h"

#include "events/EventQueue.h"  // Include the event queue library
using namespace events;         // Use the event queue namespace

// Create an LCD object to control the LCD display
LCD_DISCO_F429ZI LCD;
// Create a touchscreen object to capture touch input
TS_DISCO_F429ZI ts;
// Create a Ticker timer object for periodic tasks
Ticker ThermoTicker;
// Create a Timeout object for one-shot delayed tasks (e.g., debouncing)
Timeout timeout;
// Create an EventQueue object to safely execute code in the main thread (avoiding calling mutex-related functions in interrupts)
EventQueue queue; 

enum AppState{
    IDLE,
    FAN_ACCEL,
    FAN_MAX,
};
volatile AppState state = IDLE;   // Initialize to IDLE state

// Define two digital output LEDs for button press feedback
DigitalOut led1(PG_13);
DigitalOut led2(PG_14);

// Define an AnalogIn object to read the LM35 temperature sensor value (connected to PA_0)
AnalogIn lm35(PA_0);

// Define a PWM output object for controlling the fan speed (assumed connected to PD_14)
PwmOut fan(PD_14);

// Create an additional Timeout object to schedule the fan acceleration function
Timeout fanAccelTimeout;

// Read the LM35's analog value, convert it to temperature (dividing by 0.01 V is equivalent to multiplying by 100),
// and store it in the global variable 'temp'
volatile double temp = lm35.read() / 0.01;

// Set the initial threshold as the rounded current temperature plus 1°C
double threshold = round(temp) + 1.0;

// Flag used to indicate whether input is allowed (to prevent continuous triggering)
bool canInput = true;

// Function to read the thermometer: update the global variable 'temp' (unit: °C)
void readThermometer() {
    temp = lm35.read() / 0.01; // Read the LM35's analog value and convert it to temperature
}

// Function to toggle input allowance: set 'canInput' to true, allowing subsequent input
void ToggleInput()
{
    canInput = true;
}

// Function to update the temperature display: schedule the temperature reading via the event queue in the main thread, and clear the screen
void updateTemp()
{
    queue.call(readThermometer); // Queue the temperature reading operation to be executed by the main thread
    LCD.Clear(LCD_COLOR_WHITE);  // Clear the LCD screen with a white background
}

// Fan acceleration function: increment the PWM duty cycle at fixed intervals for smooth acceleration
void fanAccel() {
    // Only perform acceleration if the state is still FAN_ACCEL
    if(state == FAN_ACCEL) {
        double duty = fan.read(); // Get the current PWM duty cycle
        // If the duty cycle is below 1.0, then increase it
        if(duty < 1.0) {
            duty += 0.05;         // Increase by 5% each time
            if(duty > 1.0) {       // Prevent exceeding the maximum value
                duty = 1.0;
            }
            fan.write(duty);       // Write the new PWM duty cycle
            // Schedule the next acceleration using a Timeout timer with an interval of 100ms
            fanAccelTimeout.attach(&fanAccel, 100ms);
            led1 = 1;
            led2 = 0;
        } else {
            // Once the maximum duty cycle is reached, switch to the FAN_MAX state
            state = FAN_MAX;
            led1 = 0;
            led2 = 1;
        }
    }
}

// main() function, the entry point of the program, running in the main thread
int main()
{
    // Start the Ticker timer to call updateTemp() every 500ms, used to periodically update the temperature display
    ThermoTicker.attach(&updateTemp, 500ms);
    // Create a thread dedicated to processing tasks in the event queue
    Thread eventThread;
    eventThread.start(callback(&queue, &EventQueue::dispatch_forever));
    
    // Set the LCD font and text color
    LCD.SetFont(&Font20);
    LCD.SetTextColor(LCD_COLOR_BLUE);

    // Define a touchscreen state structure to store touch information
    TS_StateTypeDef TS_State;
    // Initialize the touchscreen with the LCD's X and Y dimensions
    ts.Init(LCD.GetXSize(), LCD.GetYSize());

    // Main loop: continuously update the display and process touch input
    while (1) {

        uint8_t sensorText[20]; // Define a buffer to store the sensor temperature string
        // Split the temperature into its integer and fractional parts for formatting
        int tempInt = (int)temp; // Get the integer part of the temperature
        int tempDecimal = (int)(fabs((temp - tempInt) * 10)); // Get the fractional part (one decimal place)

        // If the temperature is negative and the integer part is 0, manually add a minus sign
        if (temp < 0 && tempInt == 0)
        {
            sprintf((char *)sensorText, "Sensor: -%d.%01dC", abs(tempInt), tempDecimal);
        } else
        {
            sprintf((char *)sensorText, "Sensor: %d.%01dC", tempInt, tempDecimal);
        }
        // Display the sensor temperature on the LCD at coordinates (0, 60)
        LCD.DisplayStringAt(0, 60, (uint8_t *)&sensorText, LEFT_MODE);

        uint8_t threshText[20]; // Define a buffer to store the threshold string
        int threshInt = (int)threshold; // Get the integer part of the threshold
        int threshDecimal = (int)(fabs((threshold - threshInt) * 10)); // Get the fractional part (one decimal place)

        // Format the threshold string, handling negative values similarly
        if (threshold < 0 && threshInt == 0)
        {
            sprintf((char *)threshText, "Thresh: -%d.%01dC", abs(threshInt), threshDecimal);
        } else
        {
            sprintf((char *)threshText, "Thresh: %d.%01dC", threshInt, threshDecimal);
        }
        // Display the threshold information on the LCD at coordinates (0, 80)
        LCD.DisplayStringAt(0, 80, (uint8_t *)&threshText, LEFT_MODE);

        // Get the current touchscreen state information
        ts.GetState(&TS_State);
        // If touch is detected and input is allowed
        if ((TS_State.TouchDetected) && canInput)
        {
            // If the touch position is within the "+" button area (coordinate check)
            if ((TS_State.X > 20) && (TS_State.X < 80) &&
            (TS_State.Y < 125) && (TS_State.Y > 65))
            {
                led1 = 1;          // Turn on LED1 as button press feedback
                threshold += 0.5;  // Increase the threshold by 0.5°C
                LCD.FillRect(20, 190, 60, 65); // Fill a rectangle on the LCD to show the button pressed effect
                canInput = false;  // Disable input to prevent continuous triggering
                timeout.attach(&ToggleInput, 500ms); // Restore input allowance after 500ms
            }
            // If the touch position is within the "-" button area (coordinate check)
            else if ((TS_State.X > LCD.GetXSize() - 80) && (TS_State.X < LCD.GetXSize() - 20) &&
            (TS_State.Y < 125) && (TS_State.Y > 65))
            {
                led2 = 1;          // Turn on LED2 as button press feedback
                threshold -= 0.5;  // Decrease the threshold by 0.5°C
                LCD.FillRect(LCD.GetXSize() - 80, 190, 60, 65); // Fill a rectangle on the LCD to show the button pressed effect
                canInput = false;  // Disable input
                timeout.attach(&ToggleInput, 500ms); // Restore input allowance after 500ms
            }
        } 
        else
        {
            // If no touch is detected or input is not allowed, turn off the LEDs
            led1 = 0;
            led2 = 0;

            // Draw the outline of the "+" button
            LCD.FillRect(20, 220, 60, 10);  // Draw a horizontal line
            LCD.FillRect(45, 195, 10, 60);  // Draw a vertical line

            // Draw the outline of the "-" button
            LCD.FillRect(LCD.GetXSize() - 80, 220, 60, 10);  // Draw a horizontal line
        }
        switch(state) {
            case IDLE:
                // Ensure that the fan is off in the IDLE state
                fan.write(0.0);
                led1 = led2 = 0;
                // When the temperature exceeds the threshold, enter the FAN_ACCEL state
                if(temp > threshold) {
                    state = FAN_ACCEL;
                    fan.write(0.1);                // Set the initial duty cycle to 0.1
                    // Use a Timeout timer to schedule the fanAccel() function, ensuring that the PWM duty cycle increases at fixed intervals
                    fanAccelTimeout.attach(&fanAccel, 100ms);
                }
                break;
            case FAN_ACCEL:
                // If the temperature drops to or below the threshold, stop accelerating, turn off the fan, and return to the IDLE state
                if(temp <= threshold) {
                    fanAccelTimeout.detach();
                    fan.write(0.0);
                    state = IDLE;
                    // Test: turn off LEDs
                    led1 = led2 = 0;
                }
                // Otherwise, the acceleration process is controlled by the fanAccel() function
                break;
            case FAN_MAX:
                // In the FAN_MAX state, keep the fan running at full speed
                fan.write(1.0);
                // When the temperature drops to or below the threshold, turn off the fan and return to the IDLE state
                if(temp <= threshold) {
                    fan.write(0.0);
                    state = IDLE;
                }
                break;
            default:
                break;
        }        
    }
}
