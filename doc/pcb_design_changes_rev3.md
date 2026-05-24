# Hardware Design Changes - Revision 3 PCB Layout & Schematic

This document outlines the electrical and physical layout design changes required for **Revision 3** of the Paketkasten Modern Mainboard. These changes address the root causes of electromagnetic interference (EMI) and crosstalk identified during the analysis of Revision 2.

---

## 1. Physical Layout Changes

### 1.1 H-Bridge Placement (DRV8871)
*   **Problem**: In Rev 2, the H-bridge driver ([U6](pcb_hack_guide_rev2.md)) is placed far from the motor connector, resulting in long, high-current PWM traces running across the board parallel to the sensitive Hall sensor inputs.
*   **Change**: Move the H-bridge driver (**U6**, DRV8871) and its bypass/bulk capacitors to the far right side of the PCB, **directly adjacent to the motor terminals/connector**.
*   **Benefit**: This keeps the high-current, high-frequency output loop (`MOTOR_OUT1` and `MOTOR_OUT2`) extremely short (ideally under 1 inch), minimizing its loop area and preventing it from acting as an RF antenna.

### 1.2 Routing Long Logic-Level PWM Traces
*   **Problem**: Moving the H-bridge closer to the motor means the logic-level PWM lines (`MOTOR_IN1` and `MOTOR_IN2`) from the ESP32 to the H-bridge must travel a longer distance across the board.
*   **Change**:
    1. **Series Termination Resistors**: Place a small resistor (**$47\ \Omega$ to $100\ \Omega$**) in series with `MOTOR_IN1` and `MOTOR_IN2` **directly at the ESP32 output pins** (IO25 and IO26). This slows the sharp rise/fall times of the ESP32 GPIOs and prevents the long traces from radiating high-frequency RF noise.
    2. **Ground Plane Shielding**: Route the long logic-level traces directly over a solid, unbroken Ground copper plane on the opposite layer.
    3. **Pull-Down Resistors**: Place a **$10\text{ k}\Omega$ pull-down resistor** directly at the H-bridge inputs (`IN1` and `IN2` pins of U6) to keep the inputs from floating during MCU boot-up.

---

## 2. Schematic & Filtering Changes

### 2.1 Series Schmitt-Trigger Interlocks
*   **Problem**: In Rev 2, the configurable gates **U15** and **U16** (74LVC1G97) were placed in parallel with the Hall sensor inputs to act as a motor-block interlock. If unpopulated, the Hall signals connect directly to the ESP32 without any hardware filtering, buffering, or hysteresis.
*   **Change**:
    1. **In-Series Gating**: Route the Hall sensor signals through the Schmitt-trigger buffers (**U15** / **U16**) **in series** (e.g., `Sensor OUT` $\rightarrow$ `Schmitt Trigger` $\rightarrow$ `ESP32 Pin`).
    2. **Populate U15 & U16**: Ensure these buffers are populated in the final design to guarantee hysteresis and clean digital edges for the ESP32.
    3. **Add Buffer for HPAKET**: Add a third Schmitt-trigger buffer (e.g., a 74LVC1G17 or similar) for the `HPAKET` line, which was completely unbuffered in Rev 2.

### 2.2 Passive RC Low-Pass Filters
*   **Problem**: High-frequency coupled spikes from the motor H-bridge enter the ESP32/buffers completely unfiltered.
*   **Change**: Add a passive RC low-pass filter to each of the three Hall sensor signal lines (`HCLOSED`, `HPAKET`, `HBRIEF`) placed **close to the buffer/ESP32 inputs**:
    *   **Series Resistor**: $1\text{ k}\Omega$
    *   **Capacitor to Ground**: $1\text{ nF}$ to $10\text{ nF}$
    *   *RC Cutoff Frequency*: $\approx 15.9\text{ kHz}$ to $159\text{ kHz}$. This completely blocks high-frequency motor switching noise while passing physical switch transitions instantly.

### 2.3 Sensor Power Rail Isolation
*   **Problem**: The Hall sensors share the `+3.3V` rail directly with the ESP32, exposing them to supply voltage sag and ripple during WiFi transmissions.
*   **Change**: Isolate the Hall sensors' power supply using a small series resistor (e.g., $10\ \Omega$) or a ferrite bead (e.g., $600\ \Omega$ @ $100\text{ MHz}$) on the sensor VCC line, paired with a local $10\mu\text{F}$ decoupling capacitor.

### 2.4 Local Bulk Decoupling at the H-Bridge
*   **Problem**: The DRV8871 has no local bulk capacitance in Rev 2, leading to power rail sag and inductive ringing on `+8V2`.
*   **Change**: Place a bulk capacitor (**$10\mu\text{F}$ to $47\mu\text{F}$ electrolytic/tantalum**, rated for $\ge 16\text{V}$) directly adjacent to the VM pin of the `DRV8871` driver chip, in parallel with the $100\text{ nF}$ ceramic capacitor.

---

## 3. PCB Layout Best Practices

*   **Ground Planes**: Maintain a solid ground copper pour on both layers. Avoid slicing the ground plane under the sensitive Hall sensor traces.
*   **Route Crossings**: Never run the Hall sensor traces parallel to high-power traces. If they must cross, route them perpendicular ($90^\circ$) to each other.
*   **Star Grounding**: Connect the motor chassis/shield ground directly to the main power input ground, keeping it isolated from the signal ground plane of the microcontroller.
