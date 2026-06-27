/*
 * miros.h
 *
 *  Created on: Feb 6, 2025
 *      Author: guilh
 */

#ifndef INC_MIROS_H_
#define INC_MIROS_H_

namespace rtos {
/* Thread Control Block (TCB) */
typedef struct {
    void *sp; /* stack pointer */
    uint32_t timeout; /* timeout delay down-counter */
    uint32_t period;// periodo da tarefa em ticks
    uint32_t absDeadline; // tick da deadline absoluta
    /* ... other attributes associated with a thread */
} OSThread;

/* Semaforo contador com fila de bloqueio (Fase 1).
 * waitSet e um bitmask no mesmo estilo de OS_readySet: o bit (n-1) ligado
 * significa que a thread n esta bloqueada NESTE semaforo */
typedef struct {
    int32_t count;//creditos disponiveis
    uint32_t waitSet;// bitmask das threads bloqueadas
} OSSemaphore;

const uint16_t TICKS_PER_SEC = 100U;

enum TraceEventType {
    TRACE_SWITCH = 1,
    TRACE_YIELD,
    TRACE_DELAY,
    TRACE_SEM_WAIT,
    TRACE_SEM_BLOCK,
    TRACE_SEM_SIGNAL,
    TRACE_SEM_WAKE,
    TRACE_ISR_ENTER,
	TRACE_THREAD_READY,
    TRACE_ISR_EXIT
};

typedef struct {
	uint32_t tick;
	uint8_t event;
	uint8_t task;
	uint16_t data;
} TraceEvent;

#define TRACE_SIZE 512U

extern volatile TraceEvent traceBuffer[TRACE_SIZE];
extern volatile uint16_t traceHead;

typedef void (*OSThreadHandler)();

void Trace_log(uint8_t event, uint8_t task);

void OS_init(void *stkSto, uint32_t stkSize);

/* callback to handle the idle condition */
void OS_onIdle(void);

/* this function must be called with interrupts DISABLED */
void OS_sched(void);

/* transfer control to the RTOS to run the threads */
void OS_run(void);

/* blocking delay */
void OS_delay(uint32_t ticks);

/* process all timeouts */
void OS_tick(void);

/* callback to configure and start interrupts */
void OS_onStartup(void);

void OS_waitNextPeriod(void);

//cooperacao voluntaria:cede a CPU sem bloquear(Fase 1)
void OS_yield(void);

// semaforo contador (Fase 1): bloqueio passivo, sem espera ativa
void OSSem_init(OSSemaphore *me, int32_t initial);
void OSSem_wait(OSSemaphore *me);
void OSSem_signal(OSSemaphore *me);

void OSThread_start(
    OSThread *me,
	uint32_t period,
    OSThreadHandler threadHandler,
    void *stkSto, uint32_t stkSize);

}

#endif /* INC_MIROS_H_ */
