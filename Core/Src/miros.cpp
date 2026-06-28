/****************************************************************************
* MInimal Real-time Operating System (MiROS), GNU-ARM port.
*
* This software is a teaching aid to illustrate the concepts underlying
* a Real-Time Operating System (RTOS). The main goal of the software is
* simplicity and clear presentation of the concepts, but without dealing
* with various corner cases, portability, or error handling. For these
* reasons, the software is generally NOT intended or recommended for use
* in commercial applications.
*
* Copyright (C) 2018 Miro Samek. All Rights Reserved.
*
* SPDX-License-Identifier: GPL-3.0-or-later
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <https://www.gnu.org/licenses/>.
*
* Git repo:
* https://github.com/QuantumLeaps/MiROS
****************************************************************************/
#include <cstdint>
#include "miros.h"
#include "qassert.h"
#include "stm32g4xx.h"
#include "stm32g4xx_hal_uart.h"

//extern UART_HandleTypeDef huart2;

Q_DEFINE_THIS_FILE

namespace rtos {

OSThread * volatile OS_curr; /* pointer to the current thread */
OSThread * volatile OS_next; /* pointer to the next thread to run */

uint32_t volatile OS_tickCtr = 0U; //contador global de ticks

OSThread *OS_thread[32 + 1]; /* array of threads started so far */
uint32_t OS_readySet; /* bitmask of threads that are ready to run */

uint8_t OS_threadNum; /* number of threads started */
uint8_t OS_currIdx; /* current thread index for the circular array */


OSThread idleThread;

volatile TraceEvent traceBuffer[TRACE_SIZE];
volatile uint16_t traceHead = 0U;

void main_idleThread() {
    while (1) {
        OS_onIdle();
    }
}

void OS_init(void *stkSto, uint32_t stkSize) {
    /* set the PendSV interrupt priority to the lowest level 0xFF */
    *(uint32_t volatile *)0xE000ED20 |= (0xFFU << 16);

    /* inicia a thread idleThread.
     * period = 0 -> a idle NAO e periodica; o tick nunca a re-libera,
     * ela so executa quando nenhuma outra thread esta pronta. */
    OSThread_start(&idleThread,
                   0U,
                   &main_idleThread,
                   stkSto, stkSize);
}

void OS_sched(void) {
    if (OS_readySet == 0U) { /* idle condition? */
        OS_currIdx = 0U; /* the idle thread */
    } else {
        /* EDF: among the ready threads, pick the one whose absolute
         * deadline is the earliest (smallest absDeadline). */
        uint8_t  bestIdx = 0U;                 /* chosen thread index   */
        uint32_t bestDeadline = 0xFFFFFFFFU;   /* smallest deadline yet */
        uint8_t  n;

        for (n = 1U; n < OS_threadNum; n++) {
            /* is thread n ready? (its bit is set in OS_readySet) */
            if ((OS_readySet & (1U << (n - 1U))) != 0U) {
                if (OS_thread[n]->absDeadline < bestDeadline) {
                    bestDeadline = OS_thread[n]->absDeadline;
                    bestIdx = n;
                }
            }
        }
        OS_currIdx = bestIdx;
    }

    OS_next = OS_thread[OS_currIdx];

    /* trigger PendSV, if needed */
    if (OS_next != OS_curr) {
    	Trace_log(TRACE_SWITCH, OS_currIdx);//guarda esse evento
        *(uint32_t volatile *)0xE000ED04 = (1U << 28);
    }
}

void OS_run(void) {
    /* callback to configure and start interrupts */
    OS_onStartup();

    __disable_irq();
    OS_sched();
    __enable_irq();

    /* the following code should never execute */
    Q_ERROR();
}

void OS_tick(void) {
	OS_tickCtr = OS_tickCtr + 1U;//soma feita assim pra evita ro warning -Wvolatile do C++20
	uint8_t n = 0;
	for(n=1U;n<OS_threadNum; n++){ 				/* cycle through every thread but the idle */
		if(OS_thread[n]->timeout != 0U){
			OS_thread[n]->timeout--;			/* decrease the timeout */
			if(OS_thread[n]->timeout == 0U){
			      if(OS_thread[n]->period != 0U){ // so tarefas periodicas religam
			          OS_thread[n]->absDeadline = OS_tickCtr + OS_thread[n]->period; //define a deadline do novo trabalho
			          OS_thread[n]->timeout = OS_thread[n]->period;//reseta o timeout
			      }
			      Trace_log(TRACE_THREAD_READY, n);//guarda esse evento
			      OS_readySet |= (1U << (n-1U));
			  }
		}
	}
}

void OS_delay(uint32_t ticks) {
    __asm volatile ("cpsid i");

    /* never call OS_delay from the idleThread */
    Q_REQUIRE(OS_curr != OS_thread[0]);
    Trace_log(TRACE_DELAY, OS_currIdx);//guarda esse evento

    OS_curr->timeout = ticks;
    OS_readySet &= ~(1U << (OS_currIdx - 1U));
    OS_sched();
    __asm volatile ("cpsie i");
 }

/* Chamada por uma tarefa periodica no FIM de cada job ("run to completion").
 * Bloqueia a tarefa ate a sua proxima liberacao. Diferente de OS_delay, NAO
 * define timeout: OS_tick() ja armou a proxima liberacao (e o proximo
 * deadline absoluto) no momento em que este job foi liberado, entao aqui so
 * saimos do ready set e passamos a CPU para a tarefa de menor deadline */
void OS_waitNextPeriod(void) {
    __asm volatile ("cpsid i");//desabilita a interrupcao

    //nunca chame OS_waitNextPeriod a partir da idleThread
    Q_REQUIRE(OS_curr != OS_thread[0]);

    // bloqueia: limpa o ready bit desta tarefa. A ISR do tick o re-seta um
    //periodo apos a ultima liberacao
    OS_readySet &= ~(1U << (OS_currIdx - 1U));
    OS_sched();
    __asm volatile ("cpsie i");//reabilita a interrupcao
}

/* Cooperacao voluntaria (Fase 1): a tarefa em execucao cede a CPU para uma
 * reavaliacao do escalonador SEM se bloquear. Diferente de OS_delay e de
 * OS_waitNextPeriod, NAO mexe no timeout nem no ready bit: a tarefa continua
 * pronta. Sobre EDF, OS_sched so troca de contexto se houver outra tarefa pronta
 * com deadline absoluto menor ou igual (menor indice em caso de empate); se a
 * propria tarefa ainda for a de menor deadline, OS_next == OS_curr e nada
 * acontece. */
void OS_yield(void) {
    __asm volatile ("cpsid i");//desabilita a interrupcao
    Trace_log(TRACE_YIELD, OS_currIdx);//guarda esse evento
    OS_sched();
    __asm volatile ("cpsie i");//reabilita a interrupcao
}

/* Semaforo contador com fila de bloqueio (Fase 1):
 * Bloqueio PASSIVO: a thread sem credito sai do OS_readySet e cede a CPU; nao
 * ha espera ativa (busy-waiting). A "fila" e o bitmask waitSet do proprio
 * semaforo. Semantica de handoff direto: signal OU acorda um esperador OU
 * incrementa o contador, nunca os dois, o que evita perda de evento */

void OSSem_init(OSSemaphore *me, int32_t initial) {
    me->count   = initial;
    me->waitSet = 0U;
}

void OSSem_wait(OSSemaphore *me) {
    __asm volatile ("cpsid i");//regiao critica

    Trace_log(TRACE_SEM_WAIT, OS_currIdx); //guarda esse evento

    // a idleThread nunca pode bloquear
    Q_REQUIRE(OS_curr != OS_thread[0]);

    if (me->count > 0) {
        me->count--;// se tem credito: consome e segue
    } else {
        //sem credito: bloqueia a thread atual de forma passiva
        //entra na fila do semaforo e sai do conjunto de pronta
        me->waitSet |= (1U << (OS_currIdx - 1U));
        OS_readySet &= ~(1U << (OS_currIdx - 1U));
        Trace_log(TRACE_SEM_BLOCK, OS_currIdx);//guarda esse evento
        OS_curr->timeout = 0U; // nao e espera por tempo: o tick a ignora
        OS_sched(); //cede a CPU, retorna aqui ao ser acordada
    }
    __asm volatile ("cpsie i");
}

void OSSem_signal(OSSemaphore *me){
    __asm volatile ("cpsid i");//regiao critica

    Trace_log(TRACE_SEM_SIGNAL, OS_currIdx);//guarda esse evento

    if (me->waitSet != 0U) {
        // tem thread(s) esperando: acorda a de MENOR deadline absoluto
        //(consistente com o EDF) e passa o recurso direto (count intacto)
        uint8_t wakeIdx = 0U;
        uint32_t best = 0xFFFFFFFFU;
        uint8_t n;
        for (n = 1U; n < OS_threadNum; n++) {
            if ((me->waitSet & (1U << (n - 1U))) != 0U) {
                if (OS_thread[n]->absDeadline < best) {
                    best = OS_thread[n]->absDeadline;
                    wakeIdx = n;
                }
            }
        }
        me->waitSet &= ~(1U << (wakeIdx - 1U));//sai da fila do semaforo
        OS_readySet |= (1U << (wakeIdx - 1U)); //volta a ser pronta
        Trace_log(TRACE_SEM_WAKE, wakeIdx);//guarda esse evento
        Trace_log(TRACE_THREAD_READY, wakeIdx);//guarda esse evento
        OS_sched();// pode preemptar se a acordada for mais urgente
    } else {
        me->count++; //ninguem esperando: apenas credita
    }
    __asm volatile ("cpsie i");
}

void Trace_log(uint8_t event, uint8_t task)
{
    uint16_t i = traceHead;

    traceBuffer[i].tick = OS_tickCtr;
    traceBuffer[i].event = event;
    traceBuffer[i].task = task;
    traceBuffer[i].data = 0U;

    traceHead = traceHead + 1U;

    if(traceHead >= TRACE_SIZE)
    {
        traceHead = 0U;
    }
}

void OSThread_start(
    OSThread *me,
    uint32_t period,
    OSThreadHandler threadHandler,
    void *stkSto, uint32_t stkSize)
{
    /* round down the stack top to the 8-byte boundary
    * NOTE: ARM Cortex-M stack grows down from hi -> low memory
    */
    uint32_t *sp = (uint32_t *)((((uint32_t)stkSto + stkSize) / 8) * 8);
    uint32_t *stk_limit;

    /* thread number must be in ragne
    * and must be unused
    */
    Q_REQUIRE((OS_threadNum < Q_DIM(OS_thread)) && (OS_thread[OS_threadNum] == (OSThread *)0));

    *(--sp) = (1U << 24);  /* xPSR */
    *(--sp) = (uint32_t)threadHandler; /* PC */
    *(--sp) = 0x0000000EU; /* LR  */
    *(--sp) = 0x0000000CU; /* R12 */
    *(--sp) = 0x00000003U; /* R3  */
    *(--sp) = 0x00000002U; /* R2  */
    *(--sp) = 0x00000001U; /* R1  */
    *(--sp) = 0x00000000U; /* R0  */
    /* additionally, fake registers R4-R11 */
    *(--sp) = 0x0000000BU; /* R11 */
    *(--sp) = 0x0000000AU; /* R10 */
    *(--sp) = 0x00000009U; /* R9 */
    *(--sp) = 0x00000008U; /* R8 */
    *(--sp) = 0x00000007U; /* R7 */
    *(--sp) = 0x00000006U; /* R6 */
    *(--sp) = 0x00000005U; /* R5 */
    *(--sp) = 0x00000004U; /* R4 */

    /* save the top of the stack in the thread's attibute */
    me->sp = sp;

    /* periodic-task attributes; period == 0 means a non-periodic thread
     * (idle thread or a one-shot OS_delay user), which the tick never re-releases.
     * For a periodic task: first release one period from now (timeout = period)
     * and the first job's deadline is at t = period (absDeadline = period). */
    me->period = period;
    me->timeout = period;
    me->absDeadline = period;

    /* round up the bottom of the stack to the 8-byte boundary */
    stk_limit = (uint32_t *)(((((uint32_t)stkSto - 1U) / 8) + 1U) * 8);

    /* pre-fill the unused part of the stack with 0xDEADBEEF */
    for (sp = sp - 1U; sp >= stk_limit; --sp) {
        *sp = 0xDEADBEEFU;
    }

    /* register the thread with the OS */
    OS_thread[OS_threadNum] = me;
    /* make the thread ready to run */
    if (OS_threadNum > 0U) {
        OS_readySet |= (1U << (OS_threadNum - 1U));
    }
    OS_threadNum++;
}
/***********************************************/
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    //Default_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    //Default_Handler();
  }
}

/* Frequencia alvo do sistema: o PLL deste projeto produz 170 MHz
 * (HSI 16 MHz / M=4 * N=85 / R=2). Usamos esta constante FIXA para a recarga
 * do SysTick em vez de SystemCoreClock, que e calculado em runtime lendo o RCC
 * e sai errado sob o Renode (RCC e um stub). No hardware real o valor coincide,
 * entao o comportamento e identico la. */
const uint32_t SYS_CLOCK_HZ = 170000000U;

void OS_onStartup(void) {
	SystemClock_Config();
    SystemCoreClockUpdate();

   // const char msg[] = "UART TEST\r\n";

     //HAL_UART_Transmit(
           //&huart2,
           //(uint8_t*)msg,
          // sizeof(msg) - 1,
          // 100
       //);

    SysTick_Config(SYS_CLOCK_HZ / TICKS_PER_SEC);

    /* set the SysTick interrupt priority (highest) */
    NVIC_SetPriority(SysTick_IRQn, 0U);
}

void OS_onIdle(void) {
#ifdef NDBEBUG
    __WFI(); /* stop the CPU and Wait for Interrupt */
#endif
}

}//fim namespace

void Q_onAssert(char const *module, int loc) {
    /* TBD: damage control */
    (void)module; /* avoid the "unused parameter" compiler warning */
    (void)loc;    /* avoid the "unused parameter" compiler warning */
    NVIC_SystemReset();
}

/***********************************************/
__attribute__ ((naked, optimize("-fno-stack-protector")))
 void PendSV_Handler(void) {
	__asm volatile (

    /* __disable_irq(); */
    "  CPSID         I                 \n"

    /* if (OS_curr != (OSThread *)0) { */
    "  LDR           r1,=_ZN4rtos7OS_currE       \n"
    "  LDR           r1,[r1,#0x00]     \n"
    "  CBZ           r1,PendSV_restore \n"

    /*     push registers r4-r11 on the stack */
    "  PUSH          {r4-r11}          \n"

    /*     OS_curr->sp = sp; */
    "  LDR           r1,=_ZN4rtos7OS_currE       \n"
    "  LDR           r1,[r1,#0x00]     \n"
    "  STR           sp,[r1,#0x00]     \n"
    /* } */

    "PendSV_restore:                   \n"
    /* sp = OS_next->sp; */
    "  LDR           r1,=_ZN4rtos7OS_nextE       \n"
    "  LDR           r1,[r1,#0x00]     \n"
    "  LDR           sp,[r1,#0x00]     \n"

    /* OS_curr = OS_next; */
    "  LDR           r1,=_ZN4rtos7OS_nextE       \n"
    "  LDR           r1,[r1,#0x00]     \n"
    "  LDR           r2,=_ZN4rtos7OS_currE       \n"
    "  STR           r1,[r2,#0x00]     \n"

    /* pop registers r4-r11 */
    "  POP           {r4-r11}          \n"

    /* __enable_irq(); */
    "  CPSIE         I                 \n"

    /* return to the next thread */
    "  BX            lr                \n"
    );
}
