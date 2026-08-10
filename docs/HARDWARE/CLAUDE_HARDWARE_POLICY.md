# CLAUDE HARDWARE POLICY

Project: GRUT
Status: MANDATORY

This document defines how Claude must reason about unknown hardware.

Failure to follow this policy is considered an architecture violation.

---

# PRIMARY RULE

Hardware is never inferred.

Hardware is only accepted after verification.

If hardware is not verified, implementation MUST stop and request
verification.

---

# TARGET BOARD

MCU

ESP8285

Flash

1 MB

Flash mode

DOUT

PlatformIO target

esp8285

Factory firmware

Available

Recovery

Verified

---

# VERIFIED INFORMATION

The following information is VERIFIED.

✓ ESP8285

✓ 1 MB Flash

✓ DOUT

✓ Boot mode

✓ PlatformIO target

✓ Factory firmware backup

✓ MAIN connector physically exists

✓ Unit Ctrl V1.2 PCB

---

# UNKNOWN INFORMATION

The following information is UNKNOWN.

MAIN pin order

MAIN connector purpose

MAIN UART routing

GPIO mapping

Voltage levels

Signal direction

Claude SHALL NEVER invent these values.

---

# CONNECTOR POLICY

Connector names SHALL NOT be interpreted.

Example:

Connector named

MAIN

does NOT imply

UART

Connector named

DEBUG

does NOT imply

SWD

Connector named

IO

does NOT imply

GPIO.

Connector labels are descriptive only.

Electrical verification is mandatory.

---

# REVERSE ENGINEERING POLICY

Claude shall always prefer:

1.

Continuity measurement

2.

PCB trace inspection

3.

Oscilloscope

4.

Logic analyzer

5.

UART boot log

Only after successful verification may documentation be updated.

---

# IMPLEMENTATION POLICY

Before writing code touching hardware:

read

CLAUDE.md

↓

read

ADR

↓

read

Hardware Baseline

↓

check VERIFIED facts

↓

IF information is UNKNOWN

STOP

request hardware verification

Do not continue implementation.

---

# CLAUDE BEHAVIOR UNDER UNCERTAINTY

When uncertain:

DO NOT WRITE CODE.

DO NOT CREATE SCHEMATICS.

DO NOT MAP GPIO.

Instead:

Explain exactly which measurement is required.

Wait for measurement.

Continue only after new evidence is received.

---

# DOCUMENTATION POLICY

Hardware documentation is append-only.

Every new verified fact must include:

Date

Verification method

Engineer

Evidence

Result

Nothing becomes VERIFIED without evidence.

---

# CURRENT VERIFIED HARDWARE

Board

Unit Ctrl V1.2

MCU

ESP8285

Factory firmware

Available

Flash

1 MB

Flash mode

DOUT

Bootloader

Verified

MAIN connector

Exists

Pinout

UNKNOWN

Purpose

UNKNOWN

UART

UNKNOWN

GPIO

UNKNOWN

---

# DEVELOPMENT GOAL

Reverse engineer the board.

Never guess.

Never infer.

Never copy assumptions from similar hardware.

Only verified information becomes part of GRUT.
