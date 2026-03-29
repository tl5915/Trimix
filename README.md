# Trimix Analyser #
Gas thermo conductivity based trimix analyser. No internal battery, powered via USB-C using phone or power bank, or Magsafe. No display, user interface accessed via html.

## Principles ##
### Oxygen ###
Galvenic O2 cell outputs voltage roughly between 8 to 70mV. This is read by ADS1115 16 bit analog-to-digital converter, with PGA gain set to 16 (256mV range). Rebreather O2 cells contains load resistor, no external load resistor required.
### Helium ###
MD62 is a thermo conductivity CO2 sensor, but could also measure gases with thermo conductivity lower than air, ie helium. The sensor contains 2 pairs of temperature-sensitive resistors, one enclosed in known gas (compensation side), one exposed to enviroment (measurement side). Apply voltage to heat up the resistors, heat will desipate quicker in measurement side when exposed to helium, changing resistance. The subtle difference in resistance between the two resistors is measured by a balanced wheastone bridge.
Manufacturer specification for power supply is 3.0 +/- 0.1V (attached datasheet is outdated and wrong). 3.3V power supply passing through a 1N5819 Schottky diode give just the right amount of forward voltage drop to get 2.9V. Do not use the MCU's internal 3.3V power supply, use external supply: MD62 can can drain up to 200mA.
Wheastone bridge build contains two 2k ohm resistors, and a 500 ohm potentiometer to adjust bridge balance. I use a DS3502 digital potentiometer (10k ohm), parallel connect to 510 ohm resistor to drop total resistence to 485 ohm. Bridge voltage is read between the centre pins of MD62 and DS3502 wiper, by ADS1115, with PGA gain set to 4 (1024mV range). Maximum voltage I have seen with pure helium is around 600mV so well within range.
Flow rate matters for thermal conductivity sensors: quickly the gas flow, quicker heat got taken away from the sensor. Ideal flow rate 0.5 to 2L/min. BCD inflator nipple to 1/4 NPT male adaptor has the perfect inner diameter to tap 3/8 UNF thread, so a low pressure port blank plug without o ring can be installed, and flow rate can be adjusted by how tight it is. Low pressure constant flow gas exit via hose barb to 1/4 NPT female adaptor. Several try and errors required to set the flow rate.
### Gas quality ###
I couldn't find any small enough carbon monoxide sensor, so opted for SGP40. SGP40 is a VOC sensor, but also cross react with carbon monoxide. This might be a better option, because the sources of CO found in cylinder are usually o ring combustion, or generator/engine fume, both containing VOC.
The sensor read out if not quantitantive, but relative. Turn on in air, reading should stablise around 50 to 100, if reading in analysed gas goes up -> bad, otherwise lower the number the better. Raw sensor resistance also provided on the UI for reference (higher the better, VOC and CO reduce resistance).
### Coomunication ###
All peripheries (ADS1115, DS3502, SGP40) are connected to Adafruit QT Py ESP32-C3 through I2C, via the Stemma QT connector. ESP32-C3 wifi is set to AP hotspot mode, connect to the wifi and read/calibrate via html.
### Power ###
USB-C panel mount connects to VCC and GND on QT Py ESP32-C3 for 5V USB power. Alternatively MagSafe receiver connects to the USB-C port on QT Py ESP32-C3 for wireless 5V power.

## Material ##
1. Housing to be 3D printed in STL folder
(optional M16 x 1.0 tap to smooth out O2 connector tube)
2. BCD nipple to 1.4 NPT male convertor (gas in)
(https://ebay.us/m/m2sN6S)
3. 3/8 UNF tap (tap on the NPT side of the above convertor to mount a o-ring-removed LP blanking plug to adjust flow rate to 1 L/min)
(https://amzn.eu/d/0449Hkix)
4. 1/4 NPT female to the smallest hose barb you can find, use barb convertor if necessary (ideally 2 mm ID silicone tubing connect to sensor chamber)
(https://ebay.us/m/jr4HbL)
5. Oxygen cell, any type (molex, 3.5mm jack, coaxial), choose connector accordingly
6. Wisen MD62 CO2 sensor
7. 5V to 3.3V buck converter
(https://amzn.eu/d/0gPikEVZ)
8. 1N5819 Schottky diode (drops the 3.3v to 3.0v that MD62 requires)
9. DS3502 digital potentiometer (10k ohm), parallel connect to 510 ohm resistor to drop total resistence to 485 ohm
(https://amzn.eu/d/0adkxJX8)
10. 2k ohm resistors for MD62 wheastone bridge
11. SGP40 gas sensor
(https://amzn.eu/d/06vTv9tU)
12. M2 screw to secure SGP40 onto sensor tube
13. JST-SH 1.0 4 terminal connectors for Stemma QT
(https://amzn.eu/d/09DHS9cc)
14. Adafruit ADS1115 ADC
(https://ebay.us/m/z7J8pF)
15. Adafruit QT Py ESP32-C3
16. USB-C panel mount
(https://amzn.eu/d/07lMHozl)
17. JST-XH 2.54 2 terminal connectors for power
18. Qi wireless charging receiver unit
(https://amzn.eu/d/08BN3vmo)
19. MagSafe ring
(https://amzn.eu/d/02FF3lX3)

## Circuit connection ##
### MD62 power supply ###
- ESP32-C3 5V pin connects to buck converter VIN, GND pin connects to buck converter Gnd
- MD62 pin 4 connects to buck converter GND
- Buck converter VOUT connects to 1N5819 anode
- 1N5819 cathode connects to MD62 pin 1 (confirm voltage is 2.9-3.1V)
### MD62 wheastone bridge ###
- 2k resistor connects between MD62 pin 1 and DS3502 RL pin
- 2k resistor connects between MD62 pin 4 and DS3502 RH pin
- 510 ohm resistor connects between DS3502 RL and RH pins
- Connect MD62 pin 2 and 3
### Sensor connection ###
- MD62 pin 2/3 node and DS3502 pin RW connect to ADS1115 pin 0 and 1
- Oxygen sensor +/- pins connect to ADS1115 pin 2 and 3
### I2C connection ###
- Use Stemma QT cable to connect ESP32-C3, ADS1115, DS3502, and SGP40
### Power connection ###
- Crimp USB-C panel mount cables on JST-XH 2 pin male connector
- Solder JST-XH 2 pin female connector on ESP32-C3 5V and GND pins

## Housing assembly ##
- Insert flow adjusted BCD nipple to 1/4 NPT male adaptor to mounting hole, wrap thread with PTFE tape, thrad in 1/4 NPT female to barb adaptor from inside the case, tigthen to secure on the case
- Insert USB-C mount into the mounting hole, secure with hot glue if required
- Insert MD62 (attached to ciruit board) into mounting hole on the sensor tube
- Insert SGP40 sensor unit into mounting hole on the sensor tube, secure SGP40 with M2 screw
- Drill 3mm hole in the middle of the sensor tube, insert silicone tubing
- Insert the sensor tube assembly into mounting hole, locate the mounting hole, drill 2.5mm hole on the mounting block, secure with M3 screw
- Connect the other end of the silicone tubing to barb connector on flow limitor
- Connect molex connector to the O2 cell, screw O2 cell into the end of sensor tube
- Mount ESP32-C3 unit on the lip, secure with bracket and two M3 screws
- Connect JST-XH power cable
- Connect USB-C terminal of Qi wireless receiver to ESP32-C3
- Slide Qi wireless receiver into the narrow slot
- Mount lid onto the case, secure with four M3 screws
- Cut MagSafe ring to match the slot on the outside of the case, secure with super glue

## Usage ##
- Power with USB-C or MagSafe charger
- IPhone: connect to the Trimix_Analyser wifi hotspot, password 12345678; browser open 192.168.4.1, add to home screen. Open the app on home screen
- Reverse helium polarity in setting page if helium voltage is negative
- Use pure helium or standard gas trimix for helium calibration
- Helium or oxygen percentage used for two point calibration can be edited in setting page
- SGP40 reading only valid after 1 minute initialisation. This eading is not in PPM, but relative to fresh air (100), lower (towards 1) the better quality, higher (towards 500) the worse quality. Volatile organic compounds, CO, and CO2 (all gases you don't want in your cylinder) increases reading.
- Upload bin file in setting page for OTA firmware update
- Browser open 192.168.4.1/upload to upload a app icon, this can be any 180 x 180 pixel png of your choice