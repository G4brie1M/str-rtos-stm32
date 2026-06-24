/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

#include "main.h"
#include <cstdint>
#include "miros.h"

uint32_t conta0=0, conta1=0, conta2=0;

/* Conjunto de tarefas periodicas de teste para o escalonador EDF
 * Os periodos sao dados em ticks; TICKS_PER_SEC = 100, entao 1 tick = 10 ms
 * Eles sao DIFERENTES de proposito: com periodos iguais os deadlines empataria
 * e o EDF nao teria nada para ordenar, entao pareceria identico a round-robin */
const uint32_t PERIOD_T1 = 20U;  //200 ms
const uint32_t PERIOD_T2 = 40U;  //400 ms
const uint32_t PERIOD_T3 = 80U;  //800 ms

uint32_t stack_blinky1[40];
rtos::OSThread blinky1;
void main_blinky1() {
    while (1) {
    	conta0++; // job roda ate completar
    	rtos::OS_waitNextPeriod();// dorme ate proximo periodo
    }
}

uint32_t stack_blinky2[40];
rtos::OSThread blinky2;
void main_blinky2() {
    while (1) {
    	conta1++;
    	rtos::OS_waitNextPeriod();
    }
}

uint32_t stack_blinky3[40];
rtos::OSThread blinky3;
void main_blinky3() {
    while (1) {
    	conta2++;
    	rtos::OS_waitNextPeriod();
    }
}

uint32_t stack_idleThread[40];

int main(void)
{

	  rtos::OS_init(stack_idleThread, sizeof(stack_idleThread));

	  // comeca a thread blinky1 (200 ms)
	  rtos::OSThread_start(&blinky1,
	                 PERIOD_T1,
	                 &main_blinky1,
	                 stack_blinky1, sizeof(stack_blinky1));

	  //comeca a thread blinky2 (400ms)
	  rtos::OSThread_start(&blinky2,
	                 PERIOD_T2,
	                 &main_blinky2,
	                 stack_blinky2, sizeof(stack_blinky2));

	  // comeca a thread blinky3(800 ms)
	  rtos::OSThread_start(&blinky3,
	                 PERIOD_T3,
	                 &main_blinky3,
	                 stack_blinky3, sizeof(stack_blinky3));

	  /* transfer control to the RTOS to run the threads */
	  rtos::OS_run();
}


