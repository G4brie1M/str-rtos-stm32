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

/* Cenario que DISTINGUE EDF de Rate Monotonic (deadline = periodo).
 * Tarefa A: periodo 30 ticks, executa 10 ticks de CPU.
 * Tarefa B: periodo 50 ticks, executa 25 ticks de CPU.
 *
 * Em t=30 a B (liberada em t=0, deadline 50) ainda esta executando e a A e
 * re-liberada (deadline 60):
 *   - EDF: deadline da B (50) < deadline da A (60)  -> a B CONTINUA, sem troca.
 *   - RM : periodo da A (30) < periodo da B (50)    -> a A PREEMPTARIA a B.
 * O log g_schedLog prova qual ocorreu: se NAO houver troca em t=30, e EDF. */
const uint32_t PERIOD_A = 30U;
const uint32_t WORK_A   = 10U;
const uint32_t PERIOD_B = 50U;
const uint32_t WORK_B   = 25U;

uint32_t stack_blinky1[64];
rtos::OSThread blinky1;
void main_blinky1() {            // Tarefa A: periodo 30, executa 10
    while (1) {
    	conta0++;
    	rtos::OS_busyWork(WORK_A);    // ocupa a CPU por WORK_A ticks
    	rtos::OS_waitNextPeriod();    // dorme ate a proxima liberacao
    }
}

uint32_t stack_blinky2[64];
rtos::OSThread blinky2;
void main_blinky2() {            // Tarefa B: periodo 50, executa 25
    while (1) {
    	conta1++;
    	rtos::OS_busyWork(WORK_B);    // ocupa a CPU por WORK_B ticks
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

	  // Tarefa A (periodo 30, executa 10)
	  rtos::OSThread_start(&blinky1,
	                 PERIOD_A,
	                 &main_blinky1,
	                 stack_blinky1, sizeof(stack_blinky1));

	  // Tarefa B (periodo 50, executa 25)
	  rtos::OSThread_start(&blinky2,
	                 PERIOD_B,
	                 &main_blinky2,
	                 stack_blinky2, sizeof(stack_blinky2));

	  // blinky3 fica FORA deste teste: o cenario de 2 tarefas isola a decisao
	  // do EDF em t=30. A definicao continua no arquivo, mas nao e iniciada.

	  /* transfer control to the RTOS to run the threads */
	  rtos::OS_run();
}


