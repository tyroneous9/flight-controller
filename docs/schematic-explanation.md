


## 1. Overview

The battery connects at J1 and goes to the power sheet. The power sheet makes 5 V. Three regulators turn the 5 V into three separate 3.3 V supplies. The first is 3.3V_MCU, which also feeds a filtered analog supply called 3.3VDDA. The second is 3.3V_Peripheral, which also feeds a filtered radio supply called VDD_RADIO. The third is 3.3V_L9907, for the motor drivers.

The battery voltage, VBATTERY, also goes straight to four motor driver circuits. Each one has an L9907 gate driver chip and 6 MOSFETs, and drives one motor on connectors J4 to J7.

The STM32G474 uC is in the middle. It sends 24 PWM signals to the motor drivers. It also talks to them over SPI and turns them on and off. It gets back 8 analog current readings and the fault signals. A watchdog chip and a flip-flop act as a safety latch.

The microcontroller uses a shared SPI bus, SPI4, to talk to the SX1262 LoRa radio and the MPU-6500 IMU. It uses I2C to talk to the LC29H GNSS module. The radio's antenna connects at J3 and the GNSS antenna at J2.

## 2. Power supply sheet

The battery plugs into J1, a 2-pin screw terminal (Phoenix 1729128). CR1 is an SS36 diode rated for 3 A and 60 V. It is probably there in case the battery is connected backwards. In that case, the diode blocks the current so nothing gets damaged.

### Step 1: battery to 5 V

U2 (TPS54202 family) is a buck converter. It turns a switch on and off hundreds of thousands of times per second. The 15 µH inductor (Würth 74439369150) and the output capacitors smooth this into a steady DC voltage.

Because it switches instead of turning extra voltage into heat, it is about 90% efficient. This matters when going down from 12 to 24 V.

R10 and R11 (100k and 13.3k) set the output voltage. The chip keeps its FB pin at about 0.596 V. So the output is 0.596 × (1 + 100k / 13.3k), which is about 5.08 V. This is the most common calculation in power design.

C13 is the bootstrap capacitor, connected between the BOOT and SW pins. The chip's internal switch needs a gate voltage higher than the input voltage. This capacitor charges up and then gets lifted each cycle to supply that higher voltage. The motor drivers use the same trick.

C15 (75 pF) sits across R10. It helps the regulator stay stable and react faster to changes in load.

R12 (49.9 ohms) is in the feedback path. It gives you a place to test the regulator's stability with a network analyzer. It's a nice extra.

The 10 µF capacitors on the input give the switch the bursts of current it needs. The ones on the output smooth out the ripple.

### Step 2: 5 V to three 3.3 V supplies

Three AP2112K-3.3 linear regulators (LDOs) each turn 5 V into 3.3 V. An LDO is simpler and quieter than a buck converter, but it turns the extra voltage into heat. The power lost is (5 − 3.3) V × current. At 300 mA that is about 0.5 W, which is fine for this small package.

There are three separate supplies so that noise from one part of the board doesn't reach another.

3.3V_MCU powers the microcontroller. It also goes through ferrite bead FB1 to make 3.3VDDA, a cleaner supply for the microcontroller's analog parts. ADC readings are only as accurate as their power supply is clean.

3.3V_Peripheral powers the IMU and GNSS. It also goes through FB2 to make VDD_RADIO, a cleaner supply for the radio.

3.3V_L9907 powers the logic in the four motor drivers and the safety latch.

TP1 to TP3 are test points. They are bare pads where you can attach a probe while testing.

## 3. MCU sheet: STM32G474QET6

This is an ARM Cortex-M4F running at 170 MHz, in a 128-pin package. ST made the G4 family for motor control and power electronics. It has three advanced timers (TIM1, TIM8 and TIM20) that create matched PWM pairs with built-in dead time. It also has a high-resolution timer (HRTIM), five fast ADCs, and two math helpers (CORDIC and FMAC) that speed up the sine, cosine and filter math used in motor control.

That is why this chip was picked. The board needs 24 PWM outputs: 4 motors with 6 switches each. They are named PWM_H1 to PWM_L3, plus _MOTOR2, _MOTOR3 and _MOTOR4 versions. The three advanced timers cover 3 motors, and HRTIM covers the fourth.

C1 to C8 (100 nF) are decoupling capacitors, one for each VDD pin. C9 (4.7 µF) stores extra charge for the whole chip.

VDDA is the analog supply pin. It has C26 and C27 and is fed from the filtered 3.3VDDA supply. R13 (10 ohms) and those capacitors form a small filter that cleans it up further.

VREF+ is the reference voltage for the ADC. Every ADC reading is measured against it, so a clean VREF+ means accurate readings.

R104 (10k) probably pulls the BOOT0 pin low. That makes the chip start from its own program in flash instead of its built-in bootloader.

The microcontroller connects to the other sheets through these signals. For the motors, it has 24 PWM outputs, 8 current readings named CURRENT_SENSE_IB1 and IB2, chip selects CS_L9907_1 to 4, fault inputs FS_FLAG_1 to 4, and BST_DISABLE_L9907 signals. For safety, it has WDI (the watchdog signal), MCU_RST, RESET_EN2 and MCU_CLEAR. For the radio and sensors, it has the SPI4 signals, SPI_CS4_SX1262, IRQ_SX1262, SX1262_BUSY, RESET_SX1262, ANT_SW, I2C_SDA and SCL for the LC29H, RESET_LC29H, LC29_WAKEUP, ANTENNA_ON_LC29H and MPU_SuspendMode.

There is no external crystal on the oscillator pins (PF0 and PF1). So the chip probably runs on its internal 16 MHz clock, sped up by its PLL. That is fine for motor control. The internal clock is less accurate than a crystal, which matters for USB or CAN, but this board uses neither.

## 4. Motor controller sheet
A brushless motor has three wires, one for each coil (phase). Each wire connects to a half-bridge. A half-bridge has two MOSFETs. The high-side one connects the wire to battery plus. The low-side one connects it to ground.

Three half-bridges make a 3-phase inverter, which has six MOSFETs in total. The microcontroller switches these six MOSFETs on and off in a pattern. This creates a rotating magnetic field that pulls the motor around. The best method for this is called FOC (Field-Oriented Control).

The most important rule is to never turn on both MOSFETs in the same half-bridge at once. That connects battery plus straight to ground, which is called shoot-through. To prevent it, the hardware adds a short delay, called dead time, of a few hundred nanoseconds between one switch turning off and the other turning on.

This sheet has four of these inverters, one for each motor connector, J4 to J7. Each connector is a 3-pin Molex, with one pin per phase.

### Each motor channel

U9 to U12 are ST L9907 chips. They are automotive gate drivers for 3-phase motors. The microcontroller's 3.3 V PWM pins are too weak to drive large MOSFETs directly. The L9907 turns each PWM signal into a strong gate signal of around 10 V. It also makes the extra-high gate voltage for the high-side MOSFETs using bootstrap capacitors on its CBS pins. This is the same trick as in the buck converter. It also watches for problems like too much current, low voltage and overheating. It reports these on FS_FLAG. You set it up over SPI using its SDI, SDO, SCK and CS pins.

The pin names tell you how the circuit is wired. GHS and SHS are the gate and source of the high-side MOSFET for each phase. GLS and SLS are the gate and source of the low-side MOSFET. VB is the battery supply. VDH measures the high-side drain voltage to detect short circuits. IS1 and IS2 are two built-in current amplifiers. They take the tiny voltage from a shunt resistor and output a voltage the ADC can read, on IB1 and IB2. EN1 and EN2 are enable pins. Both must be high for the driver to work.

There are 24 STL325N4LF8AG MOSFETs. They are 40 V N-channel parts with less than 1 milliohm of resistance when on. They come in PowerFLAT 5×6 packages, where pins 1 to 3 are source, pin 4 is gate, and pins 5 to 8 are drain. The very low resistance means they stay cool even at tens of amps. The 40 V rating means the battery is probably 12 V, or maybe 24 V with little safety margin.

Every MOSFET has the same set of parts on its gate. This pattern is worth remembering.

First, 10 ohm and 6.8 ohm resistors plus a BAT54J diode. The resistors slow the switching down a little, which cuts down ringing and electrical noise. The diode sits across one resistor, so turning on goes through about 10 ohms while turning off takes a faster path. Turning off fast is the safe choice, because it lowers the chance of both MOSFETs in a half-bridge being on at once.

Second, a 100k resistor from gate to source. It keeps the MOSFET off when the driver has no power or the gate is not connected. Without it, the gate could pick up noise and partly turn on, and the MOSFET could burn.

Third, a BZX55C12 12 V Zener diode from gate to source. It limits the gate voltage. A gate is damaged above about 20 V, so this diode keeps any spike under about 12 V.

There are 10 milliohm shunt resistors (R42, R43, R62, R63, R82, R83, R102 and R103), two for each motor, in the low-side paths. Motor current through a shunt makes a small voltage. Using V = I × R, 10 A × 0.01 ohms gives 100 mV. The L9907 amplifies this voltage. Then 220 ohm and 1 nF filters (R106 to R113 and C96 to C103) clean it up. The result goes to the microcontroller's ADC as CURRENT_SENSE_IB.

Only two of the three phases are measured. The three currents always add up to zero, so you can work out the third one. Also watch how much heat the shunts make. Using P = I² × R, 20 A gives 4 W in one tiny resistor.

Each driver also has a BCP56 transistor, an STPS2H100 diode and some capacitors. These are part of the L9907's power supply circuit. The exact wiring can't be confirmed from the text export alone, so compare it with the example circuit in the L9907 datasheet. There is no boost inductor on this sheet, and the microcontroller controls BST_DI (boost disable). So the driver's built-in boost converter is probably not used.

### The safety chain

U13 is a TPS3813K33-Q1. It is a window watchdog and a voltage monitor. The firmware must toggle the WDI pin on a regular schedule. It can't be too slow, and it can't be too fast either. The "too fast" check catches firmware that is stuck in a loop but still toggling the pin. If the timing is wrong, or the 3.3 V supply drops, the chip resets the microcontroller through MCU_RST. C95 (250 pF) sets how long the timeout is.

U15 is a CD4013 chip with two flip-flops. It acts as a hardware latch. Its outputs, RESET_EN1 and RESET_EN2, drive the enable pins of all four L9907 drivers. When there is a fault or a reset, the latch turns off the gate drivers. They stay off until the microcontroller clears the latch with MCU_CLEAR. The Italian note says that after a fault, the enable pins must go low for at least 3 µs to restart the driver, and that both enable pins already have built-in pull-down resistors. The flip-flop makes sure this happens in hardware.

The main lesson is to never rely on firmware alone to stop the motors. If the code crashes while the PWM outputs are stuck on, separate hardware must shut the motors down. It works like a kill switch that keeps working even when your program has crashed.

## 5. SX1262 & Peripherals sheet

### SX1262 LoRa radio (U7)

LoRa is a radio that reaches far but sends data slowly. It can reach several kilometers at a few kbps. That makes it good for sending status data and remote control commands. The microcontroller talks to it over SPI4. NSS is the chip select. BUSY tells you the radio is still working on your last command, and you must wait for it to go low before sending another. DIO1 is an interrupt pin that signals events like a packet arriving or a send finishing. NRESET resets the radio.

Y2 (Würth 830069390) is the 32 MHz clock source. The radio's frequency is only as accurate as this part.

The 15 µH inductor on DCC_SW belongs to the radio's own small built-in buck converter, which saves power. C49 (470 nF) on VREG is its output capacitor.

U8 (pSemi PE4259, marked "4259-63") is an RF switch. The radio's transmit output (RFO) and receive input (RFI_P and RFI_N) share one antenna. The radio's DIO2 pin flips this switch automatically when it changes between sending and receiving.

D1 (SP0402) protects the antenna line from static shocks. It sits before J3, the U.FL connector for the external antenna.

### RF basics

At 868 MHz, a PCB trace is no longer just a wire. Everything is designed for 50 ohms, the standard for antennas and cables. If the impedance doesn't match, power bounces back instead of going out the antenna.

The transmitter also makes harmonics, which are unwanted signals at 2 and 3 times the main frequency. The rules don't allow sending these out, so they must be filtered. The designer's notes show this work in four parts.

The first part is the VR_PA feed, which acts as a notch filter. An inductor plus C51 and C52 pass DC power to the radio's power amplifier while stopping RF from leaking into the power supply. The inductor and capacitors resonate at f = 1 / (2π × √(LC)), which is about 1750 MHz. This is on purpose. It is close to the 2nd harmonic (2 × 868 = 1736 MHz), so it blocks that harmonic. C54 can be changed to fine-tune it. These capacitors use a C0G material, which doesn't change with temperature. That is important for RF tuning.

The second part is a DC-blocking capacitor. It must let RF through but block DC. Its impedance, 1 / (2πfC), must be under about 5 ohms. That means it must be bigger than 36.7 pF. The notes pick 47 pF.

The third part is a low-pass filter made of a capacitor, an inductor and another capacitor. It is a 3rd-order Butterworth filter with a cutoff of 1.3 GHz at 50 ohms, using the values 1, 2 and 1 from filter tables. Each capacitor is 1 / (2π × 1.3 GHz × 50), which is about 2.4 pF. These are C60 and C61. The inductor is 2 × 50 / (2π × 1.3 GHz), which is about 12 nH. The filter lets 868 MHz through and weakens everything above 1.3 GHz. RF engineers look these formulas up in standard tables instead of working them out.

The fourth part is a "Matching circuit, DNM" section. These are empty pads that you fill in while tuning with a network analyzer, because real boards never match the math exactly.

### LC29H GNSS module (U6, Quectel LC29HAA)

This module finds position using satellites on two frequency bands (L1 and L5). It talks to the microcontroller over I2C. It also has RESET_N, WAKEUP and a signal to turn the antenna on.

The module powers an active antenna. VDD_RF sends DC up the antenna cable through a 68 nH inductor. This setup is called a bias tee. The inductor lets DC through to power the amplifier inside the antenna, but it blocks the satellite signal coming back down the cable. Q9 and Q10 (BSS84, P-channel MOSFETs) look like power switches for the antenna and the module.

FL1 (B39162B8389P810) is a SAW filter in the antenna path. It blocks signals outside the GNSS bands. The LoRa radio on this same board is exactly the kind of signal it needs to block.

Q7, Q8 and Q11 (BSS138, N-channel MOSFETs) are level shifters. The module's pins run at 2.8 V (VDD_EXT_2.8), but the microcontroller runs at 3.3 V. These small MOSFETs safely convert signals between the two voltages.

### MPU-6500 IMU (U4)

This chip measures acceleration and rotation on 3 axes each. It shares the SPI4 bus with the radio and has its own chip select. Its REGOUT pin needs a small capacitor for the chip's built-in regulator. The MPU_SuspendMode signal seems to control a switch that cuts power to the IMU.

## 6. Need to check


There may not be enough capacitance on VBATTERY. Each bridge only has 1 µF and 100 nF capacitors nearby. Motor drivers usually need hundreds of µF from large electrolytic or polymer capacitors. These absorb the big current bursts and voltage spikes from the motors.

There is no fuse or TVS diode on the battery input. A battery can push hundreds of amps into a short circuit. Motors also make voltage spikes when braking.

Check the voltage limits. The buck converter can take up to 28 V and the MOSFETs up to 40 V. That is fine for a 12 V battery but tight for 24 V.

BST_DISABLE_L9907_4 appears twice on the MCU sheet. Two pins with the same name are connected together. This might be a copy-paste mistake, and one of them should probably be _1.

C59 is 3.3 pF in the schematic, but the notes calculate 47 pF for the DC-blocking capacitor. One of these is wrong.

The MPU-6500 is not recommended for new designs. Consider a newer TDK InvenSense part, such as the ICM-42xxx series.

One SPI bus is shared by everything. SPI4 connects the radio, the IMU and all four motor drivers. This works, but radio traffic can delay messages to the motor drivers. Plan your firmware timing carefully, using DMA and priorities.

A small labeling issue: the MCU sheet and the Motor controller sheet are both labeled "3/5".
