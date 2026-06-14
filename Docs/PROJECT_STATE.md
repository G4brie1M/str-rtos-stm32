# Project State

## Project

Real-Time Systems project using MiROS, STM32 and C++.

## Official Scope

- Language: C++
- RTOS base: MiROS
- Target: STM32G474
- Demonstrator: pneumatic levitation
- Control strategy: PID
- Development type: group project

## Active Architecture

The project is based on the original MiROS STM32CubeIDE structure.

Main project directories:

- `Core/`: application, MiROS source and STM32 initialization code
- `Drivers/`: STM32 HAL and CMSIS drivers
- `docs/`: technical documentation
- `hardware/`: pneumatic levitation demonstrator documentation
- `presentation/`: final presentation material
- `tests/`: validation logs and test material

## Important Restrictions

- Do not upload compiled binary files.
- Preserve the original MiROS/STM32CubeIDE structure whenever possible.

## Current Deadline Strategy

The project should be completed within 25 days.

The development priority is:

1. Repository hygiene and documentation
2. MiROS understanding and integration
3. Scheduler, timing and synchronization
4. Trace system
5. Pneumatic levitation PID demonstrator
6. Final README and presentation