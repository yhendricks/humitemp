#include <Bounce2.h>

#define LOG_INTERVAL (30 * 1000)            // log every 30 seconds
#define SENSOR_READ_INTERVAL (10 * 1000)     // read sensors every 5 seconds

#define TEMPERATURE_OFFSET              30
#define TEMPERATURE_FUDGE_FACTOR        0
#define STEPS_PER_DEGREE                10.24
#define NUM_AVG_VALUES                  10
#define TEMPERATURE_PORT                A5
#define HUMIDITY_PORT                   A3
#define HUMIDITY_FUDGE_FACTOR           0
#define HYSTERESIS                      1.5
#define TEMPERATURE_CONTROL             2                       // GPIO_0 dont use port  0 or 1
#define HUMIDITY_CONTROL                4                       // GPIO_2
#define CYCLE_LED_PIN                   8
#define CYCLE_INDEX_INDICATOR           7
#define PREHEAT_LED_PIN                 3
#define CYCLE_START_BTN_PIN             9                       // button is connected to pin 9 and GND
#define PREHEAT_BTN_PIN                 6                       // button is connected to pin 6 and GND
#define DEHUMIDIFIER_PIN                5
#define DEHUMIDIFIER_RATIO              1.10
#define SLOW_BLINK                      1000
#define FAST_BLINK                      250

// Configuration
#define MAX_CYCLES                      2
#define MAX_TEMPERATURE_CYCLE1          50                  // deg Celcius
#define MAX_HUMIDITY_CYCLE1             95                  // %relative humidity
#define DURATION_CYCLE1                 1440                // minutes
#define MAX_TEMPERATURE_CYCLE2          50                  // deg Celcius
#define MAX_HUMIDITY_CYCLE2             65                  // %relative humidity
#define DURATION_CYCLE2                 720                // minutes

// key press management
Bounce preheat_pushbutton = Bounce(); 
Bounce cycle_pushbutton = Bounce(); 

bool cycle_started = false;
byte preheat_btn_off = true;
bool temperature_element_on;
bool humidity_element_on;
bool dehumidifier_on;
byte cycle_index;
short continue_cycle_index = -1;
word duration_remaining_in_minutes;
unsigned long next_cycle_index_led_check = millis();
unsigned long next_cycle_pause_led_check = millis();
bool cycle_index_led_state = false;
bool cycle_pause_led_state = false;


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

    // dehumidifier
    pinMode(DEHUMIDIFIER_PIN, OUTPUT);                          
    digitalWrite(HUMIDITY_CONTROL, HIGH);
    bool dehumidifier_on = false;    
    
    // cycle led init
    pinMode(CYCLE_LED_PIN, OUTPUT);
    digitalWrite(CYCLE_LED_PIN, HIGH);

    // cycle led init
    pinMode(CYCLE_INDEX_INDICATOR, OUTPUT);
    digitalWrite(CYCLE_INDEX_INDICATOR, HIGH);
    
    // preheat init
    pinMode(PREHEAT_LED_PIN, OUTPUT);
    digitalWrite(PREHEAT_LED_PIN, HIGH);
        
    Serial.println(F("Temperature and Humidity measurement and control V0.03")); 
    Serial.println(F("======================================================"));
}

class Cycle {
public:
    inline Cycle(byte max_temp, byte max_hum, word cycle_duration){  
        max_temperature = max_temp;
        max_humidity = max_hum;
        cycle_duration_in_minutes = cycle_duration;
    }
    //time_t cycle_start_time;
    byte max_temperature;
    byte max_humidity;
    word cycle_duration_in_minutes;    
};

Cycle Cycles[MAX_CYCLES] = {
    Cycle(MAX_TEMPERATURE_CYCLE1, MAX_HUMIDITY_CYCLE1, DURATION_CYCLE1),
    Cycle(MAX_TEMPERATURE_CYCLE2, MAX_HUMIDITY_CYCLE2, DURATION_CYCLE2),
};

void set_all_relays_off()
{
    set_temperature_element(false, 0);
    set_humidity_element(false, 0);
    set_dehumidifier(false, 0);    
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
    for (byte index=0; index < MAX_CYCLES; index++) {
        cycle_index = index;

        if (continue_cycle_index >= 0)
        {
            if (continue_cycle_index != index)
                continue;            
        }
        blink_cycle_pause_indicator_led();
        if (cycle_started || preheat_btn_off == false) {
            if (cycle_started)
            {
                if (continue_cycle_index >= 0)
                    Serial.println(F("Continuing with a previous cycle"));
                else
                    print_config(index);
            }
            unsigned long end_time;
            if (continue_cycle_index >= 0)
                end_time = millis() + (unsigned long)duration_remaining_in_minutes * 60UL * 1000UL;
            else
                end_time = millis() + (unsigned long)Cycles[index].cycle_duration_in_minutes * 60UL * 1000UL;    
                        
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
                    if ((last_humidity / Cycles[index].max_humidity) > DEHUMIDIFIER_RATIO)
                        set_dehumidifier(true, last_humidity); 
                }
                else if (last_humidity < (Cycles[index].max_humidity - HYSTERESIS)) {
                    set_humidity_element(true, last_humidity);
                    set_dehumidifier(false, last_humidity);
                }

                unsigned long next_reading_point = millis() + SENSOR_READ_INTERVAL;
                while (millis() < next_reading_point) {
                    //  if the "cycle start/stop" key was pressed, then exit 
                    if (cycle_started) {
                        check_cycle_button();
                        if (cycle_started == false) {
                            set_all_relays_off();
                            user_stopped_cycle = true;
                            Serial.println(F("Cycle prematurely stopped by user"));
                            continue_cycle_index = index;
                            duration_remaining_in_minutes =  end_time - millis() / (1000 * 60);  
                            delay(1000);
                            break; 
                        }
                    }

                    //check if the preheat start/stop was pressed
                    if (preheat_btn_off == false) {
                        check_preheat_button();
                        if (preheat_btn_off) {
                            set_all_relays_off();
                            delay(1000);
                            break;
                        }
                    }                    

                    blink_cycle_index_indicator_led();
                
                    if (millis() > next_log_point) {
                        next_log_point = millis() + LOG_INTERVAL;
                        log_current_data(index, last_temperature, last_humidity);
                    }                    
                }

                if (cycle_started == false && preheat_btn_off)
                    break;               

            }
            if (cycle_started) { 
                if (user_stopped_cycle) {
                    user_stopped_cycle = false;         
                    break;                
                } else {
                    Serial.print(F("Cycle completed: "));   
                    Serial.println(index);                                             
                }
                set_all_relays_off();
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
    Serial.print(F("C"));
    Serial.print(index);
    Serial.print(F(", temperature = "));
    Serial.print(temperature);
    Serial.print(F(", humidity = "));
    Serial.println(humidity);
}

void set_temperature_element(bool state, float temperature) {
    if (state != temperature_element_on) {
        temperature_element_on = state;
        if (temperature_element_on) {
            digitalWrite(TEMPERATURE_CONTROL, LOW);
            Serial.print(F("Temperature Relay = on, temperature = "));
            Serial.println(temperature);
        }
        else {
            digitalWrite(TEMPERATURE_CONTROL, HIGH);
            Serial.print(F("Temperature Relay = off, temperature = "));
            Serial.println(temperature);
        }
    } 
}

void set_humidity_element(bool state, float humidity) {
    if (state != humidity_element_on) {
        humidity_element_on = state;
        if (humidity_element_on) {
            digitalWrite(HUMIDITY_CONTROL, LOW);
            Serial.print(F("Humidity Relay = on, humidity = "));
            Serial.println(humidity);
        }
        else {
            digitalWrite(HUMIDITY_CONTROL, HIGH);
            Serial.print(F("Humidity Relay = off, humidity = "));
            Serial.println(humidity);
        }
    } 
}

void set_dehumidifier(bool state, float humidity) {
    if (state != dehumidifier_on) {
        dehumidifier_on = state;
        if (dehumidifier_on) {
            digitalWrite(DEHUMIDIFIER_PIN, LOW);
            Serial.print(F("DehuHumidifier = on, humidity = "));
            Serial.println(humidity);
        }
        else {
            digitalWrite(DEHUMIDIFIER_PIN, HIGH);
            Serial.print(F("DehuHumidifier = off, humidity = "));
            Serial.println(humidity);
        }
    } 
}


void blink_cycle_pause_indicator_led() {    
    if (continue_cycle_index >= 0) {
        unsigned long blink_interval = FAST_BLINK;
        if (millis() > next_cycle_pause_led_check) {
            if (cycle_pause_led_state) {
                digitalWrite(CYCLE_LED_PIN, HIGH);   // turn the LED off (HIGH is the voltage level)
                cycle_pause_led_state = false;
            } else {
                digitalWrite(CYCLE_LED_PIN, LOW);   // turn the LED on 
                cycle_pause_led_state = true;
            }  
            next_cycle_pause_led_check = (unsigned long)(millis() + (unsigned long)blink_interval);
        }
    }
    else
        digitalWrite(CYCLE_LED_PIN, HIGH);
}

void blink_cycle_index_indicator_led() {    
    if (cycle_started) {
        unsigned long blink_interval = cycle_index == 0 ? SLOW_BLINK : FAST_BLINK;
        if (millis() > next_cycle_index_led_check) {
            if (cycle_index_led_state) {
                digitalWrite(CYCLE_INDEX_INDICATOR, HIGH);   // turn the LED off (HIGH is the voltage level)
                cycle_index_led_state = false;
            } else {
                digitalWrite(CYCLE_INDEX_INDICATOR, LOW);   // turn the LED on 
                cycle_index_led_state = true;
            }  
            next_cycle_index_led_check = (unsigned long)(millis() + (unsigned long)blink_interval);
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
                Serial.println(F("Preheat ON"));
                continue_cycle_index = -1;
                //set_dehumidifier(true, 0);                  // test button
            } else {
                digitalWrite(PREHEAT_LED_PIN, HIGH);
                Serial.println(F("Preheat OFF"));
                //set_dehumidifier(false, 0);                 // test button
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
                Serial.println(F("Cycle started"));
            } else {
                digitalWrite(CYCLE_LED_PIN, HIGH);
                Serial.println(F("Cycle stopped"));
            }
        } 
    }                
}

void print_config(byte index) {
    Serial.print(F("Cycle started: "));
    Serial.print(index);
    Serial.print(F(", Max Temperature: "));
    Serial.print(Cycles[index].max_temperature);     
    Serial.print(F("(celcius), Max Humidity: "));
    Serial.print(Cycles[index].max_humidity); 
    Serial.print(F("(%), duration: "));
    Serial.print(Cycles[index].cycle_duration_in_minutes); 
    Serial.println(F("(minutes)")); 
}

