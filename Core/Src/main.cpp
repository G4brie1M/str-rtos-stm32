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
const uint32_t PERIOD_T1 = 30U;
const uint32_t PERIOD_T2 = 35U;
const uint32_t PERIOD_T3 = 50U;
//criação do buffer produtor/consumidor
#define BUFFER_SIZE 10 //inicialmente definido para 8 mas podemos analisar valores melhores

uint8_t fifo[BUFFER_SIZE];

uint8_t head = 0;
uint8_t tail = 0;

//definição dos semaforos (usados na fase 3)
rtos::OSSemaphore empty;  // semáforo de posições livres no buffer bloqueia produtor se zerar
rtos::OSSemaphore full; // semáforo de posições ocupadas bloqueia consumidor se zerar
rtos::OSSemaphore mutex; //usado pra exclusão mutua

uint32_t stack_blinky1[40];
rtos::OSThread blinky1;

//configurado para produtor
void main_blinky1() {
    uint8_t value = 0; //inicia valores em 0

    while (1) { //repete indefinidamente

        rtos::OSSem_wait(&empty); // aguarda existir espaço livre no buffer
        rtos::OSSem_wait(&mutex);

        fifo[head] = value;
        head = (head + 1U) % BUFFER_SIZE;

        value++;
        conta0++;

        rtos::OSSem_signal(&mutex);
        rtos::OSSem_signal(&full);

        rtos::OS_waitNextPeriod();
    }
}

uint32_t stack_blinky2[40];
rtos::OSThread blinky2;


//configurada para consumidor
void main_blinky2() {
    uint8_t value;

    while (1) { //repete indefinidamente

        rtos::OSSem_wait(&full); // aguarda existir algum dado disponível
        rtos::OSSem_wait(&mutex);

        value = fifo[tail]; //pega valores do buffer
        tail = (tail + 1U) % BUFFER_SIZE;

        conta1 = value; //seta conta 1 para valor pego do buffer (como o produtor faz value++ vai crescer lineramente)

        rtos::OSSem_signal(&mutex);
        rtos::OSSem_signal(&empty);

        rtos::OS_waitNextPeriod();
    }
}

uint32_t stack_blinky3[40];
rtos::OSThread blinky3;

void main_blinky3() {
    while (1) {
    	conta2 = (head + BUFFER_SIZE - tail) % BUFFER_SIZE; //métrica de quantos elementos tem no buffer
    	rtos::OS_waitNextPeriod();
    }
}

uint32_t stack_idleThread[40];

int main(void)
{
	  //inicialização dos semaforos
	  rtos::OSSem_init(&empty, BUFFER_SIZE);
	  rtos::OSSem_init(&full, 0);
	  rtos::OSSem_init(&mutex, 1);

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


