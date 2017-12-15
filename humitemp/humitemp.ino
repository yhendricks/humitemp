#include <Bounce2.h>
#include <SPI.h>
#include <Ethernet.h>

#define LOG_INTERVAL (30 * 1000)            // log every 30 seconds
#define SENSOR_READ_INTERVAL (10 * 1000)     // read sensors every 10 seconds

#define TEMPERATURE_OFFSET              30
#define TEMPERATURE_FUDGE_FACTOR        0                       // this value depends hugely on the PSU being used
#define STEPS_PER_DEGREE                10.24
#define NUM_AVG_VALUES                  10
#define TEMPERATURE_PORT                A5
#define HUMIDITY_PORT                   A3
#define HUMIDITY_FUDGE_FACTOR           0                       // this value depends hugely on the PSU being used
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
#define DURATION_CYCLE1                 5                // minutes
#define MAX_TEMPERATURE_CYCLE2          50                  // deg Celcius
#define MAX_HUMIDITY_CYCLE2             65                  // %relative humidity
#define DURATION_CYCLE2                 5                // minutes

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
float duration_remaining_in_minutes;
unsigned long next_cycle_index_led_check = millis();
unsigned long next_cycle_pause_led_check = millis();
bool cycle_index_led_state = false;
bool cycle_pause_led_state = false;

// vvvvvvvv  THINGSPEAK STUFF vvvvvvvvvvvvvv
// Local Network Settings
byte mac[] = { 0xD4, 0xA8, 0xE2, 0xFE, 0xA0, 0xA1 }; // Must be unique on local network
//IPAddress dns_server(172, 17, 17, 27);
//IPAddress ip(172, 17, 23, 55);                // Must be unique on local network
//IPAddress gateway(172, 17, 0, 44);
//IPAddress subnet(255, 255, 0, 0);
#define updateInterval				25000       // Time interval in milliseconds to update ThingSpeak

// Variable Setup
long nextThingspeakCheck = 0;
unsigned long nextEthernetCheck = 0;
unsigned long lastSuccessfullUpload = 0;
int failedCounter = 0;

// Initialize Arduino Ethernet Client
EthernetClient client;

void updateThingSpeak(String tsData)
{
    // ThingSpeak Settings
    char thingSpeakAddress[] = "api.thingspeak.com";
    String writeAPIKey = "GXD3KFIKBGRLXH5W";    // Write API Key for a ThingSpeak Channel
    
  if (client.connect(thingSpeakAddress, 80))
  { 
    client.print("POST /update HTTP/1.1\n");
    client.print("Host: api.thingspeak.com\n");
    client.print("Connection: close\n");
    client.print("X-THINGSPEAKAPIKEY: "+writeAPIKey+"\n");
    client.print("Content-Type: application/x-www-form-urlencoded\n");
    client.print("Content-Length: ");
    client.print(tsData.length());
    client.print("\n\n");
    client.print(tsData);
    // This delay is pivitol without it the TCP client will often close before the data is fully sent
    delay(200);    
    if (client.connected())
    {
      Serial.println(F(" - Data uploaded to ThingSpeak..."));
      failedCounter = 0;
      lastSuccessfullUpload = millis();
    }
    else
    {
      failedCounter++;  
      Serial.println(" - Connection to ThingSpeak failed ("+String(failedCounter, DEC)+")"); 
    }    
    client.stop();
  }
  else
  {
    failedCounter++;    
    Serial.println(" - Connection to ThingSpeak Failed ("+String(failedCounter, DEC)+")");
  }
}

void printIP(IPAddress ip)
{  
  // print your local IP address:
  Serial.print("My IP address: ");
  ip = Ethernet.localIP();
  for (byte thisByte = 0; thisByte < 4; thisByte++) {
    // print the value of each byte of the IP address:
    Serial.print(ip[thisByte], DEC);
    Serial.print(".");
  }    
  Serial.println();
}

bool CheckIP(IPAddress ip)
{
    return ip[0] == 0;
}

void startEthernet()
{
  client.stop();

  Serial.print(F("Connecting to the network..."));
  delay(1000);
  
  // Connect to network amd obtain an IP address using DHCP
//  Ethernet.begin(mac, ip, dns_server, gateway, subnet);
//  Serial.println(F(" - Connected via static IP")); 
//  return;
   
  if (Ethernet.begin(mac) == 0)
  {
    Serial.println(F(" - DHCP Failed, will try again in 5 mins"));
    delay(2000); 
  }
  else {
    Serial.println(F(" - Unit connected to the network using DHCP"));
    printIP(Ethernet.localIP());
    Serial.println(F("Data being uploaded to https://thingspeak.com/channels/380095 "));
  }
  
  delay(1000);
}

// ^^^^^^^^^^^^  THINGSPEAK STUFF ^^^^^^^^^^^^^^^


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
        
    Serial.println(F("Temperature and Humidity measurement and control V0.04")); 
    Serial.println(F("======================================================"));

	// vvvvvvvv  THINGSPEAK STUFF vvvvvvvvvvvvvv
	startEthernet();
    nextEthernetCheck = (unsigned long)millis() + 300000UL;
	// ^^^^^^^^^^  THINGSPEAK STUFF ^^^^^^^^^^^^^^
}

void(* resetFunc) (void) = 0; //declare reset function @ address 0

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
    float last_temperature = 0;
    float last_humidity = 0;    
    
    // "preheat" key press management
    if (cycle_started == false) {
        digitalWrite(CYCLE_INDEX_INDICATOR, HIGH);
        check_preheat_button();
    }
    
    // "cycle" key press management section
    if (preheat_btn_off == true) {
        digitalWrite(PREHEAT_LED_PIN, HIGH);
        check_cycle_button();
    }
    
    blink_cycle_pause_indicator_led();
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
                if (continue_cycle_index >= 0) {
                    Serial.print(F("Continuation of cycle number: "));
                    Serial.println(continue_cycle_index);
                    Serial.print(F("Minutes remaining: "));
                    Serial.println(duration_remaining_in_minutes);
                }
                else
                    print_config(index);
            }
            continue_cycle_index = -1;
            unsigned long end_time;
            if (continue_cycle_index >= 0)
                end_time = millis() + (unsigned long)duration_remaining_in_minutes * 60UL * 1000UL;
            else
                end_time = millis() + (unsigned long)Cycles[index].cycle_duration_in_minutes * 60UL * 1000UL;    
                        
            unsigned long next_log_point = millis() + LOG_INTERVAL;
            while (millis() < end_time) {
                float last_temperature = measure_temperature(last_temperature);
                float last_humidity = measure_humidity(last_humidity);
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
                            duration_remaining_in_minutes =  ((unsigned long)end_time - millis()) / 1000UL / 60UL; 
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
                        
                        // vvvvvvvv  THINGSPEAK STUFF vvvvvvvvvvvvvv  
                        if (!CheckIP(Ethernet.localIP())) {                                                    
                            // Print Update Response to Serial Monitor
                            if (client.available())
                                char c = client.read();
                            updateThingSpeak("field1="+String(index)+"&field2="+String(last_temperature)+"&field3="+String(last_humidity)
                                                +"&field4="+String(temperature_element_on)+"&field5="+String(humidity_element_on)+"&field6="+String(dehumidifier_on));   
                            //lastSuccessfullUpload                                            
                        } else {
                            Serial.println(" - not able to upload to ThingSpeak");
                        }
                        // ^^^^^^^^^^^^  THINGSPEAK STUFF ^^^^^^^^^^^^^^                        
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
                delay(1000);
            }
        }        
    }
    cycle_started = false;
    set_humidity_element(false, -100);
    set_temperature_element(false, -100);    
    //
    if (millis() > nextEthernetCheck) {
        if (CheckIP(Ethernet.localIP())) {
            startEthernet();   
        } else {
            Ethernet.maintain();
        }
        nextEthernetCheck = (unsigned long)millis() + 300000UL;             // 5 mins
    }
}

#define MAX_RETRIES       5

// assuming a range of -30 to 70 and 0 - 1023 steps, then a factor of 10.24 units represents 1 deg, with an offset of -30
// ADC returns 0 - 1023  for a range of 0V to 5.5   
float measure_temperature(float last_value) {
    int avg_val = 0;
    for (int retry = 0; retry < MAX_RETRIES; retry++) {
        avg_val = 0;
        for (int i = 0; i <  NUM_AVG_VALUES; i++) {
            avg_val += analogRead(TEMPERATURE_PORT);
        } 
        avg_val /= NUM_AVG_VALUES; 
        avg_val = avg_val - int(avg_val * TEMPERATURE_FUDGE_FACTOR);
        float percentage_change = (last_value / ((avg_val / STEPS_PER_DEGREE) - TEMPERATURE_OFFSET));     
        if (percentage_change > 0.95 && percentage_change < 1.05 ) {
            // If the new value is within 5% of the last value, then we will use that value
            break;
        }
    }      
    return (avg_val / STEPS_PER_DEGREE) - TEMPERATURE_OFFSET;
}

// assuming a range of 0 - 100% RH, then a factor of 10.24 units represents 1 percentage of humidity
float measure_humidity(float last_value) {
    int avg_val = 0;
    for (int retry = 0; retry < MAX_RETRIES; retry++) {   
        avg_val = 0; 
        for (int i = 0; i <  NUM_AVG_VALUES; i++) {
            avg_val += analogRead(HUMIDITY_PORT);
        }  
        avg_val /= NUM_AVG_VALUES; 
        avg_val = avg_val - int(avg_val * HUMIDITY_FUDGE_FACTOR);
        float percentage_change = (last_value / (avg_val / STEPS_PER_DEGREE)); 
        if (percentage_change > 0.95 && percentage_change < 1.05 ) {
            // If the new value is within 5% of the last value, then we will use that value
            break;
        }                           
    }
    return (avg_val / STEPS_PER_DEGREE);
}

void log_current_data(byte index, float temperature, float humidity ) {
    Serial.print(F("C"));
    Serial.print(index);
    Serial.print(F(", temperature = "));
    Serial.print(temperature);
    Serial.print(F(", humidity = "));
    Serial.print(humidity);
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
            Serial.print(F("Dehuhumidifier = on, humidity = "));
            Serial.println(humidity);
        }
        else {
            digitalWrite(DEHUMIDIFIER_PIN, HIGH);
            Serial.print(F("Dehuhumidifier = off, humidity = "));
            Serial.println(humidity);
        }
    } 
}


void blink_cycle_pause_indicator_led() {    
    if (continue_cycle_index >= 0) {
        if (millis() > next_cycle_pause_led_check) {
            if (cycle_pause_led_state) {
                digitalWrite(CYCLE_LED_PIN, HIGH);   // turn the LED off (HIGH is the voltage level)
                cycle_pause_led_state = false;
            } else {
                digitalWrite(CYCLE_LED_PIN, LOW);   // turn the LED on 
                cycle_pause_led_state = true;
            }  
            next_cycle_pause_led_check = (unsigned long)(millis() + (unsigned long)(150));
        }
    }
    else
        if (!cycle_started) 
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
            if (continue_cycle_index != -1) {
                Serial.println(F("Cycle has been cancelled / stopped"));
                continue_cycle_index = -1;
                return;
            }          
            preheat_btn_off = !preheat_btn_off;
            if (preheat_btn_off == false) {
                digitalWrite(PREHEAT_LED_PIN, LOW);
                Serial.println(F("Preheat ON"));                
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


