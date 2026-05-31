# Interfaces

This document defines the minimum interfaces required for parallel development.

The goal is to allow each team member to develop their part without depending on the full implementation of the others.

## RTOS Interface

```cpp
void rtos_init();
void rtos_start();

int task_create(void (*task_function)(), int priority);
void delay_ms(uint32_t time_ms);

## Synchronization Interface

```cpp
void semaphore_init(int initial_value);
void semaphore_wait();
void semaphore_signal();

## Trace Interface

```cpp
void trace_init();
void trace_log(const char* event_name);
void trace_task_switch(const char* from_task, const char* to_task);
void trace_export();

## Control Interface

```cpp
void pid_init(float kp, float ki, float kd);
float pid_update(float setpoint, float measurement);

## Hardware Interface

```cpp
uint16_t sensor_read();
void actuator_set(float command);
void pwm_set_duty(float duty_cycle);

## Pneumatic Levitation Task

```cpp
void levitation_control_task();