/*
********************************************************************************************************************************************************
   Wiring:                                                                                                                                              
   -----------------------------------------------------------------------------------------------------------------------------------------------------  
   ESP32    UpSwitch  UpUpSwitch  DownSwitch  DownSwitch    SSD1306   System     System     BME280    SDA Pullup      SCL Pullup      Sys Default Jumper
            (N.O.)    10KResistor   (N.O.)    10KResistor   OLED      RELAY      LED        l2C       4.7K Resistor   4.7K Resistor   10KResistor
   -----------------------------------------------------------------------------------------------------------------------------------------------------
   3V3                   3V3                    3V3         3V3        3V3                  3V3           3V3             3V3         3V3
   GND        GND                   GND                     GND        GND      Cathode     GND                                       GND (If jumper is 
   GPIO 04                                                           Signal     
   GPIO 05                                                                      node                                                                    
   GPIO 15    GPIO 15    GPIO 15                                                
   GPIO 16                          GPIO 16     GPIO 16                         
   GPIO 21                                                  SDA                             SDA           GPIO 21
   GPIO 22                                                  SCL                             SCL                           GPIO 22
   GPIO 23                                                                                                                             GPIO 23
********************************************************************************************************************************************************
*/

//#include <Arduino.h>                // not needed for an ESP32
#include <Wire.h>
#include <EasyButton.h>             // EasyButton library to control switch presses
#include <Wire.h>                   // Arduino library
#include <Adafruit_GFX.h>           // Font library
#include <Adafruit_SSD1306.h>       // SSD1306 library
#include <Adafruit_Sensor.h>        // Adafruit sensor library
#include <Adafruit_BME280.h>        // For BME280 support

#define RELAY_PIN               4   // Define to control the Relay on pin 4 
#define LED_PIN                 5   // Define to control the LED on pin 5
#define SWITCH_UP_PIN           15  // ESP32 GPIO Pin 15 for Up Switch
#define SWITCH_DOWN_PIN         16  // ESP32 GPIO Pin 16 for Down Switch
#define SDA_PIN                 21  // ESP32 GPIO Pin 21 for l2c SDA
#define SCL_PIN                 22  // ESP32 GPIO Pin 22 for l2c SCL 
#define DEFAULT_START_STATE_PIN 23  // Jumper to determine thermostat startup or boot 
#define SCREEN_WIDTH            128 // OLED display width, in pixels
#define SCREEN_HEIGHT           64  // OLED display height, in pixels
#define TIMER_INTERVAL          50  // The Millis() (milliseconds)Function time interval
                                    // Used to control Events after so many ticks or intervals 
#define DEBOUNCE_CNT            4
#define MAX_TEMP                80  // Max setable temperature
#define MIN_TEMP                35  // Min setable temperature
#define DFT_TEMP                45  // Default temp at startup

// Thermostat operational states
#define THERMOSTAT_STATE_ON         0
#define THERMOSTAT_STATE_PRESET_1   1
#define THERMOSTAT_STATE_PRESET_2   2
#define THERMOSTAT_STATE_OFF        3

// Sensor object
Adafruit_BME280 bme;                // BME280 - I2C mode

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Array store: Presets[0] = 40, Min. Temp., Presets[2] = 80, Max. Temp.
int     Presets[] {MIN_TEMP, MAX_TEMP};           

// Thermostat operational state
int     ThermostatState;              

// Variable to hold timer intervals used for increment / decrement Temperature 
// and Blank Display
int     TimerIntervalCounter = 0;     

// Variable to hold the thermostat adjusted temperature - set to 45 on power up 
// after system failure
float   ManOverrideTemp = DFT_TEMP;         

// variable used with an EasyButton function to store Up Switch Pressed event
bool    SwitchUpPressed;              

// variable used with an EasyButton function to store Down Switch Pressed event
bool    SwitchDownPressed;            

// Variable used with an EasyButton function - UpSwitch 
// (True = was press for amount of time  / False = Not press for amount of time)
bool    SwitchUpPressed_for;          

// Variable used with an EasyButton function - DownSwitch 
// (True = was press for amount of time  / False = Not press for amount of time)
bool    SwitchDownPressed_for;        

// Variable to control Display refresh after the screen was cleared 
// (True = was cleared  / False = not cleared)
bool    DisplayBlankFlag;             

// Variable to hold is the relay on or off
bool    RelayState;                  

// The follow variables are a unsigned long because the time, measured in miliseconds,
// will quickly become a bigger number than can be stored in an int.
unsigned long previousMillis = 0;             // Varable will store last time was updated
unsigned long currentMillis;                  // Varable to store the timer now count

EasyButton SwitchUp(SWITCH_UP_PIN);           // Instance of the up switch button.
EasyButton SwitchDown(SWITCH_DOWN_PIN);       // Instance of the down switch button.

void BlankDisplay();
void ManageThermostat();
void IncrementTemp();                         // Increment Temperature
void DecrementTemp();                         // Deccrement Temperature
void ShowRoomTempHumidityPressure();          // Read the BME280 sensor for data

// inline functions
inline void LedOn(void)
{
    digitalWrite(LED_PIN, HIGH);        // Turn LED ON
}
inline void LedOff(void)
{
    digitalWrite(LED_PIN, LOW);         // Turn LED OFF
}

inline void RelayOn(void)
{
    digitalWrite(RELAY_PIN, LOW);       // Turn RELAY ON
    RelayState = true;
}
inline void RelayOff(void)
{
    digitalWrite(RELAY_PIN, HIGH);       // Turn RELAY OFF
    RelayState = false;
}

//*****************************************************************************************
void setup() 
{
    Serial.begin(115200);       // setup Serial Port at 115200 BAUD

    // set the digital pins for inputs and outputs:
    pinMode(SWITCH_UP_PIN, INPUT);    // 10K pullup on INPUT for UP SWITCH_UP_PIN GPIO 15.
    pinMode(SWITCH_DOWN_PIN, INPUT);  // 10K pullup on INPUT for Down UpSwitch GPIO 15. (LOW - true HIGH - False)
    pinMode(SDA_PIN, INPUT_PULLUP);   // ADD a 4.7K pullup on INPUT for I2C - SDA pin
    pinMode(SCL_PIN, INPUT_PULLUP);   // ADD a 4.7K pullup on INPUT for I2C - SCL pin
    pinMode(LED_PIN, OUTPUT);         // Output GPIO Pin 5 - used when sytem is ON (HIGH - On LOW - OFF)
    pinMode(RELAY_PIN, OUTPUT);       // Output GPIO Pin 4 - used to control relay for sytem ON / OFF (HIGH - On LOW - OFF)

    // Display DSS1306 OLED Setup as follows:
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) // Address 0x3D for 128x64 or Address 0x3C for 128x64
    {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;);
    }

    // Setup and print to the SSD1306 OLED display
    display.setTextSize(4);           // Supports text size from 1 to 8
    display.setTextColor(WHITE);      // only option white font with black background
    //display.invertDisplay(true);    // will invert display to black font and white background (true or false)
    display.setCursor(10, 28);        // X = 128 Max., Y = 64 Max.
    display.clearDisplay();           // clear the display
    display.display();                //  call this method for the changes to make effect

    bme.begin(0x76);                  // BME280 address

    // Read the default jumper pin to set the sytem default
    if (digitalRead(DEFAULT_START_STATE_PIN) == LOW) // Thermostat will start up in system OFF mode
    {
        LedOff();                     // Turn LED OFF
        RelayOff();                   // Turn system RELAY OFF
        ThermostatState = THERMOSTAT_STATE_OFF;
    }
    else // ThermostatState will startup ON
    {
        LedOn();                      // Turn LED ON
        RelayOn();                    // Turn system RELAY ON
        ThermostatState = THERMOSTAT_STATE_ON;
    }
}

//*****************************************************************************************
void loop() 
{
    // Put code that needs to be running all the time. Check to see if it's time to Read the switch,
    // Blank the screen or increment the temperature adjust that is, if the difference between the current time
    // and last time you read the switch is bigger than the interval, then read the switch. Also start a counter
    // to track the number of timer inervals that have passed and then do something after a predetermined number of counts.

    currentMillis = millis();                              // Read the timer and store it in the varable

    // if statement which controls all timer events
    if (currentMillis - previousMillis > TIMER_INTERVAL) 
    {
        previousMillis = currentMillis;                    // Save the last time you read the timer.

        TimerIntervalCounter++;                            // Variable to store delay intervals - increment counter by 1

        // Reset Counter after it reaches 1 minute (50 x 1200 = 60,000 or 60 seconds)
        if (TimerIntervalCounter > 1200) 
        {
            TimerIntervalCounter = 0;
        }

        SwitchUpPressed_for = SwitchUp.pressedFor(10);     // EasyButton Library - Function for Up Switch Press for amount of time (True / false)
        SwitchUp.read();                                   // EasyButton Library - Read event for Up switch
        SwitchDownPressed_for = SwitchDown.pressedFor(10); // EasyButton Library - Function for Down Switch Press for amount of time (True / false)
        SwitchDown.read();                                 // EasyButton Library - Read event for Up switch
        SwitchDownPressed = SwitchDown.isPressed();        // EasyButton Library - Function for Down Switch Press (True or false)
        SwitchDown.read();                                 // EasyButton Library - Read event for Up switch
        SwitchUpPressed = SwitchUp.isPressed();            // EasyButton Library - Function for Up Switch Press (True or false)
        SwitchUp.read();                                   // EasyButton Library - Read event for Up switch


        if (TimerIntervalCounter > 200)                    // If ScreenBlankCounter is greater then 200, its time to Blank Display
        {
            BlankDisplay();
        }

        // Detect double switch presses
        if ((SwitchUpPressed_for == true && SwitchDownPressed == true) ||
                (SwitchDownPressed_for == true && SwitchUpPressed == true)) 
        {
            ManageThermostat();
        }

        // if statements to increment the temperature - (to change increment speed change the TimerIntervalCounter varaible)
        if (TimerIntervalCounter > DEBOUNCE_CNT && 
                SwitchUpPressed == true && 
                RelayState == true) 
        {
            IncrementTemp();                               // temperature Up
        }

        // if statements to decrement the temperature - (to change increment speed change the TimerIntervalCounter varaible)
        if (TimerIntervalCounter > DEBOUNCE_CNT && 
                SwitchDownPressed == true && 
                RelayState == true) 
        { 
            DecrementTemp();                               // temperature Down
        }
    }

} // end of main loop

//*****************************************************************************************
void ManageThermostat() 
{
    TimerIntervalCounter = 0;                           // Reset counter

    // flag the screen blanking event control the temperature variable after an event
    if (DisplayBlankFlag == true) 
    {
        ManOverrideTemp--;
        DisplayBlankFlag = false;                       // reset the flag for blank display
    }

    switch (ThermostatState)
    {
        case THERMOSTAT_STATE_ON:

            ThermostatState++;                          // next thermostat state is incremented by 1

            LedOn();                                    // Turn LED ON
            RelayOn();                                  // Turn system RELAY ON

            display.clearDisplay();                     // Setup and print to the SSD1306 OLED display
            display.setTextSize(2);
            display.setCursor(5, 0);
            display.print("System On");
            display.display();

            delay(2000);

            display.clearDisplay();                       
            display.setTextSize(2);
            display.setTextSize(4);
            display.setCursor(10, 28);
            ManOverrideTemp = (1.8 * bme.readTemperature() + 32); // read the room temperature
            display.printf("%.0f", ManOverrideTemp);
            display.print((char)247); // degree symbol
            display.print("F");
            display.display();

            break;

        case THERMOSTAT_STATE_PRESET_1:

            ManOverrideTemp = Presets[0];               // Stored Preset - Set Low Temperature - control the temperature varable after this event
            ThermostatState++;

            display.clearDisplay();                     // Setup and print to the SSD1306 OLED display
            display.setTextSize(2);
            display.setCursor(5, 0);
            display.println("Preset 1");
            display.setTextSize(4);
            display.setCursor(10, 28);
            display.printf("%.0f", ManOverrideTemp);
            display.print((char)247); // degree symbol
            display.print("F");
            display.display();

            break;

        case THERMOSTAT_STATE_PRESET_2:

            ManOverrideTemp = Presets[1];               // Stored Preset - Set High Temperature - control the temperature varable after this event
            ThermostatState++;

            display.clearDisplay();                     // Setup and print to the SSD1306 OLED display
            display.setTextSize(2);
            display.setCursor(5, 0);
            display.println("Preset 2");
            display.setTextSize(4);
            display.setCursor(10, 28);
            display.printf("%.0f", ManOverrideTemp);
            display.print((char)247); // degree symbol
            display.print("F");
            display.display();

            break;

        case THERMOSTAT_STATE_OFF:

            ThermostatState = THERMOSTAT_STATE_ON;

            LedOff();                                   // Turn LED OFF
            RelayOff();                                 // Turn system RELAY OFF

            display.clearDisplay();                     // Setup and print to the SSD1306 OLED display
            display.setTextSize(2);
            display.setCursor(5, 0);
            display.println("System Off");
            display.display();

            break;
    }

    // Hold until UP and Down switch is released
    while (SwitchUpPressed == true || SwitchDownPressed == true) 
    {
        SwitchUpPressed = SwitchUp.isPressed();
        SwitchUp.read();
        SwitchDownPressed = SwitchDown.isPressed();
        SwitchDown.read();
    }

}   // end of MangeThePresets()


//*****************************************************************************************
void BlankDisplay() 
{
    display.clearDisplay();                       // Blank the SSD1306 OLED display
    display.display();

    DisplayBlankFlag = true;
}

//*****************************************************************************************
void IncrementTemp() 
{
    TimerIntervalCounter = 0;                               // Reset counter

    if (DisplayBlankFlag == true) 
    {
        // wait for Up Down switches are released from a double switch even
        while (SwitchDownPressed == true || SwitchUpPressed == true)
        {
            SwitchDownPressed = SwitchDown.isPressed();
            SwitchDown.read();
            SwitchUpPressed = SwitchUp.isPressed();
            SwitchUp.read();
        }

        ManOverrideTemp--;                                  // Take care of temperature on a Display Blank button press
        DisplayBlankFlag = false;
    }

    if (ManOverrideTemp < MAX_TEMP) 
    {
        ManOverrideTemp++;                                  // Increment temperature UP
    }
    else 
    {
        ManOverrideTemp = MAX_TEMP;                         // set the maximum temperature to 80
    }

    display.clearDisplay();                                 // Setup and print to the SSD1306 OLED display
    display.setCursor(10, 28);
    display.printf("%.0f", ManOverrideTemp);
    display.print((char)247); // degree symbol
    display.print("F");
    display.display();
}

//*****************************************************************************************
void DecrementTemp() 
{
    TimerIntervalCounter = 0;                               // Reset counter

    if (DisplayBlankFlag == true) 
    {
        // wait for Up Down switches are released from a double switch even
        while (SwitchDownPressed == true || SwitchUpPressed == true)
        {
            SwitchDownPressed = SwitchDown.isPressed();
            SwitchDown.read();
            SwitchUpPressed = SwitchUp.isPressed();
            SwitchUp.read();
        }

        ManOverrideTemp++;                                  // Take care of temperature on a Display Blank button press
        DisplayBlankFlag = false;
    }

    if (ManOverrideTemp > MIN_TEMP) 
    {
        ManOverrideTemp--;                                  // Decrement Temperature

        display.clearDisplay();                             // Setup and print to the SSD1306 OLED display
        display.setCursor(10, 28);
        display.printf("%.0f", ManOverrideTemp);
        display.print((char)247); // degree symbol
        display.print("F");
        display.display();
    }
    else 
    {
        ManOverrideTemp = MIN_TEMP;                         // Set minimum temperature at 40

        display.clearDisplay();                             // Setup and print to the SSD1306 OLED display
        display.setCursor(10, 28);
        display.printf("%.0f", ManOverrideTemp);
        display.print((char)247); // degree symbol
        display.print("F");
        display.display();
    }
}

void ShowRoomTempHumidityPressure()
{
    display.clearDisplay();                       
    display.setTextSize(2);
    display.setCursor(5, 0);
    display.print("Humidity");      
    display.setTextSize(4);
    display.setCursor(10, 28);
    display.printf("%.0f",bme.readHumidity());
    display.println(" %");
    display.display();

    delay(2000);

    display.clearDisplay();                       
    display.setTextSize(2);
    display.setCursor(5, 0);
    display.print("Pressure");
    display.setTextSize(4);
    display.setCursor(10, 28);
    float Pressure = bme.readPressure()* 0.0002953;
    display.printf("%.0f",Pressure);
    display.print("in.");
    display.display();

    delay(2000);

    display.clearDisplay();                       
    display.setTextSize(2);
    display.setTextSize(4);
    display.setCursor(10, 28);
    ManOverrideTemp = (1.8 * bme.readTemperature() + 32);
    display.printf("%.0f", ManOverrideTemp);
    display.print((char)247); // degree symbol
    display.print("F");
    display.display();

    delay(2000);
}
