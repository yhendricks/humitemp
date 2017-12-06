//#include <Time.h>
#include <Bounce2.h>

#define LOG_INTERVAL (30 * 1000)            // log every 30 seconds

#define TEMPERATURE_OFFSET              30
#define TEMPERATURE_FUDGE_FACTOR        0
#define STEPS_PER_DEGREE                10.24
#define NUM_AVG_VALUES                  10
#define TEMPERATURE_PORT                A5
#define HUMIDITY_PORT                   A3
#define HUMIDITY_FUDGE_FACTOR           0
#define HYSTERESIS                      0.5
#define TEMPERATURE_CONTROL             2                       // GPIO_0 dont use port  0 or 1
#define HUMIDITY_CONTROL                4                       // GPIO_2
#define CYCLE_LED_PIN                   8
#define CYCLE_INDEX_INDICATOR           7
#define PREHEAT_LED_PIN                 3
#define CYCLE_START_BTN_PIN             9                       // button is connected to pin 9 and GND
#define PREHEAT_BTN_PIN                 6 //4                   // button is connected to pin 6 and GND

// Configuration
#define MAX_TEMPERATURE_CYCLE1          50                  // deg Celcius
#define MAX_HUMIDITY_CYCLE1             95                  // %relative humidity
#define DURATION_CYCLE1                 360                // minutes
#define MAX_TEMPERATURE_CYCLE2          50                  // deg Celcius
#define MAX_HUMIDITY_CYCLE2             65                  // %relative humidity
#define DURATION_CYCLE2                 360                // minutes

// key press management
Bounce preheat_pushbutton = Bounce(); 
Bounce cycle_pushbutton = Bounce(); 

bool cycle_started = false;
byte preheat_btn_off = true;
bool temperature_element_on;
bool humidity_element_on;
byte cycle_index;
unsigned long next_cycle_led_check = millis();
bool cycle_led_state = false;


// the setup routine runs once when you press reset:
void setup() {
    // initialize serial communication at 9600 bits per second:
    Serial.begin(9600);  

    //preheat key press management
    pinMode(PREHEAT_BTN_PIN, INPUT);                            
    digitalWrite(PREHEAT_BTN_PIN, HIGH);
    preheat_pushbutton .attach(PREHEAT_BTN_PIN);
    preheat_pushbutton .interval(25);  
    
    // cycle key press management
    pinMode(CYCLE_START_BTN_PIN, INPUT);                 
    digitalWrite(CYCLE_START_BTN_PIN, HIGH);
    cycle_pushbutton .attach(CYCLE_START_BTN_PIN);
    cycle_pushbutton .interval(25);

    // temperature control
    pinMode(TEMPERATURE_CONTROL, OUTPUT);                       
    temperature_element_on = false;
    digitalWrite(TEMPERATURE_CONTROL, HIGH);

    // humidity
    pinMode(HUMIDITY_CONTROL, OUTPUT);                          
    digitalWrite(HUMIDITY_CONTROL, HIGH);
    bool humidity_element_on = false;
    
    // cycle led init
    pinMode(CYCLE_LED_PIN, OUTPUT);
    digitalWrite(CYCLE_LED_PIN, HIGH);

    // cycle led init
    pinMode(CYCLE_INDEX_INDICATOR, OUTPUT);
    digitalWrite(CYCLE_INDEX_INDICATOR, HIGH);
    
    // preheat init
    pinMode(PREHEAT_LED_PIN, OUTPUT);
    digitalWrite(PREHEAT_LED_PIN, HIGH);

        
    Serial.println("Temperature and Humidity measurement and control V0.01"); 
    Serial.println("======================================================");
}

class Cycle {
public:
    inline Cycle(byte max_temp, byte max_hum, int cycle_duration){  
        max_temperature = max_temp;
        max_humidity = max_hum;
        cycle_duration_in_minutes = cycle_duration;
    }
    //time_t cycle_start_time;
    byte max_temperature;
    byte max_humidity;
    int cycle_duration_in_minutes;    
};

Cycle Cycles[2] = {
    Cycle(MAX_TEMPERATURE_CYCLE1, MAX_HUMIDITY_CYCLE1, DURATION_CYCLE1),
    Cycle(MAX_TEMPERATURE_CYCLE2, MAX_HUMIDITY_CYCLE2, DURATION_CYCLE2),
};

void set_all_relays_off()
{
    set_temperature_element(false, 0);
    set_humidity_element(false, 0);    
}

// the loop routine runs over and over again forever:
void loop(){
    // "preheat" key press management
    if (cycle_started == false) {
        digitalWrite(CYCLE_INDEX_INDICATOR, HIGH);
        digitalWrite(CYCLE_LED_PIN, HIGH);
        check_preheat_button();
    }
    
    // "cycle" key press management section
    if (preheat_btn_off == true) {
        digitalWrite(PREHEAT_LED_PIN, HIGH);
        check_cycle_button();
    }
    

    bool user_stopped_cycle = false;
    for (byte index=0; index < 2; index++) {
        cycle_index = index;
        
        if (cycle_started || preheat_btn_off == false) {
            if (cycle_started)
            {
               print_config(index);
            }
            unsigned long end_time = millis() + (unsigned long)Cycles[index].cycle_duration_in_minutes * 60UL * 1000UL;    
                        
            unsigned long next_log_point = millis() + LOG_INTERVAL;
            while (millis() < end_time) {
                float last_temperature = measure_temperature();
                float last_humidity = measure_humidity();
                if (last_temperature > (Cycles[index].max_temperature + HYSTERESIS)) {
                    set_temperature_element(false, last_temperature);
                }
                else if (last_temperature < (Cycles[index].max_temperature - HYSTERESIS)) {
                    set_temperature_element(true, last_temperature);
                }
                if (last_humidity > (Cycles[index].max_humidity + HYSTERESIS)) {
                    set_humidity_element(false, last_humidity);
                }
                else if (last_humidity < (Cycles[index].max_humidity - HYSTERESIS)) {
                    set_humidity_element(true, last_humidity);
                }

                //  if the "cycle start/stop" key was pressed, then exit 
                if (cycle_started) {
                    check_cycle_button();
                    if (cycle_started == false) {
                        set_all_relays_off();
                        user_stopped_cycle = true;
                        Serial.println("Cycle prematurely stopped by user");
                        delay(2000);
                        break; 
                    }
                }

                //check if the preheat start/stop was pressed
                if (preheat_btn_off == false) {
                    check_preheat_button();
                    if (preheat_btn_off) {
                        set_all_relays_off();
                        delay(2000);
                        break;
                    }
                }                    

                blink_cycle_index_indicator_led();
                
                if (millis() > next_log_point) {
                    next_log_point = millis() + LOG_INTERVAL;
                    log_current_data(index, last_temperature, last_humidity);
                }
            }
            if (cycle_started) { 
                if (user_stopped_cycle) {
                    user_stopped_cycle = false;         
                    break;                
                } else {
                    Serial.print("Cycle completed: ");   
                    Serial.println(index);       
                    set_humidity_element(false, last_temperature);
                    set_temperature_element(false, last_humidity);                                       
                }
            }
        }        
    }
    cycle_started = false;
    set_humidity_element(false, -100);
    set_temperature_element(false, -100);    
}


// assuming a range of -30 to 70 and 0 - 1023 steps, then a factor of 10.24 units represents 1 deg, with an offset of -30
// fudge factor at approx 55deg is to minus 6.75% from value read
// ADC returns 0 - 1023  for a range of 0V to 5.5   
float measure_temperature() {
    int avg_val = 0;
    for (int i = 0; i <  NUM_AVG_VALUES; i++) {
        avg_val += analogRead(TEMPERATURE_PORT);
    } 
    avg_val /= NUM_AVG_VALUES; 
    avg_val = avg_val - int(avg_val * TEMPERATURE_FUDGE_FACTOR);      
    return (avg_val / STEPS_PER_DEGREE) - TEMPERATURE_OFFSET;
}

// assuming a range of 0 - 100% RH, then a factor of 10.24 units represents 1 percentage of humidity
float measure_humidity() {
    int avg_val = 0;
    for (int i = 0; i <  NUM_AVG_VALUES; i++) {
        avg_val += analogRead(HUMIDITY_PORT);
    }  
    avg_val /= NUM_AVG_VALUES; 
    avg_val = avg_val - int(avg_val * HUMIDITY_FUDGE_FACTOR);                   // tbd dont' know what the fudge factor for the "uno" is yet
   
    return (avg_val / STEPS_PER_DEGREE);
}

void log_current_data(byte index, float temperature, float humidity ) {
    Serial.print("C");
    Serial.print(index);
    Serial.print(", temperature = ");
    Serial.print(temperature);
    Serial.print(", humidity = ");
    Serial.println(humidity);
}

void set_temperature_element(bool state, float temperature) {
    if (state != temperature_element_on) {
        temperature_element_on = state;
        if (temperature_element_on) {
            digitalWrite(TEMPERATURE_CONTROL, LOW);
            Serial.print("Temperature Relay = on, temperature = ");
            Serial.println(temperature);
        }
        else {
            digitalWrite(TEMPERATURE_CONTROL, HIGH);
            Serial.print("Temperature Relay = off, temperature = ");
            Serial.println(temperature);
        }
    } 
}

void set_humidity_element(bool state, float humidity) {
    if (state != humidity_element_on) {
        humidity_element_on = state;
        if (humidity_element_on) {
            digitalWrite(HUMIDITY_CONTROL, LOW);
            Serial.print("Humidity Relay = on, humidity = ");
            Serial.println(humidity);
        }
        else {
            digitalWrite(HUMIDITY_CONTROL, HIGH);
            Serial.print("Humidity Relay = off, humidity = ");
            Serial.println(humidity);
        }
    } 
}

void blink_cycle_index_indicator_led() {    
    if (cycle_started) {
        unsigned long blink_interval = cycle_index == 0 ? 1000 : 250;
        if (millis() > next_cycle_led_check) {
            if (cycle_led_state) {
                digitalWrite(CYCLE_INDEX_INDICATOR, HIGH);   // turn the LED off (HIGH is the voltage level)
                cycle_led_state = false;
            } else {
                digitalWrite(CYCLE_INDEX_INDICATOR, LOW);   // turn the LED on 
                cycle_led_state = true;
            }
            //unsigned long end_time = millis() + (unsigned long)Cycles[index].cycle_duration_in_minutes * 60UL * 1000UL;     
            next_cycle_led_check = (unsigned long)(millis() + (unsigned long)blink_interval);
        }
    }
    else
        digitalWrite(CYCLE_INDEX_INDICATOR, HIGH);
}

void check_preheat_button() {
    if (preheat_pushbutton.update()) {
        if (preheat_pushbutton.fallingEdge()) {
            preheat_btn_off = !preheat_btn_off;
            if (preheat_btn_off == false) {
                digitalWrite(PREHEAT_LED_PIN, LOW);
                Serial.println("Preheat ON");
            } else {
                digitalWrite(PREHEAT_LED_PIN, HIGH);
                Serial.println("Preheat OFF");
            }
        } 
    }                
}

void check_cycle_button() {
    if (cycle_pushbutton.update()) {
        if (cycle_pushbutton.fallingEdge()) {
            cycle_started = !cycle_started;
            if (cycle_started) {
                digitalWrite(CYCLE_LED_PIN, LOW);
                Serial.println("Cycle started");
            } else {
                digitalWrite(CYCLE_LED_PIN, HIGH);
                Serial.println("Cycle stopped");
            }
        } 
    }                
}

void print_config(byte index) {
    Serial.print("Cycle started: ");
    Serial.print(index);
    Serial.print(", Max Temperature: ");
    Serial.print(Cycles[index].max_temperature);     
    Serial.print("(celcius), Max Humidity: ");
    Serial.print(Cycles[index].max_humidity); 
    Serial.print("(%), duration: ");
    Serial.print(Cycles[index].cycle_duration_in_minutes); 
    Serial.println("(minutes)"); 
}

