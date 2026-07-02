# Inc

This directory contains header files for the STM32 application.

Main contents:
- STM32 configuration headers
- MiROS header file
- Application interface declarations

The file `miros.h` contains the main MiROS interface used by the project.

!!!!!!!!!!
se a tarefa periódica ficar bloqueada além do próprio período,
 ela vai "perder" o(s) job(s) intermediário(s) 
 ela não roda 2x seguidas pra compensar, só continua contando as próximas deadlines a partir de agora.
 Isso é uma escolha de projeto razoável (evita acúmulo/explosão de trabalho atrasado)
 !!!!!!!