# Work Breakdown

## Person 1 - Kernel and Scheduler

Responsible for:

- Understanding the MiROS kernel structure
- Task creation and scheduling
- Context switch explanation
- Timing model
- Priority inheritance mechanism
- Kernel-related documentation

Expected outputs:

- Scheduler implementation/adaptation
- Timing model documentation
- Stack and context switch diagrams
- Priority inheritance explanation

## Person 2 - Trace, Producer/Consumer and Tests

Responsible for:

- Trace event system
- Producer/consumer example
- Test cases
- Timeline capture
- Trace analysis documentation

Expected outputs:

- Trace logger
- Producer/consumer demonstration
- Execution timeline logs
- Trace analysis section for README

## Person 3 - Hardware, Drivers and Control Demonstrator

Responsible for:

- STM32 hardware integration
- GPIO, timer, ADC and PWM usage
- PID control implementation
- Pneumatic levitation demonstrator
- Hardware documentation

Expected outputs:

- PID control task
- Sensor/actuator interface
- Pneumatic levitation test
- Hardware diagrams and setup documentation

## Integration Rule

All members must respect the shared interfaces defined in `docs/INTERFACES.md`.

Code should be integrated first into the development branch and only stable versions should go to the main branch.