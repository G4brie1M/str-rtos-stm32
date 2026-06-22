# Kernel: Pilha e Troca de Contexto (MiROS / Cortex-M4F)

Documento da camada de kernel e escalonador. Cobre o layout do TCB, o formato
da pilha de uma tarefa e a sequencia exata da troca de contexto via PendSV.
Todas as referencias apontam para `Core/Src/miros.cpp` e `Core/Inc/miros.h`.

---

## 1. Thread Control Block (TCB)

Definido em `miros.h` (`rtos::OSThread`):

```
struct OSThread {
    void     *sp;          // ponteiro de pilha (TEM que ser o primeiro campo)
    uint32_t  timeout;     // contador regressivo de ticks ate a proxima liberacao
    uint32_t  period;      // periodo em ticks; 0 = tarefa nao periodica
    uint32_t  absDeadline; // tick absoluto do deadline do job atual (chave do EDF)
};
```

Por que `sp` e o primeiro membro: o handler do PendSV e escrito em assembly e le
`OS_curr->sp` no offset `0x00` da struct (`STR sp,[r1,#0x00]` / `LDR sp,[r1,#0x00]`).
Se `sp` deixar de ser o primeiro campo, o assembly passa a ler/escrever o campo
errado e o resultado e HardFault. Os demais campos podem estar em qualquer ordem.

---

## 2. Layout da pilha de uma tarefa (apos OSThread_start)

A pilha do Cortex-M cresce de cima para baixo (endereco alto -> endereco baixo).
`OSThread_start` monta uma "moldura de excecao falsa" para que, na primeira vez
que a tarefa for restaurada, a CPU comece a executar no handler dela.

```
        ENDERECO ALTO  (topo / fundo logico da pilha)
        +-------------------+  <- (stkSto + stkSize) alinhado a 8 bytes
        |       xPSR        |  0x01000000  (bit T = Thumb ligado)   ┐
        |        PC         |  = endereco do threadHandler          │
        |        LR         |  0x0000000E                           │  MOLDURA DE
        |       R12         |  0x0000000C                           │  HARDWARE
        |        R3         |  0x00000003                           │  (8 palavras)
        |        R2         |  0x00000002                           │
        |        R1         |  0x00000001                           │
        |        R0         |  0x00000000                           ┘
        |       R11         |  0x0000000B                           ┐
        |       R10         |  0x0000000A                           │
        |        R9         |  0x00000009                           │  MOLDURA DE
        |        R8         |  0x00000008                           │  SOFTWARE
        |        R7         |  0x00000007                           │  (8 palavras)
        |        R6         |  0x00000006                           │
        |        R5         |  0x00000005                           │
        |        R4         |  0x00000004                           ┘
        +-------------------+  <- me->sp aponta AQUI (no R4 salvo)
        |    0xDEADBEEF     |  ┐
        |    0xDEADBEEF     |  │  area livre pre-preenchida; serve para
        |       ...         |  │  medir uso de pilha e detectar estouro
        +-------------------+  ┘  <- stk_limit (base) alinhado a 8 bytes
        ENDERECO BAIXO
```

Valores iniciais relevantes:
- `xPSR = 0x01000000`: o bit 24 (T) marca estado Thumb, obrigatorio no Cortex-M.
- `PC = threadHandler`: ao retornar da excecao, a CPU pula para a funcao da tarefa.
- `R0..R12, LR`: valores ficticios (so para preencher a moldura; o codigo da tarefa
  ainda nao depende deles).

---

## 3. Por que os registradores se dividem em dois grupos

O ARM Cortex-M, por convencao de chamada (AAPCS), classifica os registradores:

- **R0-R3, R12, LR, PC, xPSR** sao "caller-saved". O **hardware** os empilha e
  desempilha **automaticamente** na entrada e na saida de qualquer excecao, para
  que uma ISR escrita em C funcione sem prologo especial.
- **R4-R11** sao "callee-saved". Numa funcao C normal o compilador os preservaria,
  mas o `PendSV_Handler` e `naked` (sem prologo/epilogo) e troca a pilha por baixo
  dos panos, entao **ele mesmo** precisa salvar/restaurar esse grupo a mao, com
  `PUSH {r4-r11}` / `POP {r4-r11}`.

E por isso que a moldura completa de um contexto salvo tem 16 palavras: 8 do
hardware + 8 do software.

---

## 4. Troca de contexto (PendSV_Handler)

A troca nunca acontece "no meio" de outra rotina: `OS_sched` apenas **agenda** o
PendSV escrevendo o bit PENDSVSET (bit 28) no registrador ICSR em `0xE000ED04`.
Como o PendSV recebe a menor prioridade possivel (0xFF, configurado em `OS_init`),
ele so executa quando a CPU esta voltando ao modo thread, depois de qualquer outra
ISR. Isso garante uma troca limpa e deterministica.

Sequencia dentro do handler (assembly em `miros.cpp`):

```
PendSV_Handler:
  CPSID I                      ; 1. desabilita interrupcoes (regiao critica)

  if (OS_curr != 0) {          ; 2. existe tarefa saindo?
     PUSH {r4-r11}             ;    salva a moldura de SOFTWARE na pilha dela
     OS_curr->sp = sp          ;    guarda o topo de pilha no TCB que sai
  }

  sp = OS_next->sp             ; 3. carrega o topo de pilha da tarefa que entra
  OS_curr = OS_next            ; 4. a que entra passa a ser a corrente

  POP {r4-r11}                 ; 5. restaura a moldura de SOFTWARE da nova tarefa
  CPSIE I                      ; 6. reabilita interrupcoes
  BX lr                        ; 7. exception return: o HARDWARE desempilha
                               ;    R0-R3,R12,LR,PC,xPSR -> a nova tarefa retoma
```

O passo 2 e condicional porque, no primeiro escalonamento (em `OS_run`), ainda nao
ha tarefa corrente (`OS_curr == 0`), entao nao ha contexto a salvar; o handler
apenas restaura a primeira tarefa escolhida.

---

## 5. Linha do tempo de uma troca A -> B

```
Tarefa A em execucao
      │
      │  A chama OS_delay / OS_waitNextPeriod / OS_yield
      │  (ou o SysTick dispara OS_tick + OS_sched)
      ▼
OS_sched escolhe B (menor deadline absoluto)  ──►  ICSR.PENDSVSET = 1
      │                                              (PendSV fica pendente)
      ▼
... ao retornar ao modo thread, o PendSV (prioridade minima) dispara ...
      ▼
PendSV: HW empilha R0-R3,R12,LR,PC,xPSR de A   (moldura de hardware de A)
        SW: PUSH {r4-r11} de A ; OS_curr->sp = sp
        sp = B->sp ; OS_curr = B
        SW: POP {r4-r11} de B
        BX lr  ->  HW desempilha R0-R3,R12,LR,PC,xPSR de B
      ▼
Tarefa B retoma exatamente de onde havia parado
```

---

## 6. Resumo das tres formas de ceder a CPU

| Funcao              | Mexe em timeout? | Sai do ready set? | Quando usar                          |
|---------------------|------------------|-------------------|--------------------------------------|
| `OS_delay(t)`       | sim (= t)        | sim               | espera de duracao fixa (one-shot)    |
| `OS_waitNextPeriod` | nao              | sim               | fim de job periodico (run to completion) |
| `OS_yield`          | nao              | nao               | cessao voluntaria sem bloquear       |

As tres chamam `OS_sched` com interrupcoes desabilitadas; a troca efetiva so
ocorre se a tarefa escolhida for diferente da corrente, atraves do PendSV.
