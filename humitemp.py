#!/usr/bin/env python
# -*- coding: utf-8 -*-
# title           :humitemp.py
# description     :This program controls and logs the temperature and humidity
# author          :
# date            :
# version         :0.02
# usage           :python humitemp.py
# notes           :
# python_version  :2.7.6
# =======================================================================

# Import the modules needed to run the script.
import logging
import os
import sys
import threading
from datetime import datetime, timedelta
from logging.handlers import RotatingFileHandler
from time import sleep

try:
    from configparser import ConfigParser
except ImportError:
    from ConfigParser import ConfigParser  # ver. < 3.0

# Create a few strings for file I/O equivalence
HIGH = "1"
LOW = "0"
INPUT = "0"
OUTPUT = "1"
INPUT_PU = "8"

# GPIO Pins
TEMPERATURE_CONTROL = 0                 # GPIO_0
HUMIDITY_CONTROL = 2                    # GPIO_2

TEMPERATURE_PORT = '5'                  # A5
HUMIDITY_PORT = '3'                     # A3

# Global Variables
cycle_started = False
preheat_started = False
current_cycle_duration_in_minutes = 1440  # minutes
current_cycle_start_time = datetime.now()  # tbd
current_cycle_index = 0
last_temperature = (1, 1, 1)
last_humidity = (1, 1, 1)
temperature_element_on = False
humidity_element_on = False
control_thread = threading.Thread()         # target=measure_and_control
STEPS_PER_DEGREE = 40.95
TEMPERATURE_OFFSET = 30
TEMPERATURE_FUDGE_FACTOR = 0.0675
HUMIDITY_FUDGE_FACTOR = 0.0675
TEMPERATURE_HYSTERESIS = 1
HUMIDITY_HYSTERESIS = 1


class CycleConfig:
    def __init__(self, enabled, max_temperature, max_humidity, cycle_duration_in_minutes):
        self.enabled = enabled
        self.max_temperature = max_temperature
        self.max_humidity = max_humidity
        self.cycle_duration_in_minutes = cycle_duration_in_minutes
        pass

    cycle_start_time = datetime.now()


def measure_and_control():
    global last_temperature, last_humidity, cycle_started, current_cycle_duration_in_minutes
    global current_cycle_index, current_cycle_start_time

    next_check = datetime.now()
    next_log = datetime.now() + timedelta(seconds=10)
    for index, cycle in enumerate(cycle_config):
        current_cycle_index = index
        if not preheat_started:
            current_cycle_start_time = datetime.now()
            cycle.cycle_start_time = current_cycle_start_time
            app_log.info('cycle {} started on {} with duration of {}'
                         .format(index, current_cycle_start_time.now()
                                 .strftime("%Y-%m-%d %H:%M:%S"), cycle.cycle_duration_in_minutes))

        while cycle_started or preheat_started:
            current_cycle_duration_in_minutes = cycle.cycle_duration_in_minutes
            if cycle_started:
                if datetime.now() > cycle.cycle_start_time + timedelta(minutes=cycle.cycle_duration_in_minutes):
                    app_log.info('cycle {} completed on {}'.format(index, datetime.now().strftime("%Y-%m-%d %H:%M:%S")))
                    if index >= len(cycle_config):
                        cycle_started = False
                    break

            last_temperature = measure_temperature()
            last_humidity = measure_humidity()
            if datetime.now() > next_log:
                next_log = datetime.now() + timedelta(seconds=10)
                app_log.info("Temperature = {:.2f}, Humidity = {:.2f}".format(last_temperature[0], last_humidity[0]))

            if last_temperature[0] >= (cycle.max_temperature + TEMPERATURE_HYSTERESIS):
                set_temperature_element(False)
            elif last_temperature[0] <= (cycle.max_temperature - TEMPERATURE_HYSTERESIS):
                set_temperature_element(True)
            if last_humidity[0] >= (cycle.max_humidity + HUMIDITY_HYSTERESIS):
                set_humidity_element(False)
            elif last_humidity[0] <= (cycle.max_humidity - HUMIDITY_HYSTERESIS):
                set_humidity_element(True)

            while datetime.now() < next_check:
                sleep(0.1)  # Time in seconds.

            next_check = datetime.now() + timedelta(seconds=1)  # perform next check in 1 second
        set_temperature_element(False)
        set_humidity_element(False)

    cycle_started = False
    # ensure elements are turned off
    set_temperature_element(False)
    set_humidity_element(False)

# =======================
#     MENUS FUNCTIONS
# =======================


def main_menu_options():
    clear_screen()
    display_state()

    print "Please choose a menu option:"
    if cycle_started:
        print "1. N/A"
        print "2. Stop Cycle"
        print "3. N/A"
    else:
        print "1. N/A (set config using config.ini file)"
        print "2. Start Cycle"
        if preheat_started:
            print "3. Stop Preheating the Chamber"
        else:
            print "3. Start Preheating the chamber"
        # print "4. Read Temperature and Humidity"
        # print "5. Set GPIO pin"
    # print "8. Menu 2"
    if not cycle_started and not preheat_started:
        print "\n0. Quit"
    choice = raw_input(" >>  ")
    exec_menu(choice)
    return


# Execute menu
def exec_menu(choice):
    clear_screen()
    display_state()
    ch = choice.lower()
    if ch == '':
        mainmenu_actions['main_menu']()
    else:
        try:
            if cycle_started:
                if mainmenu_actions[ch] == cycle_toggle:
                    mainmenu_actions[ch]()                  # only allow a 'cycle_stop' while a cycle is in progress
            else:
                mainmenu_actions[ch]()
        except KeyError:
            print "Invalid selection, please try again.\n"
            mainmenu_actions['main_menu']()
    return


# Execute menu
def config_menu(choice):
    clear_screen()
    display_state()
    ch = choice.lower()
    if ch == '':
        configmenu_actions['config_menu']()
    else:
        try:
            configmenu_actions[ch]()
        except KeyError:
            print "Invalid selection, please try again.\n"
            configmenu_actions['config_menu']()
    return


def clear_screen():
    if os.name == 'nt':
        os.system('cls')
    else:
        os.system('clear')


def display_state():
    global current_cycle_start_time, current_cycle_index
    print '======================='
    print 'humitemp.py v0.02'
    print 'Configuration Settings:'
    print '======================='
    for index, cycle in enumerate(cycle_config):
        print '     Cycle {} Max Temperature     = {} deg Celcius'.format(index, cycle.max_temperature)
        print '     Cycle {} Max Humidity        = {} % '.format(index, cycle.max_humidity)
        print '     Cycle {} duration      = {} minutes'.format(index, cycle.cycle_duration_in_minutes)
    if cycle_started or preheat_started:
        print ''
        if cycle_started:
            now = datetime.now()
            tdelta = now - current_cycle_start_time
            minutes = tdelta.total_seconds() / 60
            perc = (minutes / current_cycle_duration_in_minutes) * 100
            print '============'
            print 'Cycle State:'
            print '============'
            print '     Cycle {} started on {}'\
                .format(current_cycle_index, current_cycle_start_time.strftime("%Y-%m-%d %H:%M:%S"))
            print '     Cycle {} running for {:.0f} minutes with {:.2f}% completed '.format(current_cycle_index, minutes, perc)
        else:
            print '==================='
            print 'Preheating Chamber:'
            print '==================='

        print '     Temperature = {:.1f} deg. Temperature element is {}'.\
            format(last_temperature[0], 'ON' if temperature_element_on else 'OFF')
        print '     Humidity = {:.1f}%. Humidity element is {}'.\
            format(last_humidity[0], 'ON' if humidity_element_on else 'OFF')

    print ''


def log_config():
    for index, cycle in enumerate(cycle_config):
        app_log.info('[configuration{}] - max_temperature = {}, max_humidity = {}, cycle_duration_in_minutes = {}'
                     .format(index, cycle.max_temperature, cycle.max_humidity, cycle.cycle_duration_in_minutes))


# Configuration Menu
def config_menu_options():
    # if not cycle_started:
    #     print "Configuration Menu:\n"
    #     print '1. Set Max Temperature in deg Celcius({})'.format(max_temperature_1)
    #     print '2. Set Max Humidity({})'.format(max_humidity_2)
    #     print '3. Set Cycle Duration in minutes({})'.format(cycle_1_duration_in_minutes)
    #     print "4. Save the configuration to file"
    #     print "9. Back"
    #     choice = raw_input(" >>  ")
    #     config_menu(choice)
    return


# Menu 2
def menu2():
    print "Hello Menu 2 !\n"
    print "9. Back"
    print "0. Quit"
    choice = raw_input(" >>  ")
    exec_menu(choice)
    return


def cycle_toggle():
    global cycle_started, current_cycle_start_time, control_thread, preheat_started
    cycle_started = not cycle_started
    if cycle_started:
        control_thread = threading.Thread(target=measure_and_control)
        current_cycle_start_time = datetime.now()
        control_thread.start()
        preheat_started = False


def preheat_toggle():
    global preheat_started, control_thread
    if cycle_started:
        print 'Preheat not allowed while a cycle is running'
        return
    preheat_started = not preheat_started
    if preheat_started:
        control_thread = threading.Thread(target=measure_and_control)
        control_thread.start()


# Back to main menu
def back():
    mainmenu_actions['main_menu']()


# Exit program
def exit_program():
    if not cycle_started and not preheat_started:
        sys.exit()


def set_temp():
    # global max_temperature_1
    # temp = -1
    # while temp < 0 or temp > 80:
    #     print "Enter the maximum value for the temperature (0-100 deg): "
    #     try:
    #         temp = int(raw_input(" >>  "))
    #     except:
    #         temp = -1
    #
    # max_temperature_1 = temp
    # clear_screen()
    # display_state()
    configmenu_actions['config_menu']()


def set_hum():
    # global max_humidity_2
    # hum = -1
    # while hum < 0 or hum > 95:
    #     print "Enter the maximum percentage value for the humidity (0-100): "
    #     try:
    #         hum = int(raw_input(" >>  "))
    #     except:
    #         hum = -1
    #
    # max_humidity_2 = hum
    # clear_screen()
    # display_state()
    # configmenu_actions['config_menu']()
    pass


def set_duration():
    # global cycle_1_duration_in_minutes
    # duration = -1
    # while duration < 0 or duration > 1440:
    #     print "Enter the cycle duration (0-1440 minutes): "
    #     try:
    #         duration = int(raw_input(" >>  "))
    #     except:
    #         duration = -1
    #
    # cycle_1_duration_in_minutes = duration
    # clear_screen()
    # display_state()
    # configmenu_actions['config_menu']()
    pass


def save_config():
    # config = ConfigParser()  # instantiate
    # config.read('config.ini')
    # # update existing values
    # config.set('configuration', 'max_temperature', max_temperature_1)
    # config.set('configuration', 'max_humidity', max_humidity_2)
    # config.set('configuration', 'cycle_duration_in_minutes', cycle_1_duration_in_minutes)
    # # save to  file
    # with open('config.ini', 'w') as configfile:
    #     config.write(configfile)
    # log_config()
    # print "Configuration saved to config.ini\n"
    # configmenu_actions['config_menu']()
    pass


def read_port(channel):
    fd = open('/proc/adc' + channel, 'r')
    fd.seek(0)
    value = int(fd.read(16)[5:])
    fd.close()
    return value


def read_temp_and_hum_pins():
    # fudge factor at approx 55deg is to minus 6.75% from value read
    while 1:
        temperature, avg_val, fudge_avg_val = measure_temperature()
        print "ADC Channel: {}, Raw Result: {}, Fudge Result: {}, Fudge Temp: {:.1f} deg".\
            format(TEMPERATURE_PORT, avg_val, fudge_avg_val, temperature)
        humidity, avg_val, fudge_avg_val = measure_humidity()
        print "ADC Channel: {}, Raw Result: {}, Fudge Result: {}, Fudge Humidity: {:.1f} %".\
            format(HUMIDITY_PORT, avg_val, fudge_avg_val, humidity)
        sleep(1)


def set_gpio_pin():
    try:
        pin = int(raw_input("Enter pin number >>  "))
        if 0 <= pin <= 17:  # up to 17 gpio pins on pcduino3
            choice = int(raw_input("1 for HIGH or 2 for LOW >>  "))
            if choice == 1:
                set_pin(pin, HIGH)
            elif choice == 2:
                set_pin(pin, LOW)
            else:
                print 'Invalid selection'
    except:
        print 'Invalid selection'

# =======================
#    MENUS DEFINITIONS
# =======================
# MainMenu definition
mainmenu_actions = {
    'main_menu': main_menu_options,
    # '1': config_menu_options,
    '2': cycle_toggle,
    '3': preheat_toggle,
    '4': read_temp_and_hum_pins,
    '5': set_gpio_pin,
    # '8': menu2,
    '0': exit_program,
}

# config definition
configmenu_actions = {
    'config_menu': config_menu_options,
    '1': set_temp,
    '2': set_hum,
    '3': set_duration,
    '4': save_config,
    '9': back,
}


def measure_temperature():
    # assuming a range of -30 to 70, then a factor of 40.95 units represents 1 deg, with an offset of -30
    # fudge factor at approx 55deg is to minus 6.75% from value read
    num_avg_values = 10
    values = []
    for x in range(0, num_avg_values):
        values.append(read_port(TEMPERATURE_PORT))
    avg_val = sum(values)/len(values)
    fudge_avg_val = avg_val - int(avg_val * TEMPERATURE_FUDGE_FACTOR)
    # print 'Temperature = {}'.format((fudge_avg_val / STEPS_PER_DEGREE) - TEMPERATURE_OFFSET)
    # ADC returns 0 - 4095  for a range of 0V to 3.3
    return (fudge_avg_val / STEPS_PER_DEGREE) - TEMPERATURE_OFFSET, avg_val, fudge_avg_val


def measure_humidity():
    # assuming a range of 0 - 100% RH, then a factor of 40.95 units represents 1 percentage of humidity
    num_avg_values = 10
    values = []
    for x in range(0, num_avg_values):
        values.append(read_port(HUMIDITY_PORT))
    avg_val = sum(values)/len(values)
    fudge_avg_val = avg_val - int(avg_val * HUMIDITY_FUDGE_FACTOR)
    # ADC returns 0 - 4095  for a range of 0V to 3.3
    return (fudge_avg_val / STEPS_PER_DEGREE), avg_val, fudge_avg_val,


def set_temperature_element(state):
    global temperature_element_on
    if state != temperature_element_on:
        temperature_element_on = state
        if temperature_element_on:
            set_pin(TEMPERATURE_CONTROL, LOW)
        else:
            set_pin(TEMPERATURE_CONTROL, HIGH)
        app_log.info('temperature element set to {}'.format('ON' if state else 'OFF'))


def set_humidity_element(state):
    global humidity_element_on
    if state != humidity_element_on:
        humidity_element_on = state
        if humidity_element_on:
            set_pin(HUMIDITY_CONTROL, LOW)
        else:
            set_pin(HUMIDITY_CONTROL, HIGH)
        app_log.info('humidity element set to {}'.format('ON' if state else 'OFF'))


def set_pin(pin, value):
    pin_file = open('/sys/devices/virtual/misc/gpio/pin/gpio' + str(pin), 'r+')
    pin_file.write(value)           # HIGH or LOW
    pin_file.close()


def configure_pins(pin_list, pin_type):
    for x in pin_list:
        # Now, let's make all the pins outputs...
        pin_file = open('/sys/devices/virtual/misc/gpio/mode/gpio' + str(x), 'r+')  # open the file in r/w mode
        pin_file.write(pin_type)                                                    # set the mode of the pin
        pin_file.close()                                                            # IMPORTANT- must close file

        set_pin(x, HIGH)                     # ...and finally make them HIGH.

# =======================
#      MAIN PROGRAM
# =======================
if __name__ == "__main__":
    # setup logging
    # log_formatter = logging.Formatter('%(asctime)s %(levelname)s %(funcName)s(%(lineno)d) %(message)s')
    log_formatter = logging.Formatter('%(asctime)s, %(levelname)s, %(message)s')
    logFile = 'temperature_and_humidity_log.txt'
    my_handler = RotatingFileHandler(logFile, mode='a', maxBytes=10 * 1024 * 1024,
                                     backupCount=10, encoding=None, delay=0)
    my_handler.setFormatter(log_formatter)
    my_handler.setLevel(logging.INFO)
    app_log = logging.getLogger('root')
    app_log.setLevel(logging.INFO)
    app_log.addHandler(my_handler)
    app_log.info("************* Temperature and humidity control Started *************")

    # configure GPIO pins
    if os.name != 'nt':
        configure_pins([0, 1, 2, 3, 4, 5, 6, 7], OUTPUT)

    # Load config from file
    config = ConfigParser()             # instantiate
    config.read('config.ini')             # parse existing file

    # read values from the "configuration" section
    cycle_config = []
    try:
        cycle_config.append(CycleConfig(
            True,
            config.getint('configuration0', 'max_temperature'),
            config.getint('configuration0', 'max_humidity'),
            config.getint('configuration0', 'cycle_duration_in_minutes'),
        ))
    except:
        app_log.info("Cycle 0 configuration error")

    try:
        cycle_config.append(CycleConfig(
            True,
            config.getint('configuration1', 'max_temperature'),
            config.getint('configuration1', 'max_humidity'),
            config.getint('configuration1', 'cycle_duration_in_minutes')
        ))
    except:
        app_log.info("Cycle 1 configuration error")

    log_config()

    # Launch main menu
    while True:
        main_menu_options()

# -------------------------------------------------------------------------------
# READING SPI Interface
#
# # module constants for the GPIO channels
#
# A2 = 25
# A1 = 24
# A0 = 23
#
# # make a list of requests to spead up readadc
# req=[[6,0,0], [6,64,0],[6,128,0],[6,192,0],[7,0,0], [7,64,0],[7,128,0],[7,192,0]]
#
# # list is based on 'r = spi.xfer2([6|(adcnum>>2),(adcnum&3)<<6,0])' which in turn was based on readadc12 from
# # http://git.agocontrol.com/apagg/agocontrol-work/blob
#           /93f7d9051aebf79c794e3d5fa4feea92c9221831/raspiMCP3xxxGPIO/MCP3208.py
#
# # choose the current SPI device (one of eight)
# def spi_device(devno):
#     GPIO.output(A2,(devno >> 2)&1)
#     GPIO.output(A1,(devno >> 1)&1)
#     GPIO.output(A0,devno & 1)
#
# # read SPI data from MCP3008 chip, 8 possible adc's (0 thru 7)
# def readadc(adcnum):
#     r = spi.xfer2(req[adcnum])
#     return ((r[1]&15)<<8) + r[2]
#
# # set up the demultiplexer
# GPIO.setmode(GPIO.BCM)
# GPIO.setup(A2, GPIO.OUT)
# GPIO.setup(A1, GPIO.OUT)
# GPIO.setup(A0, GPIO.OUT)
#
# spi = spidev.SpiDev()
#
# spi.open(0,1)   # port=0, CS=1
#
# spi.max_speed_hz=1953000  # any faster and we lose ADC resolution
#
# count = 1
#
#
#
# def measure_xxxx():
#
#     print "--------------------------"
#     print "SPI DEVICE", n, "SCAN ", count
#
#     spi_device(1)
#     mul = 0.002
#
#     csv = time.asctime()  # start each csv line with a time stamp
#
#     for chan in range(0, 8):
#         x = readadc(chan)
#         csv = csv + "," + str(x * mul)
#         print chan, x * mul
#
#     csv = csv + "\n"
#
#     f = open("csv.txt", "a")
#     f.write(csv)
#     f.close()
#
#     time.sleep(15)
#     count = count + 1


# ------------------------------------------------------------------------------------------------------
