# Flight controller schematic explanation

## 1. Overview

The battery connects at J1 and goes to the power sheet. The power sheet makes 5 V. Three regulators turn the 5 V into three separate 3.3 V supplies. The first is 3.3V_MCU, which also feeds a filtered analog supply called 3.3VDDA. The second is 3.3V_Peripheral, which also feeds a filtered radio supply called VDD_RADIO. The third is 3.3V_L9907, for the motor drivers.

The battery is 24 V. The battery voltage, VBATTERY, also goes straight to four motor controller circuits. Each one has an L9907 gate driver chip and 6 MOSFETs, and drives one 24 V brushless (BLDC) motor on connectors J4 to J7.

The STM32G474 uC is in the middle. It sends 24 PWM signals to the motor drivers. It also talks to them over SPI and turns them on and off. It gets back 8 analog current readings and the fault signals. A watchdog chip and a flip-flop act as a safety latch.

The microcontroller uses a shared SPI bus, SPI4, to talk to the SX1262 LoRa radio and the MPU-6500 IMU. It uses I2C to talk to the LC29H GNSS module. The radio's antenna connects at J3 and the GNSS antenna at J2.

A BQ40Z80 BMS chip watches the battery pack. It measures each cell's voltage and the pack current, and switches the pack's charge and discharge path on and off. It reports to the microcontroller over SMBus, on an I2C bus of its own.

## 2. Power supply sheet

The battery plugs into J1, a 2-pin screw terminal (Phoenix 1729128). CR1 is an SS36 diode rated for 3 A and 60 V. It is probably there in case the battery is connected backwards. In that case, the diode blocks the current so nothing gets damaged.

### Step 1: battery to 5 V

U2 is a TPS542021DDCR synchronous buck converter. It converts the battery voltage from the connector into the board's main rail, marked 5V. It accepts about 4.5 V to 30 V in, has an adjustable output, and can deliver up to 2 A.

It turns a switch on and off 500,000 times per second (500 kHz). The 15 µH inductor (Würth 74439369150) and the output capacitors smooth this into a steady DC voltage. Both switching MOSFETs, high-side and low-side, are built into the chip. "Synchronous" means the low-side switch is a MOSFET instead of a diode, which wastes less power.

Because it switches instead of turning extra voltage into heat, it is about 90% efficient. This matters when going down from 24 V to 5 V.

R10 and R11 (100k and 13.3k) set the output voltage. The chip keeps its FB pin at about 0.596 V. So the output is 0.596 × (1 + 100k / 13.3k), which is about 5.08 V. This is the most common calculation in power design.

C13 is the bootstrap capacitor, connected between the BOOT and SW pins. The chip's internal switch needs a gate voltage higher than the input voltage. This capacitor charges up and then gets lifted each cycle to supply that higher voltage. The motor drivers use the same trick.

C15 (75 pF) sits across R10. It helps the regulator stay stable and react faster to changes in load.

R12 (49.9 ohms) is in the feedback path. It gives you a place to test the regulator's stability with a network analyzer. It's a nice extra.

The 10 µF capacitors on the input give the switch the bursts of current it needs. The ones on the output smooth out the ripple.

### Step 2: 5 V to three 3.3 V supplies

Three AP2112K-3.3 linear regulators (LDOs) each turn 5 V into 3.3 V. Each one makes the supply for one kind of load: the microcontroller, the peripherals, and the motor drivers.

An LDO is simpler and quieter than a buck converter, because it doesn't switch. That is why the microcontroller is powered from an LDO: it needs a low-noise supply. The downside is that an LDO turns the extra voltage into heat. The power lost is (5 − 3.3) V × current. At 300 mA that is about 0.5 W, which is fine for this small package.

There are three separate supplies so that noise from one part of the board doesn't reach another.

- 3.3V_MCU powers the microcontroller. It also goes through ferrite bead FB1 to make 3.3VDDA, a cleaner supply for the microcontroller's analog parts. ADC readings are only as accurate as their power supply is clean.
- 3.3V_Peripheral powers the IMU and GNSS. It also goes through FB2 to make VDD_RADIO, a cleaner supply for the radio.
- 3.3V_L9907 powers the logic in the four motor drivers and the safety latch.

TP1 to TP3 are test points. They are bare pads where you can attach a probe while testing.

## 3. MCU sheet: STM32G474QET6

This is an ARM Cortex-M4F running at 170 MHz, in a 128-pin package. ST made the G4 family for motor control and power electronics. It has three advanced timers (TIM1, TIM8 and TIM20) that create matched PWM pairs with built-in dead time. It also has a high-resolution timer (HRTIM), five fast ADCs, and two math helpers (CORDIC and FMAC) that speed up the sine, cosine and filter math used in motor control.

That is why this chip was picked. The board needs 24 PWM outputs: 4 motors with 6 switches each. They are named PWM_H1 to PWM_L3, plus _MOTOR2, _MOTOR3 and _MOTOR4 versions. The three advanced timers cover 3 motors, and HRTIM covers the fourth.

C1 to C8 (100 nF) are decoupling capacitors, one for each VDD pin. C9 (4.7 µF) stores extra charge for the whole chip.

VDDA is the analog supply pin. It has C26 and C27 and is fed from the filtered 3.3VDDA supply. R13 (10 ohms) and those capacitors form a small filter that cleans it up further.

VREF+ is the reference voltage for the ADC. Every ADC reading is measured against it, so a clean VREF+ means accurate readings.

R104 (10k) probably pulls the BOOT0 pin low. That makes the chip start from its own program in flash instead of its built-in bootloader.

The microcontroller connects to the other sheets through these signals:

- Motors: 24 PWM outputs, 8 current readings named CURRENT_SENSE_IB1 and IB2, chip selects CS_L9907_1 to 4, fault inputs FS_FLAG_1 to 4, and BST_DISABLE_L9907 signals.
- Safety: WDI (the watchdog signal), MCU_RST, RESET_EN2 and MCU_CLEAR.
- Radio and sensors: the SPI4 signals, SPI_CS4_SX1262, IRQ_SX1262, SX1262_BUSY, RESET_SX1262, ANT_SW, I2C_SDA and SCL for the LC29H, RESET_LC29H, LC29_WAKEUP, ANTENNA_ON_LC29H and MPU_SuspendMode.

There is no external crystal on the oscillator pins (PF0 and PF1). So the chip probably runs on its internal 16 MHz clock, sped up by its PLL. That is fine for motor control. The internal clock is less accurate than a crystal, which matters for USB or CAN, but this board uses neither.

## 4. Motor controller sheet

A brushless motor has three wires, one for each coil (phase). Each wire connects to a half-bridge. A half-bridge has two MOSFETs. The high-side one connects the wire to battery plus. The low-side one connects it to ground.

Three half-bridges make a 3-phase inverter, which has six MOSFETs in total. The microcontroller switches these six MOSFETs on and off in a pattern. This creates a rotating magnetic field that pulls the motor around. The method used here is called FOC (Field-Oriented Control).

The FOC is sensorless. The motors have no Hall sensors to report the rotor's position and speed. Instead, the firmware works them out with math. It uses the measured phase currents, the applied voltages and a model of the motor to estimate where the rotor is. This saves parts and wiring, but the estimate is poor at very low speed, so the firmware needs a startup routine to get the motor spinning first.

The firmware doesn't need to be written from scratch. Existing FOC libraries already implement sensorless control, and this project can use them or follow their examples. For example, ST's Motor Control SDK supports the STM32G4.

The most important rule is to never turn on both MOSFETs in the same half-bridge at once. That connects battery plus straight to ground, which is called shoot-through. To prevent it, the hardware adds a short delay, called dead time, of a few hundred nanoseconds between one switch turning off and the other turning on.

This sheet has four motor controllers, one for each 24 V BLDC motor on connectors J4 to J7. Each connector is a 3-pin Molex, with one pin per phase.

### Each motor channel

U9 to U12 are ST L9907 chips. They are automotive gate drivers for 3-phase motors. Each one drives 6 MOSFETs, one half-bridge per phase. The microcontroller's 3.3 V PWM pins are too weak to drive large MOSFETs directly. The L9907 turns each PWM signal into a strong gate signal of around 10 V. It also makes the extra-high gate voltage for the high-side MOSFETs using bootstrap capacitors on its CBS pins. This is the same trick as in the buck converter. It also watches for problems like too much current, low voltage and overheating. It reports these on FS_FLAG. You set it up over SPI using its SDI, SDO, SCK and CS pins.

The pin names tell you how the circuit is wired. GHS and SHS are the gate and source of the high-side MOSFET for each phase. GLS and SLS are the gate and source of the low-side MOSFET. VB is the battery supply. VDH measures the high-side drain voltage to detect short circuits. IS1 and IS2 are two built-in current amplifiers. They take the tiny voltage from a shunt resistor and output a voltage the ADC can read, on IB1 and IB2. EN1 and EN2 are enable pins. Both must be high for the driver to work.

There are 24 STL325N4LF8AG MOSFETs. They are 40 V N-channel parts with less than 1 milliohm of resistance when on. They come in PowerFLAT 5×6 packages, where pins 1 to 3 are source, pin 4 is gate, and pins 5 to 8 are drain. The very low resistance means they stay cool even at tens of amps. With a 24 V battery, the 40 V rating leaves a margin of 16 V for voltage spikes.

Each MOSFET has the same parts on its gate. This pattern is worth remembering.

1. A charging resistor (10 ohms) that the gate turns on through. It slows the switching down a little, which cuts down ringing and electrical noise. Its value isn't fixed by calculation. It needs to be tuned on the real board. Watch the gate waveform on an oscilloscope and change the resistor until the waveform looks clean. The right value is always somewhere between 1 and 20 ohms.
2. A discharging path made of a BAT54J diode and a resistor (6.8 ohms). When the driver pulls the gate low, the diode conducts. The gate charge then flows out quickly through this path and the IC to ground, so the gate drops to 0 V fast. When turning on, the diode blocks, so the charge has to go through the charging resistor. Turning off fast is the safe choice, because it lowers the chance of both MOSFETs in a half-bridge being on at once.
3. A 100k pull-down resistor from gate to source. It keeps the MOSFET off when the driver has no power or the gate is not connected. Without it, the gate could pick up noise and partly turn on, and the MOSFET could burn.
4. On the high-side MOSFETs only, two two BZX55C12 12 V Zener diodes back to back between gate and source. They limit the gate voltage in both directions, so the gate can't go too far positive or too far negative compared with the source. A gate is damaged above about 20 V, so these diodes keep any spike under about 12 V. They matter most on the high side because the source there is the phase node, which jumps between 0 V and 24 V every switching cycle.

There are 10 milliohm shunt resistors (R42, R43, R62, R63, R82, R83, R102 and R103), two for each motor, in the low-side paths. Motor current through a shunt makes a small voltage. Using V = I × R, 10 A × 0.01 ohms gives 100 mV. The L9907 amplifies this voltage. Then 220 ohm and 1 nF filters (R106 to R113 and C96 to C103) clean it up. The result goes to the microcontroller's ADC as CURRENT_SENSE_IB.

Only two of the three phases are measured. The three currents always add up to zero, so you can work out the third one. Also watch how much heat the shunts make. Using P = I² × R, 20 A gives 4 W in one tiny resistor.

Each driver also has a BCP56 transistor, an STPS2H100 diode and some capacitors. These are part of the L9907's power supply circuit. The exact wiring can't be confirmed from the text export alone, so compare it with the example circuit in the L9907 datasheet. There is no boost inductor on this sheet, and the microcontroller controls BST_DI (boost disable). So the driver's built-in boost converter is probably not used.

### The safety chain

U13 is a TPS3813K33-Q1. It is a window watchdog and a voltage monitor. The firmware must toggle the WDI pin on a regular schedule. It can't be too slow, and it can't be too fast either. The "too fast" check catches firmware that is stuck in a loop but still toggling the pin. If the timing is wrong, or the 3.3 V supply drops, the chip resets the microcontroller through MCU_RST. C95 (250 pF) sets how long the timeout is.

U15 is a CD4013 chip with two flip-flops. It acts as a hardware latch. Its outputs, RESET_EN1 and RESET_EN2, drive the enable pins of all four L9907 drivers. When there is a fault or a reset, the latch turns off the gate drivers. They stay off until the microcontroller clears the latch with MCU_CLEAR. The Italian note says that after a fault, the enable pins must go low for at least 3 µs to restart the driver, and that both enable pins already have built-in pull-down resistors. The flip-flop makes sure this happens in hardware.

The main lesson is to never rely on firmware alone to stop the motors. If the code crashes while the PWM outputs are stuck on, separate hardware must shut the motors down. It works like a kill switch that keeps working even when your program has crashed.

## 5. SX1262 & Peripherals sheet

### SX1262 LoRa radio (U7)

The SX1262 is the radio chip. When sending, it turns digital data from the microcontroller into an RF signal. When receiving, it does the opposite. It works on sub-GHz frequencies from 150 MHz to 960 MHz, supports LoRa and FSK modulation, and can transmit at up to +22 dBm (about 160 mW).

LoRa is a radio that reaches far but sends data slowly. It can reach several kilometers at a few kbps. That makes it good for sending status data and remote control commands.

The microcontroller talks to it over SPI4. It uses SPI4_CLK, SPI4_MISO and SPI4_MOSI to set up the radio and to read and write radio data. NSS is the chip select. BUSY tells you the radio is still working on your last command, and you must wait for it to go low before sending another. DIO1 is an interrupt pin that signals events like a packet arriving or a send finishing. NRESET resets the radio.

Y2 (Würth 830069390) is the 32 MHz clock source. The radio's frequency is only as accurate as this part.

The 15 µH inductor on DCC_SW belongs to the radio's own small built-in buck converter, which saves power. C49 (470 nF) on VREG is its output capacitor.

U8 (pSemi PE4259, marked "4259-63") is an RF switch. The radio's transmit output (RFO) and receive input (RFI_P and RFI_N) share one antenna. The radio's DIO2 pin flips this switch automatically when it changes between sending and receiving.

D1 (SP0402) protects the antenna line from static shocks. It sits before J3, the U.FL connector for the external antenna.

### RF basics

At 868 MHz, a PCB trace is no longer just a wire. Everything is designed for 50 ohms, the standard for antennas and cables. If the impedance doesn't match, power bounces back instead of going out the antenna.

The transmitter also makes harmonics, which are unwanted signals at 2 and 3 times the main frequency. The rules don't allow sending these out, so they must be filtered. The designer's notes show this work in four parts.

1. The VR_PA feed, which acts as a notch filter. An inductor plus C51 and C52 pass DC power to the radio's power amplifier while stopping RF from leaking into the power supply. The inductor and capacitors resonate at f = 1 / (2π × √(LC)), which is about 1750 MHz. This is on purpose. It is close to the 2nd harmonic (2 × 868 = 1736 MHz), so it blocks that harmonic. C54 can be changed to fine-tune it. These capacitors use a C0G material, which doesn't change with temperature. That is important for RF tuning.
2. A DC-blocking capacitor. It must let RF through but block DC. Its impedance, 1 / (2πfC), must be under about 5 ohms. That means it must be bigger than 36.7 pF. The notes pick 47 pF.
3. A low-pass filter made of a capacitor, an inductor and another capacitor. It is a 3rd-order Butterworth filter with a cutoff of 1.3 GHz at 50 ohms, using the values 1, 2 and 1 from filter tables. Each capacitor is 1 / (2π × 1.3 GHz × 50), which is about 2.4 pF. These are C60 and C61. The inductor is 2 × 50 / (2π × 1.3 GHz), which is about 12 nH. The filter lets 868 MHz through and weakens everything above 1.3 GHz. RF engineers look these formulas up in standard tables instead of working them out.
4. A "Matching circuit, DNM" section. These are empty pads that you fill in while tuning with a network analyzer, because real boards never match the math exactly.

### LC29H GNSS module (U6, Quectel LC29HAA)

This is the GPS module. It gives the drone's position. It finds position using satellites on two frequency bands (L1 and L5). It talks to the microcontroller over I2C. It also has RESET_N, WAKEUP and a signal to turn the antenna on.

The module powers an active antenna. VDD_RF sends DC up the antenna cable through a 68 nH inductor. This setup is called a bias tee. The inductor lets DC through to power the amplifier inside the antenna, but it blocks the satellite signal coming back down the cable. Q9 and Q10 (BSS84, P-channel MOSFETs) look like power switches for the antenna and the module.

FL1 (B39162B8389P810) is a SAW filter in the antenna path. It blocks signals outside the GNSS bands. The LoRa radio on this same board is exactly the kind of signal it needs to block.

Q7, Q8 and Q11 (BSS138, N-channel MOSFETs) are level shifters. The module's pins run at 2.8 V (VDD_EXT_2.8), but the microcontroller runs at 3.3 V. These small MOSFETs safely convert signals between the two voltages.

### MPU-6500 IMU (U4)

The MPU-6500 is a 6-axis motion sensor. It contains a 3-axis accelerometer and a 3-axis gyroscope.

The accelerometer measures linear acceleration along X, Y and Z. Its range can be set to ±2 g, ±4 g, ±8 g or ±16 g. The gyroscope measures how fast the board rotates around X, Y and Z. Its range can be set to ±250, ±500, ±1000 or ±2000 degrees per second.

The chip only outputs raw acceleration and rotation rates. The microcontroller turns them into roll, pitch, yaw rate, the aircraft's rotation, and its movement and vibration. The attitude estimator combines these readings to track the drone's orientation. The flight-control algorithms then use that to decide how fast each motor should spin, and the FOC loops make the motors follow.

It shares the SPI4 bus with the radio and has its own chip select. Its REGOUT pin needs a small capacitor for the chip's built-in regulator. The MPU_SuspendMode signal seems to control a switch that cuts power to the IMU.

## 6. BMS sheet

The BMS (battery management system) watches the battery pack. U6 is a TI BQ40Z80 battery monitor chip. It talks to the microcontroller over SMBus (I2C_SDA_BMS and I2C_SCL_BMS). SMBus is a stricter version of I2C, so it connects to one of the microcontroller's I2C ports. That port is used only for the BMS, and nothing else shares the bus. This avoids conflicts with the GNSS module and keeps SMBus's stricter timing rules from causing problems with other devices.

### SMBus lines and configuration

SMBD (data) and SMBC (clock) are the BQ40Z80's SMBus pins. SMBus is a bus very similar to I2C. It uses the same two open-drain wires and pull-up resistors, and the same addressing. It adds a few stricter rules: a timeout if the clock is held low too long, and an optional error-check byte (PEC) at the end of each message. The STM32G4's I2C ports support SMBus directly, including PEC, so the microcontroller can talk to the BQ40Z80 with a normal I2C port.

The BQ40Z80 comes with its firmware already inside. It doesn't need a program, but it does need to be configured for this pack: the number of cells, the protection limits for voltage, current and temperature, the shunt value, and the gas-gauge settings. These settings are stored in the chip's data flash memory.

TI's usual way to do this is with their PC software (Battery Management Studio) and a USB-to-SMBus adapter (the EV2400). This board doesn't use that adapter. Instead, the microcontroller writes the configuration itself over the I2C line.

1. Unseal the chip by sending the security keys. The chip only accepts configuration changes when it is unsealed.
2. Write each data flash setting using the commands described in the BQ40Z80 Technical Reference Manual.
3. Read the settings back to check them.
4. Seal the chip again so the settings can't be changed by accident.

The same I2C line is then used in normal operation to read the cell voltages, current, temperature, state of charge and fault flags.

### Cell voltage inputs

The battery pack is made of cells connected in series. Their voltages add up to the pack voltage.

VC1, VC2, VC3 and so on are the chip's cell inputs. Each one connects to the point between two cells. The chip measures the voltage between neighboring inputs, so it can read the voltage of each cell separately.

Each of these lines has a resistor and a capacitor. Together they make an RC filter that removes noise before the voltage reaches the chip.

### Current measurement

SRP and SRN measure the voltage across the shunt resistor, R25 (1 mΩ). The pack current flows through the shunt, and V = I × R turns it into a small voltage. The chip's input range is −100 mV to +100 mV. At 75 A the shunt gives 75 mV, so that fits. The shunt makes P = I² × R = 75² × 0.001, which is about 5.6 W at 75 A, so it needs a power rating to match. The schematic note says the shunt has to be placed near the battery pack.

Between the shunt and the chip there is a filter on SRP and SRN. Each line has a series resistor (R = 100 Ω) and a capacitor to ground (Cc = 100 nF). A third capacitor (Cd) sits across the two lines.

This filter handles two kinds of noise differently. Differential noise makes the two lines move in opposite directions. This is the kind that looks like a real current reading. Common-mode noise makes both lines move up and down together.

For a differential signal, the current goes through both resistors and through Cd. The two Cc capacitors also act like one capacitor of Cc/2 across the lines. This gives:

```
τd   = R × (Cc + 2·Cd) = 100 Ω × (100 nF + 200.2 nF) = 100 Ω × 300.2 nF = 30.02 µs
fc,d = 1 / (2π × τd) = 1 / (2π × 30.02 µs) ≈ 5.3 kHz
```

For a common-mode signal, both ends of Cd move together, so no current flows through it. Each line is just its own R and Cc. Writing the current through R at the filter output, Vp:

```
(Va − Vp) / R = s × Cc × Vp   →   Vp = Va / (1 + s × R × Cc)
τc   = R × Cc = 100 Ω × 100 nF = 10 µs
fc,c = 1 / (2π × τc) = 1 / (2π × 10 µs) ≈ 15.9 kHz
```

So the filter cuts differential noise above about 5.3 kHz and common-mode noise above about 15.9 kHz.

### Temperature sensing

TS1 and TS2 are the chip's thermistor inputs. A thermistor is a resistor whose value changes with temperature. This board uses NTC thermistors, whose resistance goes down as they get hotter. The chip reads that resistance and works out the battery pack's temperature.

Each thermistor connects between its TS pin and GND. To measure the pack accurately, the thermistor has to sit on the pack itself, as close to the cells as possible, not on the board.

That is why the thermistors are not soldered on the board. Instead, TS1, TS2 and GND go to J2, a 4-pin connector. Each thermistor is on a cable. One wire goes to its TS pin and the other to GND. You tape the thermistor onto the battery pack, and the cable lets you move it around to wherever you want to measure.

### Charge and discharge FETs

All the pack current flows through two groups of MOSFETs in series: Q3 for charging and Q4 for discharging. The BMS turns them off to cut the pack off when something is wrong, such as a cell that is too high or too low, too much current, or a pack that is too hot. Because the two groups are in series, both carry the same current. Each group has 3 MOSFETs in parallel that share the current.

The MOSFET is the Infineon BSC010N04LS6. The design inputs are:

| Input | Value |
|---|---|
| Rds(on) max at 25 °C, Vgs = 10 V | 1.0 mΩ |
| Rth junction to PCB | 31.3 °C/W |
| Ambient temperature (Tamb) | 40 °C |
| Maximum design junction temperature (Tj) | 110 °C |
| Hot Rds(on) factor (assumed) | 1.5 |
| Continuous discharge current | 60 A |
| Peak discharge current | 80 A |
| Charge current | 20 A |
| Battery pack | 6 cells × 4.2 V = 25.2 V |

A MOSFET's resistance goes up as it gets hotter, so the calculation uses the hot value.

1. Hot Rds(on): Rds_hot = 1.0 mΩ × 1.5 = 1.5 mΩ.
2. Allowed temperature rise: ΔT = Tj_max − Tamb = 110 − 40 = 70 °C.
3. Maximum power per FET: Pmax = ΔT / Rth = 70 / 31.3 = 2.24 W.
4. Maximum current per FET: P = I² × R, so I = √(P / R) = √(2.24 / 0.0015) = √1491 = 38.6 A.
5. Number of FETs per group: at the 80 A peak, 80 / 38.6 = 2.07, which rounds up to 3. Q3 and Q4 each need 3, so there are 6 FETs in total.
6. Junction temperature check, using Tj = Tamb + P × Rth. With 3 FETs at 80 A, each carries 26.67 A. P = 26.67² × 0.0015 = 1.07 W, so Tj = 40 + 1.07 × 31.3 = 73.4 °C, well under 110 °C. With only 2 FETs, each would carry 40 A. P = 2.4 W, so Tj = 115 °C. This is over the limit, which is why 3 are needed.
7. Power with 3 FETs per group:

   | Case | Current per FET | Power per FET | Total, all 6 FETs |
   |---|---|---|---|
   | Continuous, 60 A | 20 A | 0.60 W | 3.6 W |
   | Peak, 80 A | 26.67 A | 1.07 W | 6.4 W |
   | Charge, 20 A | 6.67 A | 0.07 W | 0.4 W |

8. Voltage drop: one group of 3 has a resistance of 1.5 mΩ / 3 = 0.5 mΩ. Q3 and Q4 in series make 1.0 mΩ. At 60 A, the drop is 60 mV (30 mV per group). At 80 A, it is 80 mV.
9. Gate load: the BMS has to charge and discharge all 3 gates of a group at once. Total gate charge Qg = 3 × 67 nC = 201 nC. Total input capacitance Ciss = 3 × 4600 pF = 13.8 nF. A bigger gate load makes the FETs switch more slowly, which matters most when the BMS has to cut off a short circuit quickly.

### Gate parts on the BMS FETs

Each FET has a Zener diode from gate to source. It limits the gate voltage to protect the gate.

Each FET also has a 10 MΩ pull-down resistor from gate to source. It keeps the FET off when nothing is driving the gate.

Each FET has its own gate resistor, instead of one resistor shared by the whole group. There are two reasons for this:

- No two FETs are exactly the same. Each one has its own threshold voltage (Vth), which also changes with temperature. So when the gates are driven together, one FET starts conducting before the others.
- If the gates are tied together with one shared resistor, the paralleled FETs can oscillate with each other when they switch. Simulations show this clearly. The first spikes are very large, and then they get smaller but keep ringing for a while. A separate resistor on each gate damps this ringing and removes it.

### Precharge circuit

The load connected to the pack has a large capacitance, such as the capacitors on the motor drivers. An empty capacitor acts like a short circuit at first. If the main FETs turned straight on into it, a very large inrush current would flow, and this spike could damage the FETs.

The precharge circuit avoids this. It is a P-channel MOSFET (Q41) on the high side, in series with R140 (100 Ω). Before the main FETs turn on, Q41 turns on and charges the load capacitors slowly through R140. Once the capacitors are charged close to the pack voltage, the main FETs turn on with almost no inrush current.

R140 limits the precharge current. With a 25.2 V pack and empty capacitors, the starting current is 25.2 V / 100 Ω, which is about 0.25 A. At that moment the resistor has the full pack voltage across it, so it dissipates 25.2 V × 0.25 A, which is about 6.4 W. The current and power drop quickly as the capacitors charge, so this is only a short pulse. That is why the schematic note asks for a 3 W resistor and not a tiny one.

Q41 turns on when its gate is pulled to 0 V. A BSS138 N-channel MOSFET does this. When the BSS138 turns on, it connects Q41's gate to ground. When it turns off, a pull-up keeps Q41's gate at the source voltage, so Q41 stays off.

## 7. Need to check

- There may not be enough capacitance on VBATTERY. Each bridge only has 1 µF and 100 nF capacitors nearby. Motor drivers usually need hundreds of µF from large electrolytic or polymer capacitors. These absorb the big current bursts and voltage spikes from the motors.
- There is no fuse or TVS diode on the battery input. A battery can push hundreds of amps into a short circuit. Motors also make voltage spikes when braking.
- Check the voltage limits. The battery is 24 V. The buck converter can take up to 30 V and the MOSFETs up to 40 V. The buck converter margin is tight. A fully charged 24 V battery can sit above 25 V, and spikes from the motors braking can push it past 30 V.
- BST_DISABLE_L9907_4 appears twice on the MCU sheet. Two pins with the same name are connected together. This might be a copy-paste mistake, and one of them should probably be _1.
- C59 is 3.3 pF in the schematic, but the notes calculate 47 pF for the DC-blocking capacitor. One of these is wrong.
- The MPU-6500 is not recommended for new designs. Consider a newer TDK InvenSense part, such as the ICM-42xxx series.
- One SPI bus is shared by everything. SPI4 connects the radio, the IMU and all four motor drivers. This works, but radio traffic can delay messages to the motor drivers. Plan your firmware timing carefully, using DMA and priorities.
- A small labeling issue: the MCU sheet and the Motor controller sheet are both labeled "3/5".
