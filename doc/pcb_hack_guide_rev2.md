# Hardware Hack Guide - Revision 2 PCB Stabilization

This document provides step-by-step instructions to modify the existing **Revision 2** PCB on the bench. These hardware hacks apply local filtering and shielding to suppress motor-generated electromagnetic interference (EMI) and stabilize the unbuffered Hall-effect sensor inputs.

---

## 1. Overview of Modifications

On the unmodified Rev 2 board, the Hall sensors are routed directly to the ESP32 over 9-inch traces without hardware low-pass filtering. Furthermore, the motor output lines run parallel to the sensor lines, inducing noise spikes that trigger false software interrupts.

We will apply three modifications to the hardware:
1. **Source Suppression**: Solder noise-filtering capacitors to the motor and ground the motor frame to the power input ground (Star Ground).
2. **Power Stabilization**: Solder a bulk decoupling capacitor directly at the H-bridge motor driver to absorb switching transients.
3. **Signal Filtering**: Insert passive RC low-pass filters in series with the Hall sensor inputs right before they enter the ESP32.

```
                  +-----------------------------------+
                  |        DC Motor Casing            |
                  |   [100nF across Terminals]        |
                  |  [2x 10nF Terminals to Case]      |
                  +-----------------+-----------------+
                                    |
                                    | Ground Wire
                                    v
                              [Power Ground]
                              (CN1 Pin 5 / C2)
                                    |
[Hall Sensor Output] ---> [1 kΩ Series Resistor] ---> [ESP32 Input Pin]
                                                 |
                                           [1 nF Capacitor]
                                                 |
                                              [GND]
```

---

## 2. Step-by-Step Modification Instructions

### Step 2.1: Motor Casing Shielding & Suppression
*   **Goal**: Clamp brush-arcing RF noise at the motor terminals and shunt it to the power input ground rather than contaminating the PCB's signal ground plane.
*   **Components Needed**: 
    *   $1 \times 100\text{ nF}$ ceramic capacitor (labeled "104")
    *   $2 \times 10\text{ nF}$ (or $1\text{ nF}$) ceramic capacitors (labeled "103" or "102")
    *   A length of stranded hookup wire.
*   **Procedure**:
    1. Solder the **$100\text{ nF}$ capacitor** directly across the positive ($M+$) and negative ($M-$) terminals of the DC motor.
    2. Scrape off a small patch of paint or oxidation on the metal motor chassis backplate to expose bare metal, and tin it with solder.
    3. Solder one **$10\text{ nF}$ capacitor** from the $M+$ terminal to the tinned spot on the motor chassis.
    4. Solder the second **$10\text{ nF}$ capacitor** from the $M-$ terminal to that same tinned spot on the motor chassis.
    5. Solder one end of the hookup wire to the tinned spot on the motor chassis.
    6. Route this wire back to the PCB and solder it **directly to the Ground pin of the power input connector ([CN1](wiegand-hardware.md) Pin 5)** or to the negative lead of the bulk input capacitor **C2** ($X = 10937\text{ mil}$, $Y = 5619\text{ mil}$).
    7. *Do not connect it to Pad 41 or the local signal ground near the sensors.*

*Alternative (using a Ferrite Bead)*: If routing a wire to the power input is difficult, you can connect the motor chassis wire to **Pad 41 (GND)**, but you must solder a **ferrite bead** (e.g., $100\ \Omega$ to $600\ \Omega$ @ $100\text{ MHz}$) in series with this wire to block high-frequency noise from entering the PCB ground plane.

---

### Step 2.2: Bulk Decoupling at the H-Bridge Driver
*   **Goal**: Prevent high-frequency voltage sags and inductive ringing on the `+8V2` motor power rail during PWM switching.
*   **Components Needed**:
    *   $1 \times 10\mu\text{F}$ (or $22\mu\text{F}$ / $47\mu\text{F}$) electrolytic or tantalum capacitor (rated for $\ge 16\text{V}$).
*   **Procedure**:
    1. Locate the motor driver chip **U6** (DRV8871) at $X = 12126\text{ mil}$, $Y = 6743\text{ mil}$ and its local bypass capacitor **C30** ($100\text{ nF}$) at $X = 12213\text{ mil}$, $Y = 6550\text{ mil}$.
    2. Solder the electrolytic capacitor directly in parallel with **C30**:
        * Connect the positive (+) lead of the capacitor to the `+8V2` pad of `C30`.
        * Connect the negative (-) lead of the capacitor to the `GND` pad of `C30`.

---

### Step 2.3: RC Low-Pass Filters on the Hall Sensor Inputs
*   **Goal**: Intercept high-frequency coupled spikes on the Hall sensor traces right before they enter the ESP32 input pins.
*   **Components Needed**:
    *   $3 \times 1\text{ k}\Omega$ (or $4.7\text{ k}\Omega$) resistors
    *   $3 \times 1\text{ nF}$ (or $10\text{ nF}$) ceramic capacitors
*   **Procedure**:
    1. Locate the traces leading to the ESP32 pin pads:
        * **`HCLOSED`**: ESP32 Pin 30 (IO17)
        * **`HPAKET`**: ESP32 Pin 29 (IO16)
        * **`HBRIEF`**: ESP32 Pin 31 (IO5)
    2. For each of these three traces, perform the following hack:
        * **Cut the Trace**: Using a sharp hobby knife, cut a $1\text{ mm}$ gap in the copper trace a few millimeters away from the ESP32 pin pad. Scrape off the green solder mask on both sides of the cut to expose bare copper, and tin them.
        * **Solder the Resistor**: Solder a **$1\text{ k}\Omega$ resistor** across the gap, bridging the cut.
        * **Solder the Capacitor**: Solder a **$1\text{ nF}$ capacitor** from the ESP32-pin-side of the resistor to a nearby Ground point (such as the outer shield of the ESP32 module or a nearby ground via).

```
[Hall Sensor Trace] ---> [ 1 kΩ Resistor ] ---> [ ESP32 Pin ]
                                            |
                                            +---[ 1 nF Capacitor ]---> [ GND ]
```

---

## 3. High-Frequency Cable Suppression (Toroids)
To prevent the motor wires from acting as transmitting antennas, wrap the motor power wires (`MOTOR_OUT1` / `MOTOR_OUT2`) together through a **clip-on ferrite core** (toroid or sleeve bead) as close to the PCB connector as possible. If wire length allows, loop them 2 to 3 times through the core.
